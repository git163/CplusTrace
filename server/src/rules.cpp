#include "rules.h"

#include <algorithm>

#include "store.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

bool rule_matches(const Rule& rule, const std::string& service,
                  const std::string& node) {
  if (!rule.enabled) return false;
  if (!rule.service.empty() && rule.service != service) return false;
  if (!rule.node.empty() && rule.node != node) return false;
  return true;
}

bool compare(double value, const std::string& op, double threshold) {
  if (op == ">") return value > threshold;
  if (op == "<") return value < threshold;
  if (op == "<=") return value <= threshold;
  if (op == "==") return value == threshold;
  return value >= threshold;  // 默认 >=
}

}  // namespace

nlohmann::json Rule::to_json() const {
  nlohmann::json j;
  j["id"] = id;
  j["name"] = name;
  j["type"] = type;
  if (!service.empty()) j["service"] = service;
  if (!node.empty()) j["node"] = node;
  if (!metric.empty()) j["metric"] = metric;
  j["op"] = op;
  j["threshold"] = threshold;
  j["min_count"] = min_count;
  j["window_ns"] = window_ns;
  j["cooldown_ns"] = cooldown_ns;
  j["severity"] = severity;
  j["enabled"] = enabled;
  return j;
}

Rule Rule::from_json(const nlohmann::json& j) {
  Rule rule;
  rule.id = j.value("id", std::string());
  rule.name = j.value("name", std::string());
  rule.type = j.value("type", std::string());
  rule.service = j.value("service", std::string());
  rule.node = j.value("node", std::string());
  rule.metric = j.value("metric", std::string());
  rule.op = j.value("op", std::string(">="));
  rule.threshold = j.value("threshold", 0.0);
  rule.min_count = j.value("min_count", 1u);
  rule.window_ns =
      j.value("window_ns", static_cast<uint64_t>(60ull * 1000000000ull));
  rule.cooldown_ns =
      j.value("cooldown_ns", static_cast<uint64_t>(300ull * 1000000000ull));
  rule.severity = j.value("severity", std::string("warning"));
  rule.enabled = j.value("enabled", true);
  if (rule.id.empty() && !rule.name.empty()) {
    rule.id = "rule-" + hex_hash(rule.name).substr(0, 8);
  }
  return rule;
}

RuleEngine::RuleEngine() { rules_ = default_rules(); }

std::vector<Rule> RuleEngine::default_rules() {
  std::vector<Rule> rules;
  {
    Rule rule;
    rule.id = "default-heartbeat";
    rule.name = "进程心跳丢失";
    rule.type = "heartbeat";
    rule.threshold = 120.0;  // 秒
    rule.window_ns = 60ull * 1000000000ull;
    rule.cooldown_ns = 300ull * 1000000000ull;
    rule.severity = "critical";
    rules.push_back(rule);
  }
  {
    Rule rule;
    rule.id = "default-error-log";
    rule.name = "错误日志突增";
    rule.type = "error_log";
    rule.threshold = 20.0;
    rule.min_count = 20;
    rule.window_ns = 60ull * 1000000000ull;
    rule.cooldown_ns = 300ull * 1000000000ull;
    rule.severity = "warning";
    rules.push_back(rule);
  }
  {
    Rule rule;
    rule.id = "default-slow-trace";
    rule.name = "慢调用";
    rule.type = "slow_trace";
    rule.threshold = 1000.0;  // 毫秒
    rule.window_ns = 60ull * 1000000000ull;
    rule.cooldown_ns = 120ull * 1000000000ull;
    rule.severity = "warning";
    rules.push_back(rule);
  }
  return rules;
}

void RuleEngine::add_rule(const Rule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Rule& existing : rules_) {
    if (existing.id == rule.id) {
      existing = rule;
      return;
    }
  }
  rules_.push_back(rule);
}

bool RuleEngine::remove_rule(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = rules_.begin(); it != rules_.end(); ++it) {
    if (it->id == id) {
      rules_.erase(it);
      return true;
    }
  }
  return false;
}

std::vector<Rule> RuleEngine::rules() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rules_;
}

void RuleEngine::set_rules(const std::vector<Rule>& rules) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_ = rules;
  counters_.clear();
}

std::string RuleEngine::make_event_id(const std::string& rule_id,
                                      const std::string& key,
                                      uint64_t window_ns, uint64_t now_ns) {
  const uint64_t window = window_ns == 0 ? 1 : window_ns;
  const uint64_t bucket = now_ns / window;
  return hex_hash(rule_id + "|" + key + "|" + std::to_string(bucket));
}

void RuleEngine::fire(const Rule& rule, const std::string& key,
                      const std::string& title, const std::string& detail,
                      const std::string& service, const std::string& node,
                      const common::Event& source, Store* store,
                      uint64_t now_ns) {
  if (store == nullptr) return;

  std::string event_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto last = last_fire_ns_.find(rule.id + "|" + key);
    if (last != last_fire_ns_.end() &&
        now_ns < last->second + rule.cooldown_ns) {
      return;
    }
    last_fire_ns_[rule.id + "|" + key] = now_ns;
    event_id = make_event_id(rule.id, key, rule.window_ns, now_ns);
  }

  EventRecord event;
  event.id = event_id;
  event.time_ns = now_ns;
  event.type = "alert";
  event.severity = rule.severity;
  event.title = rule.name.empty() ? title : rule.name;
  event.detail = detail;
  event.service = service;
  event.node = node;
  event.fingerprint = key;
  event.status = "open";
  event.attrs["rule_id"] = rule.id;
  event.attrs["rule_type"] = rule.type;
  event.attrs["threshold"] = std::to_string(rule.threshold);
  if (!source.trace.empty()) event.attrs["trace_id"] = source.trace;
  if (!source.request_id.empty()) event.attrs["request_id"] = source.request_id;
  store->add_event(event);
}

void RuleEngine::on_event(const common::Event& event, Store* store) {
  if (store == nullptr) return;
  const uint64_t now = event.time_ns == 0 ? now_ns() : event.time_ns;

  std::vector<Rule> rules;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    rules = rules_;
  }

  for (const Rule& rule : rules) {
    if (!rule_matches(rule, event.service, event.node)) continue;

    if (rule.type == "error_log" && event.type == "log" &&
        (event.level == "error" || event.level == "fatal")) {
      const std::string key = event.service + "|" + event.error_code;
      uint64_t count = 0;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        WindowCounter& counter = counters_[rule.id + "|" + key];
        if (counter.window_start_ns == 0 ||
            now > counter.window_start_ns + rule.window_ns) {
          counter.window_start_ns = now;
          counter.count = 0;
        }
        ++counter.count;
        count = counter.count;
      }
      if (count >= rule.min_count) {
        fire(rule, key, rule.name,
             "窗口内错误日志 " + std::to_string(count) + " 条，最后一条: " +
                 event.msg,
             event.service, event.node, event, store, now);
      }
    } else if (rule.type == "slow_trace" && event.type == "span_end") {
      TraceData trace;
      double duration_ms = 0.0;
      if (store->get_trace(event.trace, &trace)) {
        auto span_it = trace.spans.find(event.span);
        if (span_it != trace.spans.end()) {
          duration_ms = static_cast<double>(span_it->second.duration_ns()) /
                        1000000.0;
        }
      }
      if (compare(duration_ms, rule.op, rule.threshold) &&
          rule.threshold > 0) {
        fire(rule, event.trace + "|" + event.name, rule.name,
             "调用 " + event.name + " 耗时 " + std::to_string(duration_ms) +
                 "ms，超过阈值 " + std::to_string(rule.threshold) + "ms",
             event.service, event.node, event, store, now);
      }
    } else if (rule.type == "metric" && event.type == "metric" &&
               (rule.metric.empty() || rule.metric == event.name)) {
      auto it = event.attrs.find("value");
      const double value = it == event.attrs.end() ? 0.0 : to_double(it->second);
      if (compare(value, rule.op, rule.threshold)) {
        fire(rule, event.name + "|" + event.service, rule.name,
             "指标 " + event.name + "=" + std::to_string(value) +
                 " 越过阈值 " + std::to_string(rule.threshold),
             event.service, event.node, event, store, now);
      }
    }
  }

  if (event.type == "span_end") {
    const bool error = event.level == "error" || event.level == "fatal" ||
                       (event.attrs.count("status") &&
                        event.attrs.at("status") == "error");
    for (const Rule& rule : rules) {
      if (rule.type != "trace_error_rate" ||
          !rule_matches(rule, event.service, event.node)) {
        continue;
      }
      double rate = 0.0;
      uint64_t total = 0;
      uint64_t errors = 0;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        WindowCounter& counter = counters_[rule.id + "|" + event.service];
        if (counter.window_start_ns == 0 ||
            now > counter.window_start_ns + rule.window_ns) {
          counter.window_start_ns = now;
          counter.total = 0;
          counter.errors = 0;
        }
        ++counter.total;
        if (error) ++counter.errors;
        total = counter.total;
        errors = counter.errors;
        rate = total == 0 ? 0.0
                          : static_cast<double>(errors) * 100.0 /
                                static_cast<double>(total);
      }
      if (total >= rule.min_count && compare(rate, rule.op, rule.threshold)) {
        fire(rule, event.service, rule.name,
             "错误率 " + std::to_string(rate) + "%（" + std::to_string(errors) +
                 "/" + std::to_string(total) + "）",
             event.service, event.node, event, store, now);
      }
    }
  }
}

void RuleEngine::evaluate(Store* store, uint64_t now_ns) {
  if (store == nullptr) return;
  std::vector<Rule> rules;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    rules = rules_;
  }

  const std::vector<InstanceRecord> instances = store->catalog().instances();
  for (const Rule& rule : rules) {
    if (rule.type != "heartbeat" || !rule.enabled) continue;
    for (const InstanceRecord& instance : instances) {
      if (!rule.service.empty() && rule.service != instance.service) continue;
      if (!rule.node.empty() && rule.node != instance.node) continue;
      if (instance.last_seen_ns == 0) continue;
      const double age_seconds =
          static_cast<double>(now_ns > instance.last_seen_ns
                                  ? now_ns - instance.last_seen_ns
                                  : 0) /
          1000000000.0;
      if (age_seconds < rule.threshold) continue;
      common::Event source;
      source.service = instance.service;
      source.node = instance.node;
      fire(rule, instance.service + "|" + instance.instance, rule.name,
           "实例 " + instance.instance + " 已 " +
               std::to_string(static_cast<long long>(age_seconds)) +
               " 秒无事件，疑似卡死或退出",
           instance.service, instance.node, source, store, now_ns);
    }
  }
}

}  // namespace server
}  // namespace vmon
