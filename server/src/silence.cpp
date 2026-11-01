#include "silence.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "util.h"

namespace vmon {
namespace server {
namespace {

bool match_field(const std::string& expected, const std::string& actual) {
  return expected.empty() || expected == actual;
}

}  // namespace

nlohmann::json silence_to_json(const Silence& s) {
  nlohmann::json j;
  j["id"] = s.id;
  j["name"] = s.name;
  if (!s.rule_id.empty()) j["rule_id"] = s.rule_id;
  if (!s.type.empty()) j["type"] = s.type;
  if (!s.service.empty()) j["service"] = s.service;
  if (!s.node.empty()) j["node"] = s.node;
  if (!s.severity.empty()) j["severity"] = s.severity;
  j["start_ns"] = s.start_ns;
  j["end_ns"] = s.end_ns;
  j["start"] = iso_time(s.start_ns);
  j["end"] = iso_time(s.end_ns);
  j["reason"] = s.reason;
  j["active"] = s.start_ns <= now_ns() && now_ns() < s.end_ns;
  return j;
}

bool SilenceStore::load(const std::string& path, std::string* error) {
  std::ifstream in(path);
  if (!in) return true;
  try {
    nlohmann::json json;
    in >> json;
    std::lock_guard<std::mutex> lock(mutex_);
    silences_.clear();
    if (json.is_array()) {
      for (const auto& item : json) {
        Silence s;
        s.id = item.value("id", std::string());
        s.name = item.value("name", std::string());
        s.rule_id = item.value("rule_id", std::string());
        s.type = item.value("type", std::string());
        s.service = item.value("service", std::string());
        s.node = item.value("node", std::string());
        s.severity = item.value("severity", std::string());
        s.start_ns = item.value("start_ns", static_cast<uint64_t>(0));
        s.end_ns = item.value("end_ns", static_cast<uint64_t>(0));
        s.reason = item.value("reason", std::string());
        if (!s.id.empty()) silences_.push_back(s);
      }
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = std::string("静默配置解析失败: ") + ex.what();
    return false;
  }
}

bool SilenceStore::save(const std::string& path, std::string* error) const {
  std::vector<Silence> copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    copy = silences_;
  }
  nlohmann::json json = nlohmann::json::array();
  for (const Silence& s : copy) json.push_back(silence_to_json(s));
  const std::string tmp = path + ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    if (error) *error = "无法写入静默配置: " + tmp;
    return false;
  }
  out << json.dump(2) << "\n";
  out.close();
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = "无法替换静默配置: " + path;
    return false;
  }
  return true;
}

std::vector<Silence> SilenceStore::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return silences_;
}

bool SilenceStore::upsert_json(const nlohmann::json& json, uint64_t now,
                               Silence* out, std::string* error) {
  if (!json.is_object()) {
    if (error) *error = "静默配置必须是 JSON 对象";
    return false;
  }
  Silence s;
  s.id = json.value("id", std::string());
  s.name = json.value("name", std::string());
  s.rule_id = json.value("rule_id", std::string());
  s.type = json.value("type", std::string());
  s.service = json.value("service", std::string());
  s.node = json.value("node", std::string());
  s.severity = json.value("severity", std::string());
  s.start_ns = json.value("start_ns", now);
  s.end_ns = json.value("end_ns", static_cast<uint64_t>(0));
  if (s.end_ns == 0) {
    const uint64_t minutes = json.value("duration_minutes", 60u);
    s.end_ns = now + minutes * 60ull * 1000000000ull;
  }
  s.reason = json.value("reason", std::string());
  if (s.end_ns <= s.start_ns) {
    if (error) *error = "静默结束时间必须晚于开始时间";
    return false;
  }
  if (s.id.empty()) {
    s.id = "silence-" + hex_hash(s.name + "|" + s.service + "|" +
                                 std::to_string(s.start_ns))
                             .substr(0, 10);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  bool replaced = false;
  for (Silence& existing : silences_) {
    if (existing.id == s.id) {
      existing = s;
      replaced = true;
      break;
    }
  }
  if (!replaced) silences_.push_back(s);
  if (out) *out = s;
  return true;
}

bool SilenceStore::remove(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = silences_.begin(); it != silences_.end(); ++it) {
    if (it->id == id) {
      silences_.erase(it);
      return true;
    }
  }
  return false;
}

bool SilenceStore::matches(const std::string& rule_id, const std::string& type,
                           const std::string& service, const std::string& node,
                           const std::string& severity, uint64_t now) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Silence& s : silences_) {
    if (now < s.start_ns || now >= s.end_ns) continue;
    if (!match_field(s.rule_id, rule_id)) continue;
    if (!match_field(s.type, type)) continue;
    if (!match_field(s.service, service)) continue;
    if (!match_field(s.node, node)) continue;
    if (!match_field(s.severity, severity)) continue;
    return true;
  }
  return false;
}

}  // namespace server
}  // namespace vmon
