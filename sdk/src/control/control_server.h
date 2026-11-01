// 进程内控制通道（UDS）。Agent 通过它下发只读/轻量指令：
//   set_level / ping / dump_stacks / get_stats
//
// 控制线程独立于业务线程；任何失败都静默降级，不影响进程运行。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <vmon/vmon.h>

namespace vmon {
namespace detail {

class ControlServer {
 public:
  void start(const Config& config, uint32_t pid) noexcept;
  void stop() noexcept;
  bool running() const noexcept { return running_.load(); }
  const std::string& path() const noexcept { return path_; }

 private:
  void run() noexcept;
  std::string handle(const std::string& request) noexcept;

  std::thread thread_;
  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::string path_;
  uint32_t pid_ = 0;
  // 动态日志级别的 TTL 回退：到期后恢复到下发前的级别。
  uint64_t revert_at_mono_ns_ = 0;
  Level revert_level_ = Level::kInfo;
};

}  // namespace detail
}  // namespace vmon
