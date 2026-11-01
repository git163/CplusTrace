// 入库前数据处理（Pipeline）：脱敏、字段清洗。
//
// 设计参考观测云 Pipeline 的「入库前处理」：在写入 Store 之前对日志正文、
// Span 名称与属性做正则替换，避免敏感信息进入内存与证据包。
// 约束：只做轻量正则替换，不引入脚本引擎；规则在启动时编译，错误配置直接拒绝。
#pragma once

#include <atomic>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

#include "json_event.h"

namespace vmon {
namespace server {

struct PipelineRule {
  std::string name;
  std::string pattern;
  std::string replace = "<redacted>";
};

struct PipelineConfig {
  bool enabled = false;
  bool use_defaults = true;  // 内置 password/token/email/IP/phone 规则
  std::vector<PipelineRule> rules;
};

class Pipeline {
 public:
  // 编译规则；非法正则返回 false 并给出错误。重复调用会覆盖旧规则。
  bool configure(const PipelineConfig& config, std::string* error);

  // 对事件做脱敏。线程安全；返回是否发生了替换。
  bool process(common::Event* event);

  bool enabled() const { return enabled_; }
  size_t rule_count() const { return rules_.size(); }
  uint64_t redacted_events() const {
    return redacted_events_.load(std::memory_order_relaxed);
  }

 private:
  struct CompiledRule {
    PipelineRule rule;
    std::regex regex;
    bool valid = false;
  };

  std::string apply(const std::string& text, bool* changed) const;

  bool enabled_ = false;
  std::vector<CompiledRule> rules_;
  mutable std::mutex mutex_;  // 保护 rules_ 的重配置（运行期只读）
  std::atomic<uint64_t> redacted_events_{0};
};

}  // namespace server
}  // namespace vmon
