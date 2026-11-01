// 文件日志采集（L0 零改造接入的基础能力）。
//
// 按 inode + 偏移跟踪文件，轮转/重建后自动重新打开；只读取完整行，
// 尾部半行留到下次。多行合并（以空白或堆栈行开头）在服务端模板化时处理，
// Agent 侧保持原始行，避免误合并。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vmon {
namespace agent {

struct TailedLine {
  std::string path;
  std::string service;
  std::string msg;
  uint64_t time_ns = 0;
};

class LogTailer {
 public:
  void add(const std::string& path, const std::string& service);

  // 读取所有新增行，最多 max_lines 行；剩余行下次继续。
  std::vector<TailedLine> poll(size_t max_lines);

  size_t file_count() const { return files_.size(); }

 private:
  struct File {
    std::string path;
    std::string service;
    uint64_t inode = 0;
    uint64_t offset = 0;
    std::string pending;
    bool known = false;
  };

  std::vector<File> files_;
};

}  // namespace agent
}  // namespace vmon
