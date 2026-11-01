#include "ingest.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/un.h>
#endif

#include "agent.h"

namespace vmon {
namespace agent {
namespace {

uint32_t peer_pid_of(int fd) {
#if defined(__linux__)
  struct ucred cred;
  socklen_t len = sizeof(cred);
  if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
    return static_cast<uint32_t>(cred.pid);
  }
#elif defined(__APPLE__)
  pid_t pid = 0;
  socklen_t len = sizeof(pid);
  if (::getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &len) == 0) {
    return static_cast<uint32_t>(pid);
  }
#endif
  return 0;
}

}  // namespace

void IngestServer::start(const std::string& uds_path, LineHandler handler) {
  if (running_.load()) return;
  handler_ = handler;
  path_ = uds_path;

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
      ::listen(fd, 64) != 0) {
    ::close(fd);
    return;
  }
  ::chmod(path_.c_str(), 0600);

  const int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  listen_fd_ = fd;
  running_.store(true);
  thread_ = std::thread(&IngestServer::run, this);
}

void IngestServer::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  ::unlink(path_.c_str());
}

void IngestServer::run() {
  std::vector<Client> clients;

  while (running_.load()) {
    std::vector<pollfd> fds;
    fds.reserve(clients.size() + 1);
    pollfd listener;
    listener.fd = listen_fd_;
    listener.events = POLLIN;
    listener.revents = 0;
    fds.push_back(listener);
    for (const Client& client : clients) {
      pollfd pfd;
      pfd.fd = client.fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      fds.push_back(pfd);
    }

    const int ready = ::poll(fds.data(), fds.size(), 200);
    if (ready < 0 && errno != EINTR) break;
    if (ready <= 0) continue;

    if (fds[0].revents & POLLIN) {
      while (true) {
        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) break;
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        Client client;
        client.fd = client_fd;
        clients.push_back(client);
      }
    }

    for (size_t i = 0; i < clients.size(); ++i) {
      if (!(fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      handle_client(clients[i], peer_pid_of(clients[i].fd));
    }

    // 清理已关闭/超限的连接
    clients.erase(std::remove_if(clients.begin(), clients.end(),
                                 [](const Client& c) { return c.fd < 0; }),
                  clients.end());
  }

  for (Client& client : clients) {
    if (client.fd >= 0) ::close(client.fd);
  }
  clients.clear();
}

void IngestServer::handle_client(Client& client, uint32_t peer_pid) {
  char buffer[65536];
  while (true) {
    const ssize_t n = ::read(client.fd, buffer, sizeof(buffer));
    if (n > 0) {
      client.buffer.append(buffer, static_cast<size_t>(n));
      if (client.buffer.size() > 4 * 1024 * 1024) {
        client.buffer.clear();  // 防御性上限
      }
      size_t begin = 0;
      while (true) {
        const size_t newline = client.buffer.find('\n', begin);
        if (newline == std::string::npos) break;
        const std::string line = client.buffer.substr(begin, newline - begin);
        begin = newline + 1;
        if (!line.empty() && handler_) {
          handler_(line, peer_pid);
        }
      }
      if (begin > 0) client.buffer.erase(0, begin);
      continue;
    }
    if (n == 0) {
      ::close(client.fd);
      client.fd = -1;
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return;
    }
    ::close(client.fd);
    client.fd = -1;
    return;
  }
}

}  // namespace agent
}  // namespace vmon
