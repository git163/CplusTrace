// 进程发现与基础采集。
//
// Linux 走 /proc，macOS 走 `ps`。两边输出统一为 ProcessInfo，
// 上层只关心 pid/ppid/cmdline/rss/线程数等字段。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vmon {
namespace agent {

struct ProcessInfo {
  int32_t pid = 0;
  int32_t ppid = 0;
  std::string name;
  std::string cmdline;
  std::string state;
  uint64_t rss_kb = 0;
  uint32_t threads = 0;
  uint64_t start_time_ticks = 0;  // Linux 下为开机后时钟滴答，用于识别重启
};

struct HostInfo {
  std::string node;
  std::string os;
  std::string kernel;
  std::string arch;
  double load1 = 0.0;
  double load5 = 0.0;
  double load15 = 0.0;
  uint64_t mem_total_kb = 0;
  uint64_t mem_available_kb = 0;
  uint64_t uptime_seconds = 0;
};

// 扫描全部进程；失败返回空列表。
std::vector<ProcessInfo> scan_processes();

// 按命令行子串过滤；filters 为空表示不过滤。支持逗号分隔的多个子串任意命中。
bool process_matches(const ProcessInfo& info,
                     const std::vector<std::string>& filters);

HostInfo collect_host_info(const std::string& node);

}  // namespace agent
}  // namespace vmon
