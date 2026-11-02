#include "diagnostics.h"

#include <algorithm>
#include <cctype>

#include <sys/statvfs.h>

#include "commands.h"
#include "crash.h"
#include "profiler.h"
#include "serialize.h"
#include "store.h"
#include "trace_builder.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

CheckResult make_check(const std::string& id, const std::string& name,
                       const std::string& status, const std::string& detail) {
  CheckResult result;
  result.id = id;
  result.name = name;
  result.status = status;
  result.detail = detail;
  return result;
}

bool text_contains_lower(const std::string& text, const std::string& needle) {
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string target = needle;
  std::transform(target.begin(), target.end(), target.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower.find(target) != std::string::npos;
}

}  // namespace

std::vector<CheckResult> Diagnostics::inspect(Store& store, CrashStore& crashes,
                                              const std::string& service,
                                              const std::string& state_dir) {
  std::vector<CheckResult> checks;
  const uint64_t now = now_ns();

  // 1) 节点在线
  const std::vector<NodeRecord> nodes = store.catalog().nodes();
  if (nodes.empty()) {
    checks.push_back(make_check("nodes_online", "节点在线", "fail",
                                "尚未收到任何节点数据，请检查 Agent 是否启动"));
  } else {
    std::vector<std::string> stale;
    for (const NodeRecord& node : nodes) {
      const uint64_t age =
          now > node.last_seen_ns ? now - node.last_seen_ns : 0;
      if (age > 120ull * 1000000000ull) stale.push_back(node.name);
    }
    checks.push_back(make_check(
        "nodes_online", "节点在线", stale.empty() ? "ok" : "warn",
        stale.empty()
            ? "全部节点 " + std::to_string(nodes.size()) + " 个在线"
            : std::to_string(stale.size()) + " 个节点超过 120 秒无数据"));
  }

  // 2) 服务实例
  const std::vector<InstanceRecord> instances = store.catalog().instances();
  size_t matched = 0;
  size_t dead = 0;
  for (const InstanceRecord& instance : instances) {
    if (!service.empty() && instance.service != service) continue;
    ++matched;
    if (!instance.alive) ++dead;
  }
  if (matched == 0) {
    checks.push_back(make_check("service_instances", "服务实例",
                                "unknown",
                                service.empty() ? "暂无服务实例数据"
                                                : "未发现服务 " + service));
  } else {
    checks.push_back(make_check(
        "service_instances", "服务实例", dead == 0 ? "ok" : "warn",
        std::to_string(matched) + " 个实例，其中 " + std::to_string(dead) +
            " 个超过 60 秒无事件"));
  }

  // 3) 错误率
  TraceQuery trace_query;
  trace_query.service = service;
  trace_query.since_ns = now > 300000000000ull ? now - 300000000000ull : 0;
  trace_query.limit = 500;
  const std::vector<TraceSummary> traces = store.query_traces(trace_query);
  size_t errors = 0;
  size_t slow = 0;
  for (const TraceSummary& summary : traces) {
    if (summary.has_error) ++errors;
    if (summary.duration_ns > 1000000000ull) ++slow;
  }
  const double rate = traces.empty()
                          ? 0.0
                          : static_cast<double>(errors) * 100.0 /
                                static_cast<double>(traces.size());
  std::string error_status = "ok";
  if (!traces.empty() && rate >= 50.0) {
    error_status = "fail";
  } else if (!traces.empty() && rate >= 10.0) {
    error_status = "warn";
  }
  checks.push_back(make_check(
      "error_rate", "调用错误率", error_status,
      traces.empty() ? "最近 5 分钟无可统计链路"
                     : "最近 5 分钟 " + std::to_string(traces.size()) +
                           " 条链路，错误率 " + std::to_string(rate) + "%"));

  // 4) 慢调用
  checks.push_back(make_check(
      "slow_traces", "慢调用", slow == 0 ? "ok" : "warn",
      std::to_string(slow) + " 条链路超过 1 秒"));

  // 5) 错误日志
  size_t error_clusters = 0;
  for (const LogClusterInfo& cluster : store.log_clusters(500)) {
    if (!cluster.is_error_level()) continue;
    if (!service.empty() && cluster.service != service) continue;
    ++error_clusters;
  }
  checks.push_back(make_check("error_logs", "错误日志",
                              error_clusters == 0 ? "ok" : "warn",
                              "错误日志模板 " + std::to_string(error_clusters) +
                                  " 个"));

  // 6) 崩溃
  size_t recent_crashes = 0;
  for (const CrashRecord& crash : crashes.list(200)) {
    if (!service.empty() && crash.service != service) continue;
    if (crash.last_ns + 86400000000000ull < now) continue;
    ++recent_crashes;
  }
  checks.push_back(make_check(
      "crashes", "崩溃", recent_crashes == 0 ? "ok" : "fail",
      "最近 24 小时崩溃 " + std::to_string(recent_crashes) + " 类"));

  // 7) 时钟偏差（用节点最后事件时间与服务器时间粗判）
  bool clock_suspect = false;
  for (const NodeRecord& node : nodes) {
    if (node.last_seen_ns > now + 60000000000ull) clock_suspect = true;
  }
  checks.push_back(make_check(
      "clock_skew", "时钟偏差", clock_suspect ? "warn" : "unknown",
      clock_suspect ? "节点上报时间领先服务器超过 60 秒，请检查 NTP"
                    : "未发现明显偏差（建议部署 NTP 监控）"));

  // 8) 磁盘
  if (!state_dir.empty()) {
    struct statvfs vfs;
    if (::statvfs(state_dir.c_str(), &vfs) == 0) {
      const uint64_t free_bytes =
          static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
      const std::string status =
          free_bytes < 1024ull * 1024 * 1024 ? "warn" : "ok";
      checks.push_back(make_check(
          "disk", "磁盘空间", status,
          "可用 " + std::to_string(free_bytes / 1024 / 1024) + " MB"));
    }
  }

  return checks;
}

nlohmann::json Diagnostics::diagnose(Store& store, CrashStore& crashes,
                                     ProfileStore& profiles,
                                     CommandStore& commands,
                                     const DiagnoseRequest& request) {
  const uint64_t now = now_ns();
  const uint64_t since =
      request.since_ns == 0
          ? (now > 900000000000ull ? now - 900000000000ull : 0)
          : request.since_ns;

  nlohmann::json report;
  report["generated_at"] = iso_time(now);
  report["scope"] = {{"trace_id", request.trace_id},
                     {"request_id", request.request_id},
                     {"service", request.service},
                     {"instance", request.instance},
                     {"node", request.node},
                     {"pid", request.pid}};

  // 收集相关 trace。
  std::vector<std::string> trace_ids;
  if (!request.trace_id.empty()) {
    trace_ids.push_back(request.trace_id);
  } else if (!request.request_id.empty()) {
    trace_ids = store.traces_for_request(request.request_id, 20);
  } else {
    TraceQuery query;
    query.service = request.service;
    query.since_ns = since;
    query.limit = 20;
    for (const TraceSummary& summary : store.query_traces(query)) {
      trace_ids.push_back(summary.trace_id);
    }
  }

  nlohmann::json traces = nlohmann::json::array();
  SpanData first_error;
  SpanData slowest;
  bool has_error = false;
  TraceData primary_trace;
  bool has_primary = false;
  for (const std::string& trace_id : trace_ids) {
    TraceData trace;
    if (!store.get_trace(trace_id, &trace)) continue;
    const TraceAnalysis analysis = analyze_trace(trace);
    if (!has_primary || (analysis.has_error && !has_error)) {
      primary_trace = trace;
      has_primary = true;
    }
    traces.push_back(analysis_to_json(trace, analysis));
    if (analysis.has_error &&
        (!has_error || analysis.first_error.start_ns < first_error.start_ns)) {
      has_error = true;
      first_error = analysis.first_error;
    }
    if (analysis.slowest_span.duration_ns() > slowest.duration_ns()) {
      slowest = analysis.slowest_span;
    }
  }
  report["traces"] = traces;
  const nlohmann::json primary_chain =
      has_primary ? error_chain(primary_trace) : nlohmann::json::object();
  const nlohmann::json primary_threads =
      has_primary ? thread_timeline(primary_trace) : nlohmann::json::object();
  report["error_chain"] = primary_chain;
  report["threads"] = primary_threads;
  report["first_error"] = has_error ? span_to_json(first_error) : nlohmann::json();
  report["slowest_span"] =
      slowest.span.empty() ? nlohmann::json() : span_to_json(slowest);

  // 相关日志：优先按 trace/request，其次按服务错误日志。
  LogQuery log_query;
  log_query.trace = request.trace_id;
  log_query.request_id = request.request_id;
  log_query.service = request.service;
  log_query.node = request.node;
  log_query.since_ns = since;
  log_query.limit = 300;
  if (request.trace_id.empty() && request.request_id.empty()) {
    log_query.errors_only = true;
  }
  nlohmann::json logs = nlohmann::json::array();
  for (const LogData& log : store.query_logs(log_query)) {
    logs.push_back(log_to_json(log));
  }
  report["logs"] = logs;

  // 崩溃与剖析。
  nlohmann::json crash_list = nlohmann::json::array();
  for (const CrashRecord& crash : crashes.list(50)) {
    if (!request.service.empty() && crash.service != request.service) continue;
    if (crash.last_ns < since) continue;
    crash_list.push_back(crash_to_json(crash));
  }
  report["crashes"] = crash_list;

  nlohmann::json profile_list = nlohmann::json::array();
  for (const ProfileRecord& profile : profiles.list(5)) {
    if (!request.service.empty() && profile.service != request.service) continue;
    if (profile.time_ns < since) continue;
    profile_list.push_back(profile_to_json(profile, 15));
  }
  report["profiles"] = profile_list;

  // 可疑根因（规则化，不做武断结论）。
  nlohmann::json hypotheses = nlohmann::json::array();
  std::string verdict = "evidence_insufficient";
  auto add_hypothesis = [&hypotheses](const std::string& title,
                                      const std::string& confidence,
                                      const std::string& evidence) {
    hypotheses.push_back(
        {{"title", title}, {"confidence", confidence}, {"evidence", evidence}});
  };

  if (has_error) {
    const std::string code = first_error.error_code;
    std::string status_code;
    auto it = first_error.attrs.find("rpc.grpc.status_code");
    if (it != first_error.attrs.end()) status_code = it->second;
    auto status_it = first_error.attrs.find("status");
    if (status_it != first_error.attrs.end() && status_code.empty()) {
      status_code = status_it->second;
    }

    if (status_code == "4" || text_contains_lower(status_code, "deadline") ||
        text_contains_lower(first_error.error_code, "timeout")) {
      add_hypothesis("下游调用超时", "high",
                     "首个错误 Span " + first_error.name + " 状态为 " +
                         status_code);
      verdict = "downstream_timeout";
    } else if (status_code == "14" ||
               text_contains_lower(status_code, "unavailable")) {
      add_hypothesis("下游服务不可用/连接失败", "high",
                     "首个错误 Span " + first_error.name +
                         " 返回 UNAVAILABLE(14)");
      verdict = "downstream_unavailable";
    } else if (!code.empty()) {
      add_hypothesis("业务错误码 " + code, "medium",
                     "首个错误 Span 携带业务错误码");
      verdict = "business_error";
    } else {
      add_hypothesis("进程内执行错误", "low",
                     "首个错误 Span " + first_error.name + " 位于 " +
                         first_error.service);
      verdict = "internal_error";
    }
  }

  for (const auto& log : logs) {
    const std::string msg = log.value("msg", std::string());
    if (text_contains_lower(msg, "out of memory") ||
        text_contains_lower(msg, "oom")) {
      add_hypothesis("内存不足（OOM）", "medium", msg.substr(0, 200));
      verdict = "oom";
      break;
    }
    if (text_contains_lower(msg, "deadlock") ||
        text_contains_lower(msg, "lock wait")) {
      add_hypothesis("锁等待/死锁", "medium", msg.substr(0, 200));
      verdict = "lock_contention";
      break;
    }
    if (text_contains_lower(msg, "connection refused")) {
      add_hypothesis("下游连接被拒绝", "high", msg.substr(0, 200));
      verdict = "connection_refused";
      break;
    }
  }

  // 多线程/时序维度：错误不止看错误码，还要看发生在哪个线程、是否有长空档。
  if (has_primary) {
    const nlohmann::json& thread_info = primary_threads;
    if (thread_info.value("multi_threaded", false)) {
      add_hypothesis(
          "多线程链路：错误跨线程传播", "medium",
          "trace 涉及 " + std::to_string(thread_info.value("thread_count", 0)) +
              " 个线程；检查线程池/队列/异步回调的上下文传递与锁竞争");
    }
    const size_t long_gaps = thread_info.value("long_gaps", 0);
    if (long_gaps > 0) {
      add_hypothesis(
          "线程存在长空档，疑似等待锁/IO/调度", "medium",
          std::to_string(long_gaps) +
              " 处线程间隔超过 100ms；结合线程视图与 /proc 快照确认阻塞点");
    }
    const nlohmann::json& chain = primary_chain;
    if (chain.value("count", 0) > 1) {
      add_hypothesis(
          "错误沿调用链传播", "high",
          std::to_string(chain.value("count", 0)) +
              " 个错误 Span；按 first_path 从根到叶子确认首个错误点");
    }
  }

  if (hypotheses.empty()) {
    add_hypothesis("证据不足，需要更多上下文", "low",
                   "未命中任何已知错误模式，建议导出证据包进一步分析");
  }
  report["hypotheses"] = hypotheses;
  report["verdict"] = verdict;

  // 建议动作：只给可审计的观察/只读命令，不自动重启或修改业务数据。
  nlohmann::json remediation = nlohmann::json::array();
  auto add_action = [&remediation](const std::string& title,
                                   const std::string& action,
                                   const std::string& risk,
                                   const std::string& detail,
                                   const std::string& command) {
    remediation.push_back({{"title", title},
                           {"action", action},
                           {"risk", risk},
                           {"detail", detail},
                           {"command", command}});
  };
  if (has_error) {
    add_action("沿 first_path 逐跳核对", "observe", "low",
               "错误码只是结果；按根到错误的路径确认每个服务的耗时、错误码与日志",
               "GET /api/traces/" +
                   (primary_trace.trace_id.empty() ? std::string("<trace_id>")
                                                   : primary_trace.trace_id));
  }
  if (primary_threads.value("long_gaps", 0) > 0 ||
      primary_threads.value("multi_threaded", false)) {
    add_action("抓取全线程栈", "command", "low",
               "线程视图出现多线程或长空档时，抓取现场线程栈确认锁/IO/调度阻塞",
               "POST /api/control/dump-stacks {node,pid}");
  }
  if (verdict == "lock_contention") {
    add_action("检查锁等待与线程池队列", "observe", "medium",
               "结合线程视图、全线程栈与队列深度，确认持锁者和等待链",
               "POST /api/control/dump-stacks");
  }
  if (verdict == "oom") {
    add_action("核对进程 RSS 与系统内存", "observe", "medium",
               "查看 trace 时间窗内的 process.rss_bytes / host.mem_used_ratio",
               "GET /api/metrics?name=process.rss_bytes");
  }
  if (verdict == "downstream_timeout" || verdict == "downstream_unavailable") {
    add_action("检查下游 deadline / 重试 / 部署状态", "observe", "low",
               "先确认下游是否重启、发布或过载；不要在证据不足时直接重启",
               "GET /api/topology");
  }
  if (!has_error) {
    add_action("临时开启 debug 日志", "command", "low",
               "对目标服务/实例下发带 TTL 的 debug 级别，复现后自动恢复",
               "POST /api/control/log-level {service,level:debug,ttl_ms}");
  }
  add_action("导出证据包", "observe", "low",
             "把 trace、日志、指标、剖析、崩溃与时间线打包，便于复核/AI 分析",
             "POST /api/evidence");
  report["remediation"] = remediation;

  // 现场命令：抓线程栈（只读）。
  nlohmann::json command_ids = nlohmann::json::array();
  if (request.collect_stack && !request.node.empty() && request.pid > 0) {
    const std::string id = commands.enqueue(
        request.node, std::string(), request.pid, "dump_stacks",
        nlohmann::json::object(), 60000000000ull);
    command_ids.push_back(id);
  }
  report["commands"] = command_ids;

  // 巡检结果一并返回，便于一键查看整体状态。
  nlohmann::json checks = nlohmann::json::array();
  for (const CheckResult& check :
       inspect(store, crashes, request.service, std::string())) {
    checks.push_back({{"id", check.id},
                      {"name", check.name},
                      {"status", check.status},
                      {"detail", check.detail}});
  }
  report["checks"] = checks;
  return report;
}

}  // namespace server
}  // namespace vmon
