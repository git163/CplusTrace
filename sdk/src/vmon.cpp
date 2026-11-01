#include "vmon/vmon.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <unistd.h>

#include "control/control_server.h"
#include "core/clock.h"
#include "crash/crash_handler.h"
#include "core/event.h"
#include "core/thread_context.h"
#include "transport/sender.h"

namespace vmon {
namespace {

std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_sample_permyriad{10000};
std::mutex        g_config_mutex;
Config            g_config;
detail::ControlServer g_control;

bool env_disabled() noexcept {
  const char* v = std::getenv("VMON_DISABLE");
  return v != nullptr && std::strcmp(v, "1") == 0;
}

std::string default_instance_id(pid_t pid) {
  char host[256];
  host[0] = '\0';
  if (::gethostname(host, sizeof(host)) != 0) {
    std::strcpy(host, "unknown");
  }
  host[sizeof(host) - 1] = '\0';
  return std::string(host) + "-" + std::to_string(static_cast<long>(pid));
}

}  // namespace

bool init(const Config& config) noexcept {
  // 任何失败都只让 SDK 保持 disabled，绝不影响业务。
  if (!config.enabled || config.service_name.empty() || env_disabled()) {
    g_enabled.store(false, std::memory_order_release);
    return false;
  }

  const pid_t pid = ::getpid();
  Config effective = config;
  if (effective.instance_id.empty()) {
    effective.instance_id = default_instance_id(pid);
  }

  {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    g_config = effective;
  }

  g_sample_permyriad.store(effective.sample_permyriad > 10000
                               ? 10000
                               : effective.sample_permyriad,
                           std::memory_order_release);
  g_enabled.store(true, std::memory_order_release);

  if (effective.enable_transport) {
    detail::Sender::instance().start(effective, static_cast<uint32_t>(pid));
  }
  if (effective.enable_control) {
    g_control.start(effective, static_cast<uint32_t>(pid));
  }
  static detail::CrashHandler crash_handler;
  if (effective.enable_crash_handler) {
    crash_handler.install(effective);
  }
  return true;
}

void shutdown() noexcept {
  g_enabled.store(false, std::memory_order_release);
  g_control.stop();
  detail::Sender::instance().stop();
  // 崩溃处理器在 init 中按配置安装；正常退出时移除占位文件。
  static detail::CrashHandler crash_handler;
  crash_handler.uninstall();
}

bool enabled() noexcept {
  return g_enabled.load(std::memory_order_acquire);
}

void heartbeat() noexcept {
  if (!enabled()) {
    return;
  }

  detail::ThreadContext& ctx = detail::thread_context();
  detail::Event ev =
      detail::make_event(ctx, detail::EventType::kHeartbeat, nullptr, nullptr, 0);

  if (ctx.depth > 0) {
    const detail::SpanFrame& frame = ctx.stack[ctx.depth - 1];
    ev.trace       = frame.trace;
    ev.span        = frame.span;
    ev.parent_span = frame.parent;
  }

  ctx.ring.push(ev);
}

void set_request_id(const char* request_id) noexcept {
  if (!enabled()) return;
  detail::ThreadContext& ctx = detail::thread_context();
  ctx.request_id = request_id;
}

void set_task_id(const char* task_id) noexcept {
  if (!enabled()) return;
  detail::ThreadContext& ctx = detail::thread_context();
  ctx.task_id = task_id;
}

const char* request_id() noexcept {
  if (!enabled()) return nullptr;
  return detail::thread_context().request_id;
}

const char* task_id() noexcept {
  if (!enabled()) return nullptr;
  return detail::thread_context().task_id;
}

uint64_t dropped_events() noexcept {
  return detail::thread_context().ring.dropped();
}

namespace detail {

uint32_t sample_permyriad() noexcept {
  return g_sample_permyriad.load(std::memory_order_relaxed);
}

}  // namespace detail

}  // namespace vmon
