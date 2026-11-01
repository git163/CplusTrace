#include "process_scan.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <sys/utsname.h>
#include <unistd.h>

#if defined(__linux__)
#include <dirent.h>
#endif

namespace vmon {
namespace agent {
namespace {

bool has_substr(const std::string& haystack, const std::string& needle) {
  return !needle.empty() && haystack.find(needle) != std::string::npos;
}

#if defined(__linux__)

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::string();
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// /proc/<pid>/cmdline 以 NUL 分隔，替换成空格便于匹配与展示。
std::string read_cmdline(pid_t pid) {
  std::string raw = read_file("/proc/" + std::to_string(pid) + "/cmdline");
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\0') raw[i] = ' ';
  }
  while (!raw.empty() && raw.back() == ' ') raw.pop_back();
  return raw;
}

void fill_status(pid_t pid, ProcessInfo* info) {
  std::ifstream in("/proc/" + std::to_string(pid) + "/status");
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 6, "VmRSS:") == 0) {
      std::istringstream ss(line.substr(6));
      ss >> info->rss_kb;
    } else if (line.compare(0, 8, "Threads:") == 0) {
      std::istringstream ss(line.substr(8));
      ss >> info->threads;
    }
  }
}

void fill_stat(pid_t pid, ProcessInfo* info) {
  std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  if (!std::getline(in, line)) return;
  // comm 可能包含空格与括号，从最后一个 ')' 之后开始解析。
  const size_t right = line.rfind(')');
  if (right == std::string::npos) return;
  std::istringstream ss(line.substr(right + 1));
  char state = '?';
  ss >> state;
  info->state.assign(1, state);
  for (int field = 4; field <= 22 && ss; ++field) {
    uint64_t value = 0;
    ss >> value;
    if (field == 4) info->ppid = static_cast<int32_t>(value);
    if (field == 22) info->start_time_ticks = value;
  }
}

std::vector<ProcessInfo> scan_linux() {
  std::vector<ProcessInfo> out;
  DIR* dir = ::opendir("/proc");
  if (dir == nullptr) return out;
  while (dirent* entry = ::readdir(dir)) {
    const char* name = entry->d_name;
    if (name[0] < '0' || name[0] > '9') continue;
    ProcessInfo info;
    info.pid = static_cast<int32_t>(std::atoi(name));
    if (info.pid <= 0) continue;
    fill_stat(info.pid, &info);
    fill_status(info.pid, &info);
    info.cmdline = read_cmdline(info.pid);
    const size_t space = info.cmdline.find(' ');
    const std::string argv0 =
        space == std::string::npos ? info.cmdline : info.cmdline.substr(0, space);
    const size_t slash = argv0.rfind('/');
    info.name = slash == std::string::npos ? argv0 : argv0.substr(slash + 1);
    out.push_back(info);
  }
  ::closedir(dir);
  return out;
}

HostInfo host_linux(const std::string& node) {
  HostInfo info;
  info.node = node;
  utsname un;
  if (::uname(&un) == 0) {
    info.os = std::string(un.sysname) + " " + un.release;
    info.kernel = un.release;
    info.arch = un.machine;
  }
  double loads[3] = {0, 0, 0};
  if (::getloadavg(loads, 3) == 3) {
    info.load1 = loads[0];
    info.load5 = loads[1];
    info.load15 = loads[2];
  }
  std::ifstream mem("/proc/meminfo");
  std::string line;
  while (std::getline(mem, line)) {
    unsigned long long kb = 0;
    if (std::sscanf(line.c_str(), "MemTotal: %llu kB", &kb) == 1) {
      info.mem_total_kb = kb;
    } else if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &kb) == 1) {
      info.mem_available_kb = kb;
    }
  }
  std::ifstream up("/proc/uptime");
  double seconds = 0;
  if (up >> seconds) info.uptime_seconds = static_cast<uint64_t>(seconds);
  return info;
}

#else  // !__linux__

std::vector<ProcessInfo> scan_ps() {
  std::vector<ProcessInfo> out;
  FILE* pipe = ::popen(
      "ps -axo pid=,ppid=,rss=,state=,comm=,args= 2>/dev/null", "r");
  if (pipe == nullptr) return out;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::istringstream ss(buffer);
    ProcessInfo info;
    std::string comm;
    if (!(ss >> info.pid >> info.ppid >> info.rss_kb >> info.state >> comm)) {
      continue;
    }
    std::string rest;
    std::getline(ss, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    info.name = comm;
    info.cmdline = rest.empty() ? comm : rest;
    // macOS 没有直接给出线程数，这里保守置 0，后续可接 task_info。
    info.threads = 0;
    out.push_back(info);
  }
  ::pclose(pipe);
  return out;
}

HostInfo host_darwin(const std::string& node) {
  HostInfo info;
  info.node = node;
  utsname un;
  if (::uname(&un) == 0) {
    info.os = std::string(un.sysname) + " " + un.release;
    info.kernel = un.release;
    info.arch = un.machine;
  }
  double loads[3] = {0, 0, 0};
  if (::getloadavg(loads, 3) == 3) {
    info.load1 = loads[0];
    info.load5 = loads[1];
    info.load15 = loads[2];
  }
  FILE* mem = ::popen("sysctl -n hw.memsize 2>/dev/null", "r");
  if (mem != nullptr) {
    unsigned long long bytes = 0;
    if (std::fscanf(mem, "%llu", &bytes) == 1) {
      info.mem_total_kb = bytes / 1024;
    }
    ::pclose(mem);
  }
  return info;
}

#endif

}  // namespace

std::vector<ProcessInfo> scan_processes() {
#if defined(__linux__)
  return scan_linux();
#else
  return scan_ps();
#endif
}

bool process_matches(const ProcessInfo& info,
                     const std::vector<std::string>& filters) {
  if (filters.empty()) return true;
  for (const std::string& filter : filters) {
    // 允许一个过滤项里写多个逗号分隔的子串，任一命中即可。
    size_t begin = 0;
    while (begin <= filter.size()) {
      const size_t comma = filter.find(',', begin);
      const std::string token =
          filter.substr(begin, comma == std::string::npos ? std::string::npos
                                                          : comma - begin);
      if (has_substr(info.cmdline, token) || has_substr(info.name, token)) {
        return true;
      }
      if (comma == std::string::npos) break;
      begin = comma + 1;
    }
  }
  return false;
}

HostInfo collect_host_info(const std::string& node) {
#if defined(__linux__)
  return host_linux(node);
#else
  return host_darwin(node);
#endif
}

}  // namespace agent
}  // namespace vmon
