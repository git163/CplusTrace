#include <gtest/gtest.h>

#include <string>

#include "rules.h"
#include "silence.h"
#include "store.h"

namespace {

using vmon::server::EventRecord;
using vmon::server::Rule;
using vmon::server::RuleEngine;
using vmon::server::Silence;
using vmon::server::SilenceStore;
using vmon::server::Store;

TEST(SilenceStore, MatchesFieldsAndWindow) {
  SilenceStore store;
  nlohmann::json body = {
      {"id", "silence-1"},
      {"name", "维护窗口"},
      {"service", "order"},
      {"rule_id", "default-error-log"},
      {"duration_minutes", 10},
      {"reason", "升级"},
  };
  const uint64_t now = 1000000000ull;
  Silence silence;
  std::string error;
  ASSERT_TRUE(store.upsert_json(body, now, &silence, &error)) << error;
  ASSERT_EQ(1u, store.list().size());

  EXPECT_TRUE(store.matches("default-error-log", "error_log", "order", "", "",
                            now + 1));
  EXPECT_FALSE(store.matches("default-error-log", "error_log", "payment", "",
                             "", now + 1));
  EXPECT_FALSE(store.matches("other-rule", "error_log", "order", "", "",
                             now + 1));
  EXPECT_FALSE(store.matches("default-error-log", "error_log", "order", "", "",
                            now + 11 * 60ull * 1000000000ull));

  EXPECT_TRUE(store.remove("silence-1"));
  EXPECT_FALSE(store.remove("silence-1"));
}

TEST(SilenceStore, RejectsInvalidWindow) {
  SilenceStore store;
  std::string error;
  nlohmann::json body = {
      {"name", "bad"}, {"start_ns", 100}, {"end_ns", 50}};
  EXPECT_FALSE(store.upsert_json(body, 100, nullptr, &error));
  EXPECT_FALSE(error.empty());
}

TEST(SilenceStore, SuppressesRuleEvents) {
  Store store;
  RuleEngine rules;
  SilenceStore silences;
  const uint64_t now = 2000000000ull;

  nlohmann::json body = {
      {"id", "silence-rule"},
      {"name", "静默错误日志"},
      {"rule_id", "error-rule"},
      {"service", "order"},
      {"start_ns", now - 1000},
      {"end_ns", now + 10ull * 1000000000ull},
  };
  Silence silence;
  std::string error;
  ASSERT_TRUE(silences.upsert_json(body, now, &silence, &error)) << error;

  Rule rule;
  rule.id = "error-rule";
  rule.name = "错误日志";
  rule.type = "error_log";
  rule.service = "order";
  rule.min_count = 1;
  rule.threshold = 1;
  rule.window_ns = 60ull * 1000000000ull;
  rule.cooldown_ns = 0;
  rules.add_rule(rule);
  rules.set_silence_matcher([&silences](const Rule& r, const std::string& svc,
                                        const std::string& node,
                                        const std::string& severity, uint64_t t) {
    return silences.matches(r.id, r.type, svc, node, severity, t);
  });

  vmon::common::Event event;
  event.type = "log";
  event.level = "error";
  event.service = "order";
  event.error_code = "E_DB";
  event.msg = "db failed";
  event.time_ns = now;
  rules.on_event(event, &store);
  EXPECT_TRUE(store.list_events(10).empty());

  // 静默过期后同一规则可以正常产生事件。
  SilenceStore expired;
  rules.set_silence_matcher([&expired](const Rule& r, const std::string& svc,
                                       const std::string& node,
                                       const std::string& severity, uint64_t t) {
    return expired.matches(r.id, r.type, svc, node, severity, t);
  });
  event.time_ns = now + 20ull * 1000000000ull;
  rules.on_event(event, &store);
  ASSERT_EQ(1u, store.list_events(10).size());
}

}  // namespace
