// Agent 本地 WAL：分段 JSONL 文件，有硬上限，满了优先删最旧段。
//
// 上传线程只处理“已封存”的段（当前写入段不处理），成功后删除；
// 失败保留，保证 Agent 重启或断网后可以续传。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vmon {
namespace agent {

class Wal {
 public:
  Wal() = default;
  ~Wal();
  Wal(const Wal&) = delete;
  Wal& operator=(const Wal&) = delete;

  bool open(const std::string& dir, uint64_t segment_bytes, uint64_t max_bytes);
  void close();

  void append(const std::string& line);

  // 当前段写入超过 age_ns 且非空时封存，交给上传线程。
  // 没有这个兜底，低流量 Agent 的当前段可能永远达不到 segment_bytes，
  // 数据会一直滞留本地，直到进程重启。
  void seal_if_older_than(uint64_t age_ns);

  std::vector<std::string> sealed_segments() const;
  void remove_segment(const std::string& path);

  uint64_t total_bytes() const;
  std::string current_segment() const;

 private:
  bool rotate_locked();
  void enforce_limit_locked();

  mutable std::mutex mutex_;
  std::string dir_;
  uint64_t segment_bytes_ = 4 * 1024 * 1024;
  uint64_t max_bytes_ = 64 * 1024 * 1024;
  int fd_ = -1;
  std::string current_path_;
  uint64_t current_size_ = 0;
  uint64_t current_created_ns_ = 0;
  uint32_t seq_ = 0;
};

}  // namespace agent
}  // namespace vmon
