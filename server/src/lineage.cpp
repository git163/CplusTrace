#include "lineage.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace vmon {
namespace server {
namespace {

uint64_t now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// 工业控制默认状态契约：设备 -> 允许状态集合。
const std::map<std::string, std::set<std::string>>& required_states() {
  static const std::map<std::string, std::set<std::string>> kMap = {
      {"job-orchestrator", {"running"}},
      {"acq-controller", {"armed", "ready"}},
      {"data-receiver", {"ready"}},
      {"algo-controller", {"idle", "ready"}},
      {"calib-service", {"calibrated", "valid"}},
      {"motion-controller", {"homed", "enabled"}},
      {"state-store", {"open"}},
  };
  return kMap;
}

std::string lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(c));
  return out;
}

}  // namespace

LineageStore::LineageStore(const LineageConfig& config) : config_(config) {}

size_t LineageStore::record_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return record_count_;
}

size_t LineageStore::run_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return runs_.size();
}

bool LineageStore::has_run(const std::string& run_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return runs_.find(run_id) != runs_.end();
}

void LineageStore::ingest(const common::Event& event,
                          const std::string& node_hint) {
  if (event.run_id.empty()) return;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = runs_.find(event.run_id);
  if (it == runs_.end()) {
    RunData run;
    run.run_id = event.run_id;
    run.first_ns = event.time_ns;
    run.last_ns = event.time_ns;
    it = runs_.insert(std::make_pair(event.run_id, std::move(run))).first;
    run_order_.push_back(event.run_id);
  }
  RunData& run = it->second;
  run.last_ns = std::max(run.last_ns, event.time_ns);
  if (run.first_ns == 0 || event.time_ns < run.first_ns) {
    run.first_ns = event.time_ns;
  }

  if (event.type == "state") {
    StateRecord s;
    s.run_id = event.run_id;
    s.iteration_id = event.iteration_id;
    s.device = event.device.empty() ? event.service : event.device;
    s.state = event.state;
    s.service = event.service;
    s.node = event.node.empty() ? node_hint : event.node;
    s.source = event.attrs.count("source") ? event.attrs.at("source") : "";
    s.time_ns = event.time_ns;
    s.mono_ns = event.mono_ns;
    run.states.push_back(std::move(s));
    return;
  }

  if (event.type != "lineage") return;

  for (const common::DataRef& d : event.data) {
    LineageRecord r;
    r.run_id = event.run_id;
    r.iteration_id = event.iteration_id;
    r.trace = event.trace;
    r.span = event.span;
    r.parent = event.parent;
    r.service = event.service;
    r.instance = event.instance;
    r.node = event.node.empty() ? node_hint : event.node;
    r.op = d.op.empty() ? event.name : d.op;
    r.object = d.object;
    r.field = d.field;
    r.dir = d.dir.empty() ? "local" : d.dir;
    r.kind = d.kind;
    r.value_hash = d.value_hash;
    r.shape_hash = d.shape_hash;
    r.value = d.value;
    r.value_len = d.value_len;
    r.truncated = d.truncated;
    r.redacted = d.redacted;
    r.opaque = d.opaque;
    r.time_ns = event.time_ns;
    r.mono_ns = event.mono_ns;
    run.records.push_back(std::move(r));
    ++record_count_;
  }

  prune_locked(now_ns());
}

void LineageStore::prune_locked(uint64_t now_ns) {
  // 时间保留。
  if (config_.retention_ns > 0) {
    while (!run_order_.empty()) {
      auto it = runs_.find(run_order_.front());
      if (it == runs_.end()) {
        run_order_.pop_front();
        continue;
      }
      if (now_ns > it->second.last_ns &&
          now_ns - it->second.last_ns <= config_.retention_ns) {
        break;
      }
      record_count_ -= std::min(record_count_, it->second.records.size());
      runs_.erase(it);
      run_order_.pop_front();
    }
  }
  // run 数量与记录总量上限。
  while ((runs_.size() > config_.max_runs ||
          record_count_ > config_.max_records) &&
         !run_order_.empty()) {
    auto it = runs_.find(run_order_.front());
    if (it != runs_.end()) {
      record_count_ -= std::min(record_count_, it->second.records.size());
      runs_.erase(it);
    }
    run_order_.pop_front();
  }
}

const LineageStore::RunData* LineageStore::find_locked(
    const std::string& run_id) const {
  auto it = runs_.find(run_id);
  return it == runs_.end() ? nullptr : &it->second;
}

bool LineageStore::parse_u64(const std::string& text, uint64_t* out) {
  if (text.empty()) return false;
  try {
    size_t idx = 0;
    const unsigned long long v = std::stoull(text, &idx);
    if (idx != text.size()) return false;
    *out = static_cast<uint64_t>(v);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LineageStore::lookup_field(const RunData& run, uint64_t iteration_id,
                                const std::string& object,
                                const std::string& field,
                                LineageRecord* out) {
  const LineageRecord* found = nullptr;
  for (const LineageRecord& r : run.records) {
    if (r.iteration_id != iteration_id) continue;
    if (!object.empty() && r.object != object) continue;
    if (r.field != field) continue;
    // 优先带原文的记录（gate 需要比较/解析值），其次再退化为任意记录。
    if (r.value.empty()) {
      if (found == nullptr) found = &r;
      continue;
    }
    if (found == nullptr || found->value.empty() || r.time_ns >= found->time_ns) {
      found = &r;
    }
  }
  if (found == nullptr) return false;
  if (out != nullptr) *out = *found;
  return true;
}

std::vector<GateResult> LineageStore::evaluate_gates(
    const RunData& run, uint64_t iteration_id, bool iteration_scoped) const {
  (void)iteration_scoped;  // G0 统一取 run 起始状态，不再按迭代截断。
  std::vector<GateResult> gates;

  // ---- G0：状态契约 -------------------------------------------------------
  {
    GateResult g;
    g.gate = "G0";
    g.check = "设备状态/使能";
    std::vector<std::string> bad;
    for (const auto& kv : required_states()) {
      const std::string& device = kv.first;
      const std::string& want = *kv.second.begin();
      // 只看 run 起始状态（iteration_id==0 中最早的一条），忽略运行结束的终态。
      const StateRecord* earliest = nullptr;
      for (const StateRecord& s : run.states) {
        if (s.device != device) continue;
        if (s.iteration_id != 0) continue;
        if (earliest == nullptr || s.time_ns < earliest->time_ns) earliest = &s;
      }
      if (earliest == nullptr) continue;  // 该设备未参与本 run
      if (kv.second.count(lower(earliest->state)) == 0) {
        bad.push_back(device + "=" + earliest->state + "（期望 " + want + "）");
      }
    }
    g.passed = bad.empty();
    if (!g.passed) {
      g.detail = "状态不满足：" + bad.front();
      g.blame_hint = bad.front().substr(0, bad.front().find('='));
    } else {
      g.detail = "全部设备状态满足契约";
    }
    gates.push_back(g);
  }

  // ---- G1：计数一致 -------------------------------------------------------
  {
    GateResult g;
    g.gate = "G1";
    g.check = "触发数=收帧数=处理数=迭代数";
    const char* names[] = {"trigger_count", "frame_count", "processed_count",
                           "iteration_count"};
    std::map<std::string, uint64_t> counts;
    for (const char* name : names) {
      // 计数是整 run 级别的，跨迭代查找（取最后一次）。
      const LineageRecord* found = nullptr;
      for (const LineageRecord& r : run.records) {
        if (r.object != "count" || r.field != name) continue;
        if (found == nullptr || r.time_ns >= found->time_ns) found = &r;
      }
      if (found == nullptr) continue;
      uint64_t v = 0;
      if (parse_u64(found->value, &v)) counts[name] = v;
    }
    if (counts.size() < 2) {
      g.passed = true;
      g.detail = "无计数数据，跳过";
    } else {
      uint64_t first = counts.begin()->second;
      std::string bad;
      for (const auto& kv : counts) {
        if (kv.second != first) {
          bad = kv.first + "=" + std::to_string(kv.second);
          break;
        }
      }
      g.passed = bad.empty();
      if (!g.passed) {
        std::ostringstream ss;
        ss << "计数不一致";
        for (const auto& kv : counts) ss << " " << kv.first << "=" << kv.second;
        g.detail = ss.str();
        g.blame_hint = "acq-controller/data-receiver";
      } else {
        std::ostringstream ss;
        ss << "计数一致";
        for (const auto& kv : counts) ss << " " << kv.first << "=" << kv.second;
        g.detail = ss.str();
      }
    }
    gates.push_back(g);
  }

  // ---- G2：身份不变式（used_frame_id == requested_frame_id） --------------
  {
    GateResult g;
    g.gate = "G2";
    g.check = "used_frame_id == requested_frame_id";
    LineageRecord want;
    LineageRecord used;
    const bool has_want =
        lookup_field(run, iteration_id, "frame", "requested_frame_id", &want);
    const bool has_used =
        lookup_field(run, iteration_id, "frame", "used_frame_id", &used);
    if (!has_want || !has_used) {
      g.passed = true;
      g.detail = "无身份字段，跳过";
    } else {
      g.passed = want.value == used.value;
      if (!g.passed) {
        g.detail = "请求帧 " + want.value + "，实际使用 " + used.value;
        g.blame_hint = "data-receiver/algo-controller";
      } else {
        g.detail = "身份一致（帧 " + want.value + "）";
      }
    }
    gates.push_back(g);
  }

  // ---- G3：时序不变式（trigger_delay >= settle_time） --------------------
  {
    GateResult g;
    g.gate = "G3";
    g.check = "trigger_delay >= settle_time";
    LineageRecord settle;
    LineageRecord trigger;
    const bool has_settle =
        lookup_field(run, iteration_id, "motion", "settle_time_ns", &settle);
    const bool has_trigger =
        lookup_field(run, iteration_id, "trigger", "trigger_delay_ns", &trigger);
    if (!has_settle || !has_trigger) {
      g.passed = true;
      g.detail = "无时序字段，跳过";
    } else {
      uint64_t settle_v = 0;
      uint64_t trigger_v = 0;
      const bool ok = parse_u64(settle.value, &settle_v) &&
                      parse_u64(trigger.value, &trigger_v);
      if (!ok) {
        g.passed = true;
        g.detail = "时序字段不可解析，跳过";
      } else {
        g.passed = trigger_v >= settle_v;
        if (!g.passed) {
          g.detail = "settle=" + std::to_string(settle_v) +
                     "ns > trigger_delay=" + std::to_string(trigger_v) + "ns";
          g.blame_hint = "trigger-controller/motion-controller";
        } else {
          g.detail = "时序满足（settle=" + std::to_string(settle_v) +
                     "ns <= trigger=" + std::to_string(trigger_v) + "ns）";
        }
      }
    }
    gates.push_back(g);
  }

  return gates;
}

nlohmann::json LineageStore::runs(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json out = nlohmann::json::array();
  std::vector<const RunData*> ordered;
  ordered.reserve(runs_.size());
  for (const auto& kv : runs_) ordered.push_back(&kv.second);
  std::sort(ordered.begin(), ordered.end(),
            [](const RunData* a, const RunData* b) {
              return a->last_ns > b->last_ns;
            });
  for (const RunData* run : ordered) {
    if (out.size() >= limit) break;
    std::set<uint64_t> iterations;
    for (const LineageRecord& r : run->records) {
      if (r.iteration_id > 0) iterations.insert(r.iteration_id);
    }
    nlohmann::json item = {
        {"run_id", run->run_id},
        {"first_ns", run->first_ns},
        {"last_ns", run->last_ns},
        {"records", run->records.size()},
        {"iterations", iterations.size()},
        {"states", run->states.size()},
    };
    out.push_back(item);
  }
  return out;
}

nlohmann::json LineageStore::graph(const std::string& run_id,
                                   uint64_t iteration_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const RunData* run = find_locked(run_id);
  if (run == nullptr) return nlohmann::json::object();

  nlohmann::json nodes = nlohmann::json::array();
  std::vector<const LineageRecord*> outs;
  std::vector<const LineageRecord*> ins;
  for (const LineageRecord& r : run->records) {
    if (r.iteration_id != iteration_id) continue;
    nodes.push_back({{"service", r.service},
                     {"op", r.op},
                     {"object", r.object},
                     {"field", r.field},
                     {"dir", r.dir},
                     {"kind", r.kind},
                     {"value_hash", r.value_hash},
                     {"value", r.value},
                     {"trace", r.trace},
                     {"span", r.span},
                     {"time_ns", r.time_ns}});
    if (r.dir == "out") outs.push_back(&r);
    else if (r.dir == "in") ins.push_back(&r);
  }

  nlohmann::json edges = nlohmann::json::array();
  std::set<std::string> seen;
  for (const LineageRecord* o : outs) {
    for (const LineageRecord* i : ins) {
      if (o->value_hash.empty() || o->value_hash != i->value_hash) continue;
      if (o->service == i->service && o->field == i->field) continue;
      const std::string key = o->service + "|" + o->field + "->" + i->service +
                              "|" + i->field;
      if (!seen.insert(key).second) continue;
      edges.push_back({{"from_service", o->service},
                       {"from_field", o->field},
                       {"to_service", i->service},
                       {"to_field", i->field},
                       {"relation", "exact"},
                       {"value_hash", o->value_hash}});
    }
  }

  return {{"run_id", run_id},
          {"iteration_id", iteration_id},
          {"nodes", nodes},
          {"edges", edges}};
}

nlohmann::json LineageStore::gates(const std::string& run_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const RunData* run = find_locked(run_id);
  if (run == nullptr) return nlohmann::json::object();

  std::set<uint64_t> iterations;
  for (const LineageRecord& r : run->records) {
    if (r.iteration_id > 0) iterations.insert(r.iteration_id);
  }
  if (iterations.empty()) iterations.insert(0);  // 只有 run 级记录时按 0 评估

  nlohmann::json per_iteration = nlohmann::json::array();
  std::map<std::string, int> failures;
  for (uint64_t iter : iterations) {
    const std::vector<GateResult> results = evaluate_gates(*run, iter, true);
    nlohmann::json gate_json = nlohmann::json::array();
    std::string first_failed;
    for (const GateResult& g : results) {
      gate_json.push_back({{"gate", g.gate},
                           {"check", g.check},
                           {"passed", g.passed},
                           {"detail", g.detail},
                           {"blame_hint", g.blame_hint}});
      if (!g.passed) {
        ++failures[g.gate];
        if (first_failed.empty()) first_failed = g.gate;
      }
    }
    per_iteration.push_back({{"iteration_id", iter},
                             {"gates", gate_json},
                             {"first_failed", first_failed}});
  }

  nlohmann::json summary = nlohmann::json::object();
  for (const char* gate : {"G0", "G1", "G2", "G3"}) {
    summary[gate] = {{"failed_iterations", failures[gate]},
                     {"passed", failures[gate] == 0}};
  }

  return {{"run_id", run_id},
          {"iterations", per_iteration},
          {"summary", summary}};
}

nlohmann::json LineageStore::rca(const std::string& run_id,
                                 uint64_t iteration_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const RunData* run = find_locked(run_id);
  if (run == nullptr) {
    return {{"error", "run not found"}, {"run_id", run_id}};
  }

  std::set<uint64_t> iterations;
  for (const LineageRecord& r : run->records) {
    if (r.iteration_id > 0) iterations.insert(r.iteration_id);
  }
  if (iterations.empty()) iterations.insert(0);  // 只有 run 级记录时按 0 评估

  // 先把每个 (object/field, iteration) 去重（优先带原文、最新）。
  std::map<std::string, std::map<uint64_t, const LineageRecord*>> per_iter;
  for (const LineageRecord& r : run->records) {
    if (r.iteration_id == 0 || r.value_hash.empty()) continue;
    const std::string key = r.object + "/" + r.field;
    const LineageRecord*& slot = per_iter[key][r.iteration_id];
    if (slot == nullptr || slot->value.empty() ||
        (!r.value.empty() && r.time_ns >= slot->time_ns)) {
      slot = &r;
    }
  }

  // 每个字段的多数值与出现次数。
  std::map<std::string, std::map<std::string, int>> histogram;
  std::map<std::string, int> total;
  for (const auto& kv : per_iter) {
    for (const auto& iv : kv.second) {
      ++histogram[kv.first][iv.second->value_hash];
      ++total[kv.first];
    }
  }
  std::map<std::string, std::string> majority_hash;
  std::map<std::string, int> majority_count;
  for (const auto& kv : histogram) {
    int best = 0;
    std::string best_hash;
    for (const auto& h : kv.second) {
      if (h.second > best) {
        best = h.second;
        best_hash = h.first;
      }
    }
    majority_hash[kv.first] = best_hash;
    majority_count[kv.first] = best;
  }

  // 各迭代的偏离字段（只保留可展示原文、多数值出现 >=2 次的字段）。
  std::map<uint64_t, std::vector<const LineageRecord*>> dev_by_iter;
  for (const auto& kv : per_iter) {
    if (total[kv.first] < 2 || majority_count[kv.first] < 2) continue;
    for (const auto& iv : kv.second) {
      const LineageRecord& r = *iv.second;
      if (r.value.empty()) continue;
      if (r.value_hash == majority_hash[kv.first]) continue;
      dev_by_iter[iv.first].push_back(&r);
    }
  }

  // 选择分析迭代：闸门失败优先，其次偏离最多的迭代，最后取最后一次。
  if (iteration_id == 0 && !iterations.empty()) {
    uint64_t chosen = *iterations.rbegin();
    bool found = false;
    for (uint64_t iter : iterations) {
      const std::vector<GateResult> rs = evaluate_gates(*run, iter, true);
      bool failed = false;
      for (const GateResult& g : rs) failed = failed || !g.passed;
      if (failed) {
        chosen = iter;
        found = true;
        break;
      }
    }
    if (!found) {
      size_t best = 0;
      for (const auto& kv : dev_by_iter) {
        if (kv.second.size() > best) {
          best = kv.second.size();
          chosen = kv.first;
        }
      }
    }
    iteration_id = chosen;
  }

  const std::vector<GateResult> gate_results =
      evaluate_gates(*run, iteration_id, true);

  nlohmann::json gates_json = nlohmann::json::array();
  std::string first_failed_gate;
  std::string blame_hint;
  for (const GateResult& g : gate_results) {
    gates_json.push_back({{"gate", g.gate},
                          {"check", g.check},
                          {"passed", g.passed},
                          {"detail", g.detail},
                          {"blame_hint", g.blame_hint}});
    if (!g.passed && first_failed_gate.empty()) {
      first_failed_gate = g.gate;
      blame_hint = g.blame_hint;
    }
  }

  // ---- 差分：当前迭代的偏离字段 -----------------------------------------
  nlohmann::json deviations = nlohmann::json::array();
  {
    const auto it = dev_by_iter.find(iteration_id);
    if (it != dev_by_iter.end()) {
      for (const LineageRecord* r : it->second) {
        const std::string key = r->object + "/" + r->field;
        const double ratio = static_cast<double>(majority_count[key]) /
                             static_cast<double>(total[key]);
        deviations.push_back({{"service", r->service},
                              {"object", r->object},
                              {"field", r->field},
                              {"value", r->value},
                              {"value_hash", r->value_hash},
                              {"baseline_ratio", ratio},
                              {"baseline_count", majority_count[key]},
                              {"total", total[key]}});
      }
    }
  }


  // ---- 候选与打分 --------------------------------------------------------
  nlohmann::json candidates = nlohmann::json::array();
  double confidence = 0.0;
  std::string verdict = "evidence_insufficient";
  if (first_failed_gate == "G0") {
    verdict = "state_precondition_violation";
    candidates.push_back({{"subject", blame_hint},
                          {"score", 0.9},
                          {"reasons", {"状态契约不满足"}}});
    confidence = 0.9;
  } else if (first_failed_gate == "G1") {
    verdict = "cardinality_mismatch";
    candidates.push_back({{"subject", blame_hint},
                          {"score", 0.85},
                          {"reasons", {"触发/收帧/处理计数不一致"}}});
    confidence = 0.85;
  } else if (first_failed_gate == "G2") {
    verdict = "identity_mismatch";
    candidates.push_back({{"subject", "data-receiver"},
                          {"score", 0.86},
                          {"reasons", {"used_frame_id != requested_frame_id",
                                       "疑似按最新帧响应，未按 frame_id 取帧"}}});
    candidates.push_back({{"subject", "algo-controller"},
                          {"score", 0.55},
                          {"reasons", {"消费了错误帧，基于旧位置计算 offset"}}});
    confidence = 0.86;
  } else if (first_failed_gate == "G3") {
    verdict = "timing_violation";
    candidates.push_back({{"subject", "trigger-controller"},
                          {"score", 0.88},
                          {"reasons", {"固定延时触发，未等待运动稳定"}}});
    candidates.push_back({{"subject", "motion-controller"},
                          {"score", 0.5},
                          {"reasons", {"settle 时间抖动"}}});
    confidence = 0.88;
  } else {
    // 闸门全过：用差分偏离打分。
    std::map<std::string, double> score;
    for (const auto& d : deviations) {
      const std::string subject = d.value("service", std::string());
      score[subject] += 0.5 + 0.4 * (1.0 - d.value("baseline_ratio", 0.0));
    }
    for (const auto& kv : score) {
      candidates.push_back({{"subject", kv.first},
                            {"score", std::min(0.75, kv.second)},
                            {"reasons", {"字段值偏离多数基线"}}});
      confidence = std::max(confidence, std::min(0.75, kv.second));
    }
    if (!candidates.empty()) verdict = "value_deviation";
  }

  // ---- 根因链（按服务出现顺序，给出值传播） -----------------------------
  nlohmann::json chain = nlohmann::json::array();
  {
    std::set<std::string> services;
    for (const LineageRecord& r : run->records) {
      if (r.iteration_id != iteration_id) continue;
      if (services.insert(r.service).second) {
        chain.push_back({{"service", r.service}, {"op", r.op}});
      }
    }
  }

  // ---- 证据与叙述 --------------------------------------------------------
  nlohmann::json evidence = nlohmann::json::array();
  for (const GateResult& g : gate_results) {
    if (!g.passed) {
      evidence.push_back({{"kind", "gate"},
                          {"gate", g.gate},
                          {"detail", g.detail}});
    }
  }
  for (const auto& d : deviations) {
    evidence.push_back({{"kind", "baseline_diff"},
                        {"field", d.value("object", std::string()) + "/" +
                                      d.value("field", std::string())},
                        {"value", d.value("value", std::string())},
                        {"baseline_ratio", d.value("baseline_ratio", 0.0)}});
  }

  std::vector<std::string> unknowns;
  if (first_failed_gate == "G2") {
    unknowns.push_back("抖动来自相机负载还是节点间网络需进一步确认");
    unknowns.push_back("是否所有取帧路径都存在该问题");
  } else if (first_failed_gate == "G3") {
    unknowns.push_back("settle 抖动来源（调度/CPU 争用）");
    unknowns.push_back("其它工位是否同样采用固定延时触发");
  } else if (first_failed_gate.empty()) {
    unknowns.push_back("闸门全过，需补充更细的中间结果或基线");
  }

  std::string narrative;
  {
    std::ostringstream ss;
    ss << "run " << run_id << " 第 " << iteration_id << " 次迭代：";
    if (!first_failed_gate.empty()) {
      ss << "闸门 " << first_failed_gate << " 命中（" << verdict << "）";
      for (const GateResult& g : gate_results) {
        if (!g.passed) ss << "；" << g.gate << " " << g.detail;
      }
      ss << "。嫌疑：" << blame_hint << "。";
    } else {
      ss << "前置闸门全部通过，字段值偏离多数基线 " << deviations.size()
         << " 处。";
    }
    narrative = ss.str();
  }

  return {{"run_id", run_id},
          {"iteration_id", iteration_id},
          {"verdict", verdict},
          {"confidence", confidence},
          {"gates", gates_json},
          {"candidates", candidates},
          {"chain", chain},
          {"deviations", deviations},
          {"evidence", evidence},
          {"unknowns", unknowns},
          {"narrative", narrative}};
}

}  // namespace server
}  // namespace vmon
