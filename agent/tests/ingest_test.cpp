#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ingest.h"
#include "tests/test_util.h"

namespace {

using vmon::agent::IngestServer;

int ConnectTo(const std::string& path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  const socklen_t len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + path.size() + 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool WriteAll(int fd, const std::string& payload) {
  size_t offset = 0;
  while (offset < payload.size()) {
    const ssize_t n =
        ::write(fd, payload.data() + offset, payload.size() - offset);
    if (n <= 0) return false;
    offset += static_cast<size_t>(n);
  }
  return true;
}

bool SendLines(const std::string& path, const std::string& payload) {
  const int fd = ConnectTo(path);
  if (fd < 0) return false;
  const bool ok = WriteAll(fd, payload);
  ::close(fd);
  return ok;
}

// 分两次写入同一条连接，验证服务端按行缓冲。
bool SendSplitLine(const std::string& path, const std::string& first,
                   const std::string& second) {
  const int fd = ConnectTo(path);
  if (fd < 0) return false;
  bool ok = WriteAll(fd, first);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  ok = ok && WriteAll(fd, second);
  ::close(fd);
  return ok;
}

TEST(Ingest, ReceivesJsonLinesAndPeerPid) {
  vmon::test::TempDir dir("vmon-ingest");
  const std::string path = dir.file("agent.sock");

  IngestServer server;
  std::mutex mutex;
  std::vector<std::string> lines;
  std::vector<uint32_t> peers;

  server.start(path, [&](const std::string& line, uint32_t peer_pid) {
    std::lock_guard<std::mutex> lock(mutex);
    lines.push_back(line);
    peers.push_back(peer_pid);
  });

  ASSERT_TRUE(vmon::test::wait_until([&path]() {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
  }));

  ASSERT_TRUE(SendLines(path, "{\"type\":\"log\"}\n{\"type\":\"metric\"}\n"));
  ASSERT_TRUE(vmon::test::wait_until([&]() {
    std::lock_guard<std::mutex> lock(mutex);
    return lines.size() >= 2;
  }));

  {
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(2u, lines.size());
    EXPECT_EQ("{\"type\":\"log\"}", lines[0]);
    EXPECT_EQ("{\"type\":\"metric\"}", lines[1]);
#if defined(__linux__)
    EXPECT_EQ(static_cast<uint32_t>(::getpid()), peers[0]);
#else
    // macOS 的 LOCAL_PEERPID 在部分系统版本上不可用，允许回退为 0。
    EXPECT_TRUE(peers[0] == 0u ||
                peers[0] == static_cast<uint32_t>(::getpid()));
#endif
  }

  server.stop();
  struct stat st;
  EXPECT_NE(0, ::stat(path.c_str(), &st));
}

TEST(Ingest, ReassemblesSplitLines) {
  vmon::test::TempDir dir("vmon-ingest-split");
  const std::string path = dir.file("agent.sock");

  IngestServer server;
  std::mutex mutex;
  std::vector<std::string> lines;
  server.start(path, [&](const std::string& line, uint32_t) {
    std::lock_guard<std::mutex> lock(mutex);
    lines.push_back(line);
  });
  ASSERT_TRUE(vmon::test::wait_until([&path]() {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
  }));

  ASSERT_TRUE(SendSplitLine(path, "{\"type\":\"lo",
                            "g\",\"msg\":\"ok\"}\n"));
  ASSERT_TRUE(vmon::test::wait_until([&]() {
    std::lock_guard<std::mutex> lock(mutex);
    return lines.size() == 1;
  }));
  {
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ("{\"type\":\"log\",\"msg\":\"ok\"}", lines[0]);
  }
  server.stop();
}

}  // namespace
