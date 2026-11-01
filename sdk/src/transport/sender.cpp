#include "transport/sender.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/registry.h"
#include "json_event.h"

namespace vmon {
namespace detail {
namespace {

const char* type_name(EventType type) {
  switch (type) {
    case EventType::kSpanBegin: return "span_begin";
    case EventType::kSpanEnd:   return "span_end";
    case EventType::kHeartbeat: return "heartbeat";
    case EventType::kLog:       return "log";
    default:                    return "event";
  }
}

struct DrainState {
  Event*  out;
  size_t  capacity;
  size_t  count;
};

void drain_visitor(ThreadContext* ctx, void* user) noexcept {
  DrainState* state = static_cast<DrainState*>(user);
  Event ev;
  while (state->count < state->capacity && ctx->ring.pop(ev)) {
    state->out[state->count++] = ev;
  }
}

void sleep_interruptible(const std::atomic<bool>& running,
                         uint32_t total_ms) noexcept {
  const uint32_t step_ms = 10;
  uint32_t slept = 0;
  while (running.load() && slept < total_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    slept += step_ms;
  }
}

size_t count_newlines(const char* data, size_t size) {
  size_t lines = 0;
  for (size_t i = 0; i < size; ++i) {
    if (data[i] == '\n') ++lines;
  }
  return lines;
}

}  // namespace

Sender& Sender::instance() noexcept {
  static Sender sender;
  return sender;
}

Sender::~Sender() {
  stop();
}

void Sender::start(const Config& config, uint32_t pid) noexcept {
  if (running_.exchange(true)) {
    return;
  }

  endpoint_ = config.agent_endpoint.empty() ? std::string("/tmp/vmon-agent.sock")
                                            : config.agent_endpoint;
  service_ = config.service_name;
  instance_ = config.instance_id;
  version_ = config.service_version;
  pid_ = pid;
  interval_ms_ = config.flush_interval_ms == 0 ? 100 : config.flush_interval_ms;
  sent_.store(0);
  dropped_.store(0);

  try {
    thread_ = std::thread(&Sender::run, this);
  } catch (...) {
    running_.store(false);
  }
}

void Sender::stop() noexcept {
  if (!running_.exchange(false)) {
    return;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  close_socket();
}

void Sender::close_socket() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool Sender::connect_socket() noexcept {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (endpoint_.size() + 1 > sizeof(addr.sun_path)) {
    ::close(fd);
    return false;
  }
  std::memcpy(addr.sun_path, endpoint_.c_str(), endpoint_.size() + 1);

  const socklen_t len =
      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                             endpoint_.size() + 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
    ::close(fd);
    return false;
  }

  // 发送超时：Agent 卡住时最多阻塞 200ms，不拖住关停。
  timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 200 * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  fd_ = fd;
  return true;
}

void Sender::run() noexcept {
  std::vector<Event> batch(1024);

  while (running_.load()) {
    if (fd_ < 0 && !connect_socket()) {
      sleep_interruptible(running_, 200);
      continue;
    }

    DrainState state;
    state.out = batch.data();
    state.capacity = batch.size();
    state.count = 0;
    for_each_thread_context(&drain_visitor, &state);

    if (state.count > 0) {
      write_events(batch.data(), state.count);
    }

    sleep_interruptible(running_, interval_ms_);
  }
}

void Sender::write_events(const Event* events, size_t count) noexcept {
  std::string buffer;
  buffer.reserve(count * 200);
  size_t prepared = 0;
  size_t lost = 0;

  for (size_t i = 0; i < count; ++i) {
    const Event& ev = events[i];
    common::Event out;
    out.type = type_name(static_cast<EventType>(ev.type));
    out.time_ns = ev.wall_ns;
    out.mono_ns = ev.mono_ns;
    if (valid(ev.trace)) {
      out.trace = common::hex128(ev.trace.hi, ev.trace.lo);
    }
    if (ev.span != 0) {
      out.span = common::hex64(ev.span);
    }
    if (ev.parent_span != 0) {
      out.parent = common::hex64(ev.parent_span);
    }
    out.service = service_;
    out.instance = instance_;
    out.version = version_;
    out.pid = static_cast<int32_t>(pid_);
    out.tid = static_cast<int32_t>(ev.thread_id);
    if (ev.name != nullptr) out.name = ev.name;
    if (ev.file != nullptr) out.file = ev.file;
    if (ev.request_id != nullptr) out.request_id = ev.request_id;
    if (ev.task_id != nullptr) out.task_id = ev.task_id;
    if (ev.error_code != nullptr) {
      out.error_code = ev.error_code;
      out.attrs["error"] = "true";
      out.attrs["error_code"] = ev.error_code;
    }
    out.line = ev.line;
    out.level = common::level_name(ev.level);
    out.msg.assign(ev.msg, ev.msg_len);
    // 业务日志可能包含非 UTF-8 字节：序列化绝不能抛异常终止业务进程。
    // 非法字节用替换字符写入，其余字段保持可读；其他异常则丢弃这一条。
    try {
      buffer += common::to_json(out).dump(
          -1, ' ', false, common::json::error_handler_t::replace);
      buffer.push_back('\n');
      ++prepared;
    } catch (...) {
      ++lost;
    }
  }

  if (prepared == 0) {
    dropped_.fetch_add(lost);
    return;
  }

  size_t offset = 0;
  while (offset < buffer.size()) {
    const ssize_t written =
        ::write(fd_, buffer.data() + offset, buffer.size() - offset);
    if (written > 0) {
      offset += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    // 写失败或超时：断开连接，按已写出的完整行计数，其余丢弃。
    const size_t delivered = count_newlines(buffer.data(), offset);
    sent_.fetch_add(delivered);
    dropped_.fetch_add(prepared - delivered + lost);
    close_socket();
    return;
  }

  sent_.fetch_add(prepared);
  dropped_.fetch_add(lost);
}

}  // namespace detail
}  // namespace vmon
