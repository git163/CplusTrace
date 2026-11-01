#include <gtest/gtest.h>

#include <vector>

#include <vmon/vmon.h>

#include "core/thread_context.h"

namespace {

using vmon::detail::Event;
using vmon::detail::EventType;

std::vector<Event> drain_events() {
  std::vector<Event> events;
  Event event;
  while (vmon::detail::thread_context().ring.pop(event)) {
    events.push_back(event);
  }
  return events;
}

vmon::Config make_config() {
  vmon::Config config;
  config.service_name = "test-service";
  config.instance_id = "test-instance";
  config.enable_transport = false;  // 单测直接读线程本地事件环
  config.enable_control = false;
  return config;
}

class SpanTest : public ::testing::Test {
 protected:
  void SetUp() override { drain_events(); }
  void TearDown() override {
    vmon::shutdown();
    drain_events();
  }
};

TEST_F(SpanTest, NestedSpansShareTraceAndLinkParent) {
  const vmon::Config config = make_config();
  ASSERT_TRUE(vmon::init(config));

  {
    VMON_SPAN("outer");
    {
      VMON_SPAN("inner");
    }
  }

  const std::vector<Event> events = drain_events();
  ASSERT_EQ(4u, events.size());

  EXPECT_EQ(EventType::kSpanBegin, static_cast<EventType>(events[0].type));
  EXPECT_STREQ("outer", events[0].name);

  EXPECT_EQ(EventType::kSpanBegin, static_cast<EventType>(events[1].type));
  EXPECT_STREQ("inner", events[1].name);
  EXPECT_EQ(events[0].trace.hi, events[1].trace.hi);
  EXPECT_EQ(events[0].trace.lo, events[1].trace.lo);
  EXPECT_EQ(events[0].span, events[1].parent_span);
  EXPECT_NE(events[0].span, events[1].span);

  EXPECT_EQ(EventType::kSpanEnd, static_cast<EventType>(events[2].type));
  EXPECT_EQ(events[1].span, events[2].span);

  EXPECT_EQ(EventType::kSpanEnd, static_cast<EventType>(events[3].type));
  EXPECT_EQ(events[0].span, events[3].span);
}

TEST_F(SpanTest, DisabledSdkIsInert) {
  vmon::shutdown();

  const vmon::Span span = vmon::Span::create("noop", __FILE__, __LINE__);
  EXPECT_FALSE(span.active());
  EXPECT_TRUE(drain_events().empty());
}

TEST_F(SpanTest, InvalidConfigKeepsSdkDisabled) {
  vmon::Config config;  // service_name 为空
  EXPECT_FALSE(vmon::init(config));
  EXPECT_FALSE(vmon::enabled());
}

TEST_F(SpanTest, HeartbeatProducesEvent) {
  const vmon::Config config = make_config();
  ASSERT_TRUE(vmon::init(config));

  vmon::heartbeat();

  const std::vector<Event> events = drain_events();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(EventType::kHeartbeat, static_cast<EventType>(events[0].type));
}

TEST_F(SpanTest, SetErrorMarksSpanEndEvent) {
  const vmon::Config config = make_config();
  ASSERT_TRUE(vmon::init(config));

  {
    vmon::Span span = vmon::Span::create("bad", __FILE__, __LINE__);
    span.set_error("E_TEST");
    span.end();
  }

  const std::vector<Event> events = drain_events();
  ASSERT_EQ(2u, events.size());
  EXPECT_EQ(nullptr, events[0].error_code);
  ASSERT_NE(nullptr, events[1].error_code);
  EXPECT_STREQ("E_TEST", events[1].error_code);

  // 未激活的 span 上调用 set_error 不应崩溃或产生事件。
  vmon::Span inactive;
  inactive.set_error("E_IGNORED");
  EXPECT_TRUE(drain_events().empty());
}

TEST_F(SpanTest, SiblingSpanDoesNotInheritErrorCode) {
  const vmon::Config config = make_config();
  ASSERT_TRUE(vmon::init(config));

  {
    vmon::Span bad = vmon::Span::create("bad", __FILE__, __LINE__);
    bad.set_error("E_BAD");
    bad.end();
  }
  {
    // 同槽位复用的正常 Span 不能继承上一个 Span 的错误码。
    vmon::Span good = vmon::Span::create("good", __FILE__, __LINE__);
    good.end();
  }

  const std::vector<Event> events = drain_events();
  ASSERT_EQ(4u, events.size());
  ASSERT_NE(nullptr, events[1].error_code);
  EXPECT_STREQ("E_BAD", events[1].error_code);
  EXPECT_EQ(nullptr, events[2].error_code);
  EXPECT_EQ(nullptr, events[3].error_code);
}

TEST_F(SpanTest, DepthLimitDegradesToNoop) {
  const vmon::Config config = make_config();
  ASSERT_TRUE(vmon::init(config));

  // 超过 kMaxSpanDepth 的 span 不再产生事件，也不报错。
  std::vector<vmon::Span> spans;
  for (int i = 0; i < vmon::detail::kMaxSpanDepth + 5; ++i) {
    spans.push_back(vmon::Span::create("deep", __FILE__, __LINE__));
  }

  const std::vector<Event> events = drain_events();
  EXPECT_EQ(static_cast<std::size_t>(vmon::detail::kMaxSpanDepth), events.size());
}

}  // namespace
