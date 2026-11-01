#include "log_tail.h"

#include <fstream>

#include <sys/stat.h>

#include "agent.h"

namespace vmon {
namespace agent {
namespace {

// 单行上限：防止某条超长行把内存吃满。
const size_t kMaxLineBytes = 64 * 1024;

}  // namespace

void LogTailer::add(const std::string& path, const std::string& service) {
  for (const File& file : files_) {
    if (file.path == path) return;
  }
  File file;
  file.path = path;
  file.service = service;
  files_.push_back(file);
}

std::vector<TailedLine> LogTailer::poll(size_t max_lines) {
  std::vector<TailedLine> out;
  if (max_lines == 0) max_lines = 1000;

  for (File& file : files_) {
    if (out.size() >= max_lines) break;

    struct stat st;
    if (::stat(file.path.c_str(), &st) != 0) {
      continue;
    }
    const uint64_t inode = static_cast<uint64_t>(st.st_ino);
    if (!file.known || inode != file.inode ||
        static_cast<uint64_t>(st.st_size) < file.offset) {
      // 首次打开、文件被轮转替换，或截断：从头开始。
      file.inode = inode;
      file.offset = 0;
      file.pending.clear();
      file.known = true;
    }

    std::ifstream in(file.path, std::ios::binary);
    if (!in) continue;
    in.seekg(static_cast<std::streamoff>(file.offset));
    std::string chunk;
    char buffer[8192];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
      chunk.append(buffer, static_cast<size_t>(in.gcount()));
      if (chunk.size() > 4 * 1024 * 1024) break;
    }
    file.offset += chunk.size();

    file.pending += chunk;
    size_t begin = 0;
    while (true) {
      const size_t newline = file.pending.find('\n', begin);
      if (newline == std::string::npos) break;
      std::string line = file.pending.substr(begin, newline - begin);
      begin = newline + 1;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > kMaxLineBytes) line.resize(kMaxLineBytes);
      if (line.empty()) continue;
      TailedLine tailed;
      tailed.path = file.path;
      tailed.service = file.service;
      tailed.msg = std::move(line);
      tailed.time_ns = now_wall_ns();
      out.push_back(std::move(tailed));
      if (out.size() >= max_lines) break;
    }
    if (begin > 0) file.pending.erase(0, begin);
    if (file.pending.size() > kMaxLineBytes) {
      file.pending.clear();  // 防御：异常超长行直接丢弃
    }
  }
  return out;
}

}  // namespace agent
}  // namespace vmon
