// 服务端内存模型：Span / Log / Trace / 事件。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vmon {
namespace server {

struct SpanData {
  std::string trace;
  std::string span;
  std::string parent;
  std::string service;
  std::string instance;
  std::string node;
  std::string version;
  int32_t pid = 0;
  int32_t tid = 0;
  std::string name;
  std::string file;
  int32_t line = 0;
  std::string kind;    // internal / server / client / producer / consumer
  std::string status;  // ok / error
  std::string error_code;
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  bool ended = false;
  std::unordered_map<std::string, std::string> attrs;

  uint64_t duration_ns() const {
    return ended && end_ns > start_ns ? end_ns - start_ns : 0;
  }
  bool is_error() const {
    if (status == "error") return true;
    auto it = attrs.find("error");
    return it != attrs.end() && it->second == "true";
  }
};

struct LogData {
  uint64_t time_ns = 0;
  std::string node;
  std::string service;
  std::string instance;
  std::string version;
  int32_t pid = 0;
  int32_t tid = 0;
  std::string level;
  std::string msg;
  std::string file;
  int32_t line = 0;
  std::string trace;
  std::string span;
  std::string request_id;
  std::string task_id;
  std::string error_code;
  std::string fingerprint;
  std::string tmpl;
  std::unordered_map<std::string, std::string> attrs;

  bool is_error_level() const {
    return level == "warn" || level == "error" || level == "fatal";
  }
};

struct MetricPoint {
  std::string name;
  uint64_t time_ns = 0;
  double value = 0.0;
  std::unordered_map<std::string, std::string> labels;
};

struct EventRecord {
  std::string id;
  uint64_t time_ns = 0;
  std::string type;      // alert / crash / restart / deploy / change
  std::string severity;  // info / warning / critical
  std::string title;
  std::string detail;
  std::string service;
  std::string node;
  std::string fingerprint;
  std::string status;  // open / ack / resolved
  std::unordered_map<std::string, std::string> attrs;
};

struct TraceData {
  std::string trace_id;
  std::unordered_map<std::string, SpanData> spans;
  uint64_t first_ns = 0;
  uint64_t last_ns = 0;
  std::string request_id;
  std::string task_id;
  std::string root_service;
  std::string root_name;
};

struct TraceSummary {
  std::string trace_id;
  std::string request_id;
  std::string task_id;
  std::string root_service;
  std::string root_name;
  uint64_t start_ns = 0;
  uint64_t duration_ns = 0;
  uint32_t span_count = 0;
  uint32_t service_count = 0;
  uint32_t depth = 0;
  bool has_error = false;
  std::string first_error_span;
  std::string error_code;
};

struct TraceNode {
  SpanData span;
  uint64_t duration_ns = 0;
  std::vector<TraceNode> children;
};

struct TraceAnalysis {
  TraceSummary summary;
  std::vector<TraceNode> roots;
  std::vector<SpanData> critical_path;
  bool has_error = false;
  SpanData first_error;
  SpanData slowest_span;
};

struct ServiceEdge {
  std::string caller;
  std::string callee;
  std::string method;
  uint64_t count = 0;
  uint64_t error_count = 0;
  uint64_t total_duration_ns = 0;
  uint64_t max_duration_ns = 0;
  std::string confidence;  // strong / weak
};

}  // namespace server
}  // namespace vmon
