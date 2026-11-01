#include <gtest/gtest.h>

#include <ctime>
#include <string>

#include "slo.h"
#include "store.h"
#include "tests/test_util.h"

namespace {

using vmon::common::Event;
using vmon::server::SloDefinition;
using vmon::server::SloManager;
using vmon::server::Store;
using vmon::server::StoreConfig;

Event SpanEvent(const std::string& type, const std::string& trace,
                uint64_t time_ns) {
  Event event;
  event.type = type;
  event.trace = trace;
  event.span = "S1";
  event.service = "order-service";
  event.instance = "order-1";
  event.node = "node-a";
  event.name = "HandleOrder";
  event.time_ns = time_ns;
  return event;
}

void AddTrace(Store* store, const std::string& trace, uint64_t start_ns,
              uint64_t end_ns, bool error) {
  store->ingest(SpanEvent("span_begin", trace, start_ns), "node-a", "agent");
  Event end = SpanEvent("span_end", trace, end_ns);
  if (error) {
    end.error_code = "E_FAIL";
    end.attrs["status"] = "error";
  }
  store->ingest(end, "node-a", "agent");
}

uint64_t NowNs() {
  struct timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

TEST(SloDefinition, ValidatesFields) {
  SloDefinition slo;
  std::string error;
  ASSERT_TRUE(SloDefinition::from_json(
      {{"id", "order-availability"},
       {"name", "订单可用性"},
       {"service", "order-service"},
       {"type", "availability"},
       {"target", 0.99},
       {"window_seconds", 3600}},
      &slo, &error))
      << error;
  EXPECT_EQ("order-availability", slo.id);
  EXPECT_EQ("order-service", slo.service);
  EXPECT_DOUBLE_EQ(0.99, slo.target);

  EXPECT_FALSE(SloDefinition::from_json(
      {{"id", "bad id"}, {"target", 0.99}, {"window_seconds", 3600}}, &slo,
      &error));
  EXPECT_FALSE(SloDefinition::from_json(
      {{"id", "x"}, {"target", 1.0}, {"window_seconds", 3600}}, &slo, &error));
  EXPECT_FALSE(SloDefinition::from_json(
      {{"id", "x"}, {"target", 0.9}, {"window_seconds", 10}}, &slo, &error));
  EXPECT_FALSE(SloDefinition::from_json(
      {{"id", "x"},
       {"target", 0.9},
       {"type", "latency"},
       {"window_seconds", 3600}},
      &slo, &error));
}

TEST(SloManager, ComputesAvailabilityBudget) {
  StoreConfig config;
  Store store(config);
  const uint64_t now = NowNs();
  // 4 条 trace，其中 1 条错误；目标 75%，错误预算 25%，已消耗 100%。
  AddTrace(&store, "T1", now - 4000000000ull, now - 3900000000ull, false);
  AddTrace(&store, "T2", now - 3000000000ull, now - 2900000000ull, false);
  AddTrace(&store, "T3", now - 2000000000ull, now - 1900000000ull, false);
  AddTrace(&store, "T4", now - 1000000000ull, now - 900000000ull, true);

  SloManager slos;
  std::string error;
  ASSERT_TRUE(slos.upsert_json(
      {{"id", "order"},
       {"name", "订单可用性"},
       {"service", "order-service"},
       {"type", "availability"},
       {"target", 0.75},
       {"window_seconds", 3600}},
      &error))
      << error;

  const nlohmann::json statuses = slos.statuses(store, now);
  ASSERT_EQ(1u, statuses.size());
  const nlohmann::json& item = statuses[0];
  EXPECT_EQ(4u, item.value("total", 0u));
  EXPECT_EQ(1u, item.value("bad", 0u));
  EXPECT_NEAR(0.75, item.value("sli", 0.0), 1e-9);
  EXPECT_NEAR(0.25, item.value("error_budget", 0.0), 1e-9);
  EXPECT_NEAR(1.0, item.value("budget_consumed", 0.0), 1e-9);
  EXPECT_NEAR(1.0, item.value("burn_rate", 0.0), 1e-9);
  EXPECT_EQ("healthy", item.value("status", std::string()));
}

TEST(SloManager, ComputesLatencySli) {
  StoreConfig config;
  Store store(config);
  const uint64_t now = NowNs();
  AddTrace(&store, "T1", now - 3000000000ull, now - 2900000000ull, false);
  AddTrace(&store, "T2", now - 2000000000ull, now - 1800000000ull, false);
  AddTrace(&store, "T3", now - 1000000000ull, now - 800000000ull, false);

  SloManager slos;
  std::string error;
  ASSERT_TRUE(slos.upsert_json(
      {{"id", "order-latency"},
       {"service", "order-service"},
       {"type", "latency"},
       {"target", 0.99},
       {"window_seconds", 3600},
       {"latency_threshold_ns", 150000000}},
      &error))
      << error;

  const nlohmann::json statuses = slos.statuses(store, now);
  ASSERT_EQ(1u, statuses.size());
  // T1 与 T3 超过 150ms，T2 未超过。
  EXPECT_EQ(3u, statuses[0].value("total", 0u));
  EXPECT_EQ(2u, statuses[0].value("bad", 0u));
  EXPECT_NEAR(1.0 / 3.0, statuses[0].value("sli", 0.0), 1e-9);
  EXPECT_EQ("critical", statuses[0].value("status", std::string()));
}

TEST(SloManager, UpsertRemoveAndPersist) {
  vmon::test::TempDir dir("vmon-slo");
  const std::string path = dir.file("slos.json");
  SloManager slos;
  std::string error;
  ASSERT_TRUE(slos.upsert_json(
      {{"id", "a"}, {"target", 0.99}, {"window_seconds", 3600}}, &error));
  ASSERT_TRUE(slos.upsert_json(
      {{"id", "a"}, {"name", "覆盖后"}, {"target", 0.95},
       {"window_seconds", 60}},
      &error));
  ASSERT_EQ(1u, slos.size());
  EXPECT_EQ("覆盖后", slos.definitions()[0].name);
  ASSERT_TRUE(slos.save(path, &error)) << error;

  SloManager loaded;
  ASSERT_TRUE(loaded.load(path, &error)) << error;
  ASSERT_EQ(1u, loaded.size());
  EXPECT_DOUBLE_EQ(0.95, loaded.definitions()[0].target);

  EXPECT_TRUE(loaded.remove("a"));
  EXPECT_FALSE(loaded.remove("a"));
  EXPECT_EQ(0u, loaded.size());
}

TEST(SloManager, RejectsInvalidJsonWithoutMutating) {
  SloManager slos;
  std::string error;
  ASSERT_TRUE(slos.upsert_json(
      {{"id", "ok"}, {"target", 0.99}, {"window_seconds", 3600}}, &error));
  EXPECT_FALSE(slos.upsert_json(
      {{"id", "bad"}, {"target", 2.0}, {"window_seconds", 3600}}, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(1u, slos.size());
}

}  // namespace
