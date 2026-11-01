// 统一事件模型（JSON Lines）
//
// SDK → Agent → Server 共用同一套字段。数据面优先保持简单：
//   - 时间双轨：time_ns（wall，跨节点对齐）+ mono_ns（进程内时序）；
//   - trace/span/parent 用十六进制字符串，便于检索与打印；
//   - attrs 只放低基数、可检索字段，高基数字段放 request_id/task_id。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace vmon {
namespace common {

using json = nlohmann::json;

inline std::string hex64(uint64_t v) {
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = digits[v & 0xF];
    v >>= 4;
  }
  return out;
}

inline std::string hex128(uint64_t hi, uint64_t lo) {
  return hex64(hi) + hex64(lo);
}

inline uint64_t parse_hex64(const std::string& s) {
  uint64_t v = 0;
  for (char c : s) {
    unsigned d;
    if (c >= '0' && c <= '9') {
      d = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      d = static_cast<unsigned>(c - 'A' + 10);
    } else {
      continue;
    }
    v = (v << 4) | d;
  }
  return v;
}

struct Event {
  std::string type;          // span_begin / span_end / log / heartbeat / metric / profile / crash / event
  uint64_t    time_ns = 0;   // wall clock
  uint64_t    mono_ns = 0;   // monotonic
  std::string trace;
  std::string span;
  std::string parent;
  std::string service;
  std::string instance;
  std::string node;
  std::string version;
  int32_t     pid = 0;
  int32_t     tid = 0;
  std::string name;
  std::string file;
  int32_t     line = 0;
  std::string level;         // trace/debug/info/warn/error/fatal
  std::string msg;
  std::string request_id;
  std::string task_id;
  std::string error_code;
  std::unordered_map<std::string, std::string> attrs;

  bool is_span_begin() const { return type == "span_begin"; }
  bool is_span_end() const { return type == "span_end"; }
  bool is_log() const { return type == "log"; }
  bool is_error_level() const {
    return level == "warn" || level == "error" || level == "fatal";
  }
};

inline const char* level_name(int level) {
  switch (level) {
    case 0: return "trace";
    case 1: return "debug";
    case 2: return "info";
    case 3: return "warn";
    case 4: return "error";
    case 5: return "fatal";
    default: return "info";
  }
}

inline json to_json(const Event& e) {
  json j;
  j["type"] = e.type;
  j["time_ns"] = e.time_ns;
  j["mono_ns"] = e.mono_ns;
  if (!e.trace.empty()) j["trace"] = e.trace;
  if (!e.span.empty()) j["span"] = e.span;
  if (!e.parent.empty()) j["parent"] = e.parent;
  if (!e.service.empty()) j["service"] = e.service;
  if (!e.instance.empty()) j["instance"] = e.instance;
  if (!e.node.empty()) j["node"] = e.node;
  if (!e.version.empty()) j["version"] = e.version;
  if (e.pid != 0) j["pid"] = e.pid;
  if (e.tid != 0) j["tid"] = e.tid;
  if (!e.name.empty()) j["name"] = e.name;
  if (!e.file.empty()) j["file"] = e.file;
  if (e.line != 0) j["line"] = e.line;
  if (!e.level.empty()) j["level"] = e.level;
  if (!e.msg.empty()) j["msg"] = e.msg;
  if (!e.request_id.empty()) j["request_id"] = e.request_id;
  if (!e.task_id.empty()) j["task_id"] = e.task_id;
  if (!e.error_code.empty()) j["error_code"] = e.error_code;
  if (!e.attrs.empty()) {
    json attrs = json::object();
    for (const auto& kv : e.attrs) attrs[kv.first] = kv.second;
    j["attrs"] = attrs;
  }
  return j;
}

inline Event from_json(const json& j) {
  Event e;
  if (!j.is_object()) return e;
  auto get_str = [&j](const char* key) -> std::string {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::string();
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
  };
  auto get_i64 = [&j](const char* key) -> int64_t {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return 0;
    return it->get<int64_t>();
  };

  e.type = get_str("type");
  e.time_ns = static_cast<uint64_t>(get_i64("time_ns"));
  e.mono_ns = static_cast<uint64_t>(get_i64("mono_ns"));
  e.trace = get_str("trace");
  e.span = get_str("span");
  e.parent = get_str("parent");
  e.service = get_str("service");
  e.instance = get_str("instance");
  e.node = get_str("node");
  e.version = get_str("version");
  e.pid = static_cast<int32_t>(get_i64("pid"));
  e.tid = static_cast<int32_t>(get_i64("tid"));
  e.name = get_str("name");
  e.file = get_str("file");
  e.line = static_cast<int32_t>(get_i64("line"));
  e.level = get_str("level");
  e.msg = get_str("msg");
  e.request_id = get_str("request_id");
  e.task_id = get_str("task_id");
  e.error_code = get_str("error_code");

  auto attrs_it = j.find("attrs");
  if (attrs_it != j.end() && attrs_it->is_object()) {
    for (auto it = attrs_it->begin(); it != attrs_it->end(); ++it) {
      if (it.value().is_string()) {
        e.attrs[it.key()] = it.value().get<std::string>();
      } else {
        e.attrs[it.key()] = it.value().dump();
      }
    }
  }
  return e;
}

}  // namespace common
}  // namespace vmon
