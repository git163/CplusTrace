#include <gtest/gtest.h>

#include <sys/stat.h>

#include <nlohmann/json.hpp>

#include <string>

#include "crash.h"
#include "evidence.h"
#include "json_event.h"
#include "profiler.h"
#include "store.h"
#include "tests/test_util.h"

namespace {

using vmon::server::EvidenceBuilder;
using vmon::server::EvidenceOptions;
using vmon::server::EvidencePack;

vmon::common::Event MakeLog(const std::string& msg, uint64_t time_ns) {
  vmon::common::Event event;
  event.type = "log";
  event.time_ns = time_ns;
  event.node = "node-a";
  event.service = "order";
  event.instance = "order-1";
  event.level = "error";
  event.error_code = "E_DB";
  event.msg = msg;
  return event;
}

TEST(Evidence, RedactsIpEmailSecretAndHomePaths) {
  const std::string text =
      "peer=10.1.2.3 user alice@example.com token=secret123 "
      "password = \"hunter2\" file /Users/alice/app.log";
  const std::string redacted = EvidenceBuilder::redact(text, "salt-1");

  EXPECT_EQ(std::string::npos, redacted.find("10.1.2.3"));
  EXPECT_EQ(std::string::npos, redacted.find("alice@example.com"));
  EXPECT_EQ(std::string::npos, redacted.find("secret123"));
  EXPECT_EQ(std::string::npos, redacted.find("hunter2"));
  EXPECT_EQ(std::string::npos, redacted.find("/Users/alice"));
  EXPECT_NE(std::string::npos, redacted.find("<ip:"));
  EXPECT_NE(std::string::npos, redacted.find("<email:"));
  EXPECT_NE(std::string::npos, redacted.find("token=<redacted>"));
  EXPECT_NE(std::string::npos, redacted.find("password = <redacted>"));
  EXPECT_NE(std::string::npos, redacted.find("/Users/<user>"));
}

TEST(Evidence, RedactionIsDeterministicPerSalt) {
  const std::string first = EvidenceBuilder::redact("ip 192.168.1.9", "salt");
  const std::string second = EvidenceBuilder::redact("ip 192.168.1.9", "salt");
  const std::string other = EvidenceBuilder::redact("ip 192.168.1.9", "other");
  EXPECT_EQ(first, second);
  EXPECT_NE(first, other);
}

TEST(Evidence, RedactionLeavesOrdinaryTextUntouched) {
  const std::string text = "order 12345 succeeded in 30ms";
  EXPECT_EQ(text, EvidenceBuilder::redact(text, "salt"));
}

TEST(Evidence, BuildsSelfContainedPackWithChecksums) {
  vmon::test::TempDir temp("vmon-evidence");
  ASSERT_FALSE(temp.path().empty());

  vmon::server::Store store;
  vmon::server::CrashStore crashes;
  vmon::server::ProfileStore profiles;

  store.ingest(MakeLog("db timeout peer 10.9.8.7 request_id=req-1 token=abc",
                       900000000000ull),
               "node-a", "agent-a");
  crashes.add_json({{"time_ns", 900000000000ull},
                    {"service", "order"},
                    {"signal", "SIGSEGV"},
                    {"build_id", "build-1"},
                    {"frames", {"0x1000 main"}}},
                   "node-a");
  profiles.add("main;work 5\n", "order", "node-a", "order-1", "cpu",
               900000000000ull);

  EvidenceOptions options;
  options.incident = "unit";
  options.service = "order";
  options.window_start_ns = 800000000000ull;
  options.window_end_ns = 1000000000000ull;
  options.redact = true;
  options.salt = "salt-1";

  const EvidencePack pack =
      EvidenceBuilder::build(store, crashes, profiles, options, temp.path());
  ASSERT_TRUE(pack.ok()) << pack.error;
  EXPECT_FALSE(pack.id.empty());
  EXPECT_FALSE(pack.dir.empty());

  const std::string logs = vmon::test::read_file(pack.dir + "/logs.jsonl");
  EXPECT_FALSE(logs.empty());
  EXPECT_EQ(std::string::npos, logs.find("10.9.8.7"));
  EXPECT_EQ(std::string::npos, logs.find("token=abc"));
  EXPECT_NE(std::string::npos, logs.find("<ip:"));
  EXPECT_NE(std::string::npos, logs.find("token=<redacted>"));

  const std::string clues = vmon::test::read_file(pack.dir + "/log_clusters.json");
  EXPECT_NE(std::string::npos, clues.find("<ip:"));

  EXPECT_FALSE(
      vmon::test::read_file(pack.dir + "/hypotheses.md").empty());
  EXPECT_FALSE(
      vmon::test::read_file(pack.dir + "/profiles/folded-0.txt").empty());

  const std::string manifest_text =
      vmon::test::read_file(pack.dir + "/manifest.json");
  ASSERT_FALSE(manifest_text.empty());
  const nlohmann::json manifest = nlohmann::json::parse(manifest_text);
  EXPECT_EQ("vmon-evidence-1", manifest["schema_version"]);
  EXPECT_EQ(pack.id, manifest["pack_id"]);
  EXPECT_TRUE(manifest["redaction"]["enabled"].get<bool>());

  ASSERT_TRUE(manifest["files"].is_array());
  EXPECT_GE(manifest["files"].size(), 6u);
  for (const auto& file : manifest["files"]) {
    const std::string relative = file["path"].get<std::string>();
    const std::string path = pack.dir + "/" + relative;
    struct stat st;
    ASSERT_EQ(0, ::stat(path.c_str(), &st)) << relative;
    EXPECT_EQ(static_cast<uint64_t>(st.st_size),
              file["size"].get<uint64_t>());
    EXPECT_EQ(64u, file["sha256"].get<std::string>().size());
  }

  // 证据包目录名位于 base_dir 下，且 index.jsonl 可检索。
  EXPECT_NE(std::string::npos, pack.dir.find(temp.path() + "/evidence-unit-"));
  EXPECT_FALSE(vmon::test::read_file(pack.dir + "/index.jsonl").empty());
}

TEST(Evidence, RedactionCanBeDisabled) {
  vmon::test::TempDir temp("vmon-evidence-raw");
  vmon::server::Store store;
  vmon::server::CrashStore crashes;
  vmon::server::ProfileStore profiles;
  store.ingest(MakeLog("peer 10.9.8.7", 5000), "node-a", "agent-a");

  EvidenceOptions options;
  options.service = "order";
  options.window_start_ns = 1;
  options.window_end_ns = 1000000000000ull;
  options.redact = false;

  const EvidencePack pack =
      EvidenceBuilder::build(store, crashes, profiles, options, temp.path());
  ASSERT_TRUE(pack.ok()) << pack.error;
  EXPECT_NE(std::string::npos,
            vmon::test::read_file(pack.dir + "/logs.jsonl").find("10.9.8.7"));
}

}  // namespace
