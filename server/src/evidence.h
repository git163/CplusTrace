// 内部 AI 证据包：把一次故障裁剪、脱敏、校验后导出为自包含目录。
//
// 产物目录（evidence-<incident>-<ts>/）：
//   manifest.json / timeline.json / request.json / metrics.json
//   logs.jsonl / log_clusters.json / profiles/ / dumps/
//   index.jsonl / hypotheses.md
//
// 该能力属于内部版（VMON_INTERNAL_AI），对外发行版不编译。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

class Store;
class CrashStore;
class ProfileStore;

struct EvidenceOptions {
  std::string incident = "manual";
  std::string severity = "warning";
  std::string service;
  std::string trace_id;
  std::string request_id;
  uint64_t window_start_ns = 0;
  uint64_t window_end_ns = 0;
  bool redact = true;
  std::string salt;
  size_t max_logs = 5000;
  size_t max_traces = 50;
  size_t max_metrics = 5000;
};

struct EvidencePack {
  std::string id;
  std::string dir;
  nlohmann::json manifest;
  std::vector<std::string> files;
  std::string error;

  bool ok() const { return error.empty() && !dir.empty(); }
};

class EvidenceBuilder {
 public:
  static EvidencePack build(Store& store, CrashStore& crashes,
                            ProfileStore& profiles, const EvidenceOptions& options,
                            const std::string& base_dir);

  // 脱敏：IP、邮箱、口令/token、家目录用户名。
  static std::string redact(const std::string& text, const std::string& salt);
};

}  // namespace server
}  // namespace vmon
