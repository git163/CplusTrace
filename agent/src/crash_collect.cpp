#include "crash_collect.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace vmon {
namespace agent {
namespace {

std::string url_encode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

bool starts_with_name(const char* name, const char* prefix) {
  for (size_t i = 0; prefix[i] != '\0'; ++i) {
    if (name[i] != prefix[i]) return false;
  }
  return true;
}

bool ends_with_name(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

CrashCollector::CrashCollector(const AgentConfig& config, std::string node,
                               std::string agent)
    : config_(config), node_(std::move(node)), agent_(std::move(agent)) {
  split_url(config_.server_url, &host_, &port_, &prefix_);
}

CrashCollector::~CrashCollector() { stop(); }

void CrashCollector::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&CrashCollector::run, this);
}

void CrashCollector::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void CrashCollector::run() {
  const uint32_t interval =
      config_.crash_upload_interval_ms == 0 ? 2000
                                            : config_.crash_upload_interval_ms;
  while (running_.load()) {
    scan_once();
    const uint64_t deadline = now_mono_ns() +
                              static_cast<uint64_t>(interval) * 1000000ull;
    while (running_.load() && now_mono_ns() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
}

size_t CrashCollector::scan_once() {
  const std::string dir =
      config_.crash_dir.empty() ? std::string("/tmp/vmon-crash")
                                : config_.crash_dir;
  DIR* handle = ::opendir(dir.c_str());
  if (handle == nullptr) return 0;

  size_t uploaded = 0;
  const uint64_t now = now_wall_ns();
  while (dirent* entry = ::readdir(handle)) {
    if (entry->d_name[0] == '.') continue;
    if (!starts_with_name(entry->d_name, "crash-")) continue;
    const std::string name = entry->d_name;
    if (!ends_with_name(name, ".json")) continue;

    const std::string path = dir + "/" + name;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) continue;
    if (st.st_size <= 0) continue;  // 占位文件：进程还活着
    // 刚写完的文件可能还在信号处理器里，等一个上传周期再收。
    const uint64_t mtime_ns =
        static_cast<uint64_t>(st.st_mtime) * 1000000000ull;
    if (now > mtime_ns && now - mtime_ns < 1000000000ull) continue;

    std::ifstream in(path);
    if (!in) continue;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string body = ss.str();
    if (body.empty()) continue;
    try {
      const nlohmann::json parsed = nlohmann::json::parse(body);
      if (!parsed.is_object()) continue;
    } catch (const std::exception&) {
      continue;
    }

    if (upload(body)) {
      ::unlink(path.c_str());
      ++uploaded;
    }
  }
  ::closedir(handle);
  return uploaded;
}

bool CrashCollector::upload(const std::string& body) {
  httplib::Client client(host_, port_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(3, 0);
  client.set_write_timeout(3, 0);

  httplib::Headers headers;
  headers.emplace("X-Vmon-Node", node_);
  headers.emplace("X-Vmon-Agent", agent_);
  if (!config_.token.empty()) headers.emplace("X-Vmon-Token", config_.token);

  const std::string path =
      prefix_ + "/v1/crash?node=" + url_encode(node_);
  auto res = client.Post(path, headers, body, "application/json");
  return res && res->status >= 200 && res->status < 300;
}

}  // namespace agent
}  // namespace vmon
