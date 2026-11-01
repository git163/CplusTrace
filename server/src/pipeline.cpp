#include "pipeline.h"

namespace vmon {
namespace server {
namespace {

const PipelineRule kDefaultRules[] = {
    // 键值型密钥：password=xxx / token: xxx / secret = xxx
    {"secret",
     R"((password|passwd|pwd|token|secret|api[_-]?key)\s*[=:]\s*[^ ,;\n]+)",
     "$1=<redacted>"},
    {"email", R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})",
     "<email>"},
    {"ipv4", R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b)", "<ip>"},
    {"phone", R"(\b1[3-9][0-9]{9}\b)", "<phone>"},
};

}  // namespace

bool Pipeline::configure(const PipelineConfig& config, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<CompiledRule> compiled;
  auto add = [&compiled, error](const PipelineRule& rule) -> bool {
    CompiledRule item;
    item.rule = rule;
    try {
      // 脱敏规则默认忽略大小写；不引入脚本引擎。
      item.regex = std::regex(rule.pattern,
                              std::regex::ECMAScript | std::regex::icase);
      item.valid = true;
    } catch (const std::exception& ex) {
      if (error) {
        *error = "Pipeline 规则 " + rule.name + " 正则非法: " + ex.what();
      }
      return false;
    }
    compiled.push_back(item);
    return true;
  };

  if (config.use_defaults) {
    for (const PipelineRule& rule : kDefaultRules) {
      if (!add(rule)) return false;
    }
  }
  for (const PipelineRule& rule : config.rules) {
    if (rule.pattern.empty()) {
      if (error) *error = "Pipeline 规则 " + rule.name + " 缺少 pattern";
      return false;
    }
    if (!add(rule)) return false;
  }

  rules_.swap(compiled);
  enabled_ = config.enabled;
  redacted_events_.store(0);
  return true;
}

std::string Pipeline::apply(const std::string& text, bool* changed) const {
  std::string out = text;
  for (const CompiledRule& rule : rules_) {
    if (!rule.valid) continue;
    const std::string replaced = std::regex_replace(out, rule.regex,
                                                    rule.rule.replace);
    if (replaced != out) {
      out = replaced;
      if (changed) *changed = true;
    }
  }
  return out;
}

bool Pipeline::process(common::Event* event) {
  if (event == nullptr || !enabled_) return false;
  bool changed = false;

  if (!event->msg.empty()) {
    event->msg = apply(event->msg, &changed);
  }
  if (!event->name.empty()) {
    event->name = apply(event->name, &changed);
  }
  if (!event->file.empty()) {
    event->file = apply(event->file, &changed);
  }
  for (auto& kv : event->attrs) {
    kv.second = apply(kv.second, &changed);
  }

  if (changed) {
    redacted_events_.fetch_add(1, std::memory_order_relaxed);
  }
  return changed;
}

}  // namespace server
}  // namespace vmon
