// Agent 配置与通用工具。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace agent {

struct LogFileConfig {
  std::string path;
  std::string service;
};

struct AgentConfig {
  std::string server_url = "http://127.0.0.1:18428";
  std::string listen_uds = "/tmp/vmon-agent.sock";
  std::string state_dir = "/tmp/vmon-agent";
  std::string node;                      // 默认取 hostname
  std::string token;                     // 访问中心服务的令牌
  std::string control_dir = "/tmp/vmon-ctrl";  // SDK 控制套接字目录
  std::string crash_dir = "/tmp/vmon-crash";   // SDK 崩溃报告目录
  std::vector<std::string> process_filters;  // 命令行子串匹配
  std::vector<LogFileConfig> log_files;
  // 默认只采集主机指标；进程级指标按服务实例放大，可能造成高基数，
  // 需要用户在 Agent 启动参数中显式开启。
  bool process_metrics = false;
  uint32_t scan_interval_ms = 2000;
  uint32_t upload_interval_ms = 1000;
  uint32_t command_interval_ms = 1000;
  uint32_t crash_upload_interval_ms = 2000;
  uint64_t wal_segment_bytes = 4 * 1024 * 1024;
  uint64_t wal_max_bytes = 64 * 1024 * 1024;
  uint32_t max_lines_per_batch = 500;
};

bool load_config(const std::string& path, AgentConfig* config, std::string* error);

std::string hostname();
std::string agent_id();
uint64_t now_wall_ns();
uint64_t now_mono_ns();
bool ensure_dir(const std::string& path);
bool split_url(const std::string& url, std::string* host, int* port,
               std::string* prefix);

}  // namespace agent
}  // namespace vmon
