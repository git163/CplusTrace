// Agent 上传器：把 WAL 中已封存的段批量推送到中心服务。
//
// 语义：
//   - 只处理 sealed 段（当前写入段不动），成功后删除该段；
//   - 任意一批失败就保留整段，下轮重试，保证断网不丢数据；
//   - 单段按行数和字节数切批，避免一次请求过大。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace vmon {
namespace agent {

class Wal;

class Uploader {
 public:
  Uploader(Wal* wal, std::string server_url, std::string node, std::string agent,
           uint32_t interval_ms, uint32_t max_lines, uint32_t max_bytes);
  ~Uploader();

  Uploader(const Uploader&) = delete;
  Uploader& operator=(const Uploader&) = delete;

  void start();
  void stop();

  // 中心服务开启鉴权时设置；为空表示不发送令牌。
  void set_token(const std::string& token) { token_ = token; }

  uint64_t uploaded_lines() const { return uploaded_lines_.load(); }
  uint64_t uploaded_batches() const { return uploaded_batches_.load(); }
  uint64_t failed_batches() const { return failed_batches_.load(); }

  // 同步上传一轮，返回是否把所有可上传段都处理完（测试与关停前使用）。
  bool upload_once();

 private:
  void run();
  bool upload_segment(const std::string& path);
  bool post_batch(const std::string& body);

  Wal* wal_ = nullptr;
  std::string server_url_;
  std::string host_;
  int port_ = 0;
  std::string prefix_;
  std::string node_;
  std::string agent_;
  std::string token_;
  uint32_t interval_ms_ = 1000;
  uint32_t max_lines_ = 500;
  uint32_t max_bytes_ = 1024 * 1024;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> uploaded_lines_{0};
  std::atomic<uint64_t> uploaded_batches_{0};
  std::atomic<uint64_t> failed_batches_{0};
};

}  // namespace agent
}  // namespace vmon
