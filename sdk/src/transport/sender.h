// SDK 后台发送线程：排空各业务线程的事件环，经 UDS 发往 Agent。
//
// - 只在后台线程做序列化与写操作，业务线程零阻塞；
// - Agent 不在线时事件被丢弃并计数（后续版本可加本地有界 spool）；
// - 任何异常都在线程边界吞掉，不影响业务。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <vmon/vmon.h>

#include "core/thread_context.h"

namespace vmon {
namespace detail {

class Sender {
 public:
  static Sender& instance() noexcept;

  void start(const Config& config, uint32_t pid) noexcept;
  void stop() noexcept;

  bool running() const noexcept { return running_.load(); }
  uint64_t sent() const noexcept { return sent_.load(); }
  uint64_t dropped() const noexcept { return dropped_.load(); }

 private:
  Sender() noexcept = default;
  ~Sender();
  Sender(const Sender&) = delete;
  Sender& operator=(const Sender&) = delete;

  void run() noexcept;
  bool connect_socket() noexcept;
  void close_socket() noexcept;
  void write_events(const Event* events, size_t count) noexcept;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> sent_{0};
  std::atomic<uint64_t> dropped_{0};

  int fd_ = -1;
  std::string endpoint_;
  std::string service_;
  std::string instance_;
  std::string version_;
  uint32_t pid_ = 0;
  uint32_t interval_ms_ = 100;
};

}  // namespace detail
}  // namespace vmon
