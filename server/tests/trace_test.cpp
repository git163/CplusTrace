#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "trace_builder.h"

namespace {

using vmon::server::ServiceEdge;
using vmon::server::SpanData;
using vmon::server::TraceData;
using vmon::server::TraceNode;
using vmon::server::TraceSummary;

SpanData MakeSpan(const std::string& span, const std::string& parent,
                  const std::string& service, const std::string& name,
                  uint64_t start, uint64_t end, bool ended = true) {
  SpanData data;
  data.trace = "trace-1";
  data.span = span;
  data.parent = parent;
  data.service = service;
  data.instance = service + "-1";
  data.node = "node-a";
  data.name = name;
  data.kind = "internal";
  data.start_ns = start;
  data.end_ns = end;
  data.ended = ended;
  return data;
}

// gateway(100) -> order client(80) -> order server(60) -> db(20)
//                     \-> cache client(30, error)
//                     \-> legacy weak edge(10)
TraceData MakeSampleTrace() {
  TraceData trace;
  trace.trace_id = "trace-1";
  trace.request_id = "req-1";
  trace.task_id = "task-1";
  trace.first_ns = 1000;
  trace.last_ns = 1100;
  trace.root_service = "gateway";
  trace.root_name = "GET /order";

  trace.spans["s-gw"] =
      MakeSpan("s-gw", "", "gateway", "GET /order", 1000, 1100);
  trace.spans["s-client"] = MakeSpan("s-client", "s-gw", "gateway",
                                     "OrderService/Create", 1010, 1090);
  trace.spans["s-server"] =
      MakeSpan("s-server", "s-client", "order", "CreateOrder", 1020, 1080);

  SpanData db = MakeSpan("s-db", "s-server", "db", "query", 1030, 1050);
  db.kind = "client";
  trace.spans["s-db"] = db;

  SpanData cache = MakeSpan("s-cache", "s-gw", "gateway", "Cache/Get", 1040,
                            1070);
  cache.status = "error";
  cache.error_code = "E_CACHE";
  trace.spans["s-cache"] = cache;

  SpanData legacy =
      MakeSpan("s-legacy", "s-gw", "order", "Legacy/Call", 1050, 1060);
  legacy.attrs["confidence"] = "weak";
  trace.spans["s-legacy"] = legacy;
  return trace;
}

TEST(TraceBuilder, SummaryCountsSpansServicesDepthAndErrors) {
  const TraceData trace = MakeSampleTrace();
  const TraceSummary summary = vmon::server::summarize_trace(trace);

  EXPECT_EQ("trace-1", summary.trace_id);
  EXPECT_EQ("req-1", summary.request_id);
  EXPECT_EQ("task-1", summary.task_id);
  EXPECT_EQ(6u, summary.span_count);
  EXPECT_EQ(3u, summary.service_count);
  EXPECT_EQ(4u, summary.depth);
  EXPECT_EQ(1000u, summary.start_ns);
  EXPECT_EQ(100u, summary.duration_ns);
  EXPECT_TRUE(summary.has_error);
  EXPECT_EQ("s-cache", summary.first_error_span);
  EXPECT_EQ("E_CACHE", summary.error_code);
}

TEST(TraceBuilder, BuildsTreeFromRoots) {
  const TraceData trace = MakeSampleTrace();
  std::vector<TraceNode> roots;
  vmon::server::build_trace_tree(trace, &roots);

  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ("s-gw", roots[0].span.span);
  ASSERT_EQ(3u, roots[0].children.size());
  EXPECT_EQ("s-client", roots[0].children[0].span.span);
  EXPECT_EQ("s-cache", roots[0].children[1].span.span);
  EXPECT_EQ("s-legacy", roots[0].children[2].span.span);
  ASSERT_EQ(1u, roots[0].children[0].children.size());
  EXPECT_EQ("s-server", roots[0].children[0].children[0].span.span);
}

TEST(TraceBuilder, CriticalPathFollowsLongestSubtree) {
  const TraceData trace = MakeSampleTrace();
  const std::vector<SpanData> path = vmon::server::critical_path(trace);

  ASSERT_EQ(4u, path.size());
  EXPECT_EQ("s-gw", path[0].span);
  EXPECT_EQ("s-client", path[1].span);
  EXPECT_EQ("s-server", path[2].span);
  EXPECT_EQ("s-db", path[3].span);
}

TEST(TraceBuilder, OrphanSpanBecomesRoot) {
  TraceData trace;
  trace.trace_id = "trace-orphan";
  trace.first_ns = 10;
  trace.spans["child"] = MakeSpan("child", "missing-parent", "svc", "work", 10,
                                  20);
  trace.spans["root"] = MakeSpan("root", "", "svc", "root", 5, 25);

  std::vector<TraceNode> roots;
  vmon::server::build_trace_tree(trace, &roots);
  ASSERT_EQ(2u, roots.size());
  EXPECT_EQ("root", roots[0].span.span);
  EXPECT_EQ("child", roots[1].span.span);
}

TEST(TraceBuilder, CycleDoesNotRecurseForever) {
  TraceData trace;
  trace.trace_id = "trace-cycle";
  trace.first_ns = 1;
  trace.spans["a"] = MakeSpan("a", "b", "svc", "a", 1, 2);
  trace.spans["b"] = MakeSpan("b", "a", "svc", "b", 1, 2);

  std::vector<TraceNode> roots;
  // 环中没有根节点，结果为空，但不能栈溢出。
  vmon::server::build_trace_tree(trace, &roots);
  EXPECT_TRUE(roots.empty());
}

TEST(TraceBuilder, TopologySplitsStrongAndWeakEdges) {
  const std::vector<TraceData> traces(1, MakeSampleTrace());
  const std::vector<ServiceEdge> edges = vmon::server::build_topology(traces);

  ASSERT_EQ(3u, edges.size());
  bool found_strong_order = false;
  bool found_weak_order = false;
  bool found_db = false;
  for (const ServiceEdge& edge : edges) {
    // 边的 method 取被调用方（子 Span）的方法名。
    if (edge.caller == "gateway" && edge.callee == "order" &&
        edge.method == "CreateOrder") {
      found_strong_order = true;
      EXPECT_EQ("strong", edge.confidence);
      EXPECT_EQ(1u, edge.count);
      EXPECT_EQ(60u, edge.total_duration_ns);
    }
    if (edge.caller == "gateway" && edge.callee == "order" &&
        edge.method == "Legacy/Call") {
      found_weak_order = true;
      EXPECT_EQ("weak", edge.confidence);
    }
    if (edge.caller == "order" && edge.callee == "db") {
      found_db = true;
      EXPECT_EQ("strong", edge.confidence);
    }
  }
  EXPECT_TRUE(found_strong_order);
  EXPECT_TRUE(found_weak_order);
  EXPECT_TRUE(found_db);
}

TEST(TraceBuilder, AnalyzePicksSlowestAndFirstError) {
  const TraceData trace = MakeSampleTrace();
  const auto analysis = vmon::server::analyze_trace(trace);

  EXPECT_TRUE(analysis.has_error);
  EXPECT_EQ("s-cache", analysis.first_error.span);
  // 测试里的时间单位是“抽象纳秒”，只看相对大小；根 Span 自身 100 最慢。
  EXPECT_EQ("s-gw", analysis.slowest_span.span);
  EXPECT_EQ(100u, analysis.slowest_span.duration_ns());
}

}  // namespace
