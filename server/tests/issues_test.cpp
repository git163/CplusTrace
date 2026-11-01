#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "crash.h"
#include "issues.h"
#include "json_event.h"
#include "store.h"

namespace {

using vmon::server::CrashStore;
using vmon::server::IssueQuery;
using vmon::server::IssueStore;
using vmon::server::Store;

vmon::common::Event MakeError(uint64_t time_ns, const std::string& request_id) {
  vmon::common::Event event;
  event.type = "log";
  event.time_ns = time_ns;
  event.level = "error";
  event.service = "order";
  event.instance = "order-1";
  event.node = "node-a";
  event.msg = "db timeout request_id=" + request_id;
  event.request_id = request_id;
  event.error_code = "E_DB";
  return event;
}

TEST(IssueStore, AggregatesErrorClustersAndLifecycle) {
  Store store;
  store.ingest(MakeError(1000, "R1"), "node-a", "agent-a");
  store.ingest(MakeError(2000, "R2"), "node-a", "agent-a");
  CrashStore crashes;

  IssueStore issues;
  IssueQuery query;
  const auto found = issues.list(store, crashes, query);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ("log", found[0].kind);
  EXPECT_EQ("order", found[0].service);
  EXPECT_EQ("E_DB", found[0].error_code);
  EXPECT_EQ(2u, found[0].count);
  EXPECT_EQ("R2", found[0].request_id);
  EXPECT_EQ("open", found[0].status);

  std::string error;
  ASSERT_TRUE(issues.update(found[0].id, "ack", "alice", "排查中", &error))
      << error;
  const std::string path = "/tmp/vmon-issues-test.json";
  ASSERT_TRUE(issues.save(path, &error)) << error;

  IssueStore loaded;
  ASSERT_TRUE(loaded.load(path, &error)) << error;
  query.status = "ack";
  const auto acked = loaded.list(store, crashes, query);
  ASSERT_EQ(1u, acked.size());
  EXPECT_EQ("alice", acked[0].assignee);
  EXPECT_EQ("排查中", acked[0].note);

  query.status = "resolved";
  EXPECT_TRUE(loaded.list(store, crashes, query).empty());
  std::remove(path.c_str());
}

TEST(IssueStore, RejectsInvalidStatus) {
  IssueStore issues;
  std::string error;
  EXPECT_FALSE(issues.update("log-x", "bad", "", "", &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
