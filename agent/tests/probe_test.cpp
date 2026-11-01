#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "probe.h"

namespace {

using vmon::agent::ProbeConfig;
using vmon::agent::ProbeRunner;

TEST(Probe, ParsesSpec) {
  ProbeConfig probe;
  std::string error;
  ASSERT_TRUE(vmon::agent::parse_probe_spec(
      "api=http://127.0.0.1:9000/health;interval_ms=5000;timeout_ms=1000;status=204",
      &probe, &error)) << error;
  EXPECT_EQ("api", probe.name);
  EXPECT_EQ("http://127.0.0.1:9000/health", probe.url);
  EXPECT_EQ(5000u, probe.interval_ms);
  EXPECT_EQ(1000u, probe.timeout_ms);
  EXPECT_EQ(204, probe.expected_status);

  EXPECT_FALSE(vmon::agent::parse_probe_spec("bad", &probe, &error));
  EXPECT_FALSE(vmon::agent::parse_probe_spec("x=https://example.com", &probe,
                                             &error));
}

TEST(Probe, ReportsUpAndDuration) {
  httplib::Server server;
  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.status = 200;
    res.set_content("ok", "text/plain");
  });
  const int port = server.bind_to_any_port("127.0.0.1");
  ASSERT_GT(port, 0);
  std::thread listener([&]() { server.listen_after_bind(); });

  ProbeConfig probe;
  probe.name = "api";
  probe.url = "http://127.0.0.1:" + std::to_string(port) + "/health";
  probe.timeout_ms = 2000;
  ProbeRunner runner("node-a", "agent-a", {probe}, nullptr);
  const auto events = runner.check(probe, 1000);

  server.stop();
  if (listener.joinable()) listener.join();

  ASSERT_EQ(3u, events.size());
  EXPECT_EQ("probe.up", events[0].name);
  EXPECT_EQ("1.000000", events[0].attrs.at("value"));
  EXPECT_EQ("200", events[0].attrs.at("status"));
  EXPECT_EQ("probe.duration_ms", events[1].name);
  EXPECT_GE(std::stod(events[1].attrs.at("value")), 0.0);
  EXPECT_EQ("probe.status_code", events[2].name);
}

TEST(Probe, ReportsDownAndLogOnFailure) {
  // 未监听的端口应产生 up=0、状态码 0 与一条错误日志。
  ProbeConfig probe;
  probe.name = "missing";
  probe.url = "http://127.0.0.1:1/health";
  probe.timeout_ms = 300;
  ProbeRunner runner("node-a", "agent-a", {probe}, nullptr);
  const auto events = runner.check(probe, 2000);
  ASSERT_EQ(4u, events.size());
  EXPECT_EQ("0.000000", events[0].attrs.at("value"));
  EXPECT_EQ("log", events[3].type);
  EXPECT_EQ("error", events[3].level);
}

}  // namespace
