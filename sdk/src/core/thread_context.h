// 线程本地上下文：事件环 + span 栈。
//
// 每个业务线程一份，首次使用时创建；不含任何全局锁。
// span 栈容量固定，超过深度上限时新建的 span 退化为空对象，不分配内存。
#pragma once

#include <cstdint>
#include <unistd.h>

#include "core/clock.h"
#include "core/event.h"
#include "core/ids.h"
#include "core/ring_buffer.h"

namespace vmon {
namespace detail {

constexpr std::size_t kRingCapacity = 512;  // 事件数，实际可用 511
constexpr int kMaxSpanDepth = 32;

struct ThreadContext;

// 线程上下文注册表：供后台 flush 线程统一排空各线程事件环。
// 注册/注销只在线程首次使用与退出时发生，不在热路径上。
void register_thread_context(ThreadContext* ctx) noexcept;
void unregister_thread_context(ThreadContext* ctx) noexcept;

struct SpanFrame {
  TraceId  trace;
  uint64_t span      = 0;
  uint64_t parent    = 0;
  uint64_t start_ns  = 0;
  const char* name       = nullptr;  // 静态字符串（__func__ / 字面量）
  const char* error_code = nullptr;  // 出错时由 set_error 写入，nullptr 表示正常
};

struct ThreadContext {
  ThreadContext() noexcept;
  ~ThreadContext() noexcept;

  RingBuffer<Event, kRingCapacity> ring;
  SpanFrame stack[kMaxSpanDepth];
  int       depth          = 0;
  int       suppress_depth = 0;  // >0 表示当前处于未采样 trace 内
  uint32_t  thread_id      = 0;
  uint64_t  seq            = 0;
  uint64_t  rng            = 0;
  const char* request_id   = nullptr;  // 当前请求关联 ID（线程本地）
  const char* task_id      = nullptr;
};

inline ThreadContext::ThreadContext() noexcept
    : thread_id(current_thread_id()),
      rng(mono_ns() ^ (static_cast<uint64_t>(::getpid()) << 32) ^
          reinterpret_cast<uintptr_t>(this)) {
  if (rng == 0) {
    rng = 0x9e3779b97f4a7c15ull;  // xorshift 需要非零种子
  }
  register_thread_context(this);
}

inline ThreadContext::~ThreadContext() noexcept {
  unregister_thread_context(this);
}

ThreadContext& thread_context() noexcept;

inline TraceId new_trace_id(ThreadContext& ctx) noexcept {
  TraceId id;
  id.hi = xorshift64(ctx.rng);
  id.lo = xorshift64(ctx.rng);
  if (!valid(id)) {
    id.lo = 1;  // 全零不是合法 trace id
  }
  return id;
}

inline uint64_t new_span_id(ThreadContext& ctx) noexcept {
  uint64_t id = xorshift64(ctx.rng);
  return id == 0 ? 1 : id;
}

// 填充事件公共字段；trace/span 字段由调用方按当前栈设置。
inline Event make_event(ThreadContext& ctx, EventType type, const char* name,
                        const char* file, int line) noexcept {
  Event ev;
  ev.mono_ns   = mono_ns();
  ev.wall_ns   = wall_ns();
  ev.seq       = ++ctx.seq;
  ev.thread_id = ctx.thread_id;
  ev.type      = static_cast<uint16_t>(type);
  ev.name      = name;
  ev.file      = file;
  ev.line      = line;
  ev.request_id = ctx.request_id;
  ev.task_id    = ctx.task_id;
  return ev;
}

}  // namespace detail
}  // namespace vmon
