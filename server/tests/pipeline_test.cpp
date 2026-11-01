#include <gtest/gtest.h>

#include <string>

#include "pipeline.h"

namespace {

using vmon::server::Pipeline;
using vmon::server::PipelineConfig;
using vmon::server::PipelineRule;

TEST(Pipeline, DefaultRulesRedactSensitiveFields) {
  PipelineConfig config;
  config.enabled = true;
  Pipeline pipeline;
  std::string error;
  ASSERT_TRUE(pipeline.configure(config, &error)) << error;
  ASSERT_TRUE(pipeline.enabled());

  vmon::common::Event event;
  event.msg =
      "login password=hunter2 email=alice@example.com ip=10.20.30.40 "
      "phone=13800138000";
  event.name = "user alice@example.com";
  event.file = "/home/alice/app/main.cpp";
  event.attrs["detail"] = "token=abc123";
  event.attrs["note"] = "from 10.20.30.40";

  EXPECT_TRUE(pipeline.process(&event));
  EXPECT_EQ(std::string::npos, event.msg.find("hunter2"));
  EXPECT_EQ(std::string::npos, event.msg.find("alice@example.com"));
  EXPECT_EQ(std::string::npos, event.msg.find("10.20.30.40"));
  EXPECT_EQ(std::string::npos, event.msg.find("13800138000"));
  EXPECT_NE(std::string::npos, event.msg.find("password=<redacted>"));
  EXPECT_NE(std::string::npos, event.msg.find("<email>"));
  EXPECT_NE(std::string::npos, event.msg.find("<ip>"));
  EXPECT_NE(std::string::npos, event.msg.find("<phone>"));

  EXPECT_EQ(std::string::npos, event.name.find("alice@example.com"));
  EXPECT_NE(std::string::npos, event.name.find("<email>"));
  EXPECT_EQ(std::string::npos, event.attrs["detail"].find("abc123"));
  EXPECT_EQ(std::string::npos, event.attrs["note"].find("10.20.30.40"));
  EXPECT_EQ(1u, pipeline.redacted_events());
}

TEST(Pipeline, DisabledLeavesEventUntouched) {
  PipelineConfig config;
  config.enabled = false;
  Pipeline pipeline;
  std::string error;
  ASSERT_TRUE(pipeline.configure(config, &error));

  vmon::common::Event event;
  event.msg = "password=plain";
  EXPECT_FALSE(pipeline.process(&event));
  EXPECT_EQ("password=plain", event.msg);
  EXPECT_EQ(0u, pipeline.redacted_events());
}

TEST(Pipeline, CustomRuleAndInvalidRegex) {
  PipelineConfig config;
  config.enabled = true;
  config.use_defaults = false;
  PipelineRule rule;
  rule.name = "order";
  rule.pattern = R"(order-[0-9]+)";
  rule.replace = "order-<id>";
  config.rules.push_back(rule);

  Pipeline pipeline;
  std::string error;
  ASSERT_TRUE(pipeline.configure(config, &error));
  vmon::common::Event event;
  event.msg = "created order-12345";
  EXPECT_TRUE(pipeline.process(&event));
  EXPECT_EQ("created order-<id>", event.msg);

  PipelineRule bad;
  bad.name = "bad";
  bad.pattern = "([";
  config.rules.clear();
  config.rules.push_back(bad);
  std::string bad_error;
  EXPECT_FALSE(pipeline.configure(config, &bad_error));
  EXPECT_FALSE(bad_error.empty());
}

}  // namespace
