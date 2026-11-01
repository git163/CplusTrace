// SLO（服务目标）定义、持久化与实时计算。
//
// 设计约束：
//   - 只依赖当前内存 Store 的 trace 窗口，不引入外部时序库；
//   - 定义数量有上限，计算是只读的，不会阻塞写入；
//   - 持久化使用「临时文件 + rename」，进程崩溃不会留下半截 JSON。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

class Store;

struct SloDefinition {
  std::string id;
  std::string name;
  std::string service;  // 为空表示全部服务
  std::string type = "availability";  // availability / latency
  double target = 0.99;
  uint64_t window_seconds = 3600;
  uint64_t latency_threshold_ns = 0;
  std::string description;
  bool enabled = true;

  nlohmann::json to_json() const;
  static bool from_json(const nlohmann::json& json, SloDefinition* out,
                        std::string* error);
};

class SloManager {
 public:
  static const size_t kMaxDefinitions = 200;

  bool upsert_json(const nlohmann::json& json, std::string* error);
  bool remove(const std::string& id);
  std::vector<SloDefinition> definitions() const;
  size_t size() const;

  // 返回每个定义的实时状态；Store 只读，不持有锁跨越查询。
  nlohmann::json statuses(const Store& store, uint64_t now_ns) const;

  bool load(const std::string& path, std::string* error);
  bool save(const std::string& path, std::string* error) const;

 private:
  mutable std::mutex mutex_;
  std::vector<SloDefinition> definitions_;
};

}  // namespace server
}  // namespace vmon
