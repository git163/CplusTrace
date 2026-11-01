// Agent 的 UDS 接入：接收 SDK 推送的 JSONL 事件。
//
// 单线程 poll 处理所有客户端；每行交给 handler，handler 内部做 WAL 落盘。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace vmon {
namespace agent {

class IngestServer {
 public:
  typedef std::function<void(const std::string& line, uint32_t peer_pid)>
      LineHandler;

  void start(const std::string& uds_path, LineHandler handler);
  void stop();

 private:
  struct Client {
    int fd = -1;
    std::string buffer;
  };

  void run();
  void handle_client(Client& client, uint32_t peer_pid);

  std::thread thread_;
  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::string path_;
  LineHandler handler_;
};

}  // namespace agent
}  // namespace vmon
