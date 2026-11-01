#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "json_event.h"
#include "rules.h"
#include "store.h"

namespace {

using vmon::common::Event;
using vmon::server::Rule;
using vmon::server::RuleEngine;
using vmon::server::Store;

Event MakeLog(const std::string& level, const std::string& msg,
              const std::string& service, const std::string& error_code,
              uint64_t time_ns) {
  Event event;
  event.type = "log";
  event.time_ns = time_ns;
  event.service = service;
  event.instance = service + "-1";
  event.node = "node-a";
  event.level = level;
  event.msg = msg;
  event.error_code = error_code;
  return event;
}

Rule MakeRule(const std::string& type, const std::string& id) {
  Rule rule;
  rule.id = id;
  rule.name = id;
  rule.type = type;
  rule.threshold = 0;
  rule.min_count = 1;
  rule.window_ns = 60ull * 1000000000ull;
  rule.cooldown_ns = 300ull * 1000000000ull;
  rule.severity = "warning";
  return rule;
}

TEST(Rules, DefaultRulesCoverCoreScenarios) {
  const std::vector<Rule> rules = RuleEngine::default_rules();
  ASSERT_EQ(3u, rules.size());
  bool heartbeat = false;
  bool error_log = false;
  bool slow = false;
  for (const Rule& rule : rules) {
    if (rule.type == "heartbeat") heartbeat = true;
    if (rule.type == "error_log") error_log = true;
    if (rule.type == "slow_trace") slow = true;
    EXPECT_FALSE(rule.id.empty());
    EXPECT_FALSE(rule.name.empty());
  }
  EXPECT_TRUE(heartbeat);
  EXPECT_TRUE(error_log);
  EXPECT_TRUE(slow);
}

TEST(Rules, ErrorLogBurstFiresOnceAfterMinCount) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("error_log", "burst");
  rule.min_count = 3;
  rule.threshold = 3;
  engine.set_rules({rule});

  for (int i = 0; i < 5; ++i) {
    const Event event = MakeLog("error", "db failed", "order", "E_DB",
                                1000 + static_cast<uint64_t>(i));
    engine.on_event(event, &store);
  }

  const auto events = store.list_events(10);
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("burst", events[0].title);
  EXPECT_EQ("alert", events[0].type);
  EXPECT_EQ("order", events[0].service);
  EXPECT_EQ("order|E_DB", events[0].fingerprint);
}

TEST(Rules, ErrorLogRuleIgnoresWarnAndInfo) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("error_log", "burst");
  rule.min_count = 1;
  engine.set_rules({rule});

  engine.on_event(MakeLog("warn", "slow", "order", "", 1), &store);
  engine.on_event(MakeLog("info", "ok", "order", "", 2), &store);
  EXPECT_TRUE(store.list_events(10).empty());
}

TEST(Rules, SlowTraceUsesSpanDurationAndHonorsCooldown) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("slow_trace", "slow");
  rule.threshold = 50.0;  // 毫秒
  rule.op = ">=";
  engine.set_rules({rule});

  Event begin;
  begin.type = "span_begin";
  begin.trace = "T1";
  begin.span = "S1";
  begin.service = "order";
  begin.name = "CreateOrder";
  begin.time_ns = 1000000000ull;
  store.ingest(begin, "node-a", "agent-a");

  Event end = begin;
  end.type = "span_end";
  end.time_ns = 1100000000ull;  // 100ms
  store.ingest(end, "node-a", "agent-a");

  engine.on_event(end, &store);
  ASSERT_EQ(1u, store.list_events(10).size());
  EXPECT_EQ("slow", store.list_events(10)[0].title);

  // 冷却期内同一 key 不重复告警。
  engine.on_event(end, &store);
  EXPECT_EQ(1u, store.list_events(10).size());
}

TEST(Rules, SlowTraceBelowThresholdDoesNotFire) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("slow_trace", "slow");
  rule.threshold = 50.0;
  engine.set_rules({rule});

  Event begin;
  begin.type = "span_begin";
  begin.trace = "T2";
  begin.span = "S1";
  begin.service = "order";
  begin.name = "Fast";
  begin.time_ns = 1000000000ull;
  store.ingest(begin, "node-a", "agent-a");

  Event end = begin;
  end.type = "span_end";
  end.time_ns = 1010000000ull;  // 10ms
  store.ingest(end, "node-a", "agent-a");
  engine.on_event(end, &store);
  EXPECT_TRUE(store.list_events(10).empty());
}

TEST(Rules, MetricThresholdFires) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("metric", "cpu-high");
  rule.metric = "cpu.usage";
  rule.op = ">=";
  rule.threshold = 90.0;
  engine.set_rules({rule});

  Event event;
  event.type = "metric";
  event.time_ns = 100;
  event.service = "order";
  event.node = "node-a";
  event.name = "cpu.usage";
  event.attrs["value"] = "95.5";
  engine.on_event(event, &store);

  ASSERT_EQ(1u, store.list_events(10).size());
  EXPECT_EQ("cpu-high", store.list_events(10)[0].title);
}

TEST(Rules, HeartbeatMissingFiresAndHonorsCooldown) {
  Store store;
  RuleEngine engine;
  Rule rule = MakeRule("heartbeat", "missing");
  rule.threshold = 1.0;  // 秒
  engine.set_rules({rule});

  const uint64_t base = 10000000000ull;
  Event heartbeat;
  heartbeat.type = "heartbeat";
  heartbeat.time_ns = base;
  heartbeat.service = "order";
  heartbeat.instance = "order-1";
  heartbeat.node = "node-a";
  store.ingest(heartbeat, "node-a", "agent-a");

  engine.evaluate(&store, base + 5000000000ull);  // 5 秒后
  ASSERT_EQ(1u, store.list_events(10).size());
  EXPECT_EQ("missing", store.list_events(10)[0].title);

  engine.evaluate(&store, base + 6000000000ull);
  EXPECT_EQ(1u, store.list_events(10).size());
}

TEST(Rules, DisabledAndScopedRulesDoNotFire) {
  Store store;
  RuleEngine engine;
  Rule disabled = MakeRule("error_log", "disabled");
  disabled.enabled = false;
  Rule scoped = MakeRule("error_log", "scoped");
  scoped.service = "pay";
  engine.set_rules({disabled, scoped});

  for (int i = 0; i < 5; ++i) {
    engine.on_event(MakeLog("error", "boom", "order", "E1",
                            static_cast<uint64_t>(i)),
                    &store);
  }
  EXPECT_TRUE(store.list_events(10).empty());
}

TEST(Rules, EventIdIsStableInsideWindow) {
  const std::string first = RuleEngine::make_event_id("r1", "svc|E1",
                                                      60000000000ull,
                                                      123000000000ull);
  const std::string same = RuleEngine::make_event_id("r1", "svc|E1",
                                                     60000000000ull,
                                                     123000000000ull);
  const std::string next = RuleEngine::make_event_id("r1", "svc|E1",
                                                     60000000000ull,
                                                     183000000000ull);
  EXPECT_EQ(first, same);
  EXPECT_NE(first, next);
}

}  // namespace
