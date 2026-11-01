// trace/span 标识与线程标识。
//
// - TraceId 采用 128 位，后续可直接映射 W3C traceparent；
// - SpanId 先用 64 位内部表示，接入 W3C 时只取低 64 位输出；
// - ID 由线程本地 xorshift64 生成，不使用全局随机数锁，也不调用 getrandom。
#pragma once

#include <cstdint>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace vmon {
namespace detail {

struct TraceId {
  uint64_t hi = 0;
  uint64_t lo = 0;
};

inline bool valid(const TraceId& id) noexcept {
  return id.hi != 0 || id.lo != 0;
}

inline bool operator==(const TraceId& a, const TraceId& b) noexcept {
  return a.hi == b.hi && a.lo == b.lo;
}

inline uint64_t xorshift64(uint64_t& state) noexcept {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

inline uint32_t current_thread_id() noexcept {
#if defined(__linux__)
  return static_cast<uint32_t>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
  uint64_t tid = 0;
  ::pthread_threadid_np(nullptr, &tid);
  return static_cast<uint32_t>(tid);
#else
  return 0;
#endif
}

}  // namespace detail
}  // namespace vmon
