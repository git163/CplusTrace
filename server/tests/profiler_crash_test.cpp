#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "crash.h"
#include "profiler.h"

namespace {

using vmon::server::CrashRecord;
using vmon::server::CrashStore;
using vmon::server::ProfileRecord;
using vmon::server::ProfileStore;

const char* kFolded =
    "main;foo;bar 10\n"
    "main;foo;baz 5\n"
    "main;foo 2\n";

TEST(Profiler, ParsesAndAggregatesFoldedStacks) {
  const auto folded = ProfileStore::parse_folded(kFolded);
  ASSERT_EQ(3u, folded.size());
  EXPECT_EQ("main;foo;bar", folded[0].first);
  EXPECT_EQ(10u, folded[0].second);
}

TEST(Profiler, ParseIgnoresCommentsAndInvalidLines) {
  const auto folded = ProfileStore::parse_folded(
      "# comment\n\nbadline\nmain;x 3\nmain;y zero\nmain;z 0\n");
  ASSERT_EQ(1u, folded.size());
  EXPECT_EQ("main;x", folded[0].first);
}

TEST(Profiler, TopSelfAndInclusive) {
  ProfileStore store;
  const std::string id = store.add(kFolded, "order", "node-a", "order-1",
                                   "cpu", 1000);
  ASSERT_FALSE(id.empty());

  ProfileRecord record;
  ASSERT_TRUE(store.get(id, &record));
  EXPECT_EQ(17u, record.total_samples);

  const auto self = ProfileStore::top_self(record, 10);
  ASSERT_EQ(3u, self.size());
  EXPECT_EQ("bar", self[0].first);
  EXPECT_EQ(10u, self[0].second);
  EXPECT_EQ("baz", self[1].first);

  const auto inclusive = ProfileStore::top_inclusive(record, 10);
  ASSERT_TRUE(inclusive.size() >= 4);
  // main 与 foo 的包含样本数相同（17），不假设同值时的先后顺序。
  uint64_t main_value = 0;
  for (const auto& entry : inclusive) {
    if (entry.first == "main") main_value = entry.second;
  }
  EXPECT_EQ(17u, main_value);
  EXPECT_EQ(17u, inclusive[0].second);
}

TEST(Profiler, FlamegraphBuildsTree) {
  ProfileStore store;
  const std::string id = store.add(kFolded, "order", "node-a", "", "cpu", 1);
  ProfileRecord record;
  ASSERT_TRUE(store.get(id, &record));

  const nlohmann::json flame = ProfileStore::flamegraph(record);
  EXPECT_EQ("all", flame["name"]);
  EXPECT_EQ(17u, flame["value"].get<uint64_t>());
  ASSERT_TRUE(flame.contains("children"));
  ASSERT_EQ(1u, flame["children"].size());
  const nlohmann::json& main_node = flame["children"][0];
  EXPECT_EQ("main", main_node["name"]);
  EXPECT_EQ(17u, main_node["value"].get<uint64_t>());
}

TEST(Profiler, EmptyProfileRejectedAndRecordsBounded) {
  ProfileStore store(2);
  EXPECT_TRUE(store.add("", "svc", "node", "", "cpu", 1).empty());
  EXPECT_EQ(0u, store.add("# only comment\n", "svc", "node", "", "cpu", 1).size());

  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(store.add("main 1\n", "svc", "node", "", "cpu",
                           static_cast<uint64_t>(100 + i))
                     .empty());
  }
  EXPECT_EQ(2u, store.list(10).size());
}

TEST(Crash, FingerprintNormalizesFramePrefixesAndTopFrames) {
  const std::vector<std::string> frames = {"#0 0x1000 main",
                                           "0x1010 handler", "0x1020 run"};
  const std::vector<std::string> prefixed = {"0x1000 main", "0x1010 handler",
                                             "0x1020 run"};
  const std::string base = CrashStore::fingerprint_of("build-1", "SIGSEGV",
                                                      frames);
  EXPECT_EQ(base, CrashStore::fingerprint_of("build-1", "SIGSEGV", prefixed));
  EXPECT_NE(base, CrashStore::fingerprint_of("build-2", "SIGSEGV", frames));
  EXPECT_NE(base, CrashStore::fingerprint_of("build-1", "SIGABRT", frames));

  // 只取 Top 5 帧，第 6 帧差异不影响指纹。
  std::vector<std::string> deep = prefixed;
  deep.push_back("0x1030 a");
  deep.push_back("0x1040 b");
  deep.push_back("0x1050 c");
  std::vector<std::string> deep_changed = deep;
  deep_changed.back() = "0x9999 different";
  EXPECT_EQ(CrashStore::fingerprint_of("build-1", "SIGSEGV", deep),
            CrashStore::fingerprint_of("build-1", "SIGSEGV", deep_changed));
}

TEST(Crash, JsonIngestAggregatesByFingerprint) {
  CrashStore store;
  nlohmann::json report = {
      {"time_ns", 1000},
      {"node", "node-a"},
      {"service", "order"},
      {"instance", "order-1"},
      {"version", "v1"},
      {"pid", 4242},
      {"signal", "SIGSEGV"},
      {"reason", "segv"},
      {"build_id", "build-1"},
      {"frames", {"0x1000 main", "0x1010 handler"}}};

  const std::string id = store.add_json(report, "node-hint");
  ASSERT_FALSE(id.empty());

  report["time_ns"] = 2000;
  EXPECT_EQ(id, store.add_json(report, "node-hint"));

  const auto list = store.list(10);
  ASSERT_EQ(1u, list.size());
  EXPECT_EQ(2u, list[0].count);
  EXPECT_EQ(1000u, list[0].first_ns);
  EXPECT_EQ(2000u, list[0].last_ns);
  EXPECT_EQ("order", list[0].service);
  EXPECT_EQ("SIGSEGV", list[0].signal);
  EXPECT_EQ("build-1", list[0].build_id);
  ASSERT_EQ(2u, list[0].frames.size());
  EXPECT_EQ("0x1000", list[0].frames[0].address);
  EXPECT_EQ("main", list[0].frames[0].function);
  EXPECT_FALSE(list[0].frames[0].resolved);

  CrashRecord fetched;
  ASSERT_TRUE(store.get(id, &fetched));
  EXPECT_EQ(2u, fetched.count);
  EXPECT_FALSE(store.get("missing", &fetched));
}

TEST(Crash, EventIngestUsesAttributes) {
  CrashStore store;
  vmon::common::Event event;
  event.type = "crash";
  event.time_ns = 3000;
  event.node = "node-b";
  event.service = "pay";
  event.pid = 77;
  event.name = "SIGABRT";
  event.msg = "assert failed";
  event.attrs["build_id"] = "build-pay";
  event.attrs["frames"] = "0x2000 begin\n0x2010 work";

  const std::string id = store.add_event(event, "node-hint");
  CrashRecord record;
  ASSERT_TRUE(store.get(id, &record));
  EXPECT_EQ("node-b", record.node);
  EXPECT_EQ("pay", record.service);
  EXPECT_EQ("SIGABRT", record.signal);
  EXPECT_EQ("assert failed", record.reason);
  ASSERT_EQ(2u, record.raw_frames.size());
  EXPECT_EQ("0x2010", record.frames[1].address);
}

}  // namespace
