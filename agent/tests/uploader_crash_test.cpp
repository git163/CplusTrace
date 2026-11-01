#include <gtest/gtest.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <ctime>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <thread>

#include "crash_collect.h"
#include "tests/test_util.h"
#include "uploader.h"
#include "wal.h"

namespace {

class FakeServer {
 public:
  FakeServer() {
    server_.Post("/v1/ingest", [this](const httplib::Request& req,
                                      httplib::Response& res) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++ingest_requests_;
        ingest_body_ += req.body;
        token_ = req.get_header_value("X-Vmon-Token");
        node_ = req.get_header_value("X-Vmon-Node");
      }
      res.set_content("{\"accepted\":1,\"rejected\":0}", "application/json");
    });
    server_.Post("/v1/crash", [this](const httplib::Request& req,
                                     httplib::Response& res) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++crash_requests_;
        crash_body_ = req.body;
        token_ = req.get_header_value("X-Vmon-Token");
      }
      res.set_content("{\"id\":\"abc123\"}", "application/json");
    });
    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
  }

  ~FakeServer() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }

  int port() const { return port_; }
  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

  size_t ingest_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    return ingest_requests_;
  }
  size_t crash_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    return crash_requests_;
  }
  std::string ingest_body() {
    std::lock_guard<std::mutex> lock(mutex_);
    return ingest_body_;
  }
  std::string crash_body() {
    std::lock_guard<std::mutex> lock(mutex_);
    return crash_body_;
  }
  std::string token() {
    std::lock_guard<std::mutex> lock(mutex_);
    return token_;
  }
  std::string node() {
    std::lock_guard<std::mutex> lock(mutex_);
    return node_;
  }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::mutex mutex_;
  size_t ingest_requests_ = 0;
  size_t crash_requests_ = 0;
  std::string ingest_body_;
  std::string crash_body_;
  std::string token_;
  std::string node_;
};

std::string LogLine(int index) {
  return "{\"type\":\"log\",\"msg\":\"line-" + std::to_string(index) + "\"}";
}

void AgeFile(const std::string& path, time_t seconds_ago) {
  struct timeval times[2];
  const time_t stamp = ::time(nullptr) - seconds_ago;
  times[0].tv_sec = stamp;
  times[0].tv_usec = 0;
  times[1].tv_sec = stamp;
  times[1].tv_usec = 0;
  ::utimes(path.c_str(), times);
}

TEST(Uploader, UploadsSealedSegmentsAndDeletesThem) {
  vmon::test::TempDir dir("vmon-uploader");
  vmon::agent::Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 120, 1024 * 1024));
  for (int i = 0; i < 5; ++i) wal.append(LogLine(i));
  ASSERT_EQ(1u, wal.sealed_segments().size());

  FakeServer server;
  vmon::agent::Uploader uploader(&wal, server.url(), "node-a", "agent-a",
                                 1000, 2, 1024 * 1024);
  uploader.set_token("tok-1");
  EXPECT_TRUE(uploader.upload_once());

  EXPECT_TRUE(wal.sealed_segments().empty());
  // 第 4 条写入后达到 120 字节并触发轮转，封存段包含 4 条；第 5 条留在当前段。
  EXPECT_EQ(4u, uploader.uploaded_lines());
  EXPECT_EQ(2u, uploader.uploaded_batches());
  EXPECT_EQ(0u, uploader.failed_batches());
  EXPECT_EQ(2u, server.ingest_requests());
  EXPECT_EQ("tok-1", server.token());
  EXPECT_EQ("node-a", server.node());
  EXPECT_NE(std::string::npos, server.ingest_body().find("line-3"));
}

TEST(Uploader, KeepsSegmentWhenServerUnavailable) {
  vmon::test::TempDir dir("vmon-uploader-fail");
  vmon::agent::Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 60, 1024 * 1024));
  wal.append(LogLine(1));
  wal.append(LogLine(2));
  ASSERT_EQ(1u, wal.sealed_segments().size());

  vmon::agent::Uploader uploader(&wal, "http://127.0.0.1:1", "node-a",
                                 "agent-a", 1000, 10, 1024 * 1024);
  EXPECT_FALSE(uploader.upload_once());
  EXPECT_EQ(1u, wal.sealed_segments().size());
  EXPECT_EQ(1u, uploader.failed_batches());
}

TEST(Uploader, EmptyWalIsNoOp) {
  vmon::test::TempDir dir("vmon-uploader-empty");
  vmon::agent::Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 120, 1024 * 1024));
  vmon::agent::Uploader uploader(&wal, "http://127.0.0.1:1", "node-a",
                                 "agent-a", 1000, 10, 1024 * 1024);
  EXPECT_TRUE(uploader.upload_once());
}

TEST(CrashCollector, UploadsValidFilesOnlyAndDeletesThem) {
  vmon::test::TempDir crash_dir("vmon-crash");
  FakeServer server;

  vmon::agent::AgentConfig config;
  config.server_url = server.url();
  config.crash_dir = crash_dir.path();
  config.token = "crash-token";

  const std::string good = crash_dir.file("crash-100.json");
  const std::string empty = crash_dir.file("crash-101.json");
  const std::string bad = crash_dir.file("crash-102.json");
  const std::string other = crash_dir.file("other.json");

  ASSERT_TRUE(vmon::test::write_file(
      good, "{\"time_ns\":123,\"node\":\"node-a\",\"service\":\"order\","
            "\"signal\":\"SIGSEGV\",\"frames\":[\"0x1000 main\"]}"));
  ASSERT_TRUE(vmon::test::write_file(empty, ""));
  ASSERT_TRUE(vmon::test::write_file(bad, "{not json"));
  ASSERT_TRUE(vmon::test::write_file(other, "{}"));
  AgeFile(good, 5);
  AgeFile(bad, 5);
  AgeFile(other, 5);

  vmon::agent::CrashCollector collector(config, "node-a", "agent-a");
  EXPECT_EQ(1u, collector.scan_once());
  EXPECT_EQ(1u, server.crash_requests());
  EXPECT_EQ("crash-token", server.token());
  EXPECT_NE(std::string::npos, server.crash_body().find("SIGSEGV"));

  struct stat st;
  EXPECT_NE(0, ::stat(good.c_str(), &st));
  EXPECT_EQ(0, ::stat(empty.c_str(), &st));
  EXPECT_EQ(0, ::stat(bad.c_str(), &st));
  EXPECT_EQ(0, ::stat(other.c_str(), &st));

  EXPECT_EQ(0u, collector.scan_once());
}

TEST(CrashCollector, MissingDirectoryIsIgnored) {
  vmon::agent::AgentConfig config;
  config.server_url = "http://127.0.0.1:1";
  config.crash_dir = "/tmp/vmon-no-such-crash-dir";
  vmon::agent::CrashCollector collector(config, "node-a", "agent-a");
  EXPECT_EQ(0u, collector.scan_once());
}

}  // namespace
