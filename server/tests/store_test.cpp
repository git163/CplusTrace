#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "json_event.h"
#include "store.h"

namespace {

using vmon::common::Event;
using vmon::server::LogQuery;
using vmon::server::Store;
using vmon::server::StoreConfig;
using vmon::server::TraceQuery;
using vmon::server::TraceSummary;

Event MakeEvent(const std::string& type, uint64_t time_ns) {
  Event event;
  event.type = type;
  event.time_ns = time_ns;
  event.node = "node-a";
  event.service = "gateway";
  event.instance = "gw-1";
  event.version = "v1";
  event.pid = 100;
  return event;
}

void IngestSampleTrace(Store* store) {
  Event begin = MakeEvent("span_begin", 1000);
  begin.trace = "T1";
  begin.span = "S1";
  begin.name = "GET /order";
  begin.attrs["span.kind"] = "server";
  begin.request_id = "R1";
  store->ingest(begin, "node-a", "agent-a");

  Event child = MakeEvent("span_begin", 1010);
  child.trace = "T1";
  child.span = "S2";
  child.parent = "S1";
  child.service = "order";
  child.instance = "order-1";
  child.name = "CreateOrder";
  store->ingest(child, "node-a", "agent-a");

  Event end_child = MakeEvent("span_end", 1090);
  end_child.trace = "T1";
  end_child.span = "S2";
  end_child.parent = "S1";
  end_child.service = "order";
  end_child.instance = "order-1";
  end_child.name = "CreateOrder";
  end_child.error_code = "E_ORDER";
  end_child.attrs["status"] = "error";
  store->ingest(end_child, "node-a", "agent-a");

  Event end = MakeEvent("span_end", 1100);
  end.trace = "T1";
  end.span = "S1";
  end.name = "GET /order";
  end.request_id = "R1";
  store->ingest(end, "node-a", "agent-a");
}

TEST(Store, AssemblesTraceAcrossBeginEndEvents) {
  StoreConfig config;
  config.dedup_max_per_window = 0;
  Store store(config);
  IngestSampleTrace(&store);

  vmon::server::TraceData trace;
  ASSERT_TRUE(store.get_trace("T1", &trace));
  EXPECT_EQ("R1", trace.request_id);
  ASSERT_EQ(2u, trace.spans.size());
  EXPECT_TRUE(trace.spans["S1"].ended);
  EXPECT_TRUE(trace.spans["S2"].ended);
  EXPECT_EQ(80u, trace.spans["S2"].duration_ns());
  EXPECT_EQ("error", trace.spans["S2"].status);
  EXPECT_EQ("E_ORDER", trace.spans["S2"].error_code);

  TraceQuery query;
  query.request_id = "R1";
  const std::vector<TraceSummary> summaries = store.query_traces(query);
  ASSERT_EQ(1u, summaries.size());
  EXPECT_EQ("T1", summaries[0].trace_id);
  EXPECT_TRUE(summaries[0].has_error);
  EXPECT_EQ(2u, summaries[0].span_count);
  EXPECT_EQ(2u, summaries[0].service_count);

  TraceQuery errors;
  errors.errors_only = true;
  EXPECT_EQ(1u, store.query_traces(errors).size());
  EXPECT_EQ(1u, store.traces_for_request("R1", 10).size());
}

TEST(Store, TraceTextSearchMatchesIdsAndServices) {
  StoreConfig config;
  config.dedup_max_per_window = 0;
  Store store(config);
  IngestSampleTrace(&store);

  // 统一搜索覆盖 trace_id / request_id / 服务名 / span 名，且忽略大小写。
  for (const char* text : {"T1", "r1", "ORDER", "createorder"}) {
    TraceQuery query;
    query.text = text;
    const std::vector<TraceSummary> found = store.query_traces(query);
    ASSERT_EQ(1u, found.size()) << text;
    EXPECT_EQ("T1", found[0].trace_id);
  }

  TraceQuery missing;
  missing.text = "not-exists";
  EXPECT_TRUE(store.query_traces(missing).empty());
}

TEST(Store, IndexesLogsByTraceRequestServiceAndFingerprint) {
  StoreConfig config;
  config.dedup_max_per_window = 0;
  Store store(config);

  Event info = MakeEvent("log", 2000);
  info.trace = "T1";
  info.level = "info";
  info.msg = "handled order 12345";
  store.ingest(info, "node-a", "agent-a");

  Event error = MakeEvent("log", 2100);
  error.level = "error";
  error.error_code = "E_DB";
  error.msg = "db timeout request_id=R1 trace_id=T1";
  store.ingest(error, "node-a", "agent-a");

  LogQuery by_trace;
  by_trace.trace = "T1";
  EXPECT_EQ(2u, store.query_logs(by_trace).size());

  // 没有显式 request_id 时，从消息里提取。
  LogQuery by_request;
  by_request.request_id = "R1";
  const auto request_logs = store.query_logs(by_request);
  ASSERT_EQ(1u, request_logs.size());
  EXPECT_EQ("E_DB", request_logs[0].error_code);
  EXPECT_EQ("T1", request_logs[0].trace);

  LogQuery errors;
  errors.errors_only = true;
  EXPECT_EQ(1u, store.query_logs(errors).size());

  LogQuery text;
  text.text = "timeout";
  EXPECT_EQ(1u, store.query_logs(text).size());

  const auto clusters = store.log_clusters(10);
  ASSERT_EQ(2u, clusters.size());
  EXPECT_GE(store.stats().logs_ingested, 2u);
}

TEST(Store, NoisePatternsAreCountedButNotStored) {
  StoreConfig config;
  config.noise_patterns.push_back("health check");
  Store store(config);

  Event noise = MakeEvent("log", 3000);
  noise.level = "info";
  noise.msg = "health check ok";
  store.ingest(noise, "node-a", "agent-a");

  Event real = MakeEvent("log", 3001);
  real.level = "info";
  real.msg = "order created";
  store.ingest(real, "node-a", "agent-a");

  EXPECT_EQ(1u, store.stats().logs_noise);
  EXPECT_EQ(1u, store.stats().logs_ingested);
  LogQuery query;
  query.text = "health check";
  EXPECT_TRUE(store.query_logs(query).empty());
}

TEST(Store, InfoSamplingKeepsErrors) {
  StoreConfig config;
  config.info_sample_permyriad = 0;  // 丢弃全部非错误日志
  config.dedup_max_per_window = 0;
  Store store(config);

  for (int i = 0; i < 5; ++i) {
    Event info = MakeEvent("log", 4000 + static_cast<uint64_t>(i));
    info.level = "info";
    info.msg = "ordinary info";
    store.ingest(info, "node-a", "agent-a");

    Event error = MakeEvent("log", 4100 + static_cast<uint64_t>(i));
    error.level = "error";
    error.msg = "failure";
    store.ingest(error, "node-a", "agent-a");
  }

  EXPECT_EQ(5u, store.stats().logs_sampled);
  EXPECT_EQ(5u, store.stats().logs_ingested);
  LogQuery query;
  query.errors_only = true;
  EXPECT_EQ(5u, store.query_logs(query).size());
}

TEST(Store, DedupCapsRepeatedInfoLogsPerWindow) {
  StoreConfig config;
  config.dedup_max_per_window = 3;
  config.dedup_window_ns = 60ull * 1000000000ull;
  Store store(config);

  for (int i = 0; i < 10; ++i) {
    Event info = MakeEvent("log", 5000 + static_cast<uint64_t>(i) * 1000);
    info.level = "info";
    info.msg = "tick";
    store.ingest(info, "node-a", "agent-a");
  }

  EXPECT_EQ(3u, store.stats().logs_ingested);
  EXPECT_EQ(7u, store.stats().logs_deduped);
}

TEST(Store, MetricsAndCatalogAreUpdated) {
  Store store;

  Event metric = MakeEvent("metric", 6000);
  metric.name = "cpu.usage";
  metric.attrs["value"] = "42.5";
  metric.service = "order";
  metric.instance = "order-1";
  store.ingest(metric, "node-a", "agent-a");

  const auto points = store.query_metrics("cpu.usage", 0, 10);
  ASSERT_EQ(1u, points.size());
  EXPECT_DOUBLE_EQ(42.5, points[0].value);
  EXPECT_EQ("order", points[0].labels.at("service"));

  Event process = MakeEvent("process", 6100);
  process.pid = 4242;
  process.name = "order";
  process.attrs["cmdline"] = "/opt/order --port=9000";
  process.attrs["rss_kb"] = "2048";
  process.attrs["threads"] = "8";
  store.ingest(process, "node-a", "agent-a");

  Event host = MakeEvent("host", 6200);
  host.node = "node-a";
  host.attrs["os"] = "Linux";
  host.attrs["kernel"] = "6.1";
  host.attrs["arch"] = "x86_64";
  host.attrs["mem_total_kb"] = "16384000";
  store.ingest(host, "node-a", "agent-a");

  EXPECT_EQ(1u, store.catalog().node_count());
  EXPECT_EQ(2u, store.catalog().instance_count());  // order-1 + 事件里的 gw-1
  EXPECT_EQ(1u, store.catalog().process_count());

  const auto nodes = store.catalog().nodes();
  ASSERT_EQ(1u, nodes.size());
  EXPECT_EQ("Linux", nodes[0].os);
  EXPECT_EQ("x86_64", nodes[0].arch);
  ASSERT_EQ(1u, nodes[0].processes.count(4242));
  EXPECT_EQ(2048u, nodes[0].processes.at(4242).rss_kb);
}

TEST(Store, RejectsMalformedLines) {
  Store store;
  EXPECT_FALSE(store.ingest_line("not json", "node-a", "agent-a"));
  EXPECT_FALSE(store.ingest_line("[]", "node-a", "agent-a"));
  EXPECT_FALSE(store.ingest_line("{\"no_type\":1}", "node-a", "agent-a"));
  EXPECT_TRUE(store.ingest_line(
      "{\"type\":\"log\",\"time_ns\":1,\"msg\":\"ok\",\"service\":\"s\"}",
      "node-a", "agent-a"));
  EXPECT_EQ(3u, store.stats().events_rejected);
}

TEST(Store, EventsCanBeListedAndAcknowledged) {
  Store store;
  vmon::server::EventRecord record;
  record.id = "evt-1";
  record.time_ns = 7000;
  record.type = "alert";
  record.title = "慢调用";
  record.status = "open";
  store.add_event(record);

  const auto events = store.list_events(10);
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("open", events[0].status);
  EXPECT_TRUE(store.acknowledge_event("evt-1"));
  EXPECT_FALSE(store.acknowledge_event("missing"));
  EXPECT_EQ("ack", store.list_events(10)[0].status);
}

TEST(Store, EventsAreGroupedByFingerprint) {
  Store store;
  vmon::server::EventRecord first;
  first.id = "evt-1";
  first.time_ns = 1000;
  first.type = "alert";
  first.fingerprint = "fp-1";
  first.status = "open";
  store.add_event(first);

  vmon::server::EventRecord second = first;
  second.id = "evt-2";
  second.time_ns = 2000;
  store.add_event(second);

  auto events = store.list_events(10);
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("evt-1", events[0].id);
  EXPECT_EQ(2u, events[0].count);
  EXPECT_EQ(2000u, events[0].last_ns);

  // 解决后同指纹事件重新开一条，而不是继续累加已关闭事件。
  EXPECT_TRUE(store.resolve_event("evt-1"));
  vmon::server::EventRecord third = first;
  third.id = "evt-3";
  third.time_ns = 3000;
  store.add_event(third);
  events = store.list_events(10);
  EXPECT_EQ(2u, events.size());
}

TEST(Store, EventsCanBeFilteredAndResolved) {
  Store store;
  vmon::server::EventRecord open;
  open.id = "evt-open";
  open.type = "alert";
  open.severity = "warning";
  open.service = "order";
  open.status = "open";
  store.add_event(open);

  vmon::server::EventRecord crash;
  crash.id = "evt-crash";
  crash.type = "crash";
  crash.severity = "critical";
  crash.service = "payment";
  crash.status = "open";
  store.add_event(crash);

  vmon::server::EventQuery query;
  query.service = "order";
  EXPECT_EQ(1u, store.list_events(query).size());

  query = vmon::server::EventQuery();
  query.severity = "critical";
  ASSERT_EQ(1u, store.list_events(query).size());
  EXPECT_EQ("evt-crash", store.list_events(query)[0].id);

  EXPECT_TRUE(store.resolve_event("evt-open"));
  EXPECT_TRUE(store.resolve_event("evt-open"));  // 幂等
  query = vmon::server::EventQuery();
  query.status = "resolved";
  ASSERT_EQ(1u, store.list_events(query).size());
  EXPECT_EQ("evt-open", store.list_events(query)[0].id);

  // 已解决的事件再次 ack 不应回退状态。
  EXPECT_TRUE(store.acknowledge_event("evt-open"));
  EXPECT_EQ("resolved", store.list_events(query)[0].status);
  EXPECT_FALSE(store.resolve_event("missing"));
}

}  // namespace
