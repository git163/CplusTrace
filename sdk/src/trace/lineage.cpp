// 数据血缘埋点：把字段值指纹写入线程本地血缘环。
//
// 约束与 span 一致：不分配、不加锁、不 IO；object/field 必须是静态字符串；
// value 拷贝到固定缓冲并截断。事件由后台 Sender 统一发出（type=lineage/state）。
#include "vmon/vmon.h"

#include <cstdio>
#include <cstring>

#include "core/thread_context.h"

namespace vmon {
namespace {

uint64_t fnv1a64(const char* text) noexcept {
  uint64_t hash = 1469598103934665603ull;
  if (text == nullptr) return hash;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
       *p != 0; ++p) {
    hash ^= static_cast<uint64_t>(*p);
    hash *= 1099511628211ull;
  }
  return hash;
}

void record_impl(const char* object, const char* field, const char* value,
                 DataDir dir, DataKind kind) noexcept {
  if (!enabled()) return;
  detail::ThreadContext& ctx = detail::thread_context();

  detail::LineageEvent ev;
  ev.mono_ns = detail::mono_ns();
  ev.wall_ns = detail::wall_ns();
  ev.seq = ++ctx.seq;
  ev.thread_id = ctx.thread_id;
  ev.type = static_cast<uint16_t>(detail::LineageType::kField);
  ev.dir = static_cast<uint8_t>(dir);
  ev.kind = static_cast<uint8_t>(kind);
  ev.run_id = ctx.run_id;
  ev.iteration_id = ctx.iteration_id;
  ev.object = object;
  ev.field = field;

  if (ctx.depth > 0) {
    const detail::SpanFrame& frame = ctx.stack[ctx.depth - 1];
    ev.trace = frame.trace;
    ev.span = frame.span;
    ev.parent_span = frame.parent;
  }

  const char* text = value == nullptr ? "" : value;
  const size_t full_len = std::strlen(text);
  size_t copy_len = full_len;
  if (copy_len > sizeof(ev.value) - 1) copy_len = sizeof(ev.value) - 1;
  std::memcpy(ev.value, text, copy_len);
  ev.value[copy_len] = '\0';
  ev.value_len = static_cast<uint32_t>(full_len);
  ev.value_hash = fnv1a64(text);

  ctx.lineage_ring.push(ev);
}

}  // namespace

void set_run_id(const char* run_id) noexcept {
  detail::thread_context().run_id = run_id;
}

void set_iteration_id(uint64_t iteration_id) noexcept {
  detail::thread_context().iteration_id = iteration_id;
}

void record_field(const char* object, const char* field, const char* value,
                  DataDir dir, DataKind kind) noexcept {
  record_impl(object, field, value, dir, kind);
}

void record_field_i64(const char* object, const char* field, int64_t value,
                      DataDir dir) noexcept {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  record_impl(object, field, buffer, dir, DataKind::kInt);
}

void record_field_f64(const char* object, const char* field, double value,
                      DataDir dir) noexcept {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  record_impl(object, field, buffer, dir, DataKind::kFloat);
}

void record_state(const char* device, const char* state) noexcept {
  if (!enabled()) return;
  detail::ThreadContext& ctx = detail::thread_context();

  detail::LineageEvent ev;
  ev.mono_ns = detail::mono_ns();
  ev.wall_ns = detail::wall_ns();
  ev.seq = ++ctx.seq;
  ev.thread_id = ctx.thread_id;
  ev.type = static_cast<uint16_t>(detail::LineageType::kState);
  ev.run_id = ctx.run_id;
  ev.iteration_id = ctx.iteration_id;
  ev.device = device;
  ev.state = state;
  ctx.lineage_ring.push(ev);
}

}  // namespace vmon
