// 数据血缘存储与分析：值指纹、迭代、状态契约、前置闸门与 RCA。
//
// 设计见 docs/data-lineage-design.md。核心思想：
//   1. 只存字段值指纹（hash），不建全量业务图；
//   2. 以 run_id + iteration_id 组织实例血缘；
//   3. 先跑前置闸门（状态/计数/身份/时序），命中即短路，再做差分与归因。
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "json_event.h"

namespace vmon {
namespace server {

struct LineageConfig {
  size_t max_records = 400000;   // 血缘字段记录上限
  size_t max_runs = 5000;        // run 数量上限
  uint64_t retention_ns = 6ull * 3600 * 1000000000ull;
};

// 一次边界上的字段值指纹。
struct LineageRecord {
  std::string run_id;
  uint64_t iteration_id = 0;
  std::string trace;
  std::string span;
  std::string parent;
  std::string service;
  std::string instance;
  std::string node;
  std::string op;
  std::string object;
  std::string field;
  std::string dir;        // in / out / local
  std::string kind;
  std::string value_hash;
  std::string shape_hash;
  std::string value;      // 可选原文（已脱敏/截断）
  uint32_t value_len = 0;
  bool truncated = false;
  bool redacted = false;
  bool opaque = false;
  uint64_t time_ns = 0;
  uint64_t mono_ns = 0;
};

// 设备状态事件。
struct StateRecord {
  std::string run_id;
  uint64_t iteration_id = 0;
  std::string device;
  std::string state;
  std::string service;
  std::string node;
  std::string source;
  uint64_t time_ns = 0;
  uint64_t mono_ns = 0;
};

struct GateResult {
  std::string gate;        // G0 / G1 / G2 / G3
  std::string check;       // 人类可读的检查项
  bool passed = true;
  std::string detail;
  std::string blame_hint;  // 失败时优先怀疑的子系统
};

struct RcaCandidate {
  std::string subject;     // 服务/字段
  double score = 0.0;
  std::vector<std::string> reasons;
};

class LineageStore {
 public:
  explicit LineageStore(const LineageConfig& config = LineageConfig());

  LineageStore(const LineageStore&) = delete;
  LineageStore& operator=(const LineageStore&) = delete;

  // 写入 type=lineage / type=state 事件；其它类型忽略。
  void ingest(const common::Event& event, const std::string& node_hint);

  bool has_run(const std::string& run_id) const;

  // run 列表（按最近更新时间倒序）。
  nlohmann::json runs(size_t limit = 100) const;

  // 单次迭代的跨进程实例血缘图。
  nlohmann::json graph(const std::string& run_id, uint64_t iteration_id) const;

  // 前置闸门结果。
  nlohmann::json gates(const std::string& run_id) const;

  // 根因分析：闸门 + 切片 + 差分 + 归因 + 叙述。
  nlohmann::json rca(const std::string& run_id, uint64_t iteration_id) const;

  size_t record_count() const;
  size_t run_count() const;

 private:
  struct RunData {
    std::string run_id;
    std::string scenario;
    uint64_t first_ns = 0;
    uint64_t last_ns = 0;
    std::vector<LineageRecord> records;
    std::vector<StateRecord> states;
  };

  void prune_locked(uint64_t now_ns);

  const RunData* find_locked(const std::string& run_id) const;

  std::vector<GateResult> evaluate_gates(const RunData& run,
                                         uint64_t iteration_id,
                                         bool iteration_scoped) const;

  // 取某迭代内字段的指纹（同名字段取最后一次）。
  static bool lookup_field(const RunData& run, uint64_t iteration_id,
                           const std::string& object, const std::string& field,
                           LineageRecord* out);

  static bool parse_u64(const std::string& text, uint64_t* out);

  mutable std::mutex mutex_;
  LineageConfig config_;
  std::unordered_map<std::string, RunData> runs_;
  std::deque<std::string> run_order_;
  size_t record_count_ = 0;
};

}  // namespace server
}  // namespace vmon
