// 性能剖析结果：折叠栈聚合、Top 函数、火焰图树。
//
// 输入为标准折叠栈文本（每行 "frame1;frame2;... count"），
// 与 FlameGraph / inferno 兼容，便于离线复现。
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

struct ProfileRecord {
  std::string id;
  std::string kind = "cpu";  // cpu / memory / lock / io
  std::string service;
  std::string node;
  std::string instance;
  uint64_t time_ns = 0;
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  uint64_t total_samples = 0;
  std::vector<std::pair<std::string, uint64_t>> folded;  // 栈 -> 样本数
};

class ProfileStore {
 public:
  explicit ProfileStore(size_t max_records = 64);

  // 写入一份折叠栈，返回记录 ID；空内容返回空串。
  std::string add(const std::string& folded_text, const std::string& service,
                  const std::string& node, const std::string& instance,
                  const std::string& kind, uint64_t time_ns);

  std::vector<ProfileRecord> list(size_t limit) const;
  bool get(const std::string& id, ProfileRecord* out) const;

  static std::vector<std::pair<std::string, uint64_t>> parse_folded(
      const std::string& text);

  // 按「自身样本数」(leaf) 与「包含样本数」排序，返回 Top N 函数。
  static std::vector<std::pair<std::string, uint64_t>> top_self(
      const ProfileRecord& record, size_t limit);
  static std::vector<std::pair<std::string, uint64_t>> top_inclusive(
      const ProfileRecord& record, size_t limit);

  // 生成火焰图树：{name, value, children:[...]}。
  static nlohmann::json flamegraph(const ProfileRecord& record);

 private:
  mutable std::mutex mutex_;
  std::deque<std::string> order_;
  std::unordered_map<std::string, ProfileRecord> records_;
  size_t max_records_ = 64;
};

}  // namespace server
}  // namespace vmon
