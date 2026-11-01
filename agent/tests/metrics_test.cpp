#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "metrics.h"

namespace {

using vmon::agent::host_metric_events;
using vmon::agent::process_metric_event;
using vmon::agent::HostInfo;
using vmon::agent::ProcessInfo;

const vmon::common::Event* find_metric(
    const std::vector<vmon::common::Event>& events, const std::string& name) {
  for (const auto& event : events) {
    if (event.name == name) return &event;
  }
  return nullptr;
}

TEST(HostMetrics, EmitsStableHostSeries) {
  HostInfo host;
  host.node = "node-a";
  host.load1 = 0.5;
  host.load5 = 0.4;
  host.load15 = 0.3;
  host.mem_total_kb = 2 * 1024 * 1024;       // 2 GiB
  host.mem_available_kb = 512 * 1024;       // 512 MiB
  host.uptime_seconds = 3600;

  const auto events = host_metric_events(host, 7, "node-a", "agent-1");
  ASSERT_EQ(events.size(), 9u);
  for (const auto& event : events) {
    EXPECT_EQ(event.type, "metric");
    EXPECT_EQ(event.node, "node-a");
    EXPECT_EQ(event.attrs.at("agent"), "agent-1");
    EXPECT_FALSE(event.attrs.at("value").empty());
    EXPECT_GT(event.time_ns, 0u);
  }

  const auto* total = find_metric(events, "host.mem_total_bytes");
  ASSERT_NE(total, nullptr);
  EXPECT_EQ(total->attrs.at("value"), "2147483648");

  const auto* used = find_metric(events, "host.mem_used_bytes");
  ASSERT_NE(used, nullptr);
  EXPECT_EQ(used->attrs.at("value"), "1610612736");

  const auto* ratio = find_metric(events, "host.mem_used_ratio");
  ASSERT_NE(ratio, nullptr);
  EXPECT_EQ(ratio->attrs.at("value"), "0.75");

  const auto* count = find_metric(events, "host.process_count");
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->attrs.at("value"), "7");
}

TEST(HostMetrics, HandlesEmptyHostInfo) {
  HostInfo host;
  const auto events = host_metric_events(host, 0, "node-b", "agent");
  ASSERT_FALSE(events.empty());
  for (const auto& event : events) {
    EXPECT_EQ(event.node, "node-b");
    EXPECT_NE(event.attrs.at("value"), "nan");
    EXPECT_NE(event.attrs.at("value"), "inf");
  }
  const auto* ratio = find_metric(events, "host.mem_used_ratio");
  ASSERT_NE(ratio, nullptr);
  EXPECT_EQ(ratio->attrs.at("value"), "0");
}

TEST(ProcessMetrics, CarriesProcessIdentity) {
  ProcessInfo info;
  info.pid = 4321;
  info.name = "order-service";
  info.rss_kb = 1024;
  info.threads = 8;

  const auto event = process_metric_event(info, "node-a", "agent-1");
  EXPECT_EQ(event.type, "metric");
  EXPECT_EQ(event.name, "process.rss_bytes");
  EXPECT_EQ(event.node, "node-a");
  EXPECT_EQ(event.pid, 4321);
  EXPECT_EQ(event.attrs.at("process"), "order-service");
  EXPECT_EQ(event.attrs.at("pid"), "4321");
  EXPECT_EQ(event.attrs.at("threads"), "8");
  EXPECT_EQ(event.attrs.at("value"), "1048576");
}

}  // namespace
