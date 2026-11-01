#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <vmon/vmon.h>

#include "command_runner.h"
#include "control_client.h"
#include "tests/test_util.h"

namespace {

using vmon::agent::CommandRunner;
using vmon::agent::control_socket_path;
using vmon::agent::send_control_command;

class ControlCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_FALSE(dir_.path().empty());
    vmon::Config config;
    config.service_name = "control-test";
    config.instance_id = "control-test-1";
    config.enable_transport = false;
    config.enable_control = true;
    config.control_dir = dir_.path();
    config.enable_crash_handler = false;
    ASSERT_TRUE(vmon::init(config));

    const std::string socket_path =
        control_socket_path(dir_.path(), static_cast<int32_t>(::getpid()));
    ASSERT_TRUE(vmon::test::wait_until([&socket_path]() {
      return ::access(socket_path.c_str(), F_OK) == 0;
    }));

    vmon::agent::AgentConfig agent_config;
    agent_config.control_dir = dir_.path();
    runner_.reset(new CommandRunner(agent_config, "node-a", "agent-a"));
  }

  void TearDown() override {
    runner_.reset();
    vmon::set_min_level(vmon::Level::kInfo);
    vmon::shutdown();
  }

  nlohmann::json Command(const std::string& type, int32_t pid,
                         const nlohmann::json& args = nlohmann::json::object()) {
    nlohmann::json command;
    command["type"] = type;
    command["pid"] = pid;
    command["args"] = args;
    return command;
  }

  vmon::test::TempDir dir_;
  std::unique_ptr<CommandRunner> runner_;
};

TEST_F(ControlCommandTest, SocketAllowsSameGroupAgent) {
  const std::string socket_path =
      control_socket_path(dir_.path(), static_cast<int32_t>(::getpid()));
  struct stat st;
  ASSERT_EQ(0, ::stat(socket_path.c_str(), &st));
  // 业务用户与 Agent 分属不同用户时，必须依赖同组权限才能连接。
  EXPECT_EQ(0660, st.st_mode & 0777);
}

TEST_F(ControlCommandTest, PingReturnsSdkPid) {
  std::string result;
  std::string error;
  ASSERT_TRUE(runner_->execute(Command("ping", static_cast<int32_t>(::getpid())),
                               &result, &error))
      << error;
  const nlohmann::json reply = nlohmann::json::parse(result);
  EXPECT_TRUE(reply["ok"].get<bool>());
  EXPECT_EQ(static_cast<int32_t>(::getpid()), reply["pid"].get<int32_t>());
}

TEST_F(ControlCommandTest, GetStatsReturnsCounters) {
  std::string result;
  std::string error;
  ASSERT_TRUE(
      runner_->execute(Command("get_stats", static_cast<int32_t>(::getpid())),
                       &result, &error))
      << error;
  const nlohmann::json reply = nlohmann::json::parse(result);
  EXPECT_TRUE(reply["ok"].get<bool>());
  EXPECT_TRUE(reply.contains("sent"));
  EXPECT_TRUE(reply.contains("dropped"));
}

TEST_F(ControlCommandTest, SetLogLevelWithTtlReverts) {
  vmon::set_min_level(vmon::Level::kInfo);
  std::string result;
  std::string error;
  ASSERT_TRUE(runner_->execute(
      Command("set_log_level", static_cast<int32_t>(::getpid()),
              {{"level", "debug"}, {"ttl_ms", 200}}),
      &result, &error))
      << error;
  EXPECT_EQ(vmon::Level::kDebug, vmon::min_level());
  EXPECT_NE(std::string::npos, result.find("\"debug\""));

  EXPECT_TRUE(vmon::test::wait_until(
      []() { return vmon::min_level() == vmon::Level::kInfo; }, 3000));
}

TEST_F(ControlCommandTest, SetLogLevelWithoutTtlSticks) {
  std::string result;
  std::string error;
  ASSERT_TRUE(runner_->execute(
      Command("set_log_level", static_cast<int32_t>(::getpid()),
              {{"level", "warn"}, {"ttl_ms", 0}}),
      &result, &error))
      << error;
  EXPECT_EQ(vmon::Level::kWarn, vmon::min_level());
}

TEST_F(ControlCommandTest, MissingPidFailsWithoutCrash) {
  std::string result;
  std::string error;
  EXPECT_FALSE(runner_->execute(Command("ping", 0), &result, &error));
  EXPECT_NE(std::string::npos, error.find("pid"));
}

TEST_F(ControlCommandTest, UnsupportedTypeFails) {
  std::string result;
  std::string error;
  EXPECT_FALSE(runner_->execute(Command("reboot", static_cast<int32_t>(::getpid())),
                                &result, &error));
  EXPECT_NE(std::string::npos, error.find("不支持"));
}

TEST_F(ControlCommandTest, DiagnoseRejectsNonWhitelistedProcFile) {
  std::string result;
  std::string error;
  EXPECT_FALSE(runner_->execute(
      Command("diagnose", static_cast<int32_t>(::getpid()), {{"op", "passwd"}}),
      &result, &error));
  EXPECT_NE(std::string::npos, error.find("白名单"));
}

TEST(ControlClient, ReportsConnectionFailure) {
  std::string response;
  std::string error;
  EXPECT_FALSE(send_control_command("/tmp/vmon-no-such-socket.sock",
                                    "{\"cmd\":\"ping\"}", 100, &response,
                                    &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
