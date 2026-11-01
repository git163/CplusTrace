// 资产目录：节点 → 进程 → 服务实例。
//
// 所有条目带 last_seen_ns，查询时据此判断存活；进程与实例按节点/服务聚合，
// 供网格视图、排障跳转与诊断编排使用。
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vmon {
namespace server {

struct ProcessRecord {
  std::string node;
  int32_t pid = 0;
  int32_t ppid = 0;
  std::string name;
  std::string cmdline;
  std::string state;
  uint64_t rss_kb = 0;
  uint64_t threads = 0;
  uint64_t start_time_ticks = 0;
  uint64_t first_seen_ns = 0;
  uint64_t last_seen_ns = 0;
  bool alive = true;
};

struct NodeRecord {
  std::string name;
  std::string os;
  std::string kernel;
  std::string arch;
  double load1 = 0.0;
  double load5 = 0.0;
  double load15 = 0.0;
  uint64_t mem_total_kb = 0;
  uint64_t mem_available_kb = 0;
  uint64_t uptime_seconds = 0;
  uint64_t last_seen_ns = 0;
  std::string agent;
  std::map<int32_t, ProcessRecord> processes;
};

struct InstanceRecord {
  std::string service;
  std::string instance;
  std::string node;
  std::string version;
  int32_t pid = 0;
  uint64_t first_seen_ns = 0;
  uint64_t last_seen_ns = 0;
  uint64_t event_count = 0;
  uint64_t error_count = 0;
  bool alive = true;
};

class Catalog {
 public:
  explicit Catalog(uint64_t stale_after_ns = 60ull * 1000000000ull);

  void observe_event(const std::string& service, const std::string& instance,
                     const std::string& node, const std::string& version,
                     int32_t pid, bool is_error, uint64_t time_ns);

  void observe_process(const std::string& node, int32_t pid, int32_t ppid,
                       const std::string& name, const std::string& cmdline,
                       const std::string& state, uint64_t rss_kb,
                       uint64_t threads, uint64_t start_time_ticks,
                       uint64_t time_ns);

  void observe_host(const std::string& node, const std::string& os,
                    const std::string& kernel, const std::string& arch,
                    double load1, double load5, double load15,
                    uint64_t mem_total_kb, uint64_t mem_available_kb,
                    uint64_t uptime_seconds, const std::string& agent,
                    uint64_t time_ns);

  std::vector<NodeRecord> nodes() const;
  std::vector<InstanceRecord> instances() const;

  size_t node_count() const;
  size_t instance_count() const;
  size_t process_count() const;

 private:
  void refresh_alive_locked(uint64_t now) const;

  mutable std::mutex mutex_;
  mutable std::map<std::string, NodeRecord> nodes_;
  mutable std::map<std::string, InstanceRecord> instances_;
  uint64_t stale_after_ns_ = 60ull * 1000000000ull;
};

}  // namespace server
}  // namespace vmon
