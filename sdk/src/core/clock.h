// 时间基准：monotonic 用于进程内时序，wall-clock 用于跨节点对齐。
// clock_gettime 走 vDSO，热路径可用；禁止在热路径调用 gettimeofday/格式化。
#pragma once

#include <cstdint>
#include <ctime>

namespace vmon {
namespace detail {

inline uint64_t mono_ns() noexcept {
  timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t wall_ns() noexcept {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

}  // namespace detail
}  // namespace vmon
