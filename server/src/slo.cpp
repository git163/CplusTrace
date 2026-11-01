#include "slo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "store.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

bool valid_id(const std::string& id) {
  if (id.empty() || id.size() > 128) return false;
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string status_name(double burn_rate, uint64_t total) {
  if (total == 0) return "unknown";
  if (burn_rate > 2.0) return "critical";
  if (burn_rate > 1.0) return "warning";
  return "healthy";
}

}  // namespace

nlohmann::json SloDefinition::to_json() const {
  return {{"id", id},
          {"name", name},
          {"service", service},
          {"type", type},
          {"target", target},
          {"window_seconds", window_seconds},
          {"latency_threshold_ns", latency_threshold_ns},
          {"description", description},
          {"enabled", enabled}};
}

bool SloDefinition::from_json(const nlohmann::json& json, SloDefinition* out,
                              std::string* error) {
  if (out == nullptr) return false;
  if (!json.is_object()) {
    if (error) *error = "SLO 必须是 JSON 对象";
    return false;
  }
  SloDefinition slo;
  slo.id = json.value("id", std::string());
  slo.name = json.value("name", slo.id);
  slo.service = json.value("service", std::string());
  slo.type = json.value("type", std::string("availability"));
  slo.target = json.value("target", 0.99);
  slo.window_seconds =
      json.value("window_seconds", static_cast<uint64_t>(3600));
  slo.latency_threshold_ns =
      json.value("latency_threshold_ns", static_cast<uint64_t>(0));
  slo.description = json.value("description", std::string());
  slo.enabled = json.value("enabled", true);

  if (!valid_id(slo.id)) {
    if (error) *error = "SLO id 为空或包含非法字符（仅允许字母数字 . _ -）";
    return false;
  }
  if (slo.name.empty()) slo.name = slo.id;
  if (slo.type != "availability" && slo.type != "latency") {
    if (error) *error = "SLO type 只能是 availability 或 latency";
    return false;
  }
  if (!(slo.target > 0.0 && slo.target < 1.0)) {
    if (error) *error = "SLO target 必须在 (0, 1) 之间，例如 0.99";
    return false;
  }
  if (slo.window_seconds < 60) {
    if (error) *error = "SLO window_seconds 不能小于 60";
    return false;
  }
  if (slo.type == "latency" && slo.latency_threshold_ns == 0) {
    if (error) *error = "延迟型 SLO 需要 latency_threshold_ns";
    return false;
  }
  *out = slo;
  return true;
}

bool SloManager::upsert_json(const nlohmann::json& json, std::string* error) {
  SloDefinition slo;
  if (!SloDefinition::from_json(json, &slo, error)) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  for (SloDefinition& existing : definitions_) {
    if (existing.id == slo.id) {
      existing = slo;
      return true;
    }
  }
  if (definitions_.size() >= kMaxDefinitions) {
    if (error) *error = "SLO 数量达到上限（200）";
    return false;
  }
  definitions_.push_back(slo);
  return true;
}

bool SloManager::remove(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = definitions_.begin(); it != definitions_.end(); ++it) {
    if (it->id == id) {
      definitions_.erase(it);
      return true;
    }
  }
  return false;
}

std::vector<SloDefinition> SloManager::definitions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return definitions_;
}

size_t SloManager::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return definitions_.size();
}

nlohmann::json SloManager::statuses(const Store& store,
                                    uint64_t now_ns) const {
  std::vector<SloDefinition> current;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current = definitions_;
  }

  nlohmann::json out = nlohmann::json::array();
  for (const SloDefinition& slo : current) {
    const uint64_t window_ns = slo.window_seconds * 1000000000ull;
    const uint64_t since_ns = now_ns > window_ns ? now_ns - window_ns : 0;

    TraceQuery query;
    query.service = slo.service;
    query.since_ns = since_ns;
    query.limit = 100000;
    const std::vector<TraceSummary> traces = store.query_traces(query);

    uint64_t total = 0;
    uint64_t bad = 0;
    if (slo.enabled) {
      for (const TraceSummary& trace : traces) {
        ++total;
        if (slo.type == "availability") {
          if (trace.has_error) ++bad;
        } else if (trace.duration_ns > slo.latency_threshold_ns) {
          ++bad;
        }
      }
    }

    const double error_budget = 1.0 - slo.target;
    const double bad_ratio =
        total == 0 ? 0.0 : static_cast<double>(bad) / static_cast<double>(total);
    const double budget_consumed =
        error_budget <= 0.0 ? 0.0 : bad_ratio / error_budget;
    const double sli = total == 0 ? 0.0 : 1.0 - bad_ratio;

    nlohmann::json status = slo.to_json();
    status["status"] = status_name(budget_consumed, total);
    status["total"] = total;
    status["bad"] = bad;
    status["sli"] = sli;
    status["error_budget"] = error_budget;
    status["budget_consumed"] = budget_consumed;
    status["budget_remaining"] = std::max(0.0, 1.0 - budget_consumed);
    status["burn_rate"] = budget_consumed;
    status["window_start_ns"] = since_ns;
    status["window_end_ns"] = now_ns;
    out.push_back(status);
  }
  return out;
}

bool SloManager::load(const std::string& path, std::string* error) {
  std::ifstream in(path);
  if (!in) return true;  // 首次启动没有文件是正常状态

  try {
    nlohmann::json json;
    in >> json;
    if (!json.is_array()) {
      if (error) *error = "SLO 文件必须是 JSON 数组";
      return false;
    }
    std::vector<SloDefinition> loaded;
    for (const auto& item : json) {
      SloDefinition slo;
      if (!SloDefinition::from_json(item, &slo, error)) return false;
      loaded.push_back(slo);
      if (loaded.size() >= kMaxDefinitions) break;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    definitions_.swap(loaded);
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = std::string("SLO 文件解析失败: ") + ex.what();
    return false;
  }
}

bool SloManager::save(const std::string& path, std::string* error) const {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string::npos &&
      !ensure_directory(path.substr(0, slash))) {
    if (error) *error = "无法创建 SLO 状态目录";
    return false;
  }

  nlohmann::json json = nlohmann::json::array();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const SloDefinition& slo : definitions_) json.push_back(slo.to_json());
  }

  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      if (error) *error = "无法写入 SLO 临时文件: " + tmp;
      return false;
    }
    out << json.dump(2) << "\n";
    if (!out.flush()) {
      if (error) *error = "写入 SLO 临时文件失败: " + tmp;
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    if (error) *error = "替换 SLO 文件失败: " + path;
    return false;
  }
  return true;
}

}  // namespace server
}  // namespace vmon
