#include "evidence.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#include <sys/stat.h>

#include "common/sha256.h"
#include "crash.h"
#include "log_cluster.h"
#include "profiler.h"
#include "store.h"
#include "trace_builder.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

std::string path_join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

bool write_text(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(out);
}

bool write_json(const std::string& path, const nlohmann::json& json) {
  return write_text(path, json.dump(2) + "\n");
}

uint64_t file_size(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<uint64_t>(st.st_size);
}

nlohmann::json span_json(const SpanData& span) {
  nlohmann::json j;
  j["trace_id"] = span.trace;
  j["span_id"] = span.span;
  if (!span.parent.empty()) j["parent_span_id"] = span.parent;
  j["service"] = span.service;
  j["instance"] = span.instance;
  j["node"] = span.node;
  j["version"] = span.version;
  j["name"] = span.name;
  if (!span.kind.empty()) j["kind"] = span.kind;
  j["start_ns"] = span.start_ns;
  j["end_ns"] = span.end_ns;
  j["duration_ns"] = span.duration_ns();
  j["pid"] = span.pid;
  j["tid"] = span.tid;
  if (!span.file.empty()) {
    j["file"] = span.file;
    j["line"] = span.line;
  }
  if (!span.status.empty()) j["status"] = span.status;
  if (!span.error_code.empty()) j["error_code"] = span.error_code;
  if (!span.attrs.empty()) {
    nlohmann::json attrs = nlohmann::json::object();
    for (const auto& kv : span.attrs) attrs[kv.first] = kv.second;
    j["attrs"] = attrs;
  }
  return j;
}

nlohmann::json node_json(const TraceNode& node) {
  nlohmann::json j = span_json(node.span);
  if (!node.children.empty()) {
    nlohmann::json children = nlohmann::json::array();
    for (const TraceNode& child : node.children) {
      children.push_back(node_json(child));
    }
    j["children"] = children;
  }
  return j;
}

nlohmann::json analysis_json(const TraceData& trace,
                             const TraceAnalysis& analysis) {
  nlohmann::json j;
  j["trace_id"] = trace.trace_id;
  j["request_id"] = trace.request_id;
  j["task_id"] = trace.task_id;
  j["summary"] = {
      {"start_ns", analysis.summary.start_ns},
      {"duration_ns", analysis.summary.duration_ns},
      {"span_count", analysis.summary.span_count},
      {"service_count", analysis.summary.service_count},
      {"depth", analysis.summary.depth},
      {"has_error", analysis.summary.has_error},
      {"first_error_span", analysis.summary.first_error_span},
      {"error_code", analysis.summary.error_code}};
  nlohmann::json roots = nlohmann::json::array();
  for (const TraceNode& root : analysis.roots) roots.push_back(node_json(root));
  j["roots"] = roots;
  nlohmann::json critical = nlohmann::json::array();
  for (const SpanData& span : analysis.critical_path) {
    critical.push_back(span_json(span));
  }
  j["critical_path"] = critical;
  if (analysis.has_error) j["first_error"] = span_json(analysis.first_error);
  if (analysis.slowest_span.span.size() > 0) {
    j["slowest_span"] = span_json(analysis.slowest_span);
  }
  return j;
}

std::string md_escape(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return out;
}

}  // namespace

std::string EvidenceBuilder::redact(const std::string& text,
                                    const std::string& salt) {
  std::string out = text;

  static const std::regex ip_re("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b");
  static const std::regex email_re(
      "\\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}\\b");
  static const std::regex secret_re(
      "(password|passwd|pwd|token|secret|authorization)(\\s*[=:]\\s*)"
      "(\"[^\"]*\"|'[^']*'|[^\\s,;]+)",
      std::regex::icase);
  static const std::regex home_re("(/home/|/Users/)([^/\\s:]+)");

  {
    std::smatch match;
    std::string result;
    std::string remaining = out;
    while (std::regex_search(remaining, match, ip_re)) {
      result += match.prefix().str();
      result += "<ip:" + common::pseudonymize(match.str(), salt) + ">";
      remaining = match.suffix().str();
    }
    result += remaining;
    out.swap(result);
  }
  {
    std::smatch match;
    std::string result;
    std::string remaining = out;
    while (std::regex_search(remaining, match, email_re)) {
      result += match.prefix().str();
      result += "<email:" + common::pseudonymize(match.str(), salt) + ">";
      remaining = match.suffix().str();
    }
    result += remaining;
    out.swap(result);
  }
  out = std::regex_replace(out, secret_re, "$1$2<redacted>");
  out = std::regex_replace(out, home_re, "$1<user>");
  return out;
}

EvidencePack EvidenceBuilder::build(Store& store, CrashStore& crashes,
                                    ProfileStore& profiles,
                                    const EvidenceOptions& options,
                                    const std::string& base_dir) {
  EvidencePack pack;
  const uint64_t now = now_ns();
  const uint64_t end = options.window_end_ns == 0 ? now : options.window_end_ns;
  const uint64_t start =
      options.window_start_ns == 0 ? (end > 900000000000ull ? end - 900000000000ull : 0)
                                   : options.window_start_ns;

  pack.id = hex_hash(options.incident + "|" + std::to_string(end)).substr(0, 12);
  const std::string pack_name = "evidence-" + options.incident + "-" +
                                std::to_string(end / 1000000ull) + "-" +
                                pack.id;
  const std::string root = path_join(base_dir, pack_name);
  if (!ensure_directory(root) || !ensure_directory(path_join(root, "profiles")) ||
      !ensure_directory(path_join(root, "dumps"))) {
    pack.error = "无法创建证据包目录: " + root;
    return pack;
  }
  const std::string salt =
      options.salt.empty() ? ("vmon-" + pack.id) : options.salt;

  // ---------------- 日志
  LogQuery log_query;
  log_query.trace = options.trace_id;
  log_query.request_id = options.request_id;
  log_query.service = options.service;
  log_query.since_ns = start;
  log_query.limit = options.max_logs;
  const std::vector<LogData> logs = store.query_logs(log_query);

  std::string logs_jsonl;
  std::vector<nlohmann::json> facts;
  for (const LogData& log : logs) {
    nlohmann::json j;
    j["time_ns"] = log.time_ns;
    j["time"] = iso_time(log.time_ns);
    j["node"] = log.node;
    j["service"] = log.service;
    j["instance"] = log.instance;
    j["level"] = log.level;
    j["msg"] = options.redact ? redact(log.msg, salt) : log.msg;
    if (!log.trace.empty()) j["trace_id"] = log.trace;
    if (!log.request_id.empty()) j["request_id"] = log.request_id;
    if (!log.task_id.empty()) j["task_id"] = log.task_id;
    if (!log.error_code.empty()) j["error_code"] = log.error_code;
    if (!log.fingerprint.empty()) j["fingerprint"] = log.fingerprint;
    logs_jsonl += j.dump();
    logs_jsonl.push_back('\n');
    if (log.is_error_level()) {
      facts.push_back({{"type", "error_log"},
                       {"time_ns", log.time_ns},
                       {"service", log.service},
                       {"node", log.node},
                       {"fingerprint", log.fingerprint},
                       {"message", j["msg"]},
                       {"trace_id", log.trace},
                       {"request_id", log.request_id}});
    }
  }
  pack.files.push_back(path_join(root, "logs.jsonl"));
  if (!write_text(pack.files.back(), logs_jsonl)) {
    pack.error = "写入 logs.jsonl 失败";
    return pack;
  }

  nlohmann::json clusters = nlohmann::json::array();
  for (const LogClusterInfo& cluster : store.log_clusters(200)) {
    if (!options.service.empty() && cluster.service != options.service) continue;
    clusters.push_back({{"fingerprint", cluster.id},
                        {"template", cluster.tmpl},
                        {"level", cluster.level},
                        {"service", cluster.service},
                        {"count", cluster.count},
                        {"first_ns", cluster.first_ns},
                        {"last_ns", cluster.last_ns},
                        {"sample", options.redact ? redact(cluster.sample, salt)
                                                  : cluster.sample},
                        {"nodes", cluster.nodes}});
  }
  pack.files.push_back(path_join(root, "log_clusters.json"));
  write_json(pack.files.back(), clusters);

  // ---------------- 链路
  std::vector<std::string> trace_ids;
  if (!options.trace_id.empty()) {
    trace_ids.push_back(options.trace_id);
  } else if (!options.request_id.empty()) {
    trace_ids = store.traces_for_request(options.request_id, options.max_traces);
  } else {
    TraceQuery trace_query;
    trace_query.service = options.service;
    trace_query.since_ns = start;
    trace_query.limit = options.max_traces;
    for (const TraceSummary& summary : store.query_traces(trace_query)) {
      trace_ids.push_back(summary.trace_id);
    }
  }

  nlohmann::json requests = nlohmann::json::array();
  for (const std::string& trace_id : trace_ids) {
    TraceData trace;
    if (!store.get_trace(trace_id, &trace)) continue;
    const TraceAnalysis analysis = analyze_trace(trace);
    requests.push_back(analysis_json(trace, analysis));
    if (analysis.has_error) {
      facts.push_back({{"type", "error_span"},
                       {"time_ns", analysis.first_error.start_ns},
                       {"service", analysis.first_error.service},
                       {"node", analysis.first_error.node},
                       {"span", analysis.first_error.name},
                       {"error_code", analysis.first_error.error_code},
                       {"trace_id", trace_id}});
    }
  }
  pack.files.push_back(path_join(root, "request.json"));
  write_json(pack.files.back(), requests);

  // ---------------- 指标
  nlohmann::json metrics = nlohmann::json::array();
  for (const MetricPoint& point :
       store.recent_metrics(start, options.max_metrics)) {
    nlohmann::json labels = nlohmann::json::object();
    for (const auto& kv : point.labels) labels[kv.first] = kv.second;
    metrics.push_back({{"name", point.name},
                       {"time_ns", point.time_ns},
                       {"value", point.value},
                       {"labels", labels}});
  }
  pack.files.push_back(path_join(root, "metrics.json"));
  write_json(pack.files.back(), metrics);

  // ---------------- 剖析
  size_t profile_index = 0;
  for (const ProfileRecord& profile : profiles.list(20)) {
    if (profile.time_ns < start) continue;
    if (!options.service.empty() && profile.service != options.service) continue;
    std::string folded;
    for (const auto& entry : profile.folded) {
      folded += entry.first + " " + std::to_string(entry.second) + "\n";
    }
    const std::string folded_path =
        path_join(root, "profiles/folded-" + std::to_string(profile_index) + ".txt");
    write_text(folded_path, folded);
    pack.files.push_back(folded_path);

    nlohmann::json top = nlohmann::json::array();
    for (const auto& entry : ProfileStore::top_self(profile, 50)) {
      top.push_back({{"function", entry.first}, {"samples", entry.second}});
    }
    const std::string top_path =
        path_join(root, "profiles/top-" + std::to_string(profile_index) + ".json");
    write_json(top_path, {{"id", profile.id},
                          {"kind", profile.kind},
                          {"service", profile.service},
                          {"node", profile.node},
                          {"total_samples", profile.total_samples},
                          {"top_self", top}});
    pack.files.push_back(top_path);
    ++profile_index;
  }

  // ---------------- 崩溃
  nlohmann::json dumps = nlohmann::json::array();
  for (const CrashRecord& crash : crashes.list(200)) {
    if (crash.last_ns < start) continue;
    if (!options.service.empty() && crash.service != options.service) continue;
    nlohmann::json frames = nlohmann::json::array();
    for (const CrashFrame& frame : crash.frames) {
      frames.push_back({{"address", frame.address},
                        {"function", frame.function},
                        {"file", options.redact ? redact(frame.file, salt)
                                                : frame.file},
                        {"line", frame.line},
                        {"resolved", frame.resolved}});
    }
    dumps.push_back({{"id", crash.id},
                     {"time_ns", crash.last_ns},
                     {"service", crash.service},
                     {"node", crash.node},
                     {"signal", crash.signal},
                     {"binary", crash.binary},
                     {"build_id", crash.build_id},
                     {"count", crash.count},
                     {"frames", frames},
                     {"report", options.redact ? redact(crash.report, salt)
                                               : crash.report}});
    facts.push_back({{"type", "crash"},
                     {"time_ns", crash.last_ns},
                     {"service", crash.service},
                     {"node", crash.node},
                     {"signal", crash.signal},
                     {"fingerprint", crash.id},
                     {"count", crash.count}});
  }
  pack.files.push_back(path_join(root, "dumps/crashes.json"));
  write_json(pack.files.back(), dumps);

  // ---------------- 时间线
  nlohmann::json timeline = nlohmann::json::array();
  for (const EventRecord& event : store.list_events(500)) {
    if (event.time_ns < start) continue;
    nlohmann::json item = {{"type", event.type},
                           {"time_ns", event.time_ns},
                           {"time", iso_time(event.time_ns)},
                           {"severity", event.severity},
                           {"title", event.title},
                           {"detail", options.redact ? redact(event.detail, salt)
                                                     : event.detail},
                           {"service", event.service},
                           {"node", event.node},
                           {"event_id", event.id}};
    timeline.push_back(item);
    facts.push_back({{"type", "alert"},
                     {"time_ns", event.time_ns},
                     {"service", event.service},
                     {"node", event.node},
                     {"severity", event.severity},
                     {"title", event.title},
                     {"event_id", event.id}});
  }
  for (const CrashRecord& crash : crashes.list(200)) {
    if (crash.last_ns < start) continue;
    timeline.push_back({{"type", "crash"},
                        {"time_ns", crash.last_ns},
                        {"time", iso_time(crash.last_ns)},
                        {"severity", "critical"},
                        {"title", "崩溃 " + crash.signal},
                        {"service", crash.service},
                        {"node", crash.node},
                        {"fingerprint", crash.id}});
  }
  std::sort(timeline.begin(), timeline.end(),
            [](const nlohmann::json& a, const nlohmann::json& b) {
              return a.value("time_ns", static_cast<uint64_t>(0)) <
                     b.value("time_ns", static_cast<uint64_t>(0));
            });
  pack.files.push_back(path_join(root, "timeline.json"));
  write_json(pack.files.back(), timeline);

  // ---------------- 事实索引
  std::string index_jsonl;
  for (const auto& fact : facts) {
    index_jsonl += fact.dump();
    index_jsonl.push_back('\n');
  }
  pack.files.push_back(path_join(root, "index.jsonl"));
  write_text(pack.files.back(), index_jsonl);

  // ---------------- 事实清单与待验证假设
  const StoreStats stats = store.stats();
  std::ostringstream md;
  md << "# 证据包 " << pack.id << "\n\n";
  md << "- 生成时间: " << iso_time(now) << "\n";
  md << "- 窗口: " << iso_time(start) << " ~ " << iso_time(end) << "\n";
  md << "- 事件总数: " << stats.events_ingested
     << "，日志: " << stats.logs_ingested
     << "，链路: " << stats.trace_count << "\n\n";

  md << "## 事实清单\n\n";
  md << "- 本窗口错误日志: ";
  size_t error_count = 0;
  for (const auto& fact : facts) {
    if (fact.value("type", std::string()) == "error_log") ++error_count;
  }
  md << error_count << " 条\n";
  size_t crash_facts = 0;
  for (const auto& fact : facts) {
    if (fact.value("type", std::string()) == "crash") ++crash_facts;
  }
  md << "- 崩溃: " << crash_facts << " 类\n";
  md << "- 告警: " << timeline.size() << " 个时间线事件\n\n";

  md << "## Top 错误模板\n\n";
  size_t shown = 0;
  for (const LogClusterInfo& cluster : store.log_clusters(50)) {
    if (!cluster.is_error_level()) continue;
    md << "- [" << cluster.count << " 次] `"
       << md_escape(options.redact ? redact(cluster.tmpl, salt) : cluster.tmpl)
       << "` (" << cluster.service << ")\n";
    if (++shown >= 10) break;
  }
  if (shown == 0) md << "- 无\n";

  md << "\n## 待验证假设（非结论）\n\n";
  md << "1. 下游超时/连接拒绝：核对首个错误 Span 的 `peer.address` 与 "
        "`rpc.grpc.status_code`。\n";
  md << "2. 资源不足：核对 `metrics.json` 中 CPU/内存/磁盘与 `/proc` 快照。\n";
  md << "3. 锁等待/卡死：核对 `profiles/` 中的折叠栈与线程栈 dump。\n";
  md << "4. OOM：核对崩溃记录、dmesg 与进程 RSS 趋势。\n";
  md << "5. 业务错误码：核对 `logs.jsonl` 中的 `error_code` 与请求参数。\n\n";
  md << "> 该文件只提供事实与假设模板，最终结论需结合代码与变更记录复核。\n";
  pack.files.push_back(path_join(root, "hypotheses.md"));
  write_text(pack.files.back(), md.str());

  // ---------------- manifest（最后写，包含前面所有文件的校验和）
  nlohmann::json manifest;
  manifest["schema_version"] = "vmon-evidence-1";
  manifest["pack_id"] = pack.id;
  manifest["created_at"] = iso_time(now);
  manifest["window"] = {{"start_ns", start}, {"end_ns", end},
                        {"start", iso_time(start)}, {"end", iso_time(end)}};
  manifest["incident"] = {{"type", options.incident},
                          {"severity", options.severity}};
  nlohmann::json nodes = nlohmann::json::array();
  for (const NodeRecord& node : store.catalog().nodes()) nodes.push_back(node.name);
  manifest["cluster"] = {{"node_ids", nodes}};
  nlohmann::json services = nlohmann::json::array();
  for (const InstanceRecord& instance : store.catalog().instances()) {
    services.push_back({{"name", instance.service},
                        {"version", instance.version},
                        {"instance", instance.instance},
                        {"node", instance.node}});
  }
  manifest["services"] = services;
  manifest["redaction"] = {{"rules_version", "v1"},
                           {"fields_masked",
                            {"ip", "email", "secret", "home_path"}},
                           {"enabled", options.redact}};
  nlohmann::json files = nlohmann::json::array();
  for (const std::string& path : pack.files) {
    files.push_back({{"path", path.substr(root.size() + 1)},
                     {"size", file_size(path)},
                     {"sha256", common::sha256_file_hex(path)}});
  }
  manifest["files"] = files;
  const std::string manifest_path = path_join(root, "manifest.json");
  write_json(manifest_path, manifest);
  pack.files.push_back(manifest_path);

  pack.manifest = manifest;
  pack.dir = root;
  return pack;
}

}  // namespace server
}  // namespace vmon
