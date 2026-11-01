#include "catalog.h"

#include "util.h"

namespace vmon {
namespace server {

Catalog::Catalog(uint64_t stale_after_ns)
    : stale_after_ns_(stale_after_ns == 0 ? 60ull * 1000000000ull
                                          : stale_after_ns) {}

void Catalog::refresh_alive_locked(uint64_t now) const {
  for (auto& kv : nodes_) {
    NodeRecord& node = kv.second;
    for (auto& process_it : node.processes) {
      ProcessRecord& process = process_it.second;
      process.alive =
          now >= process.last_seen_ns && now - process.last_seen_ns <= stale_after_ns_;
    }
  }
  for (auto& kv : instances_) {
    InstanceRecord& instance = kv.second;
    instance.alive = now >= instance.last_seen_ns &&
                     now - instance.last_seen_ns <= stale_after_ns_;
  }
}

void Catalog::observe_event(const std::string& service,
                            const std::string& instance,
                            const std::string& node,
                            const std::string& version, int32_t pid,
                            bool is_error, uint64_t time_ns) {
  if (service.empty()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  // 事件未带 instance 时（L0 采集、只上报 span 结束等），归并到该服务已有实例，
  // 避免把同一服务拆成多个空实例。
  std::string effective_instance = instance;
  if (effective_instance.empty()) {
    for (const auto& kv : instances_) {
      if (kv.second.service == service) {
        effective_instance = kv.second.instance;
        break;
      }
    }
  }
  const std::string key = service + "\x1f" + effective_instance;
  auto it = instances_.find(key);
  if (it == instances_.end()) {
    InstanceRecord record;
    record.service = service;
    record.instance = effective_instance;
    it = instances_.insert(std::make_pair(key, record)).first;
  }
  InstanceRecord& record = it->second;
  if (record.first_seen_ns == 0) record.first_seen_ns = time_ns;
  if (time_ns > record.last_seen_ns) record.last_seen_ns = time_ns;
  ++record.event_count;
  if (is_error) ++record.error_count;
  if (!node.empty()) record.node = node;
  if (!version.empty()) record.version = version;
  if (pid != 0) record.pid = pid;
}

void Catalog::observe_process(const std::string& node, int32_t pid, int32_t ppid,
                              const std::string& name,
                              const std::string& cmdline,
                              const std::string& state, uint64_t rss_kb,
                              uint64_t threads, uint64_t start_time_ticks,
                              uint64_t time_ns) {
  if (node.empty() || pid <= 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  NodeRecord& node_record = nodes_[node];
  node_record.name = node;
  if (node_record.last_seen_ns < time_ns) node_record.last_seen_ns = time_ns;

  auto it = node_record.processes.find(pid);
  if (it == node_record.processes.end() ||
      it->second.start_time_ticks != start_time_ticks) {
    ProcessRecord record;
    record.node = node;
    record.pid = pid;
    record.first_seen_ns = time_ns;
    record.start_time_ticks = start_time_ticks;
    it = node_record.processes.insert(std::make_pair(pid, record)).first;
  }
  ProcessRecord& record = it->second;
  record.ppid = ppid;
  record.name = name;
  record.cmdline = cmdline;
  record.state = state;
  record.rss_kb = rss_kb;
  record.threads = threads;
  record.last_seen_ns = time_ns;
  record.alive = true;

  // 清掉两倍存活期未见的进程（进程已退出）。
  const uint64_t cutoff = stale_after_ns_ * 2;
  for (auto process_it = node_record.processes.begin();
       process_it != node_record.processes.end();) {
    const ProcessRecord& process = process_it->second;
    if (time_ns > process.last_seen_ns &&
        time_ns - process.last_seen_ns > cutoff) {
      process_it = node_record.processes.erase(process_it);
    } else {
      ++process_it;
    }
  }
}

void Catalog::observe_host(const std::string& node, const std::string& os,
                           const std::string& kernel, const std::string& arch,
                           double load1, double load5, double load15,
                           uint64_t mem_total_kb, uint64_t mem_available_kb,
                           uint64_t uptime_seconds, const std::string& agent,
                           uint64_t time_ns) {
  if (node.empty()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  NodeRecord& record = nodes_[node];
  record.name = node;
  record.os = os;
  record.kernel = kernel;
  record.arch = arch;
  record.load1 = load1;
  record.load5 = load5;
  record.load15 = load15;
  record.mem_total_kb = mem_total_kb;
  record.mem_available_kb = mem_available_kb;
  record.uptime_seconds = uptime_seconds;
  record.agent = agent;
  if (time_ns > record.last_seen_ns) record.last_seen_ns = time_ns;
}

std::vector<NodeRecord> Catalog::nodes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_alive_locked(now_ns());
  std::vector<NodeRecord> out;
  out.reserve(nodes_.size());
  for (const auto& kv : nodes_) out.push_back(kv.second);
  return out;
}

std::vector<InstanceRecord> Catalog::instances() const {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_alive_locked(now_ns());
  std::vector<InstanceRecord> out;
  out.reserve(instances_.size());
  for (const auto& kv : instances_) out.push_back(kv.second);
  return out;
}

size_t Catalog::node_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.size();
}

size_t Catalog::instance_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return instances_.size();
}

size_t Catalog::process_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t total = 0;
  for (const auto& kv : nodes_) total += kv.second.processes.size();
  return total;
}

}  // namespace server
}  // namespace vmon
