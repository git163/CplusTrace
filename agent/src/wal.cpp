#include "wal.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "agent.h"

namespace vmon {
namespace agent {
namespace {

bool is_segment(const std::string& name) {
  return name.compare(0, 4, "seg-") == 0 &&
         name.size() > 4 &&
         name.compare(name.size() - 6, 6, ".jsonl") == 0;
}

}  // namespace

Wal::~Wal() {
  close();
}

bool Wal::open(const std::string& dir, uint64_t segment_bytes,
               uint64_t max_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (dir.empty() || !ensure_dir(dir)) {
    return false;
  }
  dir_ = dir;
  segment_bytes_ = segment_bytes == 0 ? 4 * 1024 * 1024 : segment_bytes;
  max_bytes_ = max_bytes == 0 ? 64 * 1024 * 1024 : max_bytes;
  current_path_.clear();
  current_size_ = 0;
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  return rotate_locked();
}

void Wal::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void Wal::append(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0 && !rotate_locked()) {
    return;
  }

  std::string record = line;
  record.push_back('\n');
  size_t offset = 0;
  while (offset < record.size()) {
    const ssize_t n =
        ::write(fd_, record.data() + offset, record.size() - offset);
    if (n > 0) {
      offset += static_cast<size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      ::close(fd_);
      fd_ = -1;
      return;
    }
  }
  current_size_ += record.size();

  if (current_size_ >= segment_bytes_) {
    rotate_locked();
    enforce_limit_locked();
  }
}

void Wal::seal_if_older_than(uint64_t age_ns) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0 || current_size_ == 0) return;
  const uint64_t now = now_mono_ns();
  if (current_created_ns_ != 0 && now >= current_created_ns_ &&
      now - current_created_ns_ < age_ns) {
    return;
  }
  if (rotate_locked()) {
    enforce_limit_locked();
  }
}

bool Wal::rotate_locked() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (dir_.empty()) return false;

  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  const long long stamp = static_cast<long long>(ts.tv_sec) * 1000 +
                          ts.tv_nsec / 1000000;

  // 用 O_EXCL 保证段文件不复用：进程重启恰好在同一毫秒时，seq 递增直到
  // 找到新文件名。旧文件自然成为“已封存”段，等待上传线程处理。
  for (int attempt = 0; attempt < 1000; ++attempt) {
    char name[64];
    std::snprintf(name, sizeof(name), "seg-%lld-%u.jsonl", stamp, seq_++);
    current_path_ = dir_ + "/" + name;
    fd_ = ::open(current_path_.c_str(),
                 O_CREAT | O_WRONLY | O_APPEND | O_EXCL, 0600);
    if (fd_ >= 0) {
      current_size_ = 0;
      current_created_ns_ = now_mono_ns();
      return true;
    }
    if (errno != EEXIST) break;
  }
  current_path_.clear();
  return false;
}

void Wal::enforce_limit_locked() {
  std::vector<std::string> files;
  DIR* dir = ::opendir(dir_.c_str());
  if (dir == nullptr) return;
  while (dirent* entry = ::readdir(dir)) {
    if (is_segment(entry->d_name)) files.push_back(entry->d_name);
  }
  ::closedir(dir);
  std::sort(files.begin(), files.end());

  uint64_t total = 0;
  for (const auto& name : files) {
    struct stat st;
    if (::stat((dir_ + "/" + name).c_str(), &st) == 0) {
      total += static_cast<uint64_t>(st.st_size);
    }
  }

  for (const auto& name : files) {
    if (total <= max_bytes_) break;
    const std::string path = dir_ + "/" + name;
    if (path == current_path_) continue;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) continue;
    if (::unlink(path.c_str()) == 0) {
      total -= static_cast<uint64_t>(st.st_size);
    }
  }
}

std::vector<std::string> Wal::sealed_segments() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  DIR* dir = ::opendir(dir_.c_str());
  if (dir == nullptr) return out;
  while (dirent* entry = ::readdir(dir)) {
    const std::string name = entry->d_name;
    if (!is_segment(name)) continue;
    const std::string path = dir_ + "/" + name;
    if (path == current_path_) continue;
    out.push_back(path);
  }
  ::closedir(dir);
  std::sort(out.begin(), out.end());
  return out;
}

void Wal::remove_segment(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path == current_path_) return;
  ::unlink(path.c_str());
}

uint64_t Wal::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  uint64_t total = current_size_;
  DIR* dir = ::opendir(dir_.c_str());
  if (dir == nullptr) return total;
  while (dirent* entry = ::readdir(dir)) {
    if (!is_segment(entry->d_name)) continue;
    const std::string path = dir_ + "/" + entry->d_name;
    if (path == current_path_) continue;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
      total += static_cast<uint64_t>(st.st_size);
    }
  }
  ::closedir(dir);
  return total;
}

std::string Wal::current_segment() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_path_;
}

}  // namespace agent
}  // namespace vmon
