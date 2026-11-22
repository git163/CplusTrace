// 数据血缘存储与闸门/RCA 单元测试。
#include <chrono>

#include <gtest/gtest.h>

#include "json_event.h"
#include "lineage.h"

namespace {

using vmon::common::DataRef;
using vmon::common::Event;
using vmon::server::LineageStore;

uint64_t wall_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

Event make_data(const std::string& run, uint64_t iter,
                const std::string& service, const std::string& object,
                const std::string& field, const std::string& value,
                const std::string& dir = "out", bool raw = true) {
  Event ev;
  ev.type = "lineage";
  ev.run_id = run;
  ev.iteration_id = iter;
  ev.service = service;
  ev.name = service + "/op";
  ev.time_ns = wall_ns() + iter;
  DataRef d;
  d.object = object;
  d.field = field;
  d.dir = dir;
  d.kind = "int";
  d.value_hash = vmon::common::value_fingerprint(value);
  d.value_len = static_cast<uint32_t>(value.size());
  if (raw) d.value = value;
  d.op = ev.name;
  ev.data.push_back(d);
  return ev;
}

Event make_state(const std::string& run, const std::string& device,
                 const std::string& state) {
  Event ev;
  ev.type = "state";
  ev.run_id = run;
  ev.device = device;
  ev.state = state;
  ev.service = device;
  ev.time_ns = wall_ns();
  return ev;
}

void emit_baseline_states(LineageStore& store, const std::string& run) {
  store.ingest(make_state(run, "job-orchestrator", "running"), "");
  store.ingest(make_state(run, "acq-controller", "armed"), "");
  store.ingest(make_state(run, "data-receiver", "ready"), "");
  store.ingest(make_state(run, "algo-controller", "idle"), "");
  store.ingest(make_state(run, "calib-service", "calibrated"), "");
  store.ingest(make_state(run, "motion-controller", "homed"), "");
  store.ingest(make_state(run, "state-store", "open"), "");
}

void emit_counts(LineageStore& store, const std::string& run, int n) {
  const char* names[] = {"trigger_count", "frame_count", "processed_count",
                         "iteration_count"};
  for (const char* name : names) {
    store.ingest(make_data(run, 0, "job-orchestrator", "count", name,
                           std::to_string(n)),
                 "");
  }
}

TEST(LineageTest, IdentityGateAndRca) {
  LineageStore store;
  const std::string run = "run-1";
  emit_baseline_states(store, run);
  emit_counts(store, run, 2);
  // 迭代 1：身份一致；迭代 2：请求 2 实际 1。
  store.ingest(make_data(run, 1, "job-orchestrator", "frame",
                         "requested_frame_id", "1"), "");
  store.ingest(make_data(run, 1, "data-receiver", "frame", "used_frame_id",
                         "1"), "");
  store.ingest(make_data(run, 2, "job-orchestrator", "frame",
                         "requested_frame_id", "2"), "");
  store.ingest(make_data(run, 2, "data-receiver", "frame", "used_frame_id",
                         "1"), "");

  const nlohmann::json gates = store.gates(run);
  EXPECT_EQ(gates["summary"]["G2"]["failed_iterations"].get<int>(), 1);
  EXPECT_TRUE(gates["summary"]["G0"]["passed"].get<bool>());
  EXPECT_TRUE(gates["summary"]["G1"]["passed"].get<bool>());

  const nlohmann::json rca = store.rca(run, 0);
  EXPECT_EQ(rca["verdict"].get<std::string>(), "identity_mismatch");
  EXPECT_GT(rca["confidence"].get<double>(), 0.5);
  EXPECT_EQ(rca["iteration_id"].get<uint64_t>(), 2u);
  ASSERT_FALSE(rca["candidates"].empty());
  EXPECT_EQ(rca["candidates"][0]["subject"].get<std::string>(),
            "data-receiver");
}

TEST(LineageTest, TimingGateAndRca) {
  LineageStore store;
  const std::string run = "run-2";
  emit_baseline_states(store, run);
  store.ingest(make_data(run, 7, "motion-controller", "motion",
                         "settle_time_ns", "13100000"), "");
  store.ingest(make_data(run, 7, "acq-controller", "trigger",
                         "trigger_delay_ns", "10000000"), "");

  const nlohmann::json gates = store.gates(run);
  EXPECT_EQ(gates["summary"]["G3"]["failed_iterations"].get<int>(), 1);
  const nlohmann::json rca = store.rca(run, 0);
  EXPECT_EQ(rca["verdict"].get<std::string>(), "timing_violation");
  EXPECT_EQ(rca["candidates"][0]["subject"].get<std::string>(),
            "trigger-controller");
}

TEST(LineageTest, CardinalityGate) {
  LineageStore store;
  const std::string run = "run-3";
  emit_baseline_states(store, run);
  store.ingest(make_data(run, 0, "job-orchestrator", "count", "trigger_count",
                         "10"), "");
  store.ingest(make_data(run, 0, "job-orchestrator", "count", "frame_count",
                         "9"), "");

  const nlohmann::json gates = store.gates(run);
  EXPECT_EQ(gates["summary"]["G1"]["failed_iterations"].get<int>(), 1);
  const nlohmann::json rca = store.rca(run, 0);
  EXPECT_EQ(rca["verdict"].get<std::string>(), "cardinality_mismatch");
}

TEST(LineageTest, StateGate) {
  LineageStore store;
  const std::string run = "run-4";
  emit_baseline_states(store, run);
  // 覆盖 motion 起始状态为 disabled（同一时刻，取最早一条，需要更早时间）。
  Event bad = make_state(run, "motion-controller", "disabled");
  bad.time_ns = wall_ns() - 1000000000ull;  // 早于 emit_baseline_states 1s
  store.ingest(bad, "");
  store.ingest(make_data(run, 1, "motion-controller", "motion",
                         "actual_position", "10"), "");

  const nlohmann::json gates = store.gates(run);
  EXPECT_EQ(gates["summary"]["G0"]["failed_iterations"].get<int>(), 1);
  const nlohmann::json rca = store.rca(run, 0);
  EXPECT_EQ(rca["verdict"].get<std::string>(), "state_precondition_violation");
}

TEST(LineageTest, GraphHasValueEdges) {
  LineageStore store;
  const std::string run = "run-5";
  emit_baseline_states(store, run);
  store.ingest(make_data(run, 1, "acq-controller", "frame", "frame_id", "7"),
               "");
  store.ingest(make_data(run, 1, "data-receiver", "frame", "used_frame_id",
                         "7", "in"),
               "");
  const nlohmann::json graph = store.graph(run, 1);
  EXPECT_FALSE(graph["nodes"].empty());
  EXPECT_FALSE(graph["edges"].empty());
}

TEST(LineageTest, MissingRunReturnsError) {
  LineageStore store;
  const nlohmann::json rca = store.rca("nope", 0);
  EXPECT_TRUE(rca.contains("error"));
  EXPECT_FALSE(store.has_run("nope"));
}

}  // namespace
