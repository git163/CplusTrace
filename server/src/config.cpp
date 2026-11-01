#include "config.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

bool load_server_config(const std::string& path, ServerConfig* config,
                        std::string* error) {
  if (config == nullptr) return false;
  if (path.empty()) return true;
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "无法打开配置文件: " + path;
    return false;
  }
  try {
    nlohmann::json j;
    in >> j;
    if (j.contains("listen_host")) config->listen_host = j["listen_host"];
    if (j.contains("listen_port")) config->listen_port = j["listen_port"];
    if (j.contains("web_dir")) config->web_dir = j["web_dir"];
    if (j.contains("state_dir")) config->state_dir = j["state_dir"];
    if (j.contains("token")) config->token = j["token"];
    if (j.contains("max_traces")) config->max_traces = j["max_traces"];
    if (j.contains("max_log_records"))
      config->max_log_records = j["max_log_records"];
    if (j.contains("retention_seconds"))
      config->retention_seconds = j["retention_seconds"];
    if (j.contains("info_sample_permyriad"))
      config->info_sample_permyriad = j["info_sample_permyriad"];
    if (j.contains("dedup_window_ms")) config->dedup_window_ms = j["dedup_window_ms"];
    if (j.contains("dedup_max_per_window"))
      config->dedup_max_per_window = j["dedup_max_per_window"];
    if (j.contains("noise_patterns")) {
      config->noise_patterns =
          j["noise_patterns"].get<std::vector<std::string>>();
    }
    if (j.contains("vm_url")) config->vm_url = j["vm_url"];
    if (j.contains("vl_url")) config->vl_url = j["vl_url"];
    if (j.contains("forward_enabled")) config->forward_enabled = j["forward_enabled"];
    if (j.contains("enable_evidence")) config->enable_evidence = j["enable_evidence"];
    if (j.contains("evidence_dir")) config->evidence_dir = j["evidence_dir"];
    if (j.contains("evidence_salt")) config->evidence_salt = j["evidence_salt"];
    if (j.contains("enable_mcp")) config->enable_mcp = j["enable_mcp"];
    if (j.contains("rule_interval_ms"))
      config->rule_interval_ms = j["rule_interval_ms"];
    if (j.contains("inventory_interval_ms"))
      config->inventory_interval_ms = j["inventory_interval_ms"];

    if (j.contains("pipeline") && j["pipeline"].is_object()) {
      const nlohmann::json& p = j["pipeline"];
      if (p.contains("enabled")) config->pipeline.enabled = p["enabled"];
      if (p.contains("use_defaults"))
        config->pipeline.use_defaults = p["use_defaults"];
      if (p.contains("rules") && p["rules"].is_array()) {
        for (const auto& item : p["rules"]) {
          if (!item.is_object()) continue;
          PipelineRule rule;
          rule.name = item.value("name", std::string("custom"));
          rule.pattern = item.value("pattern", std::string());
          rule.replace = item.value("replace", std::string("<redacted>"));
          config->pipeline.rules.push_back(rule);
        }
      }
    }

    if (j.contains("webhook") && j["webhook"].is_object()) {
      const nlohmann::json& w = j["webhook"];
      if (w.contains("enabled")) config->webhook_enabled = w["enabled"];
      if (w.contains("url")) config->webhook_url = w["url"];
      if (w.contains("timeout_ms"))
        config->webhook_timeout_ms = w["timeout_ms"];
      if (w.contains("token")) config->webhook_token = w["token"];
    }
  } catch (const std::exception& ex) {
    if (error) *error = std::string("配置解析失败: ") + ex.what();
    return false;
  }
  return true;
}

}  // namespace server
}  // namespace vmon
