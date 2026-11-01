#include "control/control_server.h"

#include <cerrno>
#include <cstring>
#include <string>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__linux__)
#include <dirent.h>
#include <fstream>
#endif

#include <nlohmann/json.hpp>

#include "core/clock.h"
#include "transport/sender.h"

namespace vmon {
namespace detail {
namespace {

#if defined(__linux__)
std::string read_file(const std::string& path, size_t limit) {
  std::ifstream in(path);
  if (!in) return std::string();
  std::string content;
  std::getline(in, content);
  if (content.size() > limit) content.resize(limit);
  return content;
}

nlohmann::json dump_threads() {
  nlohmann::json tasks = nlohmann::json::array();
  DIR* dir = ::opendir("/proc/self/task");
  if (dir == nullptr) return tasks;
  while (dirent* entry = ::readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    const std::string base = std::string("/proc/self/task/") + entry->d_name;
    nlohmann::json task;
    task["tid"] = std::atoi(entry->d_name);
    task["comm"] = read_file(base + "/comm", 64);
    task["state"] = read_file(base + "/stat", 256);
    tasks.push_back(task);
    if (tasks.size() >= 512) break;
  }
  ::closedir(dir);
  return tasks;
}
#endif

}  // namespace

void ControlServer::start(const Config& config, uint32_t pid) noexcept {
  if (running_.load()) return;
  pid_ = pid;

  std::string dir = config.control_dir.empty() ? std::string("/tmp/vmon-ctrl")
                                               : config.control_dir;
  // 0700 会让不同用户的 Agent 无法连接；生产上业务进程与 Agent 通过
  // 受控用户组共享该目录，目录本身应预置为 0770。
  ::mkdir(dir.c_str(), 0770);
  path_ = dir + "/" + std::to_string(pid) + ".sock";

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return;

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (path_.size() + 1 > sizeof(addr.sun_path)) {
    ::close(fd);
    return;
  }
  std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);

  ::unlink(path_.c_str());
  const socklen_t len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + path_.size() + 1);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0 ||
      ::listen(fd, 8) != 0) {
    ::close(fd);
    ::unlink(path_.c_str());
    return;
  }
  // 0660：业务进程属主可读写，同组 Agent 可连接；其他用户不可访问。
  ::chmod(path_.c_str(), 0660);

  listen_fd_ = fd;
  running_.store(true);
  try {
    thread_ = std::thread(&ControlServer::run, this);
  } catch (...) {
    running_.store(false);
    ::close(fd);
    listen_fd_ = -1;
    ::unlink(path_.c_str());
  }
}

void ControlServer::stop() noexcept {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  ::unlink(path_.c_str());
}

void ControlServer::run() noexcept {
  while (running_.load()) {
    if (revert_at_mono_ns_ != 0 && mono_ns() >= revert_at_mono_ns_) {
      set_min_level(revert_level_);
      revert_at_mono_ns_ = 0;
    }
    pollfd pfd;
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int ready = ::poll(&pfd, 1, 200);
    if (ready <= 0) continue;

    const int client = ::accept(listen_fd_, nullptr, nullptr);
    if (client < 0) continue;

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string request;
    char buffer[1024];
    while (request.size() < 4096) {
      const ssize_t n = ::read(client, buffer, sizeof(buffer));
      if (n <= 0) break;
      request.append(buffer, static_cast<size_t>(n));
      if (request.find('\n') != std::string::npos) break;
    }
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
      request.pop_back();
    }

    const std::string response = handle(request);
    size_t offset = 0;
    while (offset < response.size()) {
      const ssize_t n =
          ::write(client, response.data() + offset, response.size() - offset);
      if (n <= 0) break;
      offset += static_cast<size_t>(n);
    }
    ::close(client);
  }
}

std::string ControlServer::handle(const std::string& request) noexcept {
  nlohmann::json reply;
  try {
    const nlohmann::json req = nlohmann::json::parse(request);
    const std::string cmd = req.value("cmd", std::string());
    if (cmd == "ping") {
      reply["ok"] = true;
      reply["pid"] = pid_;
      reply["enabled"] = enabled();
    } else if (cmd == "get_level") {
      const Level current = min_level();
      const char* names[] = {"trace", "debug", "info", "warn", "error", "fatal"};
      const size_t index = static_cast<size_t>(current);
      reply["ok"] = true;
      reply["level"] = index < 6 ? names[index] : "info";
    } else if (cmd == "set_level") {
      const std::string level = req.value("level", std::string("info"));
      Level parsed = Level::kInfo;
      if (level == "trace") parsed = Level::kTrace;
      else if (level == "debug") parsed = Level::kDebug;
      else if (level == "info") parsed = Level::kInfo;
      else if (level == "warn") parsed = Level::kWarn;
      else if (level == "error") parsed = Level::kError;
      else if (level == "fatal") parsed = Level::kFatal;
      else {
        reply["ok"] = false;
        reply["error"] = "unknown level";
        return reply.dump() + "\n";
      }
      const uint64_t ttl_ms = req.value("ttl_ms", static_cast<uint64_t>(0));
      if (ttl_ms > 0) {
        revert_level_ = min_level();
        revert_at_mono_ns_ =
            mono_ns() + ttl_ms * 1000000ull;
        reply["revert_at_ns"] = revert_at_mono_ns_;
      } else {
        revert_at_mono_ns_ = 0;
      }
      set_min_level(parsed);
      reply["ok"] = true;
      reply["level"] = level;
    } else if (cmd == "get_stats") {
      reply["ok"] = true;
      reply["sent"] = Sender::instance().sent();
      reply["dropped"] = Sender::instance().dropped();
    } else if (cmd == "dump_stacks") {
#if defined(__linux__)
      reply["ok"] = true;
      reply["threads"] = dump_threads();
#else
      reply["ok"] = false;
      reply["error"] = "dump_stacks 仅在 Linux 上支持";
#endif
    } else {
      reply["ok"] = false;
      reply["error"] = "unknown cmd";
    }
  } catch (...) {
    reply["ok"] = false;
    reply["error"] = "invalid request";
  }
  return reply.dump() + "\n";
}

}  // namespace detail
}  // namespace vmon
