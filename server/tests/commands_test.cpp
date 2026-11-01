#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "commands.h"

namespace {

using vmon::server::CommandRecord;
using vmon::server::CommandStore;

TEST(Commands, PendingCommandsAreClaimedOnce) {
  CommandStore store;
  const std::string id =
      store.enqueue("node-a", "agent-a", 1234, "set_log_level",
                    {{"level", "debug"}, {"ttl_ms", 1000}}, 60000000000ull);
  ASSERT_FALSE(id.empty());

  const auto first = store.pending_for("node-a", "agent-a", 10);
  ASSERT_EQ(1u, first.size());
  EXPECT_EQ(id, first[0].id);
  EXPECT_EQ(1234, first[0].pid);
  EXPECT_EQ("running", first[0].status);
  EXPECT_EQ("debug", first[0].args["level"]);

  // 已被认领，不能重复下发。
  EXPECT_TRUE(store.pending_for("node-a", "agent-a", 10).empty());
}

TEST(Commands, PendingFiltersByNodeAndAgent) {
  CommandStore store;
  const std::string id1 =
      store.enqueue("node-a", "agent-a", 1, "ping", nlohmann::json::object(),
                    60000000000ull);
  const std::string id2 =
      store.enqueue("node-b", "agent-b", 2, "ping", nlohmann::json::object(),
                    60000000000ull);

  const auto node_b = store.pending_for("node-b", "", 10);
  ASSERT_EQ(1u, node_b.size());
  EXPECT_EQ(id2, node_b[0].id);

  const auto agent_a = store.pending_for("", "agent-a", 10);
  ASSERT_EQ(1u, agent_a.size());
  EXPECT_EQ(id1, agent_a[0].id);
}

TEST(Commands, ExpiredCommandIsNotDelivered) {
  CommandStore store;
  const std::string id =
      store.enqueue("node-a", "agent-a", 1, "ping", nlohmann::json::object(),
                    1ull);  // 1ns 后过期
  EXPECT_TRUE(store.pending_for("node-a", "agent-a", 10).empty());
  CommandRecord record;
  ASSERT_TRUE(store.get(id, &record));
  EXPECT_EQ("expired", record.status);
}

TEST(Commands, CompleteStoresResultAndError) {
  CommandStore store;
  const std::string id =
      store.enqueue("node-a", "agent-a", 1, "diagnose",
                    {{"op", "fds"}}, 60000000000ull);
  ASSERT_TRUE(store.pending_for("node-a", "agent-a", 10).size() == 1);

  ASSERT_TRUE(store.complete(id, "done", "{\"ok\":true}", ""));
  CommandRecord record;
  ASSERT_TRUE(store.get(id, &record));
  EXPECT_EQ("done", record.status);
  EXPECT_EQ("{\"ok\":true}", record.result);
  EXPECT_GT(record.completed_ns, 0u);

  ASSERT_TRUE(store.complete(id, "failed", "", "timeout"));
  ASSERT_TRUE(store.get(id, &record));
  EXPECT_EQ("failed", record.status);
  EXPECT_EQ("timeout", record.error);

  EXPECT_FALSE(store.complete("missing", "done", "", ""));
}

TEST(Commands, ListContainsAllCommands) {
  CommandStore store;
  const std::string first =
      store.enqueue("node-a", "agent-a", 1, "ping", nlohmann::json::object(),
                    60000000000ull);
  const std::string second =
      store.enqueue("node-a", "agent-a", 2, "ping", nlohmann::json::object(),
                    60000000000ull);
  const auto list = store.list(10);
  ASSERT_EQ(2u, list.size());
  const std::string ids = list[0].id + "|" + list[1].id;
  EXPECT_NE(std::string::npos, ids.find(first));
  EXPECT_NE(std::string::npos, ids.find(second));
  EXPECT_EQ(2u, store.size());
}

}  // namespace
