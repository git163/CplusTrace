#include "store.h"

#include <algorithm>
#include <utility>

#include "trace_builder.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

std::string attr_of(const common::Event& event, const std::string& key,
                    const std::string& fallback = std::string()) {
  auto it = event.attrs.find(key);
  return it == event.attrs.end() ? fallback : it->second;
}

bool level_is_error(const std::string& level) {
  return level == "warn" || level == "error" || level == "fatal";
}

std::string lower_ascii(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

bool contains_ci(const std::string& haystack, const std::string& needle_lower) {
  return lower_ascii(haystack).find(needle_lower) != std::string::npos;
}

void trim_deque(std::deque<std::shared_ptr<LogData>>* deque, size_t limit) {
  while (deque->size() > limit) deque->pop_front();
}

std::string metric_series_key(const std::string& name,
                              const std::unordered_map<std::string, std::string>& labels) {
  std::vector<std::string> parts;
  parts.reserve(labels.size());
  for (const auto& kv : labels) parts.push_back(kv.first + "=" + kv.second);
  std::sort(parts.begin(), parts.end());
  std::string key = name;
  for (const std::string& part : parts) {
    key += '|';
    key += part;
  }
  return key;
}

}  // namespace

Store::Store(const StoreConfig& config)
    : config_(config), clusterer_(5000) {
  started_ns_ = now_ns();
  last_prune_ns_ = started_ns_;
}

Store::~Store() = default;

void Store::ingest(const common::Event& input, const std::string& node_hint,
                   const std::string& agent) {
  (void)agent;  // Agent 身份已由调用方写入事件；此处保留参数便于后续扩展。
  common::Event event = input;
  if (event.node.empty()) event.node = node_hint;

  const bool is_error = level_is_error(event.level);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.events_ingested;
    if (raw_.size() >= 20000) raw_.pop_front();
    raw_.push_back(event);
  }

  catalog_.observe_event(event.service, event.instance, event.node,
                         event.version, event.pid, is_error, event.time_ns);

  if (event.type == "log") {
    ingest_log(event, node_hint);
  } else if (event.type == "span_begin" || event.type == "span_end") {
    ingest_span(event, node_hint);
  } else if (event.type == "metric") {
    ingest_metric(event);
  } else if (event.type == "process") {
    catalog_.observe_process(
        event.node, event.pid,
        static_cast<int32_t>(to_i64(attr_of(event, "ppid"))),
        event.name, attr_of(event, "cmdline"), attr_of(event, "state"),
        static_cast<uint64_t>(to_i64(attr_of(event, "rss_kb"))),
        static_cast<uint64_t>(to_i64(attr_of(event, "threads"))),
        static_cast<uint64_t>(to_i64(attr_of(event, "start_time_ticks"))),
        event.time_ns);
  } else if (event.type == "host") {
    catalog_.observe_host(
        event.node, attr_of(event, "os"), attr_of(event, "kernel"),
        attr_of(event, "arch"), to_double(attr_of(event, "load1")),
        to_double(attr_of(event, "load5")), to_double(attr_of(event, "load15")),
        static_cast<uint64_t>(to_i64(attr_of(event, "mem_total_kb"))),
        static_cast<uint64_t>(to_i64(attr_of(event, "mem_available_kb"))),
        static_cast<uint64_t>(to_i64(attr_of(event, "uptime_seconds"))),
        attr_of(event, "agent"), event.time_ns);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  maybe_prune_locked(now_ns());
}

bool Store::ingest_line(const std::string& line, const std::string& node_hint,
                        const std::string& agent) {
  try {
    const nlohmann::json json = nlohmann::json::parse(line);
    if (!json.is_object()) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.events_rejected;
      return false;
    }
    common::Event event = common::from_json(json);
    if (event.type.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.events_rejected;
      return false;
    }
    ingest(event, node_hint, agent);
    return true;
  } catch (const std::exception&) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.events_rejected;
    return false;
  }
}

void Store::ingest_span(const common::Event& event,
                        const std::string& node_hint) {
  if (event.trace.empty() || event.span.empty()) return;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = traces_.find(event.trace);
  if (it == traces_.end()) {
    TraceState state;
    state.data.trace_id = event.trace;
    state.data.first_ns = event.time_ns;
    it = traces_.insert(std::make_pair(event.trace, std::move(state))).first;
    trace_order_.push_back(event.trace);
    while (traces_.size() > config_.max_traces && !trace_order_.empty()) {
      const std::string victim = trace_order_.front();
      trace_order_.pop_front();
      traces_.erase(victim);
    }
  }

  TraceState& state = it->second;
  TraceData& trace = state.data;
  state.last_touch_ns = event.time_ns;
  ++stats_.spans_ingested;

  if (trace.first_ns == 0 || event.time_ns < trace.first_ns) {
    trace.first_ns = event.time_ns;
  }
  if (event.time_ns > trace.last_ns) trace.last_ns = event.time_ns;
  if (trace.request_id.empty()) trace.request_id = event.request_id;
  if (trace.task_id.empty()) trace.task_id = event.task_id;
  if (trace.root_service.empty() && event.parent.empty()) {
    trace.root_service = event.service;
    trace.root_name = event.name;
  }

  auto span_it = trace.spans.find(event.span);
  if (span_it == trace.spans.end()) {
    SpanData span;
    span.trace = event.trace;
    span.span = event.span;
    span.parent = event.parent;
    span.service = event.service;
    span.instance = event.instance;
    span.node = event.node.empty() ? node_hint : event.node;
    span.version = event.version;
    span.pid = event.pid;
    span.tid = event.tid;
    span.name = event.name;
    span.file = event.file;
    span.line = event.line;
    span.kind = attr_of(event, "span.kind", attr_of(event, "kind"));
    span.status = attr_of(event, "status");
    span.error_code = event.error_code.empty()
                          ? attr_of(event, "error_code")
                          : event.error_code;
    span.start_ns = event.time_ns;
    span.attrs = event.attrs;
    span_it = trace.spans.insert(std::make_pair(event.span, span)).first;
  }

  SpanData& span = span_it->second;
  if (span.parent.empty()) span.parent = event.parent;
  if (span.service.empty()) span.service = event.service;
  if (span.node.empty()) span.node = event.node.empty() ? node_hint : event.node;
  if (span.instance.empty()) span.instance = event.instance;
  if (span.version.empty()) span.version = event.version;
  if (span.pid == 0) span.pid = event.pid;
  if (span.tid == 0) span.tid = event.tid;
  if (!event.name.empty()) span.name = event.name;
  if (!event.file.empty()) span.file = event.file;
  if (event.line != 0) span.line = event.line;
  if (span.kind.empty()) {
    span.kind = attr_of(event, "span.kind", attr_of(event, "kind"));
  }
  if (span.status.empty()) span.status = attr_of(event, "status");
  if (span.error_code.empty()) {
    span.error_code =
        event.error_code.empty() ? attr_of(event, "error_code") : event.error_code;
  }
  if (level_is_error(event.level) || attr_of(event, "error") == "true") {
    span.status = "error";
  }
  for (const auto& kv : event.attrs) span.attrs[kv.first] = kv.second;

  if (event.type == "span_end") {
    span.ended = true;
    span.end_ns = event.time_ns;
    if (span.start_ns == 0 || span.end_ns < span.start_ns) {
      span.start_ns = span.end_ns;
    }
  } else if (span.start_ns == 0 || event.time_ns < span.start_ns) {
    span.start_ns = event.time_ns;
  }
  if (span.end_ns > trace.last_ns) trace.last_ns = span.end_ns;

  if (!trace.request_id.empty() &&
      traces_by_request_.find(trace.request_id) == traces_by_request_.end()) {
    std::deque<std::string>& list = traces_by_request_[trace.request_id];
    list.push_back(trace.trace_id);
    while (list.size() > 128) list.pop_front();
  }
}

void Store::ingest_log(const common::Event& event,
                       const std::string& node_hint) {
  LogData log;
  log.time_ns = event.time_ns;
  log.node = event.node.empty() ? node_hint : event.node;
  log.service = event.service;
  log.instance = event.instance;
  log.version = event.version;
  log.pid = event.pid;
  log.tid = event.tid;
  log.level = event.level.empty() ? "info" : event.level;
  log.msg = event.msg;
  log.file = event.file;
  log.line = event.line;
  log.trace = event.trace;
  log.span = event.span;
  log.request_id = event.request_id;
  log.task_id = event.task_id;
  log.error_code = event.error_code;
  log.attrs = event.attrs;

  // 从消息里补关键字段（存量程序未必已经结构化）。
  const auto extracted = LogClusterer::extract_keys(event.msg);
  auto pick = [&extracted](const char* key) -> std::string {
    auto it = extracted.find(key);
    return it == extracted.end() ? std::string() : it->second;
  };
  if (log.trace.empty()) log.trace = pick("trace_id");
  if (log.request_id.empty()) log.request_id = pick("request_id");
  if (log.request_id.empty()) log.request_id = pick("req_id");
  if (log.task_id.empty()) log.task_id = pick("task_id");
  if (log.error_code.empty()) log.error_code = pick("error_code");
  if (log.error_code.empty()) log.error_code = pick("errno");
  if (log.error_code.empty()) log.error_code = pick("errcode");

  const LogClusterInfo cluster =
      clusterer_.annotate(event.msg, log.level, log.service, log.node,
                          log.error_code, event.time_ns);
  log.fingerprint = cluster.id;
  log.tmpl = cluster.tmpl;

  std::lock_guard<std::mutex> lock(mutex_);

  // 噪声屏蔽：已知无害模板直接丢弃，但计数保留在聚类里。
  for (const std::string& pattern : config_.noise_patterns) {
    if (!pattern.empty() && event.msg.find(pattern) != std::string::npos) {
      ++stats_.logs_noise;
      return;
    }
  }

  // INFO 及以下按比例采样；错误级别 100% 保留。
  if (!log.is_error_level() && config_.info_sample_permyriad < 10000) {
    const std::string sample_key = !log.trace.empty() ? log.trace : log.fingerprint;
    if (fnv1a64(sample_key) % 10000 >= config_.info_sample_permyriad) {
      ++stats_.logs_sampled;
      return;
    }
  }

  // 去重窗口：同一指纹在窗口内只保留前 N 条，其余只计数。
  if (!log.is_error_level() && config_.dedup_max_per_window > 0) {
    DedupState& state = dedup_[log.fingerprint];
    if (state.window_start_ns == 0 ||
        event.time_ns > state.window_start_ns + config_.dedup_window_ns) {
      state.window_start_ns = event.time_ns;
      state.count = 0;
    }
    if (state.count >= config_.dedup_max_per_window) {
      ++state.count;
      ++stats_.logs_deduped;
      return;
    }
    ++state.count;
  }

  std::shared_ptr<LogData> record = std::make_shared<LogData>(log);
  logs_.push_back(record);
  while (logs_.size() > config_.max_log_records) logs_.pop_front();
  index_log_locked(record);
  ++stats_.logs_ingested;
}

void Store::index_log_locked(const std::shared_ptr<LogData>& log) {
  if (!log->trace.empty()) {
    trim_deque(&logs_by_trace_[log->trace], config_.per_key_logs);
    logs_by_trace_[log->trace].push_back(log);
  }
  if (!log->request_id.empty()) {
    trim_deque(&logs_by_request_[log->request_id], config_.per_key_logs);
    logs_by_request_[log->request_id].push_back(log);
  }
  if (!log->service.empty()) {
    trim_deque(&logs_by_service_[log->service], config_.per_key_logs);
    logs_by_service_[log->service].push_back(log);
  }
  if (!log->fingerprint.empty()) {
    trim_deque(&logs_by_fingerprint_[log->fingerprint], config_.per_key_logs);
    logs_by_fingerprint_[log->fingerprint].push_back(log);
  }
}

void Store::ingest_metric(const common::Event& event) {
  MetricPoint point;
  point.name = event.name;
  if (point.name.empty()) return;
  point.time_ns = event.time_ns;
  point.value = to_double(attr_of(event, "value", event.msg));
  for (const auto& kv : event.attrs) {
    if (kv.first != "value") point.labels[kv.first] = kv.second;
  }
  if (!event.service.empty()) point.labels["service"] = event.service;
  if (!event.node.empty()) point.labels["node"] = event.node;

  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = metric_series_key(point.name, point.labels);
  auto it = metrics_.find(key);
  if (it == metrics_.end()) {
    if (metrics_.size() >= 5000) return;  // 基数保护
    it = metrics_.insert(std::make_pair(key, MetricSeries())).first;
  }
  it->second.points.push_back(point);
  while (it->second.points.size() > 2000) it->second.points.pop_front();
  ++stats_.metric_points;
  (void)config_.max_metrics;
}

bool Store::get_trace(const std::string& trace_id, TraceData* out) const {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = traces_.find(trace_id);
  if (it == traces_.end()) return false;
  *out = it->second.data;
  return true;
}

std::vector<TraceSummary> Store::query_traces(const TraceQuery& query) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TraceSummary> out;

  std::vector<const TraceState*> candidates;
  if (!query.trace_id.empty()) {
    auto it = traces_.find(query.trace_id);
    if (it != traces_.end()) candidates.push_back(&it->second);
  } else if (!query.request_id.empty()) {
    auto it = traces_by_request_.find(query.request_id);
    if (it != traces_by_request_.end()) {
      for (const std::string& id : it->second) {
        auto trace_it = traces_.find(id);
        if (trace_it != traces_.end()) candidates.push_back(&trace_it->second);
      }
    }
  } else {
    // 从新到旧扫描，命中 limit 即停。
    for (auto order_it = trace_order_.rbegin();
         order_it != trace_order_.rend() && candidates.size() < query.limit * 20;
         ++order_it) {
      auto trace_it = traces_.find(*order_it);
      if (trace_it != traces_.end()) candidates.push_back(&trace_it->second);
    }
  }

  for (const TraceState* state : candidates) {
    const TraceData& trace = state->data;
    if (query.since_ns != 0 && trace.last_ns < query.since_ns) continue;
    if (!query.task_id.empty() && trace.task_id != query.task_id) continue;

    TraceSummary summary = summarize_trace(trace);
    if (query.errors_only && !summary.has_error) continue;
    if (query.min_duration_ns != 0 &&
        summary.duration_ns < query.min_duration_ns) {
      continue;
    }
    if (query.slow_only && summary.duration_ns < query.min_duration_ns) continue;
    if (!query.service.empty()) {
      bool found = false;
      for (const auto& kv : trace.spans) {
        if (kv.second.service == query.service) {
          found = true;
          break;
        }
      }
      if (!found) continue;
    }
    if (!query.text.empty()) {
      const std::string needle = lower_ascii(query.text);
      bool found = contains_ci(trace.trace_id, needle) ||
                   contains_ci(trace.request_id, needle) ||
                   contains_ci(trace.task_id, needle) ||
                   contains_ci(trace.root_service, needle) ||
                   contains_ci(trace.root_name, needle);
      for (auto it = trace.spans.begin(); !found && it != trace.spans.end();
           ++it) {
        found = contains_ci(it->second.service, needle) ||
                contains_ci(it->second.name, needle);
      }
      if (!found) continue;
    }
    out.push_back(summary);
    if (out.size() >= query.limit) break;
  }

  std::sort(out.begin(), out.end(),
            [](const TraceSummary& a, const TraceSummary& b) {
              return a.start_ns > b.start_ns;
            });
  return out;
}

std::vector<TraceData> Store::recent_traces(uint64_t since_ns,
                                            size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TraceData> out;
  for (auto order_it = trace_order_.rbegin();
       order_it != trace_order_.rend() && out.size() < limit; ++order_it) {
    auto trace_it = traces_.find(*order_it);
    if (trace_it == traces_.end()) continue;
    if (trace_it->second.data.last_ns < since_ns) continue;
    out.push_back(trace_it->second.data);
  }
  return out;
}

std::vector<std::string> Store::traces_for_request(
    const std::string& request_id, size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  auto it = traces_by_request_.find(request_id);
  if (it == traces_by_request_.end()) return out;
  for (const std::string& id : it->second) {
    if (traces_.count(id) != 0) out.push_back(id);
    if (out.size() >= limit) break;
  }
  return out;
}

std::vector<LogData> Store::query_logs(const LogQuery& query) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LogData> out;

  const std::deque<std::shared_ptr<LogData>>* source = nullptr;
  if (!query.trace.empty()) {
    auto it = logs_by_trace_.find(query.trace);
    if (it != logs_by_trace_.end()) source = &it->second;
  } else if (!query.request_id.empty()) {
    auto it = logs_by_request_.find(query.request_id);
    if (it != logs_by_request_.end()) source = &it->second;
  } else if (!query.fingerprint.empty()) {
    auto it = logs_by_fingerprint_.find(query.fingerprint);
    if (it != logs_by_fingerprint_.end()) source = &it->second;
  } else if (!query.service.empty()) {
    auto it = logs_by_service_.find(query.service);
    if (it != logs_by_service_.end()) source = &it->second;
  }

  auto matches = [&query](const LogData& log) {
    if (!query.trace.empty() && log.trace != query.trace) return false;
    if (!query.request_id.empty() && log.request_id != query.request_id) {
      return false;
    }
    if (!query.task_id.empty() && log.task_id != query.task_id) return false;
    if (!query.service.empty() && log.service != query.service) return false;
    if (!query.node.empty() && log.node != query.node) return false;
    if (!query.level.empty() && log.level != query.level) return false;
    if (!query.fingerprint.empty() && log.fingerprint != query.fingerprint) {
      return false;
    }
    if (query.errors_only && !log.is_error_level()) return false;
    if (query.since_ns != 0 && log.time_ns < query.since_ns) return false;
    if (query.until_ns != 0 && log.time_ns > query.until_ns) return false;
    if (!query.text.empty() &&
        log.msg.find(query.text) == std::string::npos) {
      return false;
    }
    return true;
  };

  if (source != nullptr) {
    for (auto it = source->rbegin(); it != source->rend(); ++it) {
      if (matches(**it)) out.push_back(**it);
      if (out.size() >= query.limit) break;
    }
  } else {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
      if (matches(**it)) out.push_back(**it);
      if (out.size() >= query.limit) break;
    }
  }
  return out;
}

std::vector<LogClusterInfo> Store::log_clusters(size_t limit) const {
  return clusterer_.list(limit);
}

std::vector<MetricPoint> Store::query_metrics(const std::string& name,
                                              uint64_t since_ns,
                                              size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<MetricPoint> out;
  for (const auto& kv : metrics_) {
    for (const MetricPoint& point : kv.second.points) {
      if (point.name != name) continue;
      if (point.time_ns < since_ns) continue;
      out.push_back(point);
      if (out.size() >= limit) break;
    }
    if (out.size() >= limit) break;
  }
  std::sort(out.begin(), out.end(),
            [](const MetricPoint& a, const MetricPoint& b) {
              return a.time_ns < b.time_ns;
            });
  return out;
}

std::vector<MetricPoint> Store::recent_metrics(uint64_t since_ns,
                                               size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<MetricPoint> out;
  for (const auto& kv : metrics_) {
    for (const MetricPoint& point : kv.second.points) {
      if (point.time_ns < since_ns) continue;
      out.push_back(point);
      if (out.size() >= limit) break;
    }
    if (out.size() >= limit) break;
  }
  std::sort(out.begin(), out.end(),
            [](const MetricPoint& a, const MetricPoint& b) {
              return a.time_ns < b.time_ns;
            });
  return out;
}

void Store::add_event(const EventRecord& event) {
  EventSink sink;
  bool grouped = false;
  EventRecord stored = event;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!event.fingerprint.empty()) {
      // 同指纹、仍处于 open 的事件在窗口内合并，避免告警风暴刷屏。
      for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->fingerprint == event.fingerprint && it->status == "open") {
          const uint64_t last = it->last_ns == 0 ? it->time_ns : it->last_ns;
          if (event.time_ns <= last + config_.event_group_window_ns) {
            ++it->count;
            it->last_ns = event.time_ns;
            if (!event.detail.empty()) it->detail = event.detail;
            grouped = true;
            break;
          }
        }
      }
    }
    if (!grouped) {
      if (stored.last_ns == 0) stored.last_ns = stored.time_ns;
      if (stored.count == 0) stored.count = 1;
      events_.push_back(stored);
      while (events_.size() > config_.max_events) events_.pop_front();
    }
    sink = event_sink_;
  }
  // 合并事件不重复通知；新事件才触发 Webhook。
  if (sink && !grouped) sink(stored);
}

void Store::set_event_sink(EventSink sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  event_sink_ = std::move(sink);
}

std::vector<EventRecord> Store::list_events(size_t limit) const {
  EventQuery query;
  query.limit = limit;
  return list_events(query);
}

std::vector<EventRecord> Store::list_events(const EventQuery& query) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<EventRecord> out;
  for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
    if (!query.status.empty() && it->status != query.status) continue;
    if (!query.severity.empty() && it->severity != query.severity) continue;
    if (!query.service.empty() && it->service != query.service) continue;
    if (!query.type.empty() && it->type != query.type) continue;
    out.push_back(*it);
    if (out.size() >= query.limit) break;
  }
  return out;
}

bool Store::acknowledge_event(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (EventRecord& event : events_) {
    if (event.id == id) {
      // 已解决的事件不再回退到 ack；重复确认保持幂等。
      if (event.status != "resolved") event.status = "ack";
      return true;
    }
  }
  return false;
}

bool Store::resolve_event(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (EventRecord& event : events_) {
    if (event.id == id) {
      event.status = "resolved";
      return true;
    }
  }
  return false;
}

std::vector<common::Event> Store::raw_events(uint64_t since_ns,
                                             size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<common::Event> out;
  for (auto it = raw_.rbegin(); it != raw_.rend(); ++it) {
    if (it->time_ns < since_ns) continue;
    out.push_back(*it);
    if (out.size() >= limit) break;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

StoreStats Store::stats() const {
  StoreStats out;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out = stats_;
    out.trace_count = traces_.size();
    out.log_count = logs_.size();
    out.event_count = events_.size();
  }
  out.cluster_count = clusterer_.size();
  return out;
}

void Store::maybe_prune_locked(uint64_t now) {
  if (now < last_prune_ns_ + 10ull * 1000000000ull) return;
  last_prune_ns_ = now;
  const uint64_t cutoff =
      now > config_.retention_ns ? now - config_.retention_ns : 0;
  prune_traces_locked(cutoff);
  prune_logs_locked(cutoff);
  while (!events_.empty() && events_.front().time_ns < cutoff) {
    events_.pop_front();
  }
  for (auto it = dedup_.begin(); it != dedup_.end();) {
    if (it->second.window_start_ns < cutoff) {
      it = dedup_.erase(it);
    } else {
      ++it;
    }
  }
}

void Store::prune_traces_locked(uint64_t cutoff) {
  if (cutoff == 0) return;
  for (auto it = traces_.begin(); it != traces_.end();) {
    if (it->second.data.last_ns < cutoff) {
      it = traces_.erase(it);
    } else {
      ++it;
    }
  }
  while (!trace_order_.empty() && traces_.count(trace_order_.front()) == 0) {
    trace_order_.pop_front();
  }
  for (auto it = traces_by_request_.begin(); it != traces_by_request_.end();) {
    std::deque<std::string>& list = it->second;
    while (!list.empty() && traces_.count(list.front()) == 0) list.pop_front();
    if (list.empty()) {
      it = traces_by_request_.erase(it);
    } else {
      ++it;
    }
  }
}

void Store::prune_logs_locked(uint64_t cutoff) {
  if (cutoff == 0) return;
  while (!logs_.empty() && logs_.front()->time_ns < cutoff) {
    logs_.pop_front();
  }
}

void Store::prune() {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t now = now_ns();
  const uint64_t cutoff =
      now > config_.retention_ns ? now - config_.retention_ns : 0;
  prune_traces_locked(cutoff);
  prune_logs_locked(cutoff);
  while (!logs_by_trace_.empty() &&
         logs_by_trace_.begin()->second.empty()) {
    logs_by_trace_.erase(logs_by_trace_.begin());
  }
}

}  // namespace server
}  // namespace vmon
