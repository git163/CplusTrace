// 中心服务配置。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vmon {
namespace server {

struct ServerConfig {
  std::string listen_host = "0.0.0.0";
  int listen_port = 18428;
  std::string web_dir;              // Web UI 静态资源目录
  std::string state_dir = "./vmon-data";
  std::string token;                // 为空表示不鉴权（仅建议内网使用）

  size_t max_traces = 20000;
  size_t max_log_records = 200000;
  uint64_t retention_seconds = 6ull * 3600;
  uint32_t info_sample_permyriad = 10000;
  uint64_t dedup_window_ms = 30000;
  uint32_t dedup_max_per_window = 50;
  std::vector<std::string> noise_patterns;

  // 可选：把指标/日志转发到 VictoriaMetrics / VictoriaLogs。
  std::string vm_url;  // 例如 http://127.0.0.1:8428
  std::string vl_url;  // 例如 http://127.0.0.1:9428
  bool forward_enabled = true;

  // 证据包（内部版）。对外发行版可关闭。
  bool enable_evidence = true;
  std::string evidence_dir;   // 默认 state_dir/evidence
  std::string evidence_salt;  // 假名化盐

  bool enable_mcp = true;
  uint32_t rule_interval_ms = 5000;
  uint32_t inventory_interval_ms = 3000;
};

bool load_server_config(const std::string& path, ServerConfig* config,
                        std::string* error);

}  // namespace server
}  // namespace vmon
