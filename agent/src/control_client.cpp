#include "control_client.h"

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace vmon {
namespace agent {
namespace {

const size_t kMaxResponseBytes = 8 * 1024 * 1024;

bool send_all(int fd, const std::string& data) {
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t n = ::send(fd, data.data() + offset, data.size() - offset,
                             MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    offset += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

std::string control_socket_path(const std::string& control_dir, int32_t pid) {
  const std::string dir =
      control_dir.empty() ? std::string("/tmp/vmon-ctrl") : control_dir;
  return dir + "/" + std::to_string(pid) + ".sock";
}

bool send_control_command(const std::string& socket_path,
                          const std::string& request_json,
                          uint32_t timeout_ms, std::string* response,
                          std::string* error) {
  if (response != nullptr) response->clear();
  if (error != nullptr) error->clear();
  if (socket_path.empty() || socket_path.size() + 1 > sizeof(sockaddr_un::sun_path)) {
    if (error) *error = "控制套接字路径非法";
    return false;
  }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) *error = std::string("socket 创建失败: ") + std::strerror(errno);
    return false;
  }

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);
  const socklen_t len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
    if (error) {
      *error = std::string("连接控制通道失败(") + socket_path +
               "): " + std::strerror(errno);
    }
    ::close(fd);
    return false;
  }

  const uint32_t wait_ms = timeout_ms == 0 ? 3000 : timeout_ms;
  timeval tv;
  tv.tv_sec = wait_ms / 1000;
  tv.tv_usec = static_cast<suseconds_t>((wait_ms % 1000) * 1000);
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  const std::string payload = request_json + "\n";
  if (!send_all(fd, payload)) {
    if (error) *error = "向控制通道发送请求失败";
    ::close(fd);
    return false;
  }

  std::string buffer;
  char chunk[4096];
  while (buffer.size() < kMaxResponseBytes) {
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int ready = ::poll(&pfd, 1, static_cast<int>(wait_ms));
    if (ready < 0) {
      if (errno == EINTR) continue;
      if (error) *error = "等待控制通道响应失败";
      ::close(fd);
      return false;
    }
    if (ready == 0) {
      if (error) *error = "等待控制通道响应超时";
      ::close(fd);
      return false;
    }
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (error) *error = "读取控制通道响应失败";
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    buffer.append(chunk, static_cast<size_t>(n));
    if (buffer.find('\n') != std::string::npos) break;
  }
  ::close(fd);

  while (!buffer.empty() &&
         (buffer.back() == '\n' || buffer.back() == '\r')) {
    buffer.pop_back();
  }
  if (buffer.size() >= kMaxResponseBytes) {
    if (error) *error = "控制通道响应超过上限";
    return false;
  }
  if (response != nullptr) *response = buffer;
  return true;
}

}  // namespace agent
}  // namespace vmon
