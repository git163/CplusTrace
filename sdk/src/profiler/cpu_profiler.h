// 常驻 CPU 采样（诊断版，默认不编译）。
//
// 原理：ITIMER_PROF + SIGPROF，信号处理里用 backtrace() 抓取少量地址，
// 写入无锁环形槽；后台线程解析符号、聚合成折叠栈并通过 Agent UDS 上报
// type=profile 事件。仅建议在诊断版打开，生产默认关闭。
#pragma once

#include <atomic>
#include <thread>

#include <vmon/vmon.h>

namespace vmon {
namespace detail {

class CpuProfiler {
 public:
  static CpuProfiler& instance() noexcept;

  void start(const Config& config, uint32_t pid) noexcept;
  void stop() noexcept;
  bool running() const noexcept;

 private:
  CpuProfiler() noexcept = default;
  ~CpuProfiler();
  CpuProfiler(const CpuProfiler&) = delete;
  CpuProfiler& operator=(const CpuProfiler&) = delete;

  void run() noexcept;
  void flush() noexcept;
  bool send(const std::string& body) noexcept;

  Config config_;
  uint32_t pid_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace detail
}  // namespace vmon
