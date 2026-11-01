// 日志模板聚类与指纹（Drain 思路的轻量实现）。
//
// 目的不是精确还原日志语法，而是：
//   - 把「变量不同、结构相同」的日志收敛成同一模板；
//   - 用 模板 + 级别 + 服务 + 错误码 生成指纹，用于聚合与告警；
//   - 从原始行中提取 request_id/task_id/错误码/耗时等关键字段。
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vmon {
namespace server {

struct LogClusterInfo {
  std::string id;          // 指纹
  std::string tmpl;        // 归一化模板
  std::string level;
  std::string service;
  std::string error_code;
  std::string sample;      // 首个样本
  std::string last_sample; // 最近样本
  uint64_t count = 0;
  uint64_t first_ns = 0;
  uint64_t last_ns = 0;
  std::vector<std::string> nodes;
  std::vector<std::string> services;

  bool is_error_level() const {
    return level == "warn" || level == "error" || level == "fatal";
  }
};

class LogClusterer {
 public:
  explicit LogClusterer(size_t max_clusters = 5000);

  // 归一化：数字、十六进制、UUID、IP、路径、引号内容替换成占位符。
  static std::string make_template(const std::string& msg);

  // 指纹 = hash(level|service|error_code|template)。
  static std::string make_fingerprint(const std::string& level,
                                      const std::string& service,
                                      const std::string& error_code,
                                      const std::string& tmpl);

  // 提取常用键值字段（request_id、task_id、error_code、耗时、重试等）。
  static std::unordered_map<std::string, std::string> extract_keys(
      const std::string& msg);

  // 记录一条日志，返回其所属聚类信息（拷贝）；超过上限时淘汰最久未见的聚类。
  LogClusterInfo annotate(const std::string& msg, const std::string& level,
                          const std::string& service, const std::string& node,
                          const std::string& error_code, uint64_t time_ns);

  std::vector<LogClusterInfo> top(size_t limit) const;
  std::vector<LogClusterInfo> list(size_t limit) const;
  size_t size() const;

 private:
  struct Entry {
    LogClusterInfo info;
    uint64_t last_touch = 0;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> clusters_;
  std::deque<std::string> order_;  // 近似 LRU：记录创建顺序，淘汰时取最旧
  size_t max_clusters_ = 5000;
};

}  // namespace server
}  // namespace vmon
