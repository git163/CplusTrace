// 崩溃记录与指纹聚合。
//
// 崩溃来源：
//   - core_pattern helper 上报的 JSON（推荐）；
//   - SDK 信号处理器兜底上报的 type=crash 事件。
// 符号化在 Server 侧离线完成；失败不影响记录入库。
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "json_event.h"

namespace vmon {
namespace server {

struct CrashFrame {
  std::string address;
  std::string function;
  std::string file;
  int line = 0;
  bool resolved = false;
};

struct CrashRecord {
  std::string id;  // 指纹
  uint64_t time_ns = 0;
  uint64_t first_ns = 0;
  uint64_t last_ns = 0;
  uint64_t count = 0;
  std::string node;
  std::string service;
  std::string instance;
  std::string version;
  std::string binary;
  std::string build_id;
  std::string signal;
  std::string reason;
  std::string report;
  int32_t pid = 0;
  std::vector<std::string> raw_frames;
  std::vector<CrashFrame> frames;
  std::unordered_map<std::string, std::string> attrs;
};

class CrashStore {
 public:
  explicit CrashStore(size_t max_records = 1000);

  // 从统一事件写入（type=crash）。
  std::string add_event(const common::Event& event, const std::string& node_hint);

  // 从崩溃 JSON 写入，返回指纹。
  std::string add_json(const nlohmann::json& json, const std::string& node_hint);

  std::vector<CrashRecord> list(size_t limit) const;
  bool get(const std::string& id, CrashRecord* out) const;

  static std::string fingerprint_of(const std::string& build_id,
                                    const std::string& signal,
                                    const std::vector<std::string>& frames);

 private:
  std::string add_record(CrashRecord record, bool symbolize);

  mutable std::mutex mutex_;
  std::deque<std::string> order_;
  std::unordered_map<std::string, CrashRecord> records_;
  size_t max_records_ = 1000;
};

}  // namespace server
}  // namespace vmon
