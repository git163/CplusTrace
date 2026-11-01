#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "dashboard.h"
#include "json_event.h"
#include "store.h"

namespace {

using vmon::server::Dashboard;
using vmon::server::DashboardSnapshot;
using vmon::server::DashboardStore;
using vmon::server::Store;

TEST(DashboardStore, ComputesWidgetsAndSnapshots) {
  Store store;
  for (int i = 0; i < 3; ++i) {
    vmon::common::Event metric;
    metric.type = "metric";
    metric.time_ns = 1000 + static_cast<uint64_t>(i);
    metric.name = "host.load1";
    metric.attrs["value"] = std::to_string(1.0 + i);
    metric.node = "node-a";
    store.ingest(metric, "node-a", "agent-a");
  }

  DashboardStore dashboards;
  nlohmann::json body = {
      {"id", "dash-test"},
      {"name", "测试看板"},
      {"widgets",
       nlohmann::json::array({
           {{"type", "metric"}, {"title", "负载"}, {"metric", "host.load1"}},
           {{"type", "text"}, {"title", "说明"}, {"text", "值班交接"}},
       })},
  };
  Dashboard dashboard;
  std::string error;
  ASSERT_TRUE(dashboards.upsert_json(body, 5000, &dashboard, &error)) << error;
  ASSERT_EQ(2u, dashboard.widgets.size());

  const nlohmann::json data = dashboards.data(dashboard, store, 1000000);
  ASSERT_EQ(2u, data["widgets"].size());
  EXPECT_EQ(3u, data["widgets"][0]["count"]);
  EXPECT_DOUBLE_EQ(3.0, data["widgets"][0]["last"].get<double>());
  EXPECT_EQ("值班交接", data["widgets"][1]["text"]);

  DashboardSnapshot snapshot;
  ASSERT_TRUE(dashboards.create_snapshot("dash-test", store, 2000, &snapshot,
                                         &error)) << error;
  ASSERT_EQ(1u, dashboards.snapshots().size());
  EXPECT_EQ("dash-test", snapshot.dashboard_id);
  EXPECT_EQ(2u, snapshot.data["widgets"].size());

  const std::string path = "/tmp/vmon-dashboards-test.json";
  ASSERT_TRUE(dashboards.save(path, &error)) << error;
  DashboardStore loaded;
  ASSERT_TRUE(loaded.load(path, &error)) << error;
  ASSERT_EQ(1u, loaded.list().size());
  ASSERT_EQ(1u, loaded.snapshots().size());
  DashboardSnapshot read;
  ASSERT_TRUE(loaded.get_snapshot(snapshot.id, &read));
  EXPECT_EQ("测试看板", read.name);
  std::remove(path.c_str());
}

TEST(DashboardStore, RejectsInvalidWidget) {
  DashboardStore dashboards;
  Dashboard dashboard;
  std::string error;
  nlohmann::json body = {
      {"name", "bad"},
      {"widgets", nlohmann::json::array({{{"type", "unknown"}}})},
  };
  EXPECT_FALSE(dashboards.upsert_json(body, 1000, &dashboard, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
