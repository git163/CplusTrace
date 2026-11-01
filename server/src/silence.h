// 告警静默：在时间窗内抑制匹配的规则事件。
//
// 匹配维度都可为空（通配）：rule_id / type / service / node / severity。
// 静默只阻止“新事件”，不影响已产生事件的处理与查询。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

struct Silence {
  std::string id;
  std::string name;
  std::string rule_id;
  std::string type;
  std::string service;
  std::string node;
  std::string severity;
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  std::string reason;
};

class SilenceStore {
 public:
  bool load(const std::string& path, std::string* error);
  bool save(const std::string& path, std::string* error) const;

  std::vector<Silence> list() const;
  bool upsert_json(const nlohmann::json& json, uint64_t now_ns,
                   Silence* out, std::string* error);
  bool remove(const std::string& id);

  bool matches(const std::string& rule_id, const std::string& type,
               const std::string& service, const std::string& node,
               const std::string& severity, uint64_t now_ns) const;

 private:
  mutable std::mutex mutex_;
  std::vector<Silence> silences_;
};

nlohmann::json silence_to_json(const Silence& silence);

}  // namespace server
}  // namespace vmon
