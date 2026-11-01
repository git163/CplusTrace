#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <vmon/vmon.h>

#include "adapters/grpc/propagation.h"
#include "adapters/grpc/streaming.h"
#include "core/event.h"
#include "core/thread_context.h"

namespace {

using vmon::adapters::Traceparent;

std::vector<vmon::detail::Event> DrainEvents() {
  std::vector<vmon::detail::Event> events;
  vmon::detail::Event event;
  while (vmon::detail::thread_context().ring.pop(event)) {
    events.push_back(event);
  }
  return events;
}

TEST(Propagation, TraceparentRoundTrip) {
  const std::string header = vmon::adapters::make_traceparent(
      0x0123456789abcdefull, 0xfedcba9876543210ull, 0x1111222233334444ull,
      true);
  EXPECT_EQ(55u, header.size());
  EXPECT_EQ("00-0123456789abcdeffedcba9876543210-1111222233334444-01", header);
  EXPECT_STREQ("traceparent", vmon::adapters::kHeaderTraceparent);
  EXPECT_STREQ("x-request-id", vmon::adapters::kHeaderRequestId);
  EXPECT_STREQ("x-task-id", vmon::adapters::kHeaderTaskId);

  Traceparent parsed;
  ASSERT_TRUE(vmon::adapters::parse_traceparent(header, &parsed));
  EXPECT_TRUE(parsed.valid);
  EXPECT_TRUE(parsed.sampled);
  EXPECT_EQ(0x0123456789abcdefull, parsed.trace_hi);
  EXPECT_EQ(0xfedcba9876543210ull, parsed.trace_lo);
  EXPECT_EQ(0x1111222233334444ull, parsed.span);
}

TEST(Propagation, PublicHelperRestoresParentContext) {
  const std::string header = vmon::adapters::make_traceparent(
      0x0123456789abcdefull, 0xfedcba9876543210ull, 0x1111222233334444ull,
      true);
  vmon::SpanContext context;
  ASSERT_TRUE(vmon::context_from_traceparent(header.c_str(), &context));
  EXPECT_TRUE(context.valid);
  EXPECT_EQ(0x0123456789abcdefull, context.trace_hi);
  EXPECT_EQ(0xfedcba9876543210ull, context.trace_lo);
  EXPECT_EQ(0x1111222233334444ull, context.span);

  // 非法头只影响本次链路，不抛异常；调用方应新建 trace。
  EXPECT_FALSE(vmon::context_from_traceparent("bad", &context));
  EXPECT_FALSE(context.valid);
  EXPECT_FALSE(vmon::context_from_traceparent(nullptr, &context));
  EXPECT_FALSE(vmon::context_from_traceparent(header.c_str(), nullptr));
}

TEST(Propagation, StreamingSpanUsesParentAndMarksError) {
  vmon::Config config;
  config.service_name = "stream-test";
  config.instance_id = "stream-test-1";
  config.enable_transport = false;
  config.enable_control = false;
  ASSERT_TRUE(vmon::init(config));
  DrainEvents();

  vmon::SpanContext parent;
  const std::string header = vmon::adapters::make_traceparent(
      0x1ull, 0x2ull, 0x3ull, true);
  ASSERT_TRUE(vmon::context_from_traceparent(header.c_str(), &parent));

  vmon::adapters::StreamingSpan stream;
  stream.begin(parent, "Order/Stream");
  EXPECT_TRUE(stream.active());
  stream.message(128);
  stream.error("E_STREAM");
  stream.end();
  EXPECT_FALSE(stream.active());

  const std::vector<vmon::detail::Event> events = DrainEvents();
  ASSERT_GE(events.size(), 2u);  // begin + end（debug 可能被级别过滤）
  EXPECT_EQ(0x1ull, events.front().trace.hi);
  EXPECT_EQ(0x3ull, events.front().parent_span);
  ASSERT_NE(nullptr, events.back().error_code);
  EXPECT_STREQ("E_STREAM", events.back().error_code);
  vmon::shutdown();
  DrainEvents();
}

TEST(Propagation, NotSampledFlagIsParsed) {
  const std::string header = vmon::adapters::make_traceparent(
      0x1ull, 0x2ull, 0x3ull, false);
  EXPECT_EQ("00-00000000000000010000000000000002-0000000000000003-00", header);
  Traceparent parsed;
  ASSERT_TRUE(vmon::adapters::parse_traceparent(header, &parsed));
  EXPECT_FALSE(parsed.sampled);
}

TEST(Propagation, RejectsMalformedHeaders) {
  Traceparent parsed;
  EXPECT_FALSE(vmon::adapters::parse_traceparent("", &parsed));
  EXPECT_FALSE(vmon::adapters::parse_traceparent("bad", &parsed));
  EXPECT_FALSE(vmon::adapters::parse_traceparent(
      "01-0123456789abcdef0123456789abcdef-1111222233334444-01", &parsed));
  EXPECT_FALSE(vmon::adapters::parse_traceparent(
      "00-0123456789abcdef0123456789abcdef-1111222233334444", &parsed));
  EXPECT_FALSE(vmon::adapters::parse_traceparent(
      "00-zzzz456789abcdef0123456789abcdef-1111222233334444-01", &parsed));
  EXPECT_FALSE(vmon::adapters::parse_traceparent(
      "00-00000000000000000000000000000000-1111222233334444-01", &parsed));
}

TEST(RequestContext, RequestAndTaskIdsAreAttachedToEvents) {
  vmon::Config config;
  config.service_name = "request-test";
  config.instance_id = "request-test-1";
  config.enable_transport = false;
  config.enable_control = false;
  config.enable_crash_handler = false;
  ASSERT_TRUE(vmon::init(config));
  DrainEvents();

  const std::string request_id = "req-42";
  const std::string task_id = "task-7";
  vmon::set_request_id(request_id.c_str());
  vmon::set_task_id(task_id.c_str());
  EXPECT_STREQ(request_id.c_str(), vmon::request_id());
  EXPECT_STREQ(task_id.c_str(), vmon::task_id());

  {
    VMON_SPAN("handler");
    VMON_INFO("handling request %d", 42);
  }

  const std::vector<vmon::detail::Event> events = DrainEvents();
  ASSERT_EQ(3u, events.size());
  for (const vmon::detail::Event& event : events) {
    ASSERT_NE(nullptr, event.request_id);
    ASSERT_NE(nullptr, event.task_id);
    EXPECT_STREQ(request_id.c_str(), event.request_id);
    EXPECT_STREQ(task_id.c_str(), event.task_id);
  }

  vmon::set_request_id(nullptr);
  vmon::set_task_id(nullptr);
  EXPECT_EQ(nullptr, vmon::request_id());
  EXPECT_EQ(nullptr, vmon::task_id());

  vmon::shutdown();
  DrainEvents();
}

}  // namespace
