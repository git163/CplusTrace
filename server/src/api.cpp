#include "api.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "crash.h"
#include "diagnostics.h"
#include "evidence.h"
#include "json_event.h"
#include "profiler.h"
#include "serialize.h"
#include "symbolizer/symbolizer.h"
#include "trace_builder.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

const char* kServerVersion = "0.1.0";

StoreConfig make_store_config(const ServerConfig& config) {
  StoreConfig out;
  out.max_traces = config.max_traces;
  out.max_log_records = config.max_log_records;
  out.retention_ns = config.retention_seconds * 1000000000ull;
  out.info_sample_permyriad = config.info_sample_permyriad;
  out.dedup_window_ns = config.dedup_window_ms * 1000000ull;
  out.dedup_max_per_window = config.dedup_max_per_window;
  out.noise_patterns = config.noise_patterns;
  return out;
}

ForwarderConfig make_forwarder_config(const ServerConfig& config) {
  ForwarderConfig out;
  if (config.forward_enabled) {
    out.vm_url = config.vm_url;
    out.vl_url = config.vl_url;
  }
  out.flush_interval_ms = 500;
  return out;
}

NotifierConfig make_notifier_config(const ServerConfig& config) {
  NotifierConfig out;
  out.enabled = config.webhook_enabled;
  out.url = config.webhook_url;
  out.timeout_ms = config.webhook_timeout_ms;
  out.token = config.webhook_token;
  return out;
}

void send_json(httplib::Response& res, const nlohmann::json& json,
               int status = 200) {
  res.status = status;
  res.set_content(json.dump(), "application/json; charset=utf-8");
}

void send_error(httplib::Response& res, int status, const std::string& message) {
  send_json(res, {{"error", message}}, status);
}

bool authorized(const httplib::Request& req, const ServerConfig& config) {
  if (config.token.empty()) return true;
  const std::string bearer = req.get_header_value("Authorization");
  if (bearer == "Bearer " + config.token) return true;
  return req.get_header_value("X-Vmon-Token") == config.token;
}

std::string param(const httplib::Request& req, const char* name,
                  const std::string& fallback = std::string()) {
  return req.has_param(name) ? req.get_param_value(name) : fallback;
}

uint64_t param_u64(const httplib::Request& req, const char* name,
                   uint64_t fallback = 0) {
  if (!req.has_param(name)) return fallback;
  return static_cast<uint64_t>(to_i64(req.get_param_value(name),
                                      static_cast<int64_t>(fallback)));
}

bool param_bool(const httplib::Request& req, const char* name) {
  const std::string value = param(req, name);
  return value == "1" || value == "true" || value == "yes";
}

bool parse_body(const httplib::Request& req, nlohmann::json* out,
                std::string* error) {
  try {
    *out = nlohmann::json::parse(req.body);
    if (!out->is_object()) {
      if (error) *error = "请求体必须是 JSON 对象";
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = std::string("JSON 解析失败: ") + ex.what();
    return false;
  }
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::string();
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

nlohmann::json summary_json(const TraceSummary& summary) {
  return {{"trace_id", summary.trace_id},
          {"request_id", summary.request_id},
          {"task_id", summary.task_id},
          {"root_service", summary.root_service},
          {"root_name", summary.root_name},
          {"start_ns", summary.start_ns},
          {"time", iso_time(summary.start_ns)},
          {"duration_ns", summary.duration_ns},
          {"duration_ms", static_cast<double>(summary.duration_ns) / 1000000.0},
          {"span_count", summary.span_count},
          {"service_count", summary.service_count},
          {"depth", summary.depth},
          {"has_error", summary.has_error},
          {"first_error_span", summary.first_error_span},
          {"error_code", summary.error_code}};
}

nlohmann::json check_json(const CheckResult& check) {
  return {{"id", check.id},
          {"name", check.name},
          {"status", check.status},
          {"detail", check.detail},
          {"evidence", check.evidence}};
}

nlohmann::json overview_json(ServerContext& ctx) {
  const uint64_t now = now_ns();
  const uint64_t since = now > 300000000000ull ? now - 300000000000ull : 0;

  TraceQuery query;
  query.since_ns = since;
  query.limit = 2000;
  const std::vector<TraceSummary> traces = ctx.store.query_traces(query);

  std::vector<uint64_t> durations;
  durations.reserve(traces.size());
  size_t errors = 0;
  for (const TraceSummary& summary : traces) {
    durations.push_back(summary.duration_ns);
    if (summary.has_error) ++errors;
  }
  std::sort(durations.begin(), durations.end());
  auto percentile = [&durations](double p) -> double {
    if (durations.empty()) return 0.0;
    size_t index = static_cast<size_t>(p * static_cast<double>(durations.size()));
    if (index >= durations.size()) index = durations.size() - 1;
    return static_cast<double>(durations[index]) / 1000000.0;
  };

  const std::vector<InstanceRecord> instances = ctx.store.catalog().instances();
  std::set<std::string> services;
  std::set<std::string> nodes;
  size_t alive_instances = 0;
  for (const InstanceRecord& instance : instances) {
    services.insert(instance.service);
    nodes.insert(instance.node);
    if (instance.alive) ++alive_instances;
  }
  for (const NodeRecord& node : ctx.store.catalog().nodes()) nodes.insert(node.name);

  nlohmann::json top_errors = nlohmann::json::array();
  size_t shown = 0;
  for (const LogClusterInfo& cluster : ctx.store.log_clusters(200)) {
    if (!cluster.is_error_level()) continue;
    top_errors.push_back(cluster_to_json(cluster));
    if (++shown >= 5) break;
  }

  std::vector<TraceSummary> slow = traces;
  std::sort(slow.begin(), slow.end(),
            [](const TraceSummary& a, const TraceSummary& b) {
              return a.duration_ns > b.duration_ns;
            });
  nlohmann::json slow_traces = nlohmann::json::array();
  for (size_t i = 0; i < slow.size() && i < 5; ++i) {
    slow_traces.push_back(summary_json(slow[i]));
  }

  const StoreStats stats = ctx.store.stats();
  nlohmann::json out;
  out["now_ns"] = now;
  out["window_ns"] = 300000000000ull;
  out["node_count"] = nodes.size();
  out["service_count"] = services.size();
  out["instance_count"] = instances.size();
  out["alive_instances"] = alive_instances;
  out["process_count"] = ctx.store.catalog().process_count();
  out["trace_count_5m"] = traces.size();
  out["error_count_5m"] = errors;
  out["error_rate"] =
      traces.empty() ? 0.0
                     : static_cast<double>(errors) * 100.0 /
                           static_cast<double>(traces.size());
  out["qps"] = static_cast<double>(traces.size()) / 300.0;
  out["p50_ms"] = percentile(0.50);
  out["p95_ms"] = percentile(0.95);
  out["p99_ms"] = percentile(0.99);
  out["logs_ingested"] = stats.logs_ingested;
  out["logs_sampled"] = stats.logs_sampled;
  out["logs_deduped"] = stats.logs_deduped;
  out["logs_noise"] = stats.logs_noise;
  out["trace_count"] = stats.trace_count;
  out["log_count"] = stats.log_count;
  out["pipeline_redacted_events"] = ctx.pipeline.redacted_events();
  out["webhook_sent"] = ctx.notifier.sent();
  out["webhook_failed"] = ctx.notifier.failed();
  out["webhook_dropped"] = ctx.notifier.dropped();
  out["top_errors"] = top_errors;
  out["slow_traces"] = slow_traces;
  out["recent_events"] = [&ctx]() {
    nlohmann::json events = nlohmann::json::array();
    for (const EventRecord& event : ctx.store.list_events(10)) {
      events.push_back(event_to_json(event));
    }
    return events;
  }();
  return out;
}

nlohmann::json error_codes_json(ServerContext& ctx, uint64_t since_ns,
                                size_t limit) {
  struct Group {
    std::set<std::string> traces;
    std::set<std::string> services;
    std::set<std::string> root_services;
    uint64_t spans = 0;
    uint64_t first_ns = 0;
    uint64_t last_ns = 0;
    std::string sample_trace_id;
    std::string sample_request_id;
    nlohmann::json sample_path = nlohmann::json::array();
  };

  std::map<std::string, Group> groups;
  for (const TraceData& trace : ctx.store.recent_traces(since_ns, 2000)) {
    for (const auto& kv : trace.spans) {
      const SpanData& span = kv.second;
      if (span.error_code.empty()) continue;
      Group& group = groups[span.error_code];
      group.traces.insert(trace.trace_id);
      group.services.insert(span.service);
      ++group.spans;
      if (group.first_ns == 0 || span.start_ns < group.first_ns) {
        group.first_ns = span.start_ns;
      }
      if (span.end_ns > group.last_ns) group.last_ns = span.end_ns;
      if (!trace.root_service.empty()) {
        group.root_services.insert(trace.root_service);
      }

      if (group.sample_trace_id.empty()) {
        group.sample_trace_id = trace.trace_id;
        group.sample_request_id = trace.request_id;
        // 从错误 Span 沿 parent 回溯到根，形成跨服务错误传播路径。
        std::vector<const SpanData*> path;
        const SpanData* current = &span;
        std::set<std::string> visited;
        while (current != nullptr && !current->span.empty() &&
               visited.insert(current->span).second) {
          path.push_back(current);
          if (current->parent.empty()) break;
          auto parent_it = trace.spans.find(current->parent);
          current = parent_it == trace.spans.end() ? nullptr : &parent_it->second;
        }
        std::reverse(path.begin(), path.end());
        for (const SpanData* node : path) {
          group.sample_path.push_back(
              {{"service", node->service},
               {"name", node->name},
               {"error_code", node->error_code},
               {"duration_ms", static_cast<double>(node->duration_ns()) / 1000000.0}});
        }
      }
    }
  }

  std::vector<std::pair<std::string, Group>> ordered(groups.begin(),
                                                     groups.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const std::pair<std::string, Group>& a,
               const std::pair<std::string, Group>& b) {
              return a.second.last_ns > b.second.last_ns;
            });

  nlohmann::json out = nlohmann::json::array();
  for (const auto& item : ordered) {
    if (out.size() >= limit) break;
    const Group& group = item.second;
    out.push_back(
        {{"code", item.first},
         {"trace_count", group.traces.size()},
         {"span_count", group.spans},
         {"services", std::vector<std::string>(group.services.begin(),
                                               group.services.end())},
         {"root_services",
          std::vector<std::string>(group.root_services.begin(),
                                   group.root_services.end())},
         {"first_ns", group.first_ns},
         {"first", iso_time(group.first_ns)},
         {"last_ns", group.last_ns},
         {"last", iso_time(group.last_ns)},
         {"sample_trace_id", group.sample_trace_id},
         {"sample_request_id", group.sample_request_id},
         {"sample_path", group.sample_path}});
  }
  return out;
}

nlohmann::json services_json(ServerContext& ctx) {
  struct Aggregate {
    std::string service;
    size_t instances = 0;
    size_t alive = 0;
    uint64_t events = 0;
    uint64_t errors = 0;
    uint64_t last_seen = 0;
    std::set<std::string> nodes;
    std::set<std::string> versions;
    nlohmann::json instance_list = nlohmann::json::array();
    // 近 5 分钟服务级性能（来自 Span 而不是日志）：
    // 调用量、错误 Span、P95 耗时，对应竞品 APM 的“服务性能分析”。
    uint64_t spans = 0;
    uint64_t error_spans = 0;
    std::vector<uint64_t> durations_ns;
  };

  std::map<std::string, Aggregate> aggregates;
  for (const InstanceRecord& instance : ctx.store.catalog().instances()) {
    Aggregate& agg = aggregates[instance.service];
    agg.service = instance.service;
    ++agg.instances;
    if (instance.alive) ++agg.alive;
    agg.events += instance.event_count;
    agg.errors += instance.error_count;
    agg.last_seen = std::max(agg.last_seen, instance.last_seen_ns);
    if (!instance.node.empty()) agg.nodes.insert(instance.node);
    if (!instance.version.empty()) agg.versions.insert(instance.version);
    agg.instance_list.push_back(instance_to_json(instance));
  }

  const uint64_t now = now_ns();
  const uint64_t since = now > 300000000000ull ? now - 300000000000ull : 0;
  for (const TraceData& trace : ctx.store.recent_traces(since, 2000)) {
    for (const auto& kv : trace.spans) {
      const SpanData& span = kv.second;
      if (span.service.empty()) continue;
      Aggregate& agg = aggregates[span.service];
      agg.service = span.service;
      ++agg.spans;
      if (span.status == "error" || !span.error_code.empty()) ++agg.error_spans;
      agg.durations_ns.push_back(span.duration_ns());
      agg.last_seen = std::max(agg.last_seen, span.end_ns);
    }
  }

  nlohmann::json out = nlohmann::json::array();
  for (auto& kv : aggregates) {
    Aggregate& agg = kv.second;
    std::sort(agg.durations_ns.begin(), agg.durations_ns.end());
    double p95_ms = 0.0;
    if (!agg.durations_ns.empty()) {
      size_t index = static_cast<size_t>(agg.durations_ns.size() * 0.95);
      if (index >= agg.durations_ns.size()) index = agg.durations_ns.size() - 1;
      p95_ms = static_cast<double>(agg.durations_ns[index]) / 1000000.0;
    }
    const double error_rate =
        agg.spans == 0 ? 0.0
                       : static_cast<double>(agg.error_spans) * 100.0 /
                             static_cast<double>(agg.spans);
    out.push_back({{"service", agg.service},
                   {"instances", agg.instances},
                   {"alive_instances", agg.alive},
                   {"event_count", agg.events},
                   {"error_count", agg.errors},
                   {"last_seen_ns", agg.last_seen},
                   {"last_seen", iso_time(agg.last_seen)},
                   {"span_count_5m", agg.spans},
                   {"error_spans_5m", agg.error_spans},
                   {"error_rate_5m", error_rate},
                   {"qps_5m", static_cast<double>(agg.spans) / 300.0},
                   {"p95_ms", p95_ms},
                   {"nodes", std::vector<std::string>(agg.nodes.begin(),
                                                      agg.nodes.end())},
                   {"versions", std::vector<std::string>(agg.versions.begin(),
                                                         agg.versions.end())},
                   {"instance_details", agg.instance_list}});
  }
  return out;
}

nlohmann::json processes_json(ServerContext& ctx, const std::string& node_filter,
                              const std::string& text_filter,
                              size_t limit) {
  nlohmann::json out = nlohmann::json::array();
  for (const NodeRecord& node : ctx.store.catalog().nodes()) {
    if (!node_filter.empty() && node.name != node_filter) continue;
    for (const auto& kv : node.processes) {
      const ProcessRecord& process = kv.second;
      if (!text_filter.empty() &&
          process.cmdline.find(text_filter) == std::string::npos &&
          process.name.find(text_filter) == std::string::npos) {
        continue;
      }
      out.push_back(process_to_json(process));
      if (out.size() >= limit) return out;
    }
  }
  return out;
}


nlohmann::json trace_context_json(ServerContext& ctx, const TraceData& trace) {
  std::set<std::string> trace_nodes;
  std::set<std::pair<std::string, int32_t>> trace_processes;
  for (const auto& kv : trace.spans) {
    const SpanData& span = kv.second;
    if (!span.node.empty()) trace_nodes.insert(span.node);
    if (span.pid != 0) trace_processes.insert({span.node, span.pid});
  }

  nlohmann::json nodes = nlohmann::json::array();
  nlohmann::json processes = nlohmann::json::array();
  for (const NodeRecord& node : ctx.store.catalog().nodes()) {
    if (!trace_nodes.count(node.name)) continue;
    nodes.push_back({{"name", node.name},
                     {"os", node.os},
                     {"kernel", node.kernel},
                     {"arch", node.arch},
                     {"load1", node.load1},
                     {"load5", node.load5},
                     {"load15", node.load15},
                     {"mem_total_kb", node.mem_total_kb},
                     {"mem_available_kb", node.mem_available_kb},
                     {"uptime_seconds", node.uptime_seconds},
                     {"last_seen_ns", node.last_seen_ns},
                     {"last_seen", iso_time(node.last_seen_ns)}});
    for (const auto& proc : node.processes) {
      if (!trace_processes.count({node.name, proc.second.pid})) continue;
      processes.push_back({{"node", node.name},
                           {"pid", proc.second.pid},
                           {"ppid", proc.second.ppid},
                           {"name", proc.second.name},
                           {"cmdline", proc.second.cmdline},
                           {"state", proc.second.state},
                           {"rss_kb", proc.second.rss_kb},
                           {"threads", proc.second.threads},
                           {"alive", proc.second.alive},
                           {"last_seen_ns", proc.second.last_seen_ns},
                           {"last_seen", iso_time(proc.second.last_seen_ns)}});
    }
  }

  const uint64_t window = 60000000000ull;
  const uint64_t since = trace.first_ns > window ? trace.first_ns - window : 0;
  const uint64_t until = trace.last_ns + window;
  nlohmann::json metrics = nlohmann::json::array();
  auto summarize = [&](const std::string& name,
                       const std::function<bool(const MetricPoint&)>& match) {
    double min = 0.0;
    double max = 0.0;
    double last = 0.0;
    size_t count = 0;
    for (const MetricPoint& point : ctx.store.query_metrics(name, since, 1000)) {
      if (point.time_ns < since || point.time_ns > until) continue;
      if (!match(point)) continue;
      if (count == 0) min = max = point.value;
      min = std::min(min, point.value);
      max = std::max(max, point.value);
      last = point.value;
      ++count;
    }
    if (count > 0) {
      metrics.push_back({{"name", name},
                         {"count", count},
                         {"min", min},
                         {"max", max},
                         {"last", last}});
    }
  };
  summarize("host.load1", [&](const MetricPoint& p) {
    auto it = p.labels.find("node");
    return it != p.labels.end() && trace_nodes.count(it->second) != 0;
  });
  summarize("host.mem_used_ratio", [&](const MetricPoint& p) {
    auto it = p.labels.find("node");
    return it != p.labels.end() && trace_nodes.count(it->second) != 0;
  });
  summarize("process.rss_bytes", [&](const MetricPoint& p) {
    auto node_it = p.labels.find("node");
    auto pid_it = p.labels.find("pid");
    if (node_it == p.labels.end() || pid_it == p.labels.end()) return false;
    int32_t pid = std::atoi(pid_it->second.c_str());
    return trace_processes.count({node_it->second, pid}) != 0;
  });

  return {{"nodes", nodes},
          {"processes", processes},
          {"metrics", metrics},
          {"trace_window",
           {{"start_ns", trace.first_ns},
            {"end_ns", trace.last_ns},
            {"start", iso_time(trace.first_ns)},
            {"end", iso_time(trace.last_ns)}}}};
}

nlohmann::json trace_detail_json(ServerContext& ctx, const std::string& trace_id) {
  TraceData trace;
  if (!ctx.store.get_trace(trace_id, &trace)) {
    return nlohmann::json();
  }
  const TraceAnalysis analysis = analyze_trace(trace);
  nlohmann::json out = analysis_to_json(trace, analysis);

  LogQuery log_query;
  log_query.trace = trace_id;
  log_query.limit = 500;
  nlohmann::json logs = nlohmann::json::array();
  for (const LogData& log : ctx.store.query_logs(log_query)) {
    logs.push_back(log_to_json(log));
  }
  out["logs"] = logs;
  out["context"] = trace_context_json(ctx, trace);
  return out;
}

nlohmann::json request_detail_json(ServerContext& ctx,
                                   const std::string& request_id) {
  nlohmann::json out;
  out["request_id"] = request_id;
  nlohmann::json traces = nlohmann::json::array();
  for (const std::string& trace_id :
       ctx.store.traces_for_request(request_id, 50)) {
    const nlohmann::json detail = trace_detail_json(ctx, trace_id);
    if (!detail.is_null()) traces.push_back(detail);
  }
  out["traces"] = traces;

  LogQuery log_query;
  log_query.request_id = request_id;
  log_query.limit = 500;
  nlohmann::json logs = nlohmann::json::array();
  for (const LogData& log : ctx.store.query_logs(log_query)) {
    logs.push_back(log_to_json(log));
  }
  out["logs"] = logs;

  nlohmann::json clusters = nlohmann::json::array();
  for (const LogClusterInfo& cluster : ctx.store.log_clusters(200)) {
    clusters.push_back(cluster_to_json(cluster));
  }
  out["log_clusters"] = clusters;
  return out;
}

nlohmann::json metrics_json(ServerContext& ctx, const std::string& name,
                            uint64_t since_ns, size_t limit) {
  std::vector<MetricPoint> points =
      name.empty() ? ctx.store.recent_metrics(since_ns, limit)
                   : ctx.store.query_metrics(name, since_ns, limit);
  nlohmann::json out = nlohmann::json::array();
  for (const MetricPoint& point : points) {
    nlohmann::json labels = nlohmann::json::object();
    for (const auto& kv : point.labels) labels[kv.first] = kv.second;
    out.push_back({{"name", point.name},
                   {"time_ns", point.time_ns},
                   {"time", iso_time(point.time_ns)},
                   {"value", point.value},
                   {"labels", labels}});
  }
  return out;
}

nlohmann::json topology_json(ServerContext& ctx, uint64_t since_ns,
                             size_t limit) {
  if (since_ns == 0) {
    const uint64_t now = now_ns();
    since_ns = now > 300000000000ull ? now - 300000000000ull : 0;
  }
  const std::vector<TraceData> traces = ctx.store.recent_traces(since_ns, limit);
  const std::vector<ServiceEdge> edges = build_topology(traces);
  nlohmann::json out = nlohmann::json::array();
  for (const ServiceEdge& edge : edges) {
    out.push_back({{"caller", edge.caller},
                   {"callee", edge.callee},
                   {"method", edge.method},
                   {"count", edge.count},
                   {"error_count", edge.error_count},
                   {"avg_duration_ns",
                    edge.count == 0 ? 0 : edge.total_duration_ns / edge.count},
                   {"max_duration_ns", edge.max_duration_ns},
                   {"confidence", edge.confidence}});
  }
  return {{"edges", out}, {"count", out.size()}};
}

nlohmann::json rules_json(ServerContext& ctx) {
  nlohmann::json out = nlohmann::json::array();
  for (const Rule& rule : ctx.rules.rules()) out.push_back(rule.to_json());
  return out;
}

nlohmann::json mcp_tools() {
  auto schema = [](const nlohmann::json& properties) {
    return nlohmann::json{{"type", "object"}, {"properties", properties}};
  };
  nlohmann::json tools = nlohmann::json::array();
  tools.push_back({{"name", "query_logs"},
                   {"description", "按 trace_id / request_id / service / 级别查询日志"},
                   {"inputSchema",
                    schema({{"trace_id", {{"type", "string"}}},
                            {"request_id", {{"type", "string"}}},
                            {"service", {{"type", "string"}}},
                            {"level", {{"type", "string"}}},
                            {"text", {{"type", "string"}}},
                            {"since_ns", {{"type", "integer"}}},
                            {"limit", {{"type", "integer"}}}})}});
  tools.push_back({{"name", "query_metrics"},
                   {"description", "查询指标序列"},
                   {"inputSchema",
                    schema({{"name", {{"type", "string"}}},
                            {"since_ns", {{"type", "integer"}}},
                            {"limit", {{"type", "integer"}}}})}});
  tools.push_back({{"name", "get_trace"},
                   {"description", "获取一条完整调用链（Span 树 + 关键路径 + 日志）"},
                   {"inputSchema", schema({{"trace_id", {{"type", "string"}}}})}});
  tools.push_back(
      {{"name", "get_request_chain"},
       {"description", "按 request_id 获取跨进程链路与日志"},
       {"inputSchema", schema({{"request_id", {{"type", "string"}}}})}});
  tools.push_back({{"name", "query_traces"},
                   {"description", "按 trace/request/task/服务/错误/耗时查询调用链"},
                   {"inputSchema",
                    schema({{"trace_id", {{"type", "string"}}},
                            {"request_id", {{"type", "string"}}},
                            {"task_id", {{"type", "string"}}},
                            {"service", {{"type", "string"}}},
                            {"error_code", {{"type", "string"}}},
                            {"q", {{"type", "string"}}},
                            {"error", {{"type", "boolean"}}},
                            {"min_duration_ns", {{"type", "integer"}}},
                            {"limit", {{"type", "integer"}}}})}});
  tools.push_back({{"name", "get_topology"},
                   {"description", "获取服务级调用拓扑边"},
                   {"inputSchema",
                    schema({{"since_ns", {{"type", "integer"}}},
                            {"limit", {{"type", "integer"}}}})}});
  tools.push_back({{"name", "list_slos"},
                   {"description", "列出 SLO 定义与实时 SLI/错误预算"},
                   {"inputSchema", schema(nlohmann::json::object())}});
  tools.push_back({{"name", "get_process_snapshot"},
                   {"description", "获取节点进程快照"},
                   {"inputSchema",
                    schema({{"node", {{"type", "string"}}},
                            {"q", {{"type", "string"}}}})}});
  tools.push_back({{"name", "run_diagnosis"},
                   {"description", "对 trace/request/service 执行只读诊断"},
                   {"inputSchema",
                    schema({{"trace_id", {{"type", "string"}}},
                            {"request_id", {{"type", "string"}}},
                            {"service", {{"type", "string"}}},
                            {"node", {{"type", "string"}}},
                            {"pid", {{"type", "integer"}}},
                            {"collect_stack", {{"type", "boolean"}}}})}});
  tools.push_back({{"name", "create_bundle"},
                   {"description", "生成脱敏证据包（内部版）"},
                   {"inputSchema",
                    schema({{"incident", {{"type", "string"}}},
                            {"service", {{"type", "string"}}},
                            {"trace_id", {{"type", "string"}}},
                            {"request_id", {{"type", "string"}}},
                            {"window_ns", {{"type", "integer"}}}})}});
  tools.push_back({{"name", "list_events"},
                   {"description", "列出事件中心最近的告警/崩溃/变更"},
                   {"inputSchema", schema({{"limit", {{"type", "integer"}}}})}});
  return tools;
}

nlohmann::json mcp_call_tool(ServerContext& ctx, const std::string& name,
                             const nlohmann::json& args) {
  nlohmann::json result;
  if (name == "query_logs") {
    LogQuery query;
    query.trace = args.value("trace_id", std::string());
    query.request_id = args.value("request_id", std::string());
    query.service = args.value("service", std::string());
    query.level = args.value("level", std::string());
    query.text = args.value("text", std::string());
    query.since_ns = args.value("since_ns", static_cast<uint64_t>(0));
    query.limit = args.value("limit", 200u);
    nlohmann::json logs = nlohmann::json::array();
    for (const LogData& log : ctx.store.query_logs(query)) {
      logs.push_back(log_to_json(log));
    }
    result = {{"logs", logs}};
  } else if (name == "query_metrics") {
    const std::string metric = args.value("name", std::string());
    const uint64_t since = args.value("since_ns", static_cast<uint64_t>(0));
    const size_t limit = args.value("limit", 1000u);
    result = {{"points", metrics_json(ctx, metric, since, limit)}};
  } else if (name == "get_trace") {
    result = trace_detail_json(ctx, args.value("trace_id", std::string()));
  } else if (name == "get_request_chain") {
    result = request_detail_json(ctx, args.value("request_id", std::string()));
  } else if (name == "query_traces") {
    TraceQuery query;
    query.trace_id = args.value("trace_id", std::string());
    query.request_id = args.value("request_id", std::string());
    query.task_id = args.value("task_id", std::string());
    query.service = args.value("service", std::string());
    query.error_code = args.value("error_code", std::string());
    query.text = args.value("q", std::string());
    query.errors_only = args.value("error", false);
    query.min_duration_ns =
        args.value("min_duration_ns", static_cast<uint64_t>(0));
    query.limit = args.value("limit", 100u);
    nlohmann::json traces = nlohmann::json::array();
    for (const TraceSummary& summary : ctx.store.query_traces(query)) {
      traces.push_back(summary_json(summary));
    }
    result = {{"traces", traces}, {"count", traces.size()}};
  } else if (name == "get_topology") {
    result = topology_json(
        ctx, args.value("since_ns", static_cast<uint64_t>(0)),
        args.value("limit", static_cast<size_t>(500)));
  } else if (name == "list_slos") {
    result = {{"slos", ctx.slos.statuses(ctx.store, now_ns())}};
  } else if (name == "get_process_snapshot") {
    result = {{"processes",
               processes_json(ctx, args.value("node", std::string()),
                              args.value("q", std::string()), 1000)}};
  } else if (name == "run_diagnosis") {
    DiagnoseRequest request;
    request.trace_id = args.value("trace_id", std::string());
    request.request_id = args.value("request_id", std::string());
    request.service = args.value("service", std::string());
    request.node = args.value("node", std::string());
    request.pid = args.value("pid", 0);
    request.collect_stack = args.value("collect_stack", false);
    result = Diagnostics::diagnose(ctx.store, ctx.crashes, ctx.profiles,
                                   ctx.commands, request);
  } else if (name == "create_bundle") {
    EvidenceOptions options;
    options.incident = args.value("incident", std::string("mcp"));
    options.service = args.value("service", std::string());
    options.trace_id = args.value("trace_id", std::string());
    options.request_id = args.value("request_id", std::string());
    const uint64_t window_ns = args.value("window_ns", static_cast<uint64_t>(0));
    if (window_ns != 0) options.window_start_ns = now_ns() - window_ns;
    options.salt = ctx.config.evidence_salt;
    const EvidencePack pack =
        EvidenceBuilder::build(ctx.store, ctx.crashes, ctx.profiles, options,
                               ctx.config.evidence_dir);
    result = {{"ok", pack.ok()},
              {"dir", pack.dir},
              {"manifest", pack.manifest},
              {"error", pack.error}};
  } else if (name == "list_events") {
    nlohmann::json events = nlohmann::json::array();
    for (const EventRecord& event :
         ctx.store.list_events(args.value("limit", 50u))) {
      events.push_back(event_to_json(event));
    }
    result = {{"events", events}};
  } else {
    throw std::runtime_error("未知工具: " + name);
  }
  return result;
}

nlohmann::json mcp_handle(ServerContext& ctx, const nlohmann::json& request) {
  const nlohmann::json id = request.contains("id") ? request["id"]
                                                  : nlohmann::json();
  const std::string method = request.value("method", std::string());
  auto ok = [&id](const nlohmann::json& result) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  };
  auto fail = [&id](int code, const std::string& message) {
    return nlohmann::json{{"jsonrpc", "2.0"},
                          {"id", id},
                          {"error", {{"code", code}, {"message", message}}}};
  };

  if (method == "initialize") {
    return ok({{"protocolVersion", "2024-11-05"},
               {"capabilities", {{"tools", nlohmann::json::object()}}},
               {"serverInfo", {{"name", "vmon-server"}, {"version", kServerVersion}}}});
  }
  if (method == "notifications/initialized") {
    return ok(nlohmann::json::object());
  }
  if (method == "ping") {
    return ok(nlohmann::json::object());
  }
  if (method == "tools/list") {
    return ok({{"tools", mcp_tools()}});
  }
  if (method == "tools/call") {
    const nlohmann::json params = request.value("params", nlohmann::json::object());
    const std::string name = params.value("name", std::string());
    const nlohmann::json args = params.value("arguments", nlohmann::json::object());
    try {
      const nlohmann::json result = mcp_call_tool(ctx, name, args);
      nlohmann::json content = nlohmann::json::array();
      nlohmann::json item;
      item["type"] = "text";
      item["text"] = result.dump();
      content.push_back(item);
      return ok({{"content", content}, {"isError", false}});
    } catch (const std::exception& ex) {
      const std::string message = ex.what();
      // 未知工具属于参数错误（-32602）；其余执行异常按内部错误返回。
      const int code =
          message.compare(0, 12, "未知工具") == 0 ? -32602 : -32603;
      return fail(code, message);
    }
  }
  return fail(-32601, "不支持的方法: " + method);
}

nlohmann::json evidence_response(ServerContext& ctx,
                                 const nlohmann::json& body) {
  if (!ctx.config.enable_evidence) {
    return {{"ok", false}, {"error", "证据包功能未启用（内部版专属）"}};
  }
  EvidenceOptions options;
  options.incident = body.value("incident", std::string("manual"));
  options.severity = body.value("severity", std::string("warning"));
  options.service = body.value("service", std::string());
  options.trace_id = body.value("trace_id", std::string());
  options.request_id = body.value("request_id", std::string());
  options.window_start_ns = body.value("window_start_ns", static_cast<uint64_t>(0));
  options.window_end_ns = body.value("window_end_ns", static_cast<uint64_t>(0));
  options.redact = body.value("redact", true);
  options.salt = body.value("salt", ctx.config.evidence_salt);
  if (body.contains("window_ns")) {
    const uint64_t window_ns = body.value("window_ns", static_cast<uint64_t>(0));
    if (window_ns != 0) options.window_start_ns = now_ns() - window_ns;
  }
  const EvidencePack pack = EvidenceBuilder::build(
      ctx.store, ctx.crashes, ctx.profiles, options, ctx.config.evidence_dir);
  return {{"ok", pack.ok()},
          {"id", pack.id},
          {"dir", pack.dir},
          {"files", pack.files},
          {"manifest", pack.manifest},
          {"error", pack.error}};
}

}  // namespace

ServerContext::ServerContext(const ServerConfig& server_config)
    : config(server_config),
      store(make_store_config(server_config)),
      crashes(1000),
      profiles(64),
      rules(),
      slos(),
      forwarder(make_forwarder_config(server_config)),
      pipeline(),
      notifier(make_notifier_config(server_config)) {
  started_ns = now_ns();
  if (config.evidence_dir.empty()) config.evidence_dir = config.state_dir + "/evidence";
  rules_path = config.state_dir + "/rules.json";
  slos_path = config.state_dir + "/slos.json";
  silences_path = config.state_dir + "/silences.json";
  std::string silence_error;
  if (!silences.load(silences_path, &silence_error)) {
    std::cerr << "静默配置加载失败: " << silence_error << std::endl;
  }
  issues_path = config.state_dir + "/issues.json";
  std::string issue_error;
  if (!issues.load(issues_path, &issue_error)) {
    std::cerr << "Issue 状态加载失败: " << issue_error << std::endl;
  }
  dashboards_path = config.state_dir + "/dashboards.json";
  std::string dashboard_error;
  if (!dashboards.load(dashboards_path, &dashboard_error)) {
    std::cerr << "仪表板加载失败: " << dashboard_error << std::endl;
  }
  rules.set_silence_matcher(
      [this](const Rule& rule, const std::string& service,
             const std::string& node, const std::string& severity,
             uint64_t now) {
        return silences.matches(rule.id, rule.type, service, node, severity,
                                now);
      });
  std::string pipeline_error;
  if (!pipeline.configure(config.pipeline, &pipeline_error)) {
    std::cerr << "Pipeline 配置失败，已关闭脱敏: " << pipeline_error
              << std::endl;
  }
  // 所有事件（规则、崩溃、手工注入）统一进入通知队列。
  store.set_event_sink(
      [this](const EventRecord& event) { notifier.enqueue(event); });
  load_rules();
  load_slos();
}

void ServerContext::load_rules() {
  std::ifstream in(rules_path);
  if (!in) {
    save_rules();
    return;
  }
  try {
    nlohmann::json json;
    in >> json;
    if (json.is_array() && !json.empty()) {
      std::vector<Rule> loaded;
      for (const auto& item : json) loaded.push_back(Rule::from_json(item));
      rules.set_rules(loaded);
    }
  } catch (const std::exception&) {
    // 配置损坏时保留内存中的默认规则，避免服务起不来。
  }
}

void ServerContext::save_rules() {
  ensure_directory(config.state_dir);
  nlohmann::json json = nlohmann::json::array();
  for (const Rule& rule : rules.rules()) json.push_back(rule.to_json());
  std::ofstream out(rules_path, std::ios::trunc);
  if (out) out << json.dump(2) << "\n";
}

void ServerContext::load_slos() {
  std::string error;
  if (!slos.load(slos_path, &error)) {
    // 文件损坏不阻塞服务启动；下一次保存会覆盖它。
    std::cerr << "SLO 加载失败: " << error << std::endl;
  }
}

bool ServerContext::save_slos(std::string* error) {
  return slos.save(slos_path, error);
}

void ServerContext::start_background() {
  if (running_.exchange(true)) return;
  forwarder.start();
  notifier.start();
  background_ = std::thread(&ServerContext::background_loop, this);
}

void ServerContext::stop_background() {
  if (!running_.exchange(false)) return;
  if (background_.joinable()) background_.join();
  notifier.stop();
  forwarder.stop();
}

void ServerContext::background_loop() {
  uint64_t last_eval = 0;
  while (running_.load()) {
    const uint64_t now = now_ns();
    if (last_eval == 0 || now - last_eval >= config.rule_interval_ms * 1000000ull) {
      last_eval = now;
      rules.evaluate(&store, now);
      store.prune();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

void register_routes(httplib::Server& server, ServerContext& ctx) {
  auto guard = [&ctx](const httplib::Request& req, httplib::Response& res) {
    if (authorized(req, ctx.config)) return true;
    send_error(res, 401, "未授权：请提供 X-Vmon-Token 或 Bearer Token");
    return false;
  };

  server.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers",
                   "Content-Type, X-Vmon-Token, Authorization");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.status = 204;
  });

  // ------------------------------------------------------------ 接入
  server.Post("/v1/ingest", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::string node =
        req.get_header_value("X-Vmon-Node").empty()
            ? param(req, "node")
            : req.get_header_value("X-Vmon-Node");
    const std::string agent =
        req.get_header_value("X-Vmon-Agent").empty()
            ? param(req, "agent")
            : req.get_header_value("X-Vmon-Agent");

    size_t accepted = 0;
    size_t rejected = 0;
    size_t offset = 0;
    const std::string& body = req.body;
    while (offset < body.size()) {
      const size_t newline = body.find('\n', offset);
      const std::string line =
          newline == std::string::npos
              ? body.substr(offset)
              : body.substr(offset, newline - offset);
      offset = newline == std::string::npos ? body.size() : newline + 1;
      if (line.empty() || line[0] == '\r') continue;
      try {
        const nlohmann::json json = nlohmann::json::parse(line);
        if (!json.is_object()) {
          ++rejected;
          continue;
        }
        common::Event event = common::from_json(json);
        if (event.type.empty()) {
          ++rejected;
          continue;
        }
        if (event.node.empty()) event.node = node;
        // 诊断版 SDK 的 SIGPROF 采样：折叠栈直接进入 Probe/ProfileStore。
        if (event.type == "profile") {
          auto folded = event.attrs.find("folded");
          if (folded != event.attrs.end() && !folded->second.empty()) {
            ctx.profiles.add(folded->second, event.service,
                             event.node.empty() ? node : event.node,
                             event.instance,
                             event.attrs.count("kind") ? event.attrs.at("kind")
                                                       : "cpu",
                             event.time_ns);
            ++accepted;
          }
          continue;
        }
        // 入库前脱敏：敏感字段不进入 Store，也不会出现在证据包/AI 上下文里。
        ctx.pipeline.process(&event);
        ctx.store.ingest(event, node, agent);
        ctx.rules.on_event(event, &ctx.store);
        ctx.forwarder.submit(event);
        ++accepted;
      } catch (const std::exception&) {
        ++rejected;
      }
    }
    send_json(res, {{"accepted", accepted}, {"rejected", rejected}});
  });

  server.Post("/v1/profile", [&ctx, guard](const httplib::Request& req,
                                           httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::string id = ctx.profiles.add(
        req.body, param(req, "service"), param(req, "node"),
        param(req, "instance"), param(req, "kind", "cpu"), now_ns());
    if (id.empty()) {
      send_error(res, 400, "折叠栈为空或格式不正确");
      return;
    }
    send_json(res, {{"id", id}});
  });

  server.Post("/v1/crash", [&ctx, guard](const httplib::Request& req,
                                         httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    const std::string id = ctx.crashes.add_json(body, param(req, "node"));
    EventRecord event;
    event.id = "crash-" + id;
    event.time_ns = now_ns();
    event.type = "crash";
    event.severity = "critical";
    event.title = "进程崩溃 " + body.value("signal", std::string());
    event.detail = body.value("reason", std::string());
    event.service = body.value("service", std::string());
    event.node = body.value("node", param(req, "node"));
    event.fingerprint = id;
    event.status = "open";
    ctx.store.add_event(event);
    send_json(res, {{"id", id}});
  });

  server.Post("/v1/events", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    EventRecord event;
    event.id = body.value("id", "evt-" + hex_hash(req.body).substr(0, 12));
    event.time_ns = body.value("time_ns", now_ns());
    event.type = body.value("type", std::string("change"));
    event.severity = body.value("severity", std::string("info"));
    event.title = body.value("title", std::string());
    event.detail = body.value("detail", std::string());
    event.service = body.value("service", std::string());
    event.node = body.value("node", std::string());
    event.fingerprint = body.value("fingerprint", std::string());
    event.status = "open";
    ctx.store.add_event(event);
    send_json(res, {{"id", event.id}});
  });

  // ------------------------------------------------------------ 查询
  server.Get("/api/health", [&ctx](const httplib::Request&, httplib::Response& res) {
    const StoreStats stats = ctx.store.stats();
    send_json(res, {{"status", "ok"},
                    {"version", kServerVersion},
                    {"started_ns", ctx.started_ns},
                    {"uptime_seconds",
                     (now_ns() - ctx.started_ns) / 1000000000ull},
                    {"nodes", ctx.store.catalog().node_count()},
                    {"instances", ctx.store.catalog().instance_count()},
                    {"traces", stats.trace_count},
                    {"logs", stats.log_count},
                    {"events_ingested", stats.events_ingested},
                    {"events_rejected", stats.events_rejected},
                    {"forwarder",
                     {{"enabled", ctx.forwarder.enabled()},
                      {"forwarded", ctx.forwarder.forwarded()},
                      {"dropped", ctx.forwarder.dropped()},
                      {"failed", ctx.forwarder.failed()}}}});
  });

  server.Get("/api/overview", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, overview_json(ctx));
  });

  server.Get("/api/services", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, services_json(ctx));
  });

  server.Get("/api/nodes", [&ctx, guard](const httplib::Request& req,
                                         httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json out = nlohmann::json::array();
    for (const NodeRecord& node : ctx.store.catalog().nodes()) {
      out.push_back(catalog_node_to_json(node));
    }
    send_json(res, out);
  });

  server.Get("/api/processes", [&ctx, guard](const httplib::Request& req,
                                             httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, processes_json(ctx, param(req, "node"), param(req, "q"),
                                  param_u64(req, "limit", 1000)));
  });

  server.Get("/api/traces", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    TraceQuery query;
    query.trace_id = param(req, "trace_id");
    query.request_id = param(req, "request_id");
    query.task_id = param(req, "task_id");
    query.service = param(req, "service");
    query.error_code = param(req, "error_code");
    query.text = param(req, "q");
    query.errors_only = param_bool(req, "error");
    query.since_ns = param_u64(req, "since_ns");
    query.min_duration_ns = param_u64(req, "min_duration_ns");
    query.limit = param_u64(req, "limit", 100);
    nlohmann::json traces = nlohmann::json::array();
    for (const TraceSummary& summary : ctx.store.query_traces(query)) {
      traces.push_back(summary_json(summary));
    }
    send_json(res, {{"traces", traces}, {"count", traces.size()}});
  });

  server.Get(R"(/api/traces/([0-9a-zA-Z_-]+))",
             [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
               if (!guard(req, res)) return;
               const std::string trace_id = req.matches[1].str();
               const nlohmann::json detail = trace_detail_json(ctx, trace_id);
               if (detail.is_null()) {
                 send_error(res, 404, "trace 不存在: " + trace_id);
                 return;
               }
               send_json(res, detail);
             });

  server.Get(R"(/api/requests/([^/]+))",
             [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
               if (!guard(req, res)) return;
               send_json(res, request_detail_json(ctx, req.matches[1].str()));
             });

  server.Get("/api/logs", [&ctx, guard](const httplib::Request& req,
                                        httplib::Response& res) {
    if (!guard(req, res)) return;
    LogQuery query;
    query.trace = param(req, "trace_id");
    query.request_id = param(req, "request_id");
    query.task_id = param(req, "task_id");
    query.service = param(req, "service");
    query.node = param(req, "node");
    query.level = param(req, "level");
    query.fingerprint = param(req, "fingerprint");
    query.text = param(req, "text");
    query.since_ns = param_u64(req, "since_ns");
    query.until_ns = param_u64(req, "until_ns");
    query.errors_only = param_bool(req, "errors_only");
    query.limit = param_u64(req, "limit", 500);
    nlohmann::json logs = nlohmann::json::array();
    for (const LogData& log : ctx.store.query_logs(query)) {
      logs.push_back(log_to_json(log));
    }
    send_json(res, {{"logs", logs}, {"count", logs.size()}});
  });

  server.Get("/api/logs/clusters", [&ctx, guard](const httplib::Request& req,
                                                 httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::string service = param(req, "service");
    const bool errors_only = param_bool(req, "errors_only");
    nlohmann::json clusters = nlohmann::json::array();
    for (const LogClusterInfo& cluster :
         ctx.store.log_clusters(param_u64(req, "limit", 200))) {
      if (!service.empty() && cluster.service != service) continue;
      if (errors_only && !cluster.is_error_level()) continue;
      clusters.push_back(cluster_to_json(cluster));
    }
    send_json(res, {{"clusters", clusters}, {"count", clusters.size()}});
  });

  server.Get("/api/topology", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, topology_json(ctx, param_u64(req, "since_ns"),
                                 param_u64(req, "limit", 500)));
  });

  server.Get("/api/metrics", [&ctx, guard](const httplib::Request& req,
                                           httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, metrics_json(ctx, param(req, "name"),
                                param_u64(req, "since_ns"),
                                param_u64(req, "limit", 2000)));
  });

  server.Get("/api/error-codes", [&ctx, guard](const httplib::Request& req,
                                               httplib::Response& res) {
    if (!guard(req, res)) return;
    uint64_t since = param_u64(req, "since_ns", 0);
    if (since == 0) {
      const uint64_t now = now_ns();
      since = now > 3600000000000ull ? now - 3600000000000ull : 0;
    }
    send_json(res, {{"error_codes",
                     error_codes_json(ctx, since,
                                      param_u64(req, "limit", 100))}});
  });

  server.Get("/api/probes", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    const uint64_t now = now_ns();
    const uint64_t since = now > 3600000000000ull
                               ? now - 3600000000000ull
                               : 0;
    std::map<std::string, nlohmann::json> latest;
    for (const MetricPoint& point : ctx.store.recent_metrics(since, 5000)) {
      if (point.name != "probe.up") continue;
      auto probe_it = point.labels.find("probe");
      const std::string probe =
          probe_it == point.labels.end() ? "default" : probe_it->second;
      auto existing = latest.find(probe);
      if (existing != latest.end() &&
          existing->second.value("time_ns", static_cast<uint64_t>(0)) >=
              point.time_ns) {
        continue;
      }
      nlohmann::json item;
      item["name"] = probe;
      item["up"] = point.value >= 1.0;
      item["time_ns"] = point.time_ns;
      item["time"] = iso_time(point.time_ns);
      for (const auto& kv : point.labels) item[kv.first] = kv.second;
      latest[probe] = item;
    }
    nlohmann::json out = nlohmann::json::array();
    for (const auto& kv : latest) out.push_back(kv.second);
    send_json(res, {{"probes", out}, {"count", out.size()}});
  });

  server.Get("/api/events", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    EventQuery query;
    query.status = param(req, "status");
    query.severity = param(req, "severity");
    query.service = param(req, "service");
    query.type = param(req, "type");
    query.limit = param_u64(req, "limit", 100);
    nlohmann::json events = nlohmann::json::array();
    for (const EventRecord& event : ctx.store.list_events(query)) {
      events.push_back(event_to_json(event));
    }
    send_json(res, {{"events", events}, {"count", events.size()}});
  });

  server.Post(R"(/api/events/([^/]+)/ack)",
              [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
                if (!guard(req, res)) return;
                const std::string id = req.matches[1].str();
                if (!ctx.store.acknowledge_event(id)) {
                  send_error(res, 404, "事件不存在: " + id);
                  return;
                }
                send_json(res, {{"ok", true}});
              });

  server.Post(R"(/api/events/([^/]+)/resolve)",
              [&ctx, guard](const httplib::Request& req,
                            httplib::Response& res) {
                if (!guard(req, res)) return;
                const std::string id = req.matches[1].str();
                if (!ctx.store.resolve_event(id)) {
                  send_error(res, 404, "事件不存在: " + id);
                  return;
                }
                send_json(res, {{"ok", true}});
              });

  // ------------------------------------------------------------ SLO
  server.Get("/api/slos", [&ctx, guard](const httplib::Request& req,
                                         httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, {{"slos", ctx.slos.statuses(ctx.store, now_ns())}});
  });

  server.Post("/api/slos", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    if (!ctx.slos.upsert_json(body, &error)) {
      send_error(res, 400, error);
      return;
    }
    if (!ctx.save_slos(&error)) {
      send_error(res, 500, error);
      return;
    }
    const std::string id = body.value("id", std::string());
    nlohmann::json statuses = ctx.slos.statuses(ctx.store, now_ns());
    for (const auto& item : statuses) {
      if (item.value("id", std::string()) == id) {
        send_json(res, {{"ok", true}, {"slo", item}});
        return;
      }
    }
    send_json(res, {{"ok", true}});
  });

  server.Delete(R"(/api/slos/([^/]+))",
                [&ctx, guard](const httplib::Request& req,
                              httplib::Response& res) {
                  if (!guard(req, res)) return;
                  const std::string id = req.matches[1].str();
                  if (!ctx.slos.remove(id)) {
                    send_error(res, 404, "SLO 不存在: " + id);
                    return;
                  }
                  std::string error;
                  if (!ctx.save_slos(&error)) {
                    send_error(res, 500, error);
                    return;
                  }
                  send_json(res, {{"ok", true}});
                });

  // ------------------------------------------------------------ 规则
  server.Get("/api/rules", [&ctx, guard](const httplib::Request& req,
                                         httplib::Response& res) {
    if (!guard(req, res)) return;
    send_json(res, {{"rules", rules_json(ctx)}});
  });

  server.Post("/api/rules", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    Rule rule = Rule::from_json(body);
    if (rule.id.empty()) {
      send_error(res, 400, "规则需要 id 或 name");
      return;
    }
    if (rule.type.empty()) {
      send_error(res, 400, "规则需要 type");
      return;
    }
    ctx.rules.add_rule(rule);
    ctx.save_rules();
    send_json(res, rule.to_json());
  });

  server.Delete(R"(/api/rules/([^/]+))",
                [&ctx, guard](const httplib::Request& req,
                              httplib::Response& res) {
                  if (!guard(req, res)) return;
                  const std::string id = req.matches[1].str();
                  if (!ctx.rules.remove_rule(id)) {
                    send_error(res, 404, "规则不存在: " + id);
                    return;
                  }
                  ctx.save_rules();
                  send_json(res, {{"ok", true}});
                });

  // ------------------------------------------------------------ 告警静默
  server.Get("/api/silences", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json out = nlohmann::json::array();
    for (const Silence& silence : ctx.silences.list()) {
      out.push_back(silence_to_json(silence));
    }
    send_json(res, {{"silences", out}});
  });

  server.Post("/api/silences", [&ctx, guard](const httplib::Request& req,
                                             httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    Silence silence;
    if (!ctx.silences.upsert_json(body, now_ns(), &silence, &error)) {
      send_error(res, 400, error);
      return;
    }
    if (!ctx.silences.save(ctx.silences_path, &error)) {
      send_error(res, 500, error);
      return;
    }
    send_json(res, {{"ok", true}, {"silence", silence_to_json(silence)}});
  });

  server.Delete(R"(/api/silences/([^/]+))",
                [&ctx, guard](const httplib::Request& req,
                              httplib::Response& res) {
                  if (!guard(req, res)) return;
                  const std::string id = req.matches[1].str();
                  if (!ctx.silences.remove(id)) {
                    send_error(res, 404, "静默不存在: " + id);
                    return;
                  }
                  std::string error;
                  if (!ctx.silences.save(ctx.silences_path, &error)) {
                    send_error(res, 500, error);
                    return;
                  }
                  send_json(res, {{"ok", true}});
                });

  // ------------------------------------------------------------ 异常追踪 Issue
  server.Get("/api/issues", [&ctx, guard](const httplib::Request& req,
                                          httplib::Response& res) {
    if (!guard(req, res)) return;
    IssueQuery query;
    query.status = param(req, "status");
    query.service = param(req, "service");
    query.level = param(req, "level");
    query.limit = param_u64(req, "limit", 100);
    nlohmann::json out = nlohmann::json::array();
    for (const IssueRecord& issue :
         ctx.issues.list(ctx.store, ctx.crashes, query)) {
      out.push_back(issue_to_json(issue));
    }
    send_json(res, {{"issues", out}, {"count", out.size()}});
  });

  server.Post(R"(/api/issues/([^/]+))",
              [&ctx, guard](const httplib::Request& req,
                            httplib::Response& res) {
                if (!guard(req, res)) return;
                nlohmann::json body;
                std::string error;
                if (!parse_body(req, &body, &error)) {
                  send_error(res, 400, error);
                  return;
                }
                const std::string id = req.matches[1].str();
                if (!ctx.issues.update(id, body.value("status", std::string()),
                                       body.value("assignee", std::string()),
                                       body.value("note", std::string()),
                                       &error)) {
                  send_error(res, 400, error);
                  return;
                }
                if (!ctx.issues.save(ctx.issues_path, &error)) {
                  send_error(res, 500, error);
                  return;
                }
                send_json(res, {{"ok", true}, {"id", id}});
              });

  // ------------------------------------------------------------ 数据洞察 / 快照
  server.Get("/api/dashboards", [&ctx, guard](const httplib::Request& req,
                                              httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json out = nlohmann::json::array();
    for (const Dashboard& dashboard : ctx.dashboards.list()) {
      out.push_back(DashboardStore::dashboard_to_json(dashboard));
    }
    send_json(res, {{"dashboards", out}});
  });

  server.Post("/api/dashboards", [&ctx, guard](const httplib::Request& req,
                                               httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    Dashboard dashboard;
    if (!ctx.dashboards.upsert_json(body, now_ns(), &dashboard, &error)) {
      send_error(res, 400, error);
      return;
    }
    if (!ctx.dashboards.save(ctx.dashboards_path, &error)) {
      send_error(res, 500, error);
      return;
    }
    send_json(res, {{"ok", true},
                    {"dashboard", DashboardStore::dashboard_to_json(dashboard)}});
  });

  server.Get(R"(/api/dashboards/([^/]+))",
             [&ctx, guard](const httplib::Request& req,
                           httplib::Response& res) {
               if (!guard(req, res)) return;
               Dashboard dashboard;
               if (!ctx.dashboards.get(req.matches[1].str(), &dashboard)) {
                 send_error(res, 404, "仪表板不存在");
                 return;
               }
               send_json(res, ctx.dashboards.data(dashboard, ctx.store,
                                                   now_ns()));
             });

  server.Delete(R"(/api/dashboards/([^/]+))",
                [&ctx, guard](const httplib::Request& req,
                              httplib::Response& res) {
                  if (!guard(req, res)) return;
                  if (!ctx.dashboards.remove(req.matches[1].str())) {
                    send_error(res, 404, "仪表板不存在");
                    return;
                  }
                  std::string error;
                  if (!ctx.dashboards.save(ctx.dashboards_path, &error)) {
                    send_error(res, 500, error);
                    return;
                  }
                  send_json(res, {{"ok", true}});
                });

  server.Post(R"(/api/dashboards/([^/]+)/snapshot)",
              [&ctx, guard](const httplib::Request& req,
                            httplib::Response& res) {
                if (!guard(req, res)) return;
                DashboardSnapshot snapshot;
                std::string error;
                if (!ctx.dashboards.create_snapshot(
                        req.matches[1].str(), ctx.store, now_ns(), &snapshot,
                        &error)) {
                  send_error(res, 404, error);
                  return;
                }
                if (!ctx.dashboards.save(ctx.dashboards_path, &error)) {
                  send_error(res, 500, error);
                  return;
                }
                send_json(res, {{"ok", true},
                                {"snapshot",
                                 DashboardStore::snapshot_to_json(snapshot)}});
              });

  server.Get("/api/snapshots", [&ctx, guard](const httplib::Request& req,
                                             httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json out = nlohmann::json::array();
    for (const DashboardSnapshot& snapshot : ctx.dashboards.snapshots()) {
      nlohmann::json item = DashboardStore::snapshot_to_json(snapshot);
      item.erase("data");
      out.push_back(item);
    }
    send_json(res, {{"snapshots", out}});
  });

  server.Get(R"(/api/snapshots/([^/]+))",
             [&ctx, guard](const httplib::Request& req,
                           httplib::Response& res) {
               if (!guard(req, res)) return;
               DashboardSnapshot snapshot;
               if (!ctx.dashboards.get_snapshot(req.matches[1].str(),
                                                &snapshot)) {
                 send_error(res, 404, "快照不存在");
                 return;
               }
               send_json(res, DashboardStore::snapshot_to_json(snapshot));
             });

  // ------------------------------------------------------------ 命令与动态级别
  server.Get("/v1/commands", [&ctx, guard](const httplib::Request& req,
                                           httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::vector<CommandRecord> pending = ctx.commands.pending_for(
        param(req, "node"), param(req, "agent"),
        param_u64(req, "limit", 50));
    nlohmann::json out = nlohmann::json::array();
    for (const CommandRecord& command : pending) {
      out.push_back(command_to_json(command));
    }
    send_json(res, {{"commands", out}});
  });

  server.Get("/api/commands", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json out = nlohmann::json::array();
    for (const CommandRecord& command : ctx.commands.list(param_u64(req, "limit", 100))) {
      out.push_back(command_to_json(command));
    }
    send_json(res, {{"commands", out}});
  });

  server.Post(R"(/v1/commands/([^/]+)/result)",
              [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
                if (!guard(req, res)) return;
                nlohmann::json body;
                std::string error;
                if (!parse_body(req, &body, &error)) {
                  send_error(res, 400, error);
                  return;
                }
                const std::string id = req.matches[1].str();
                if (!ctx.commands.complete(
                        id, body.value("status", std::string("done")),
                        body.value("result", std::string()),
                        body.value("error", std::string()))) {
                  send_error(res, 404, "命令不存在: " + id);
                  return;
                }
                send_json(res, {{"ok", true}});
              });

  server.Post("/api/control/log-level",
              [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
                if (!guard(req, res)) return;
                nlohmann::json body;
                std::string error;
                if (!parse_body(req, &body, &error)) {
                  send_error(res, 400, error);
                  return;
                }
                const std::string level = body.value("level", std::string());
                if (level != "trace" && level != "debug" && level != "info" &&
                    level != "warn" && level != "error" && level != "fatal") {
                  send_error(res, 400, "非法日志级别: " + level);
                  return;
                }
                const std::string service = body.value("service", std::string());
                const std::string instance = body.value("instance", std::string());
                const std::string node = body.value("node", std::string());
                const int32_t pid = body.value("pid", 0);
                const uint64_t ttl_ms = body.value("ttl_ms", 300000u);

                nlohmann::json args = {{"level", level}, {"ttl_ms", ttl_ms}};
                nlohmann::json ids = nlohmann::json::array();
                if (pid > 0 && !node.empty()) {
                  ids.push_back(ctx.commands.enqueue(node, std::string(), pid,
                                                     "set_log_level", args,
                                                     ttl_ms * 1000000ull + 60000000000ull));
                } else {
                  for (const InstanceRecord& record :
                       ctx.store.catalog().instances()) {
                    if (!service.empty() && record.service != service) continue;
                    if (!instance.empty() && record.instance != instance) continue;
                    if (!node.empty() && record.node != node) continue;
                    if (record.pid <= 0 || record.node.empty()) continue;
                    ids.push_back(ctx.commands.enqueue(
                        record.node, std::string(), record.pid, "set_log_level",
                        args, ttl_ms * 1000000ull + 60000000000ull));
                  }
                }
                if (ids.empty()) {
                  send_error(res, 404, "没有匹配的进程实例");
                  return;
                }
                send_json(res, {{"commands", ids}});
              });

  server.Post("/api/control/dump-stacks",
              [&ctx, guard](const httplib::Request& req,
                            httplib::Response& res) {
                if (!guard(req, res)) return;
                nlohmann::json body;
                std::string error;
                if (!parse_body(req, &body, &error)) {
                  send_error(res, 400, error);
                  return;
                }
                const std::string node = body.value("node", std::string());
                const int32_t pid = body.value("pid", 0);
                if (node.empty() || pid <= 0) {
                  send_error(res, 400, "需要 node 与 pid");
                  return;
                }
                const std::string id = ctx.commands.enqueue(
                    node, std::string(), pid, "dump_stacks",
                    nlohmann::json::object(), 60000000000ull);
                send_json(res, {{"command", id}});
              });

  // ------------------------------------------------------------ 诊断 / 巡检 / 证据
  server.Get("/api/inspection", [&ctx, guard](const httplib::Request& req,
                                              httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json checks = nlohmann::json::array();
    size_t ok = 0;
    size_t warn = 0;
    size_t fail = 0;
    for (const CheckResult& check : Diagnostics::inspect(
             ctx.store, ctx.crashes, param(req, "service"), ctx.config.state_dir)) {
      checks.push_back(check_json(check));
      if (check.status == "ok") ++ok;
      else if (check.status == "warn") ++warn;
      else if (check.status == "fail") ++fail;
    }
    send_json(res, {{"checks", checks},
                    {"summary", {{"ok", ok}, {"warn", warn}, {"fail", fail}}}});
  });

  server.Post("/api/diagnose", [&ctx, guard](const httplib::Request& req,
                                             httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    DiagnoseRequest request;
    request.trace_id = body.value("trace_id", std::string());
    request.request_id = body.value("request_id", std::string());
    request.service = body.value("service", std::string());
    request.instance = body.value("instance", std::string());
    request.node = body.value("node", std::string());
    request.pid = body.value("pid", 0);
    request.since_ns = body.value("since_ns", static_cast<uint64_t>(0));
    request.collect_stack = body.value("collect_stack", false);
    send_json(res, Diagnostics::diagnose(ctx.store, ctx.crashes, ctx.profiles,
                                         ctx.commands, request));
  });

  server.Post("/api/evidence", [&ctx, guard](const httplib::Request& req,
                                             httplib::Response& res) {
    if (!guard(req, res)) return;
    nlohmann::json body;
    std::string error;
    if (!parse_body(req, &body, &error)) {
      send_error(res, 400, error);
      return;
    }
    const nlohmann::json out = evidence_response(ctx, body);
    send_json(res, out, out.value("ok", false) ? 200 : 500);
  });

  // ------------------------------------------------------------ 崩溃 / 剖析
  server.Get("/api/crashes", [&ctx, guard](const httplib::Request& req,
                                           httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::string service = param(req, "service");
    nlohmann::json out = nlohmann::json::array();
    for (const CrashRecord& crash : ctx.crashes.list(param_u64(req, "limit", 100))) {
      if (!service.empty() && crash.service != service) continue;
      out.push_back(crash_to_json(crash));
    }
    send_json(res, {{"crashes", out}, {"count", out.size()}});
  });

  server.Get(R"(/api/crashes/([0-9a-f]+))",
             [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
               if (!guard(req, res)) return;
               CrashRecord crash;
               if (!ctx.crashes.get(req.matches[1].str(), &crash)) {
                 send_error(res, 404, "崩溃记录不存在");
                 return;
               }
               send_json(res, crash_to_json(crash));
             });

  server.Post("/api/crashes/symbolize",
              [guard](const httplib::Request& req, httplib::Response& res) {
                if (!guard(req, res)) return;
                nlohmann::json body;
                std::string error;
                if (!parse_body(req, &body, &error)) {
                  send_error(res, 400, error);
                  return;
                }
                const std::string binary = body.value("binary", std::string());
                std::vector<std::string> addresses;
                if (body.contains("addresses") && body["addresses"].is_array()) {
                  for (const auto& item : body["addresses"]) {
                    if (item.is_string()) addresses.push_back(item.get<std::string>());
                  }
                }
                symbolizer::Symbolizer tool;
                std::vector<symbolizer::Frame> frames;
                std::string symbol_error;
                if (!tool.symbolize(binary, addresses, &frames, &symbol_error)) {
                  send_error(res, 400, symbol_error);
                  return;
                }
                nlohmann::json out = nlohmann::json::array();
                for (const auto& frame : frames) {
                  out.push_back({{"address", frame.address},
                                 {"function", frame.function},
                                 {"file", frame.file},
                                 {"line", frame.line},
                                 {"resolved", frame.resolved}});
                }
                send_json(res, {{"frames", out}, {"tool", tool.tool()}});
              });

  server.Get("/api/profiles", [&ctx, guard](const httplib::Request& req,
                                            httplib::Response& res) {
    if (!guard(req, res)) return;
    const std::string service = param(req, "service");
    nlohmann::json out = nlohmann::json::array();
    for (const ProfileRecord& profile : ctx.profiles.list(param_u64(req, "limit", 50))) {
      if (!service.empty() && profile.service != service) continue;
      out.push_back(profile_to_json(profile, 15));
    }
    send_json(res, {{"profiles", out}, {"count", out.size()}});
  });

  server.Get(R"(/api/profiles/([0-9a-zA-Z_-]+))",
             [&ctx, guard](const httplib::Request& req, httplib::Response& res) {
               if (!guard(req, res)) return;
               ProfileRecord profile;
               if (!ctx.profiles.get(req.matches[1].str(), &profile)) {
                 send_error(res, 404, "剖析记录不存在");
                 return;
               }
               nlohmann::json out = profile_to_json(profile, 50);
               out["flamegraph"] = ProfileStore::flamegraph(profile);
               send_json(res, out);
             });

  // ------------------------------------------------------------ MCP
  server.Post("/mcp", [&ctx, guard](const httplib::Request& req,
                                    httplib::Response& res) {
    if (!guard(req, res)) return;
    if (!ctx.config.enable_mcp) {
      send_error(res, 404, "MCP 未启用");
      return;
    }
    try {
      const nlohmann::json request = nlohmann::json::parse(req.body);
      send_json(res, mcp_handle(ctx, request));
    } catch (const std::exception& ex) {
      send_json(res,
                {{"jsonrpc", "2.0"},
                 {"id", nullptr},
                 {"error", {{"code", -32700}, {"message", ex.what()}}}},
                400);
    }
  });

  // ------------------------------------------------------------ Web UI
  server.Get("/", [&ctx](const httplib::Request&, httplib::Response& res) {
    const std::string html = read_file(ctx.config.web_dir + "/index.html");
    if (html.empty()) {
      res.set_content(
          "<html><body><h1>vmon-server</h1><p>Web 资源未找到，"
          "请用 --web-dir 指定目录。</p></body></html>",
          "text/html; charset=utf-8");
      return;
    }
    res.set_content(html, "text/html; charset=utf-8");
  });

  if (!ctx.config.web_dir.empty()) {
    server.set_mount_point("/", ctx.config.web_dir);
  }
}

}  // namespace server
}  // namespace vmon
