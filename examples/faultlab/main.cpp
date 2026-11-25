// 故障注入实验室（faultlab）：同一个二进制的多种角色 + 多种故障。
//
// 目的：用一条命令复现「多进程、跨服务、带故障」的真实排障场景，然后通过
// CplusTrace 的调用链 / 错误码 / 日志聚类 / 诊断 / 崩溃 / 证据包快速定位。
//
// 角色：
//   gateway    入口，调用 order，决定对外 HTTP 状态码
//   order      业务编排，调用 inventory（带重试）与 payment
//   inventory  库存服务，可注入慢响应 / 日志风暴 / 队列过载
//   payment    支付服务，可注入业务拒绝
//   worker     后台任务，可注入崩溃
//   driver     压测客户端，按场景产生请求
//
// 故障：
//   none       健康基线
//   slow       下游慢 + 上游超时 + 重试放大
//   payment    业务错误码跨服务传播
//   lockwait   锁等待 / 线程阻塞
//   logstorm   日志风暴 / 队列过载
//   crash      进程崩溃（SIGSEGV）
//
// 用法示例：
//   vmon_faultlab --role gateway --port 19101 --order-port 19102 \
//     --agent /tmp/vmon-agent.sock --fault slow
//   vmon_faultlab --role driver --gateway-port 19101 --iterations 6
//
// 事件流：SDK → Agent(UDS) → Server。跨进程用 W3C traceparent 传播。
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <vmon/vmon.h>

#include "adapters/grpc/propagation.h"

namespace {

using json = nlohmann::json;

void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// SDK 只保存 request_id 指针，Sender 稍后才序列化。用有界队列持有字符串；
// std::deque 的 push_back 不会让已有元素失效。
std::mutex g_keep_mutex;
std::deque<std::string> g_keep;

const char* Keep(const std::string& value) {
  std::lock_guard<std::mutex> lock(g_keep_mutex);
  g_keep.push_back(value);
  while (g_keep.size() > 65536) g_keep.pop_front();
  return g_keep.back().c_str();
}

struct Options {
  std::string role;
  std::string fault = "none";
  std::string agent = "/tmp/vmon-agent.sock";
  std::string control_dir = "/tmp/vmon-ctrl";
  std::string crash_dir = "/tmp/vmon-crash";
  std::string request_prefix = "faultlab";
  int port = 0;
  int gateway_port = 0;
  int order_port = 0;
  int inventory_port = 0;
  int payment_port = 0;
  int iterations = 6;
  int concurrency = 1;
};

bool Parse(int argc, char** argv, Options* o) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--role") o->role = next();
    else if (a == "--fault") o->fault = next();
    else if (a == "--agent") o->agent = next();
    else if (a == "--control-dir") o->control_dir = next();
    else if (a == "--crash-dir") o->crash_dir = next();
    else if (a == "--request-prefix") o->request_prefix = next();
    else if (a == "--port") o->port = std::atoi(next().c_str());
    else if (a == "--gateway-port") o->gateway_port = std::atoi(next().c_str());
    else if (a == "--order-port") o->order_port = std::atoi(next().c_str());
    else if (a == "--inventory-port") o->inventory_port = std::atoi(next().c_str());
    else if (a == "--payment-port") o->payment_port = std::atoi(next().c_str());
    else if (a == "--iterations") o->iterations = std::atoi(next().c_str());
    else if (a == "--concurrency") o->concurrency = std::atoi(next().c_str());
  }
  return !o->role.empty();
}

void InitSdk(const Options& o, const std::string& service, bool crash_handler) {
  vmon::Config config;
  config.service_name = service;
  config.instance_id = service + "-" + std::to_string(::getpid());
  config.agent_endpoint = o.agent;
  config.control_dir = o.control_dir;
  config.enable_control = true;
  config.enable_crash_handler = crash_handler;
  config.crash_report_dir = o.crash_dir;
  config.flush_interval_ms = 50;
  vmon::init(config);
}

json ParseBody(const httplib::Request& req) {
  if (req.body.empty()) return json::object();
  try {
    json body = json::parse(req.body);
    return body.is_object() ? body : json::object();
  } catch (...) {
    return json::object();
  }
}

std::string MakeTraceparent() {
  const vmon::SpanContext ctx = vmon::current_context();
  return vmon::adapters::make_traceparent(ctx.trace_hi, ctx.trace_lo, ctx.span,
                                          true);
}

// 从请求体恢复父上下文并建 span；同时设置 request_id 便于日志收口。
vmon::Span BeginSpan(const json& body, const char* name) {
  const std::string request_id = body.value("request_id", std::string());
  vmon::set_request_id(Keep(request_id));

  vmon::SpanContext parent;
  const std::string traceparent = body.value("traceparent", std::string());
  if (vmon::context_from_traceparent(traceparent.c_str(), &parent)) {
    return vmon::create_span_with_parent(parent, name, __FILE__, __LINE__);
  }
  return vmon::Span::create(name, __FILE__, __LINE__);
}

struct HttpResult {
  bool ok = false;      // 是否拿到 HTTP 响应（false 通常表示超时/连接失败）
  int status = 0;
  json body;
  std::string error;
};

HttpResult PostJson(const std::string& host, int port, const std::string& path,
                    const json& payload, int timeout_ms) {
  HttpResult result;
  httplib::Client client(host, port);
  const time_t seconds = timeout_ms / 1000;
  const time_t micros = (timeout_ms % 1000) * 1000;
  client.set_connection_timeout(seconds, micros);
  client.set_read_timeout(seconds, micros);
  client.set_write_timeout(seconds, micros);
  auto res = client.Post(path, payload.dump(), "application/json");
  if (!res) {
    result.error = httplib::to_string(res.error());
    return result;
  }
  result.ok = true;
  result.status = res->status;
  try {
    result.body = json::parse(res->body);
  } catch (...) {
    result.body = json::object();
  }
  return result;
}

void SetErrorFromCode(vmon::Span* span, const std::string& code) {
  span->set_error(Keep(code));
}

// ---------------------------------------------------------------- gateway
void HandleCheckout(const Options& o, const httplib::Request& req,
                    httplib::Response& res) {
  const json body = ParseBody(req);
  const std::string request_id = body.value("request_id", std::string());
  const uint64_t iteration = body.value("iteration_id", 0ull);

  vmon::Span root = BeginSpan(body, "Gateway/Checkout");
  VMON_INFO("gateway checkout begin request_id=%s iteration=%llu",
            request_id.c_str(), static_cast<unsigned long long>(iteration));

  json downstream = {{"request_id", request_id},
                     {"iteration_id", iteration},
                     {"traceparent", MakeTraceparent()}};
  const HttpResult order =
      PostJson("127.0.0.1", o.order_port, "/api/order", downstream, 600);

  if (!order.ok) {
    root.set_error("E_ORDER_TIMEOUT");
    VMON_ERROR("gateway order call timeout request_id=%s error=%s",
               request_id.c_str(), order.error.c_str());
    res.status = 504;
    res.set_content(json{{"ok", false}, {"code", "E_ORDER_TIMEOUT"}}.dump(),
                    "application/json");
  } else if (!order.body.value("ok", false)) {
    const std::string code = order.body.value("code", std::string("E_ORDER_FAILED"));
    SetErrorFromCode(&root, code);
    VMON_ERROR("gateway checkout failed request_id=%s code=%s",
               request_id.c_str(), code.c_str());
    res.status = 502;
    res.set_content(json{{"ok", false}, {"code", code}}.dump(),
                    "application/json");
  } else {
    VMON_INFO("gateway checkout ok request_id=%s", request_id.c_str());
    res.status = 200;
    res.set_content(json{{"ok", true}}.dump(), "application/json");
  }
  root.end();
  vmon::set_request_id(nullptr);
}

// ---------------------------------------------------------------- order
std::mutex g_order_mutex;
std::atomic<bool> g_order_holder{false};

void HandleOrder(const Options& o, const httplib::Request& req,
                 httplib::Response& res) {
  const json body = ParseBody(req);
  const std::string request_id = body.value("request_id", std::string());
  const uint64_t iteration = body.value("iteration_id", 0ull);

  vmon::Span span = BeginSpan(body, "Order/Create");
  bool ok = true;
  std::string code;

  if (o.fault == "lockwait") {
    const auto wait_start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(g_order_mutex);
    const auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - wait_start)
                               .count();
    bool expected = false;
    if (g_order_holder.compare_exchange_strong(expected, true)) {
      // 第一个请求持有锁并进入慢临界区，其余请求只能排队等待。
      // 日志里保留 "lock wait" 字样，便于诊断规则命中锁竞争。
      VMON_ERROR("order lock wait: critical section held 2500ms request_id=%s",
                 request_id.c_str());
      SleepMs(2500);
    } else {
      span.set_error("E_ORDER_LOCK_WAIT");
      VMON_ERROR("order lock wait exceeded 800ms waited_ms=%lld request_id=%s",
                 static_cast<long long>(waited_ms), request_id.c_str());
      ok = false;
      code = "E_ORDER_LOCK_WAIT";
    }
  }

  if (ok) {
    // 库存调用：3 次重试，每次超时 350ms。慢下游会把重试放大成整体超时。
    for (int attempt = 1; attempt <= 3; ++attempt) {
      vmon::Span inventory =
          vmon::Span::create("Inventory/Reserve", __FILE__, __LINE__);
      json call = {{"request_id", request_id},
                   {"iteration_id", iteration},
                   {"attempt", attempt},
                   {"traceparent", MakeTraceparent()}};
      const HttpResult result =
          PostJson("127.0.0.1", o.inventory_port, "/api/reserve", call, 350);
      if (!result.ok) {
        inventory.set_error("E_INVENTORY_TIMEOUT");
        VMON_WARN("inventory timeout attempt=%d/3 request_id=%s error=%s",
                  attempt, request_id.c_str(), result.error.c_str());
        inventory.end();
        if (attempt == 3) {
          ok = false;
          code = "E_INVENTORY_TIMEOUT";
        }
        continue;
      }
      if (result.status >= 500) {
        const std::string upstream_code =
            result.body.value("code", std::string("E_INVENTORY_FAILED"));
        SetErrorFromCode(&inventory, upstream_code);
        VMON_ERROR("inventory failed status=%d code=%s request_id=%s",
                   result.status, upstream_code.c_str(), request_id.c_str());
        inventory.end();
        ok = false;
        code = upstream_code;
        break;
      }
      inventory.end();
      break;
    }
  }

  if (ok) {
    vmon::Span payment =
        vmon::Span::create("Payment/Charge", __FILE__, __LINE__);
    json call = {{"request_id", request_id},
                 {"iteration_id", iteration},
                 {"traceparent", MakeTraceparent()}};
    const HttpResult result =
        PostJson("127.0.0.1", o.payment_port, "/api/charge", call, 800);
    if (!result.ok) {
      payment.set_error("E_PAYMENT_TIMEOUT");
      VMON_ERROR("payment call timeout request_id=%s", request_id.c_str());
      ok = false;
      code = "E_PAYMENT_TIMEOUT";
    } else if (!result.body.value("ok", false)) {
      const std::string upstream_code =
          result.body.value("code", std::string("E_PAYMENT_DECLINED"));
      SetErrorFromCode(&payment, upstream_code);
      VMON_ERROR("payment declined code=%s request_id=%s",
                 upstream_code.c_str(), request_id.c_str());
      ok = false;
      code = upstream_code;
    }
    payment.end();
  }

  if (!ok) {
    SetErrorFromCode(&span, code);
    VMON_ERROR("order failed code=%s request_id=%s", code.c_str(),
               request_id.c_str());
  } else {
    VMON_INFO("order ok request_id=%s", request_id.c_str());
  }
  res.status = 200;
  res.set_content(json{{"ok", ok}, {"code", code}}.dump(), "application/json");
  span.end();
  vmon::set_request_id(nullptr);
}

// ---------------------------------------------------------------- inventory
void HandleReserve(const Options& o, const httplib::Request& req,
                   httplib::Response& res) {
  const json body = ParseBody(req);
  const std::string request_id = body.value("request_id", std::string());
  const uint64_t iteration = body.value("iteration_id", 0ull);

  vmon::Span span = BeginSpan(body, "Inventory/Reserve");

  if (o.fault == "slow") {
    VMON_WARN("inventory slow: simulating 700ms latency request_id=%s",
              request_id.c_str());
    SleepMs(700);
  }

  if (o.fault == "logstorm") {
    // 大量同模板 INFO + 少量错误模板：用于演示采样/去重/聚类。
    for (int i = 0; i < 300; ++i) {
      VMON_INFO("inventory scan item=%d shard=%d", i, i % 8);
    }
    for (int i = 0; i < 10; ++i) {
      VMON_ERROR("inventory queue depth exceeded shard=%d code=E_QUEUE_OVERFLOW",
                 i);
    }
    if (iteration % 2 == 0) {
      span.set_error("E_INVENTORY_OVERLOADED");
      VMON_ERROR("inventory overloaded: reject request_id=%s",
                 request_id.c_str());
      res.status = 503;
      res.set_content(
          json{{"ok", false}, {"code", "E_INVENTORY_OVERLOADED"}}.dump(),
          "application/json");
      span.end();
      vmon::set_request_id(nullptr);
      return;
    }
  }

  VMON_INFO("inventory reserved sku=sku-42 qty=1 request_id=%s",
            request_id.c_str());
  res.status = 200;
  res.set_content(json{{"ok", true}}.dump(), "application/json");
  span.end();
  vmon::set_request_id(nullptr);
}

// ---------------------------------------------------------------- payment
void HandleCharge(const Options& o, const httplib::Request& req,
                  httplib::Response& res) {
  const json body = ParseBody(req);
  const std::string request_id = body.value("request_id", std::string());
  const uint64_t iteration = body.value("iteration_id", 0ull);

  vmon::Span span = BeginSpan(body, "Payment/Charge");

  if (o.fault == "payment" && iteration % 3 == 0) {
    span.set_error("E_PAYMENT_DECLINED");
    VMON_ERROR("payment declined: card expired request_id=%s",
               request_id.c_str());
    res.status = 200;
    res.set_content(
        json{{"ok", false}, {"code", "E_PAYMENT_DECLINED"}}.dump(),
        "application/json");
  } else {
    VMON_INFO("payment charged amount=128.00 request_id=%s",
              request_id.c_str());
    res.status = 200;
    res.set_content(json{{"ok", true}}.dump(), "application/json");
  }
  span.end();
  vmon::set_request_id(nullptr);
}

int RunController(const Options& o) {
  InitSdk(o, "faultlab-" + o.role, false);
  httplib::Server server;
  auto wrap = [&o](void (*fn)(const Options&, const httplib::Request&,
                              httplib::Response&)) {
    return [&o, fn](const httplib::Request& req, httplib::Response& res) {
      try {
        fn(o, req, res);
      } catch (...) {
        res.status = 500;
        res.set_content(json{{"ok", false}, {"code", "E_HANDLER_EXCEPTION"}}.dump(),
                        "application/json");
      }
    };
  };

  if (o.role == "gateway") {
    server.Post("/api/checkout", wrap(&HandleCheckout));
  } else if (o.role == "order") {
    server.Post("/api/order", wrap(&HandleOrder));
  } else if (o.role == "inventory") {
    server.Post("/api/reserve", wrap(&HandleReserve));
  } else if (o.role == "payment") {
    server.Post("/api/charge", wrap(&HandleCharge));
  } else {
    std::fprintf(stderr, "未知角色: %s\n", o.role.c_str());
    return 2;
  }

  std::printf("{\"role\":\"%s\",\"port\":%d,\"fault\":\"%s\"}\n",
              o.role.c_str(), o.port, o.fault.c_str());
  std::fflush(stdout);
  if (!server.listen("127.0.0.1", o.port)) {
    std::fprintf(stderr, "端口 %d 监听失败\n", o.port);
    return 2;
  }
  return 0;
}

// ---------------------------------------------------------------- worker
int RunWorker(const Options& o) {
  InitSdk(o, "faultlab-worker", true);
  vmon::set_request_id("worker-batch-1");
  VMON_STATE("worker", "running");
  std::printf("{\"role\":\"worker\",\"pid\":%d,\"fault\":\"%s\"}\n",
              static_cast<int>(::getpid()), o.fault.c_str());
  std::fflush(stdout);

  for (int tick = 1; tick <= 80; ++tick) {
    vmon::heartbeat();
    VMON_INFO("worker tick=%d queue_depth=%d", tick, tick % 5);
    if (o.fault == "crash" && tick == 8) {
      VMON_ERROR("worker fatal: simulated null pointer dereference");
      // 给后台 Sender 一点时间把最后一条错误日志发出去，再触发崩溃。
      SleepMs(150);
      volatile int* pointer = nullptr;
      *pointer = 42;  // SIGSEGV；崩溃处理器会写最小 JSON
    }
    SleepMs(250);
  }
  vmon::shutdown();
  return 0;
}

// ---------------------------------------------------------------- driver
int RunDriver(const Options& o) {
  const int total = o.iterations <= 0 ? 1 : o.iterations;
  const int concurrency = o.concurrency <= 0 ? 1 : o.concurrency;
  const std::string run_tag =
      std::to_string(::getpid()) + "-" +
      std::to_string(static_cast<long long>(time(nullptr)));

  std::vector<std::string> request_ids(static_cast<size_t>(total));
  for (int i = 0; i < total; ++i) {
    request_ids[static_cast<size_t>(i)] =
        o.request_prefix + "-" + run_tag + "-" + std::to_string(i + 1);
  }
  std::vector<int> statuses(static_cast<size_t>(total), 0);

  std::atomic<int> next{0};
  auto worker = [&]() {
    for (;;) {
      const int index = next.fetch_add(1);
      if (index >= total) return;
      json payload = {{"request_id", request_ids[static_cast<size_t>(index)]},
                      {"iteration_id", static_cast<uint64_t>(index + 1)}};
      const HttpResult result =
          PostJson("127.0.0.1", o.gateway_port, "/api/checkout", payload, 5000);
      statuses[static_cast<size_t>(index)] =
          result.ok ? result.status : 0;
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < concurrency; ++t) threads.emplace_back(worker);
  for (auto& thread : threads) thread.join();

  json results = json::array();
  int ok_count = 0;
  int failed_count = 0;
  for (int i = 0; i < total; ++i) {
    const int status = statuses[static_cast<size_t>(i)];
    const bool ok = status == 200;
    if (ok) ++ok_count;
    else ++failed_count;
    results.push_back({{"request_id", request_ids[static_cast<size_t>(i)]},
                       {"status", status},
                       {"ok", ok}});
  }
  json out = {{"request_ids", request_ids},
              {"results", results},
              {"ok", ok_count},
              {"failed", failed_count}};
  std::printf("%s\n", out.dump().c_str());
  std::fflush(stdout);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!Parse(argc, argv, &options)) {
    std::fprintf(stderr,
                 "用法: %s --role <gateway|order|inventory|payment|worker|driver>\n"
                 "       [--fault none|slow|payment|lockwait|logstorm|crash]\n"
                 "       [--port N] [--gateway-port N] [--order-port N]\n"
                 "       [--inventory-port N] [--payment-port N]\n"
                 "       [--agent PATH] [--control-dir DIR] [--crash-dir DIR]\n"
                 "       [--iterations N] [--concurrency N]\n",
                 argv[0]);
    return 2;
  }
  if (options.role == "driver") return RunDriver(options);
  if (options.role == "worker") return RunWorker(options);
  return RunController(options);
}
