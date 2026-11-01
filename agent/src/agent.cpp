#include "agent.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace vmon {
namespace agent {

bool load_config(const std::string& path, AgentConfig* config,
                 std::string* error) {
  if (config == nullptr) return false;
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "无法打开配置文件: " + path;
    return false;
  }

  try {
    nlohmann::json j;
    in >> j;
    if (j.contains("server_url")) config->server_url = j["server_url"];
    if (j.contains("listen_uds")) config->listen_uds = j["listen_uds"];
    if (j.contains("state_dir")) config->state_dir = j["state_dir"];
    if (j.contains("node")) config->node = j["node"];
    if (j.contains("token")) config->token = j["token"];
    if (j.contains("control_dir")) config->control_dir = j["control_dir"];
    if (j.contains("crash_dir")) config->crash_dir = j["crash_dir"];
    if (j.contains("process_metrics"))
      config->process_metrics = j["process_metrics"].get<bool>();
    if (j.contains("scan_interval_ms"))
      config->scan_interval_ms = j["scan_interval_ms"];
    if (j.contains("upload_interval_ms"))
      config->upload_interval_ms = j["upload_interval_ms"];
    if (j.contains("command_interval_ms"))
      config->command_interval_ms = j["command_interval_ms"];
    if (j.contains("crash_upload_interval_ms"))
      config->crash_upload_interval_ms = j["crash_upload_interval_ms"];
    if (j.contains("wal_segment_bytes"))
      config->wal_segment_bytes = j["wal_segment_bytes"];
    if (j.contains("wal_max_bytes"))
      config->wal_max_bytes = j["wal_max_bytes"];
    if (j.contains("max_lines_per_batch"))
      config->max_lines_per_batch = j["max_lines_per_batch"];

    if (j.contains("process_filters")) {
      config->process_filters =
          j["process_filters"].get<std::vector<std::string>>();
    }
    if (j.contains("log_files")) {
      for (const auto& item : j["log_files"]) {
        LogFileConfig lf;
        lf.path = item.value("path", std::string());
        lf.service = item.value("service", std::string());
        if (!lf.path.empty()) config->log_files.push_back(lf);
      }
    }
  } catch (const std::exception& ex) {
    if (error) *error = std::string("配置解析失败: ") + ex.what();
    return false;
  }
  return true;
}

std::string hostname() {
  char buf[256];
  buf[0] = '\0';
  if (::gethostname(buf, sizeof(buf)) != 0) {
    return "unknown";
  }
  buf[sizeof(buf) - 1] = '\0';
  return std::string(buf);
}

std::string agent_id() {
  return hostname() + "-" + std::to_string(static_cast<long>(::getpid()));
}

uint64_t now_wall_ns() {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t now_mono_ns() {
  timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

bool ensure_dir(const std::string& path) {
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
      if (current.size() > 1 || current != "/") current += "/";
      current += part;
      if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
      }
    }
    if (slash == std::string::npos) break;
    begin = slash + 1;
  }
  return true;
}

bool split_url(const std::string& url, std::string* host, int* port,
               std::string* prefix) {
  if (host == nullptr || port == nullptr) return false;
  std::string rest = url;
  if (rest.compare(0, 7, "http://") == 0) {
    rest = rest.substr(7);
  } else if (rest.compare(0, 4, "http") == 0) {
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    if (prefix) *prefix = rest.substr(slash);
    rest = rest.substr(0, slash);
  } else if (prefix) {
    prefix->clear();
  }
  const size_t colon = rest.find(':');
  if (colon == std::string::npos) {
    *host = rest;
    *port = 80;
  } else {
    *host = rest.substr(0, colon);
    *port = std::atoi(rest.c_str() + colon + 1);
  }
  return !host->empty() && *port > 0;
}

}  // namespace agent
}  // namespace vmon
