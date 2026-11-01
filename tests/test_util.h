// 测试通用工具：临时目录、文件写入、异步等待。
#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace vmon {
namespace test {

// 递归删除文件/目录；不跟随符号链接。
inline bool remove_tree(const std::string& path) {
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0) return false;
  if (S_ISDIR(st.st_mode)) {
    DIR* dir = ::opendir(path.c_str());
    if (dir != nullptr) {
      while (dirent* entry = ::readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
          continue;
        }
        remove_tree(path + "/" + entry->d_name);
      }
      ::closedir(dir);
    }
    return ::rmdir(path.c_str()) == 0;
  }
  return ::unlink(path.c_str()) == 0;
}

// 在 /tmp 下创建唯一目录，析构时清理。
class TempDir {
 public:
  explicit TempDir(const char* prefix = "vmon-test") {
    char tmpl[256];
    std::snprintf(tmpl, sizeof(tmpl), "/tmp/%s-XXXXXX", prefix);
    char* created = ::mkdtemp(tmpl);
    if (created != nullptr) path_ = created;
  }

  ~TempDir() {
    if (!path_.empty()) remove_tree(path_);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::string& path() const { return path_; }
  std::string file(const std::string& name) const { return path_ + "/" + name; }

 private:
  std::string path_;
};

inline bool write_file(const std::string& path, const std::string& content,
                       bool append = false) {
  std::ofstream out(path, std::ios::binary |
                              (append ? std::ios::app : std::ios::trunc));
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(out);
}

inline std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string out;
  if (!in) return out;
  char buffer[8192];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    out.append(buffer, static_cast<size_t>(in.gcount()));
  }
  return out;
}

// 轮询等待条件成立，超时返回 false。
inline bool wait_until(const std::function<bool()>& predicate,
                       int timeout_ms = 3000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

}  // namespace test
}  // namespace vmon
