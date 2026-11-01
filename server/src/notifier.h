// 告警事件 Webhook 通知。
//
// 设计目标：
//   - 事件进入 Store 后异步推送，不阻塞规则评估与 HTTP 线程；
//   - 队列有界，中心/对端故障时丢弃并计数，不无限占内存；
//   - 只支持 HTTP（内网 Webhook 通常是飞书/钉钉/企微机器人或自建网关）。
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "model.h"

namespace vmon {
namespace server {

struct NotifierConfig {
  bool enabled = false;
  std::string url;
  uint32_t timeout_ms = 3000;
  std::string token;  // 可选：作为 X-Vmon-Token 发送
};

class Notifier {
 public:
  explicit Notifier(const NotifierConfig& config);
  ~Notifier();

  Notifier(const Notifier&) = delete;
  Notifier& operator=(const Notifier&) = delete;

  void start();
  void stop();
  bool enabled() const { return config_.enabled && !config_.url.empty(); }

  // 事件进入 Store 后调用；队列满时丢弃最旧事件。
  void enqueue(const EventRecord& event);

  uint64_t sent() const { return sent_.load(std::memory_order_relaxed); }
  uint64_t failed() const { return failed_.load(std::memory_order_relaxed); }
  uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  void run();
  bool post(const EventRecord& event);

  NotifierConfig config_;
  std::deque<EventRecord> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> sent_{0};
  std::atomic<uint64_t> failed_{0};
  std::atomic<uint64_t> dropped_{0};
};

}  // namespace server
}  // namespace vmon
