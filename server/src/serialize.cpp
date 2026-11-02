#include "serialize.h"

#include "trace_builder.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

nlohmann::json attrs_to_json(
    const std::unordered_map<std::string, std::string>& attrs) {
  nlohmann::json out = nlohmann::json::object();
  for (const auto& kv : attrs) out[kv.first] = kv.second;
  return out;
}

}  // namespace

nlohmann::json span_to_json(const SpanData& span) {
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
  if (!span.attrs.empty()) j["attrs"] = attrs_to_json(span.attrs);
  return j;
}

nlohmann::json trace_node_to_json(const TraceNode& node) {
  nlohmann::json j = span_to_json(node.span);
  if (!node.children.empty()) {
    nlohmann::json children = nlohmann::json::array();
    for (const TraceNode& child : node.children) {
      children.push_back(trace_node_to_json(child));
    }
    j["children"] = children;
  }
  return j;
}

nlohmann::json analysis_to_json(const TraceData& trace,
                                const TraceAnalysis& analysis) {
  nlohmann::json j;
  j["trace_id"] = trace.trace_id;
  j["request_id"] = trace.request_id;
  j["task_id"] = trace.task_id;
  j["summary"] = {{"start_ns", analysis.summary.start_ns},
                  {"duration_ns", analysis.summary.duration_ns},
                  {"span_count", analysis.summary.span_count},
                  {"service_count", analysis.summary.service_count},
                  {"depth", analysis.summary.depth},
                  {"has_error", analysis.summary.has_error},
                  {"first_error_span", analysis.summary.first_error_span},
                  {"error_code", analysis.summary.error_code}};
  nlohmann::json roots = nlohmann::json::array();
  for (const TraceNode& root : analysis.roots) roots.push_back(trace_node_to_json(root));
  j["roots"] = roots;
  nlohmann::json critical = nlohmann::json::array();
  for (const SpanData& span : analysis.critical_path) {
    critical.push_back(span_to_json(span));
  }
  j["critical_path"] = critical;
  j["error_chain"] = error_chain(trace);
  j["threads"] = thread_timeline(trace);
  if (analysis.has_error) j["first_error"] = span_to_json(analysis.first_error);
  if (!analysis.slowest_span.span.empty()) {
    j["slowest_span"] = span_to_json(analysis.slowest_span);
  }
  return j;
}

nlohmann::json log_to_json(const LogData& log) {
  nlohmann::json j;
  j["time_ns"] = log.time_ns;
  j["time"] = iso_time(log.time_ns);
  j["node"] = log.node;
  j["service"] = log.service;
  j["instance"] = log.instance;
  j["version"] = log.version;
  j["pid"] = log.pid;
  j["tid"] = log.tid;
  j["level"] = log.level;
  j["msg"] = log.msg;
  if (!log.file.empty()) {
    j["file"] = log.file;
    j["line"] = log.line;
  }
  if (!log.trace.empty()) j["trace_id"] = log.trace;
  if (!log.span.empty()) j["span_id"] = log.span;
  if (!log.request_id.empty()) j["request_id"] = log.request_id;
  if (!log.task_id.empty()) j["task_id"] = log.task_id;
  if (!log.error_code.empty()) j["error_code"] = log.error_code;
  if (!log.fingerprint.empty()) j["fingerprint"] = log.fingerprint;
  if (!log.tmpl.empty()) j["template"] = log.tmpl;
  if (!log.attrs.empty()) j["attrs"] = attrs_to_json(log.attrs);
  return j;
}

nlohmann::json cluster_to_json(const LogClusterInfo& cluster) {
  nlohmann::json j;
  j["fingerprint"] = cluster.id;
  j["template"] = cluster.tmpl;
  j["level"] = cluster.level;
  j["service"] = cluster.service;
  j["error_code"] = cluster.error_code;
  j["count"] = cluster.count;
  j["first_ns"] = cluster.first_ns;
  j["last_ns"] = cluster.last_ns;
  j["first_time"] = iso_time(cluster.first_ns);
  j["last_time"] = iso_time(cluster.last_ns);
  j["sample"] = cluster.sample;
  j["last_sample"] = cluster.last_sample;
  j["nodes"] = cluster.nodes;
  j["services"] = cluster.services;
  return j;
}

nlohmann::json event_to_json(const EventRecord& event) {
  nlohmann::json j;
  j["id"] = event.id;
  j["time_ns"] = event.time_ns;
  j["time"] = iso_time(event.time_ns);
  j["last_ns"] = event.last_ns;
  j["last_time"] = iso_time(event.last_ns == 0 ? event.time_ns : event.last_ns);
  j["count"] = event.count;
  j["type"] = event.type;
  j["severity"] = event.severity;
  j["title"] = event.title;
  j["detail"] = event.detail;
  j["service"] = event.service;
  j["node"] = event.node;
  j["fingerprint"] = event.fingerprint;
  j["status"] = event.status;
  if (!event.attrs.empty()) j["attrs"] = attrs_to_json(event.attrs);
  return j;
}

nlohmann::json crash_to_json(const CrashRecord& crash) {
  nlohmann::json j;
  j["id"] = crash.id;
  j["time_ns"] = crash.last_ns;
  j["time"] = iso_time(crash.last_ns);
  j["first_ns"] = crash.first_ns;
  j["first_time"] = iso_time(crash.first_ns);
  j["count"] = crash.count;
  j["node"] = crash.node;
  j["service"] = crash.service;
  j["instance"] = crash.instance;
  j["version"] = crash.version;
  j["pid"] = crash.pid;
  j["binary"] = crash.binary;
  j["build_id"] = crash.build_id;
  j["signal"] = crash.signal;
  j["reason"] = crash.reason;
  nlohmann::json frames = nlohmann::json::array();
  for (const CrashFrame& frame : crash.frames) {
    frames.push_back({{"address", frame.address},
                      {"function", frame.function},
                      {"file", frame.file},
                      {"line", frame.line},
                      {"resolved", frame.resolved}});
  }
  j["frames"] = frames;
  if (!crash.raw_frames.empty()) j["raw_frames"] = crash.raw_frames;
  if (!crash.report.empty()) j["report"] = crash.report;
  if (!crash.attrs.empty()) j["attrs"] = attrs_to_json(crash.attrs);
  return j;
}

nlohmann::json profile_to_json(const ProfileRecord& profile, size_t top_limit) {
  nlohmann::json j;
  j["id"] = profile.id;
  j["kind"] = profile.kind;
  j["service"] = profile.service;
  j["node"] = profile.node;
  j["instance"] = profile.instance;
  j["time_ns"] = profile.time_ns;
  j["time"] = iso_time(profile.time_ns);
  j["total_samples"] = profile.total_samples;
  nlohmann::json top_self = nlohmann::json::array();
  for (const auto& entry : ProfileStore::top_self(profile, top_limit)) {
    top_self.push_back({{"function", entry.first}, {"samples", entry.second}});
  }
  nlohmann::json top_inclusive = nlohmann::json::array();
  for (const auto& entry : ProfileStore::top_inclusive(profile, top_limit)) {
    top_inclusive.push_back({{"function", entry.first}, {"samples", entry.second}});
  }
  j["top_self"] = top_self;
  j["top_inclusive"] = top_inclusive;
  return j;
}

nlohmann::json command_to_json(const CommandRecord& command) {
  nlohmann::json j;
  j["id"] = command.id;
  j["node"] = command.node;
  j["agent"] = command.agent;
  j["pid"] = command.pid;
  j["type"] = command.type;
  j["args"] = command.args;
  j["status"] = command.status;
  j["created_ns"] = command.created_ns;
  j["created"] = iso_time(command.created_ns);
  j["expires_ns"] = command.expires_ns;
  if (command.completed_ns != 0) {
    j["completed_ns"] = command.completed_ns;
    j["completed"] = iso_time(command.completed_ns);
  }
  if (!command.result.empty()) j["result"] = command.result;
  if (!command.error.empty()) j["error"] = command.error;
  return j;
}

nlohmann::json catalog_node_to_json(const NodeRecord& node) {
  nlohmann::json j;
  j["name"] = node.name;
  j["os"] = node.os;
  j["kernel"] = node.kernel;
  j["arch"] = node.arch;
  j["load1"] = node.load1;
  j["load5"] = node.load5;
  j["load15"] = node.load15;
  j["mem_total_kb"] = node.mem_total_kb;
  j["mem_available_kb"] = node.mem_available_kb;
  j["uptime_seconds"] = node.uptime_seconds;
  j["last_seen_ns"] = node.last_seen_ns;
  j["last_seen"] = iso_time(node.last_seen_ns);
  j["agent"] = node.agent;
  j["process_count"] = node.processes.size();
  nlohmann::json processes = nlohmann::json::array();
  for (const auto& kv : node.processes) {
    processes.push_back(process_to_json(kv.second));
  }
  j["processes"] = processes;
  return j;
}

nlohmann::json instance_to_json(const InstanceRecord& instance) {
  nlohmann::json j;
  j["service"] = instance.service;
  j["instance"] = instance.instance;
  j["node"] = instance.node;
  j["version"] = instance.version;
  j["pid"] = instance.pid;
  j["first_seen_ns"] = instance.first_seen_ns;
  j["last_seen_ns"] = instance.last_seen_ns;
  j["last_seen"] = iso_time(instance.last_seen_ns);
  j["event_count"] = instance.event_count;
  j["error_count"] = instance.error_count;
  j["alive"] = instance.alive;
  return j;
}

nlohmann::json process_to_json(const ProcessRecord& process) {
  nlohmann::json j;
  j["node"] = process.node;
  j["pid"] = process.pid;
  j["ppid"] = process.ppid;
  j["name"] = process.name;
  j["cmdline"] = process.cmdline;
  j["state"] = process.state;
  j["rss_kb"] = process.rss_kb;
  j["threads"] = process.threads;
  j["start_time_ticks"] = process.start_time_ticks;
  j["first_seen_ns"] = process.first_seen_ns;
  j["last_seen_ns"] = process.last_seen_ns;
  j["last_seen"] = iso_time(process.last_seen_ns);
  j["alive"] = process.alive;
  return j;
}

}  // namespace server
}  // namespace vmon
