// 规则引擎与事件中心。
//
// 规则类型：
//   heartbeat         —— 服务实例超过阈值时间无事件（进程卡死/退出）；
//   error_log         —— 同一日志指纹在窗口内次数达到阈值；
//   trace_error_rate  —— 服务在窗口内错误调用占比达到阈值；
//   slow_trace        —— 单次调用耗时超过阈值；
//   metric            —— 指标越过阈值。
//
// 所有告警带冷却时间，避免风暴；事件进入 Store 的事件中心，可确认。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "json_event.h"
#include "model.h"

namespace vmon {
namespace server {

class Store;

struct Rule {
  std::string id;
  std::string name;
  std::string type;
  std::string service;   // 可选，限定服务
  std::string node;      // 可选，限定节点
  std::string metric;    // type=metric 时的指标名
  std::string op = ">="; // 比较符
  double threshold = 0.0;
  uint32_t min_count = 1;       // 统计窗口内最少样本数
  uint64_t window_ns = 60ull * 1000000000ull;
  uint64_t cooldown_ns = 300ull * 1000000000ull;
  std::string severity = "warning";
  bool enabled = true;

  nlohmann::json to_json() const;
  static Rule from_json(const nlohmann::json& json);
};

class RuleEngine {
 public:
  RuleEngine();

  void add_rule(const Rule& rule);
  bool remove_rule(const std::string& id);
  std::vector<Rule> rules() const;
  void set_rules(const std::vector<Rule>& rules);

  // 每收到一条事件调用；内部按规则类型做窗口统计。
  void on_event(const common::Event& event, Store* store);

  // 周期调用：心跳缺失、错误率等需要「无事件」也能触发的规则。
  void evaluate(Store* store, uint64_t now_ns);

  // 默认规则：心跳缺失 + 错误日志突增 + 慢调用。
  static std::vector<Rule> default_rules();

  // 生成一个稳定的告警 ID（同一规则 + 对象 + 时间窗内不重复）。
  static std::string make_event_id(const std::string& rule_id,
                                   const std::string& key,
                                   uint64_t window_ns, uint64_t now_ns);

 private:
  struct WindowCounter {
    uint64_t window_start_ns = 0;
    uint64_t total = 0;
    uint64_t errors = 0;
    uint64_t count = 0;
  };

  void fire(const Rule& rule, const std::string& key,
            const std::string& title, const std::string& detail,
            const std::string& service, const std::string& node,
            const common::Event& source, Store* store, uint64_t now_ns);

  mutable std::mutex mutex_;
  std::vector<Rule> rules_;
  std::unordered_map<std::string, uint64_t> last_fire_ns_;
  std::unordered_map<std::string, WindowCounter> counters_;
};

}  // namespace server
}  // namespace vmon
