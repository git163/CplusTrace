#include <vmon/vmon.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include "core/thread_context.h"

namespace vmon {
namespace {

std::atomic<int> g_min_level{static_cast<int>(Level::kInfo)};

}  // namespace

bool should_log(Level level) noexcept {
  if (!enabled()) {
    return false;
  }
  return static_cast<int>(level) >=
         g_min_level.load(std::memory_order_relaxed);
}

void set_min_level(Level level) noexcept {
  g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

Level min_level() noexcept {
  return static_cast<Level>(g_min_level.load(std::memory_order_relaxed));
}

void log(Level level, const char* file, int line, const char* fmt, ...) noexcept {
  if (!enabled() ||
      static_cast<int>(level) < g_min_level.load(std::memory_order_relaxed)) {
    return;
  }

  detail::ThreadContext& ctx = detail::thread_context();
  detail::Event ev =
      detail::make_event(ctx, detail::EventType::kLog, nullptr, file, line);
  ev.level = static_cast<uint16_t>(level);

  if (fmt != nullptr) {
    va_list ap;
    va_start(ap, fmt);
    const int written = std::vsnprintf(ev.msg, sizeof(ev.msg), fmt, ap);
    va_end(ap);
    if (written < 0) {
      ev.msg_len = 0;
      ev.msg[0] = '\0';
    } else if (static_cast<size_t>(written) >= sizeof(ev.msg)) {
      ev.msg_len = static_cast<uint16_t>(sizeof(ev.msg) - 1);
    } else {
      ev.msg_len = static_cast<uint16_t>(written);
    }
  }

  if (ctx.depth > 0) {
    const detail::SpanFrame& frame = ctx.stack[ctx.depth - 1];
    ev.trace       = frame.trace;
    ev.span        = frame.span;
    ev.parent_span = frame.parent;
  }

  ctx.ring.push(ev);
}

}  // namespace vmon
