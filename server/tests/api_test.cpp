#include <gtest/gtest.h>

#include <sys/stat.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <thread>

#include "api.h"
#include "config.h"
#include "tests/test_util.h"
#include "util.h"

namespace {

const char* kToken = "unit-token";

std::string TraceEventsBody() {
  const uint64_t now = vmon::server::now_ns();
  nlohmann::json begin = {
      {"type", "span_begin"}, {"time_ns", now - 50000000ull},
      {"trace", "trace-api"}, {"span", "span-root"},
      {"service", "gateway"}, {"instance", "gw-1"}, {"node", "node-a"},
      {"name", "GET /order"}, {"pid", 100}, {"request_id", "req-api"}};
  nlohmann::json child = {
      {"type", "span_begin"}, {"time_ns", now - 40000000ull},
      {"trace", "trace-api"}, {"span", "span-child"},
      {"parent", "span-root"},
      {"service", "order"}, {"instance", "order-1"}, {"node", "node-a"},
      {"name", "CreateOrder"}, {"pid", 200}, {"request_id", "req-api"}};
  nlohmann::json end_child = {
      {"type", "span_end"}, {"time_ns", now - 10000000ull},
      {"trace", "trace-api"}, {"span", "span-child"},
      {"parent", "span-root"},
      {"service", "order"}, {"instance", "order-1"}, {"node", "node-a"},
      {"name", "CreateOrder"}, {"pid", 200}, {"request_id", "req-api"},
      {"level", "error"}, {"error_code", "E_ORDER"},
      {"attrs", {{"status", "error"}}}};
  nlohmann::json end_root = {
      {"type", "span_end"}, {"time_ns", now},
      {"trace", "trace-api"}, {"span", "span-root"},
      {"service", "gateway"}, {"instance", "gw-1"}, {"node", "node-a"},
      {"name", "GET /order"}, {"pid", 100}, {"request_id", "req-api"}};
  nlohmann::json log = {
      {"type", "log"}, {"time_ns", now - 20000000ull},
      {"service", "order"}, {"instance", "order-1"}, {"node", "node-a"},
      {"level", "error"}, {"msg", "db timeout request_id=req-api"},
      {"trace", "trace-api"}, {"request_id", "req-api"},
      {"error_code", "E_DB"}};

  std::string body;
  for (const auto& item : {begin, child, end_child, end_root, log}) {
    body += item.dump();
    body.push_back('\n');
  }
  return body;
}

class ApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_FALSE(temp_.path().empty());
    vmon::server::ServerConfig config;
    config.listen_host = "127.0.0.1";
    config.state_dir = temp_.file("state");
    config.web_dir = temp_.file("web");
    config.token = kToken;
    config.forward_enabled = false;
    config.enable_evidence = true;
    config.evidence_salt = "unit-salt";
    context_.reset(new vmon::server::ServerContext(config));
    vmon::server::register_routes(server_, *context_);
    port_ = server_.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port_, 0);
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    client_.reset(new httplib::Client("127.0.0.1", port_));
    client_->set_connection_timeout(2, 0);
    client_->set_read_timeout(5, 0);
  }

  void TearDown() override {
    server_.stop();
    if (thread_.joinable()) thread_.join();
    context_->stop_background();
    client_.reset();
    context_.reset();
  }

  httplib::Headers Auth() const {
    httplib::Headers headers;
    headers.emplace("X-Vmon-Token", kToken);
    return headers;
  }

  nlohmann::json GetJson(const std::string& path, bool auth = true) {
    auto res = auth ? client_->Get(path, Auth()) : client_->Get(path);
    EXPECT_TRUE(res) << path;
    if (!res) return nlohmann::json::object();
    return nlohmann::json::parse(res->body);
  }

  nlohmann::json PostJson(const std::string& path, const nlohmann::json& body,
                          bool auth = true) {
    auto res = auth ? client_->Post(path, Auth(), body.dump(),
                                    "application/json")
                    : client_->Post(path, body.dump(), "application/json");
    EXPECT_TRUE(res) << path;
    if (!res) return nlohmann::json::object();
    return nlohmann::json::parse(res->body);
  }

  vmon::test::TempDir temp_;
  std::unique_ptr<vmon::server::ServerContext> context_;
  httplib::Server server_;
  std::thread thread_;
  std::unique_ptr<httplib::Client> client_;
  int port_ = 0;
};

TEST_F(ApiTest, HealthIsPublicButEverythingElseNeedsToken) {
  auto health = client_->Get("/api/health");
  ASSERT_TRUE(health);
  EXPECT_EQ(200, health->status);
  EXPECT_EQ("ok", nlohmann::json::parse(health->body)["status"]);

  auto denied = client_->Get("/api/overview");
  ASSERT_TRUE(denied);
  EXPECT_EQ(401, denied->status);

  httplib::Headers wrong;
  wrong.emplace("X-Vmon-Token", "wrong");
  auto wrong_res = client_->Get("/api/overview", wrong);
  ASSERT_TRUE(wrong_res);
  EXPECT_EQ(401, wrong_res->status);

  auto allowed = client_->Get("/api/overview", Auth());
  ASSERT_TRUE(allowed);
  EXPECT_EQ(200, allowed->status);

  auto options = client_->Options("/api/overview");
  ASSERT_TRUE(options);
  EXPECT_EQ(204, options->status);
  EXPECT_EQ("*", options->get_header_value("Access-Control-Allow-Origin"));
}

TEST_F(ApiTest, IngestsEventsAndServesTraceAndLogQueries) {
  const std::string body = TraceEventsBody();
  auto ingest = client_->Post("/v1/ingest?node=node-a",
                              Auth(), body, "application/x-ndjson");
  ASSERT_TRUE(ingest);
  EXPECT_EQ(200, ingest->status);
  const nlohmann::json result = nlohmann::json::parse(ingest->body);
  EXPECT_EQ(5, result["accepted"]);
  EXPECT_EQ(0, result["rejected"]);

  const nlohmann::json traces = GetJson("/api/traces?request_id=req-api");
  ASSERT_EQ(1, traces["count"]);
  EXPECT_EQ("trace-api", traces["traces"][0]["trace_id"]);
  EXPECT_TRUE(traces["traces"][0]["has_error"].get<bool>());

  auto detail_res = client_->Get("/api/traces/trace-api", Auth());
  ASSERT_TRUE(detail_res);
  EXPECT_EQ(200, detail_res->status);
  const nlohmann::json detail = nlohmann::json::parse(detail_res->body);
  EXPECT_TRUE(detail.contains("summary"));
  EXPECT_TRUE(detail.contains("roots"));
  EXPECT_TRUE(detail.contains("critical_path"));

  auto request_res = client_->Get("/api/requests/req-api", Auth());
  ASSERT_TRUE(request_res);
  EXPECT_EQ(200, request_res->status);

  const nlohmann::json logs = GetJson("/api/logs?trace_id=trace-api");
  EXPECT_EQ(1, logs["count"]);
  EXPECT_EQ("E_DB", logs["logs"][0]["error_code"]);

  const nlohmann::json by_request =
      GetJson("/api/logs?request_id=req-api");
  EXPECT_EQ(1, by_request["count"]);

  const nlohmann::json overview = GetJson("/api/overview");
  EXPECT_GE(overview["trace_count"].get<size_t>(), 1u);
  EXPECT_GE(overview["logs_ingested"].get<uint64_t>(), 1u);

  const nlohmann::json clusters = GetJson("/api/logs/clusters");
  EXPECT_GE(clusters["count"].get<int>(), 1);

  const nlohmann::json mcp =
      PostJson("/mcp", {{"jsonrpc", "2.0"}, {"id", 1},
                        {"method", "tools/list"}});
  ASSERT_TRUE(mcp.contains("result"));
  EXPECT_TRUE(mcp["result"].contains("tools"));
  EXPECT_GT(mcp["result"]["tools"].size(), 0u);
}

TEST_F(ApiTest, ControlLogLevelEnqueuesAndAgentReportsResult) {
  nlohmann::json event = {
      {"type", "log"}, {"time_ns", vmon::server::now_ns()},
      {"service", "order"}, {"instance", "order-1"}, {"node", "node-a"},
      {"level", "info"}, {"msg", "ready"}, {"pid", 555}};
  EXPECT_EQ(1, PostJson("/v1/ingest", event)["accepted"]);

  const nlohmann::json control =
      PostJson("/api/control/log-level",
               {{"service", "order"}, {"level", "debug"}, {"ttl_ms", 5000}});
  ASSERT_TRUE(control.contains("commands"));
  ASSERT_EQ(1u, control["commands"].size());
  const std::string command_id = control["commands"][0].get<std::string>();

  auto polled = client_->Get("/v1/commands?node=node-a&agent=agent-a", Auth());
  ASSERT_TRUE(polled);
  const nlohmann::json commands = nlohmann::json::parse(polled->body);
  ASSERT_EQ(1u, commands["commands"].size());
  EXPECT_EQ(command_id, commands["commands"][0]["id"]);
  EXPECT_EQ("set_log_level", commands["commands"][0]["type"]);
  EXPECT_EQ(555, commands["commands"][0]["pid"]);
  EXPECT_EQ("debug", commands["commands"][0]["args"]["level"]);

  const nlohmann::json done =
      PostJson("/v1/commands/" + command_id + "/result",
               {{"status", "done"}, {"result", "{\"ok\":true}"},
                {"error", ""}});
  EXPECT_TRUE(done["ok"].get<bool>());

  const nlohmann::json listed = GetJson("/api/commands");
  ASSERT_EQ(1u, listed["commands"].size());
  EXPECT_EQ("done", listed["commands"][0]["status"]);
  EXPECT_EQ("{\"ok\":true}", listed["commands"][0]["result"]);
}

TEST_F(ApiTest, CrashIngestCreatesCrashAndEvent) {
  const nlohmann::json report = {
      {"time_ns", vmon::server::now_ns()},
      {"node", "node-a"}, {"service", "order"}, {"instance", "order-1"},
      {"pid", 777}, {"signal", "SIGSEGV"}, {"reason", "segv"},
      {"build_id", "build-1"}, {"frames", {"0x1000 main", "0x1010 run"}}};

  auto res = client_->Post("/v1/crash?node=node-a", Auth(), report.dump(),
                           "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  const std::string id = nlohmann::json::parse(res->body)["id"];

  const nlohmann::json crashes = GetJson("/api/crashes");
  EXPECT_EQ(1, crashes["count"]);
  EXPECT_EQ(id, crashes["crashes"][0]["id"]);

  auto detail = client_->Get(("/api/crashes/" + id).c_str(), Auth());
  ASSERT_TRUE(detail);
  EXPECT_EQ(200, detail->status);

  const nlohmann::json events = GetJson("/api/events");
  ASSERT_GE(events["count"].get<int>(), 1);
}

TEST_F(ApiTest, EvidenceEndpointWritesPack) {
  const uint64_t now = vmon::server::now_ns();
  nlohmann::json event = {
      {"type", "log"}, {"time_ns", now},
      {"service", "order"}, {"instance", "order-1"}, {"node", "node-a"},
      {"level", "error"}, {"msg", "db fail peer 10.0.0.8 token=abc"},
      {"error_code", "E_DB"}};
  EXPECT_EQ(1, PostJson("/v1/ingest", event)["accepted"]);

  const nlohmann::json out =
      PostJson("/api/evidence",
               {{"incident", "unit"}, {"service", "order"},
                {"window_start_ns", now - 1000000000ull},
                {"window_end_ns", now + 1000000000ull}});
  ASSERT_TRUE(out["ok"].get<bool>()) << out.dump();
  const std::string dir = out["dir"].get<std::string>();
  struct stat st;
  ASSERT_EQ(0, ::stat((dir + "/manifest.json").c_str(), &st));
  EXPECT_EQ(std::string::npos,
            vmon::test::read_file(dir + "/logs.jsonl").find("10.0.0.8"));
  EXPECT_TRUE(out["manifest"].contains("files"));
}

TEST_F(ApiTest, RulesCanBeListedAddedAndDeleted) {
  const nlohmann::json initial = GetJson("/api/rules");
  const size_t before = initial["rules"].size();
  EXPECT_GE(before, 3u);

  const nlohmann::json created =
      PostJson("/api/rules", {{"name", "自定义慢调用"},
                              {"type", "slow_trace"},
                              {"threshold", 250},
                              {"cooldown_ns", 1000000000ull}});
  EXPECT_EQ("自定义慢调用", created["name"]);
  const std::string id = created["id"].get<std::string>();

  const nlohmann::json after = GetJson("/api/rules");
  EXPECT_EQ(before + 1, after["rules"].size());

  auto deleted = client_->Delete(("/api/rules/" + id).c_str(), Auth());
  ASSERT_TRUE(deleted);
  EXPECT_EQ(200, deleted->status);
  EXPECT_EQ(before, GetJson("/api/rules")["rules"].size());
}

TEST_F(ApiTest, ProfilesEndpointParsesFoldedStacks) {
  auto res = client_->Post("/v1/profile?service=order&node=node-a", Auth(),
                           "main;work;leaf 7\nmain;work 3\n",
                           "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  const std::string id = nlohmann::json::parse(res->body)["id"];

  const nlohmann::json list = GetJson("/api/profiles");
  EXPECT_EQ(1, list["count"]);

  auto detail = client_->Get(("/api/profiles/" + id).c_str(), Auth());
  ASSERT_TRUE(detail);
  EXPECT_EQ(200, detail->status);
  const nlohmann::json detail_json = nlohmann::json::parse(detail->body);
  EXPECT_TRUE(detail_json.contains("flamegraph"));
}

}  // namespace
