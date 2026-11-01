#include "vmon/vmon.h"

#include "core/config.h"
#include "core/thread_context.h"

namespace vmon {
namespace detail {

ThreadContext& thread_context() noexcept {
  thread_local ThreadContext ctx;
  return ctx;
}

}  // namespace detail

namespace {

bool root_is_sampled(detail::ThreadContext& ctx) noexcept {
  const uint32_t rate = detail::sample_permyriad();
  if (rate >= 10000) return true;
  if (rate == 0) return false;
  return (detail::xorshift64(ctx.rng) % 10000) < rate;
}

}  // namespace

SpanContext current_context() noexcept {
  SpanContext out;
  if (!enabled()) return out;

  detail::ThreadContext& ctx = detail::thread_context();
  if (ctx.depth <= 0) return out;

  const detail::SpanFrame& frame = ctx.stack[ctx.depth - 1];
  out.trace_hi = frame.trace.hi;
  out.trace_lo = frame.trace.lo;
  out.span = frame.span;
  out.valid = true;
  return out;
}

Span create_span_with_parent(const SpanContext& parent, const char* name,
                             const char* file, int line) noexcept {
  if (!parent.valid) {
    return Span::create(name, file, line);
  }
  return Span::create_impl(&parent, name, file, line);
}

Span::Span(Span&& other) noexcept
    : active_(other.active_),
      suppressed_(other.suppressed_),
      slot_(other.slot_) {
  other.active_ = false;
  other.suppressed_ = false;
  other.slot_ = -1;
}

Span& Span::operator=(Span&& other) noexcept {
  if (this != &other) {
    end();
    active_ = other.active_;
    suppressed_ = other.suppressed_;
    slot_ = other.slot_;
    other.active_ = false;
    other.suppressed_ = false;
    other.slot_ = -1;
  }
  return *this;
}

Span::~Span() {
  end();
}

Span Span::create(const char* name, const char* file, int line) noexcept {
  return create_impl(nullptr, name, file, line);
}

Span Span::create_impl(const SpanContext* parent, const char* name,
                       const char* file, int line) noexcept {
  Span span;
  if (!enabled()) {
    return span;
  }

  detail::ThreadContext& ctx = detail::thread_context();

  // 未采样的 trace 内所有 span 都抑制，避免子 span 变成新的根 trace。
  if (ctx.suppress_depth > 0) {
    ++ctx.suppress_depth;
    span.suppressed_ = true;
    return span;
  }

  const bool is_root = (parent == nullptr && ctx.depth == 0);
  if (is_root && !root_is_sampled(ctx)) {
    ctx.suppress_depth = 1;
    span.suppressed_ = true;
    return span;
  }

  if (ctx.depth >= detail::kMaxSpanDepth) {
    // 超过深度上限：退化为空 span，不分配、不报错。
    return span;
  }

  const int slot = ctx.depth;
  detail::SpanFrame& frame = ctx.stack[slot];

  if (parent != nullptr) {
    frame.trace.hi = parent->trace_hi;
    frame.trace.lo = parent->trace_lo;
    frame.parent = parent->span;
  } else if (slot > 0) {
    const detail::SpanFrame& parent_frame = ctx.stack[slot - 1];
    frame.trace = parent_frame.trace;
    frame.parent = parent_frame.span;
  } else {
    frame.trace = detail::new_trace_id(ctx);
    frame.parent = 0;
  }

  frame.span = detail::new_span_id(ctx);
  frame.start_ns = detail::mono_ns();
  frame.name = name;

  detail::Event ev =
      detail::make_event(ctx, detail::EventType::kSpanBegin, name, file, line);
  ev.trace = frame.trace;
  ev.span = frame.span;
  ev.parent_span = frame.parent;
  ctx.ring.push(ev);

  ++ctx.depth;
  span.active_ = true;
  span.slot_ = slot;
  return span;
}

void Span::set_error(const char* error_code) noexcept {
  if (!active_ || slot_ < 0) return;
  detail::ThreadContext& ctx = detail::thread_context();
  if (slot_ >= ctx.depth) return;
  ctx.stack[slot_].error_code = error_code;
}

void Span::end() noexcept {
  if (!active_) {
    if (suppressed_) {
      suppressed_ = false;
      detail::ThreadContext& ctx = detail::thread_context();
      if (ctx.suppress_depth > 0) {
        --ctx.suppress_depth;
      }
    }
    return;
  }
  active_ = false;

  detail::ThreadContext& ctx = detail::thread_context();
  if (slot_ < 0 || slot_ != ctx.depth - 1) {
    // 非 LIFO 析构（例如跨线程移动后乱序释放）：放弃本次修正，
    // 保证不越界、不破坏其他 span 的栈帧。
    return;
  }

  const detail::SpanFrame& frame = ctx.stack[slot_];
  detail::Event ev = detail::make_event(ctx, detail::EventType::kSpanEnd,
                                        frame.name, nullptr, 0);
  ev.trace = frame.trace;
  ev.span = frame.span;
  ev.parent_span = frame.parent;
  ev.error_code = frame.error_code;
  ctx.ring.push(ev);

  --ctx.depth;
}

}  // namespace vmon
