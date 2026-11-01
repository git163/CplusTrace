// 可选存储转发：指标 → VictoriaMetrics，日志 → VictoriaLogs。
//
// 转发是「尽力而为」：队列满时丢弃并计数，绝不阻塞接入线程；
// 本地内存存储始终是查询主路径，转发失败不影响维测可用性。
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json_event.h"

namespace vmon {
namespace server {

struct ForwarderConfig {
  std::string vm_url;  // http://host:8428
  std::string vl_url;  // http://host:9428
  size_t queue_limit = 20000;
  size_t batch_size = 500;
  uint32_t flush_interval_ms = 500;
  uint32_t timeout_ms = 1500;
};

class Forwarder {
 public:
  explicit Forwarder(const ForwarderConfig& config);
  ~Forwarder();

  Forwarder(const Forwarder&) = delete;
  Forwarder& operator=(const Forwarder&) = delete;

  void start();
  void stop();

  // 非阻塞入队；只处理 metric / log 事件。
  void submit(const common::Event& event);

  // 同步发送一轮（测试与关停使用）。
  void flush_once();

  uint64_t forwarded() const { return forwarded_.load(); }
  uint64_t dropped() const { return dropped_.load(); }
  uint64_t failed() const { return failed_.load(); }
  bool enabled() const { return enabled_; }

 private:
  void run();
  std::vector<common::Event> take_batch();
  bool send_vm(const std::vector<common::Event>& events);
  bool send_vl(const std::vector<common::Event>& events);

  ForwarderConfig config_;
  bool enabled_ = false;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<common::Event> queue_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> forwarded_{0};
  std::atomic<uint64_t> dropped_{0};
  std::atomic<uint64_t> failed_{0};
};

}  // namespace server
}  // namespace vmon
