// 异常追踪（Issue）：把错误日志模板与崩溃指纹聚合成可处理的 Issue。
//
// 设计参考观测云异常追踪：
//   - Issue 来自已有日志聚类/崩溃记录，不新增采集链路；
//   - 生命周期 open → ack → resolved，可指派负责人；
//   - 处理状态单独持久化，聚合数据仍以 Store/CrashStore 为准。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

class Store;
class CrashStore;

struct IssueRecord {
  std::string id;
  std::string kind;  // log / crash
  std::string title;
  std::string level;
  std::string service;
  std::string error_code;
  std::string status = "open";  // open / ack / resolved
  std::string assignee;
  std::string note;
  uint64_t count = 0;
  uint64_t first_ns = 0;
  uint64_t last_ns = 0;
  std::string sample;
  std::string trace_id;
  std::string request_id;
  std::string node;
};

struct IssueQuery {
  std::string status;
  std::string service;
  std::string level;
  size_t limit = 100;
};

class IssueStore {
 public:
  bool load(const std::string& path, std::string* error);
  bool save(const std::string& path, std::string* error) const;

  // 从 Store 的错误聚类 + CrashStore 派生 Issue，并合并持久化的处理状态。
  std::vector<IssueRecord> list(const Store& store, const CrashStore& crashes,
                                const IssueQuery& query) const;

  bool update(const std::string& id, const std::string& status,
              const std::string& assignee, const std::string& note,
              std::string* error);

 private:
  struct State {
    std::string status = "open";
    std::string assignee;
    std::string note;
    uint64_t updated_ns = 0;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, State> states_;
};

nlohmann::json issue_to_json(const IssueRecord& issue);

}  // namespace server
}  // namespace vmon
