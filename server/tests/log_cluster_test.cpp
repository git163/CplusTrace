#include <gtest/gtest.h>

#include <string>

#include "log_cluster.h"

namespace {

using vmon::server::LogClusterInfo;
using vmon::server::LogClusterer;

TEST(LogCluster, TemplateNormalizesNumbersDurationsAndIds) {
  const std::string tmpl = LogClusterer::make_template(
      "order 12345 failed after 300ms request_id=req-9f2a");
  EXPECT_NE(std::string::npos, tmpl.find("<num>"));
  EXPECT_NE(std::string::npos, tmpl.find("<dur>"));
  // request_id 属于高基数字段，统一归一化，避免每个请求一个模板。
  EXPECT_NE(std::string::npos, tmpl.find("<var>"));
  EXPECT_EQ(std::string::npos, tmpl.find("12345"));
}

TEST(LogCluster, EmptyMessageHasStableTemplate) {
  EXPECT_EQ("<empty>", LogClusterer::make_template(""));
  EXPECT_EQ("<empty>", LogClusterer::make_template("   \t "));
}

TEST(LogCluster, SameShapeSharesFingerprintAndCounts) {
  LogClusterer clusterer;
  const LogClusterInfo first =
      clusterer.annotate("order 111 failed", "error", "order", "node-a",
                         "E42", 1000);
  const LogClusterInfo second =
      clusterer.annotate("order 222 failed", "error", "order", "node-b",
                         "E42", 2000);

  EXPECT_EQ(first.id, second.id);
  EXPECT_EQ(2u, second.count);
  EXPECT_EQ("order 111 failed", second.sample);
  EXPECT_EQ("order 222 failed", second.last_sample);
  EXPECT_EQ(1000u, second.first_ns);
  EXPECT_EQ(2000u, second.last_ns);
  ASSERT_EQ(2u, second.nodes.size());
  EXPECT_EQ(1u, clusterer.size());
}

TEST(LogCluster, LevelServiceAndErrorCodeIsolateFingerprints) {
  LogClusterer clusterer;
  const std::string base = clusterer.make_fingerprint("error", "order", "E42",
                                                      "boom <num>");
  EXPECT_NE(base, clusterer.make_fingerprint("warn", "order", "E42",
                                             "boom <num>"));
  EXPECT_NE(base, clusterer.make_fingerprint("error", "pay", "E42",
                                             "boom <num>"));
  EXPECT_NE(base, clusterer.make_fingerprint("error", "order", "E43",
                                             "boom <num>"));
}

TEST(LogCluster, ExtractKeysPullsBusinessFields) {
  const auto keys = LogClusterer::extract_keys(
      "request failed request_id=req-1 trace_id=abc123 task_id=t1 "
      "error_code=E42 retry=3 cost_ms=120 pid=9999");
  ASSERT_EQ(7u, keys.size());
  EXPECT_EQ("req-1", keys.at("request_id"));
  EXPECT_EQ("abc123", keys.at("trace_id"));
  EXPECT_EQ("t1", keys.at("task_id"));
  EXPECT_EQ("E42", keys.at("error_code"));
  EXPECT_EQ("3", keys.at("retry"));
  EXPECT_EQ("120", keys.at("cost_ms"));
  EXPECT_EQ("9999", keys.at("pid"));
}

TEST(LogCluster, ExtractKeysIgnoresUninterestingFields) {
  const auto keys =
      LogClusterer::extract_keys("session=abc user_name=alice status=ok");
  // session 不在白名单；user_name 也不在。
  EXPECT_EQ(1u, keys.count("status"));
  EXPECT_EQ(0u, keys.count("session"));
  EXPECT_EQ(0u, keys.count("user_name"));
}

TEST(LogCluster, MaxClustersEvictsOldest) {
  LogClusterer clusterer(2);
  clusterer.annotate("alpha happened", "info", "svc", "", "", 1);
  clusterer.annotate("beta happened", "info", "svc", "", "", 2);
  clusterer.annotate("gamma happened", "info", "svc", "", "", 3);
  EXPECT_EQ(2u, clusterer.size());

  const auto listed = clusterer.list(10);
  ASSERT_EQ(2u, listed.size());
  for (const auto& cluster : listed) {
    EXPECT_NE(std::string::npos, cluster.tmpl.find("happened"));
    EXPECT_NE(std::string::npos, std::string("beta gamma").find(
                                     cluster.tmpl.substr(0, 4)));
  }
}

TEST(LogCluster, TopSortsByCountThenRecency) {
  LogClusterer clusterer;
  clusterer.annotate("noisy <num>", "info", "svc", "", "", 1);
  clusterer.annotate("noisy <num>", "info", "svc", "", "", 2);
  clusterer.annotate("rare <num>", "warn", "svc", "", "", 3);

  const auto top = clusterer.top(10);
  ASSERT_EQ(2u, top.size());
  EXPECT_EQ(2u, top[0].count);
  EXPECT_EQ(1u, top[1].count);
}

}  // namespace
