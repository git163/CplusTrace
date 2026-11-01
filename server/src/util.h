// 服务端通用小工具：时间、哈希、字符串、编码。
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace vmon {
namespace server {

// 递归创建目录（mode 0700）；已存在视为成功。
inline bool ensure_directory(const std::string& path) {
  if (path.empty()) return false;
  std::string current;
  size_t begin = 0;
  if (path[0] == '/') {
    current = "/";
    begin = 1;
  }
  while (begin <= path.size()) {
    const size_t slash = path.find('/', begin);
    const std::string part =
        path.substr(begin, slash == std::string::npos ? std::string::npos
                                                      : slash - begin);
    if (!part.empty()) {
      if (!current.empty() && current.back() != '/') current += "/";
      current += part;
      if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    if (slash == std::string::npos) break;
    begin = slash + 1;
  }
  return true;
}

inline uint64_t now_ns() {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t fnv1a64(const std::string& data) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : data) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::string hex_hash(const std::string& data) {
  static const char* digits = "0123456789abcdef";
  uint64_t v = fnv1a64(data);
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = digits[v & 0xF];
    v >>= 4;
  }
  return out;
}

inline std::string iso_time(uint64_t ns, bool with_ms = true) {
  if (ns == 0) return std::string();
  const time_t seconds = static_cast<time_t>(ns / 1000000000ull);
  const uint64_t ms = (ns / 1000000ull) % 1000ull;
  std::tm tm_value;
  std::tm* tm_ptr = ::gmtime_r(&seconds, &tm_value);
  char buffer[64];
  if (tm_ptr == nullptr) return std::string();
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_ptr);
  if (!with_ms) return std::string(buffer);
  char out[80];
  std::snprintf(out, sizeof(out), "%s.%03lluZ", buffer,
                static_cast<unsigned long long>(ms));
  return std::string(out);
}

inline std::string url_decode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const std::string hex = value.substr(i + 1, 2);
      out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
      i += 2;
    } else if (value[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

inline std::vector<std::string> split(const std::string& text, char sep) {
  std::vector<std::string> out;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t pos = text.find(sep, begin);
    if (pos == std::string::npos) {
      out.push_back(text.substr(begin));
      break;
    }
    out.push_back(text.substr(begin, pos - begin));
    begin = pos + 1;
  }
  return out;
}

inline bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

inline std::string trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                         text[begin] == '\r' || text[begin] == '\n')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

inline int64_t to_i64(const std::string& text, int64_t fallback = 0) {
  if (text.empty()) return fallback;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (end == text.c_str()) return fallback;
  return static_cast<int64_t>(value);
}

inline double to_double(const std::string& text, double fallback = 0.0) {
  if (text.empty()) return fallback;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) return fallback;
  return value;
}

}  // namespace server
}  // namespace vmon
