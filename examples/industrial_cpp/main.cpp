// 工业控制闭环示例（C++ 版）：7 个进程 + SDK 血缘埋点。
//
// 与 Python 版行为一致，但每个进程都链接 vmon SDK：
//   - 跨进程用 HTTP + W3C traceparent 传播；
//   - 字段用 VMON_FIELD_* 记录，设备状态用 VMON_STATE；
//   - 事件经 SDK → Agent(UDS) → Server。
//
// 用法：
//   vmon_industrial --role motion-controller --port 19101 \
//     --agent /tmp/vmon-agent.sock --fault identity --iterations 50
//   vmon_industrial --role orchestrator --agent ... --fault identity \
//     --iterations 50 --motion-port 19101 ...
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <vmon/vmon.h>

#include "adapters/grpc/propagation.h"

namespace {

using json = nlohmann::json;

constexpr long long kTriggerDelayNs = 10000000LL;   // 固定触发延时 10ms
constexpr long long kSettleNs = 8200000LL;          // 正常 settle
constexpr long long kSettleJitterNs = 13100000LL;   // 抖动 settle

// SDK 只保存 run_id 指针；Sender 稍后才序列化。用有界队列持有字符串。
std::mutex g_str_mutex;
std::deque<std::string> g_strs;

const char* Keep(const std::string& value) {
  std::lock_guard<std::mutex> lock(g_str_mutex);
  g_strs.push_back(value);
  while (g_strs.size() > 4096) g_strs.pop_front();
  return g_strs.back().c_str();
}

struct Options {
  std::string role;
  std::string agent = "/tmp/vmon-agent.sock";
  std::string fault = "identity";
  int port = 0;
  int iterations = 50;
  int motion_port = 19101;
  int acq_port = 19102;
  int receiver_port = 19103;
  int algo_port = 19104;
  int calib_port = 19105;
  int store_port = 19106;
};

bool Parse(int argc, char** argv, Options* o) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--role") o->role = next();
    else if (a == "--agent") o->agent = next();
    else if (a == "--fault") o->fault = next();
    else if (a == "--port") o->port = std::atoi(next().c_str());
    else if (a == "--iterations") o->iterations = std::atoi(next().c_str());
    else if (a == "--motion-port") o->motion_port = std::atoi(next().c_str());
    else if (a == "--acq-port") o->acq_port = std::atoi(next().c_str());
    else if (a == "--receiver-port") o->receiver_port = std::atoi(next().c_str());
    else if (a == "--algo-port") o->algo_port = std::atoi(next().c_str());
    else if (a == "--calib-port") o->calib_port = std::atoi(next().c_str());
    else if (a == "--store-port") o->store_port = std::atoi(next().c_str());
  }
  return !o->role.empty();
}

void InitSdk(const Options& o) {
  vmon::Config config;
  config.service_name = o.role;
  config.instance_id = o.role + "-" + std::to_string(::getpid());
  config.agent_endpoint = o.agent;
  config.enable_control = false;
  config.enable_crash_handler = false;
  config.flush_interval_ms = 50;
  vmon::init(config);
}

int Post(const std::string& host_port, const std::string& path,
         const json& body, json* out) {
  const size_t colon = host_port.rfind(':');
  if (colon == std::string::npos) return 0;
  const std::string host = host_port.substr(0, colon);
  const int port = std::atoi(host_port.c_str() + colon + 1);
  httplib::Client client(host, port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(5, 0);
  auto res = client.Post(path, body.dump(), "application/json");
  if (!res) return 0;
  if (out != nullptr) {
    try {
      *out = json::parse(res->body);
    } catch (...) {
      *out = json::object();
    }
  }
  return res->status;
}

// 从请求体恢复父上下文并建 span。
vmon::Span BeginSpan(const json& body, const char* name) {
  vmon::set_run_id(Keep(body.value("run_id", std::string())));
  vmon::set_iteration_id(body.value("iteration_id", 0ull));
  vmon::SpanContext parent;
  const std::string traceparent = body.value("traceparent", std::string());
  const bool has_parent =
      vmon::context_from_traceparent(traceparent.c_str(), &parent);
  if (has_parent) {
    return vmon::create_span_with_parent(parent, name, __FILE__, __LINE__);
  }
  return vmon::Span::create(name, __FILE__, __LINE__);
}

// ---------------------------------------------------------------- 控制器
void HandleMotion(const Options& o, const httplib::Request& req,
                  httplib::Response& res) {
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  const uint64_t it = body.value("iteration_id", 0ull);
  vmon::Span span = BeginSpan(body, "Motion/Move");
  long long settle = kSettleNs;
  const long long jitter_at =
      o.iterations >= 37 ? 37 : (o.iterations > 2 ? o.iterations : 2);
  if (req.path == "/move" && o.fault == "timing" &&
      static_cast<long long>(it) == jitter_at) {
    settle = kSettleJitterNs;
  }
  if (req.path == "/move") {
    VMON_FIELD_OUT("motion", "settle_time_ns", std::to_string(settle).c_str());
  }
  VMON_FIELD_OUT("motion", "actual_position",
                 std::to_string(body.value("position", 0)).c_str());
  span.end();
  res.set_content(json{{"settle_time_ns", settle},
                       {"actual_position", body.value("position", 0)}}
                      .dump(),
                  "application/json");
}

void HandleAcq(const Options& o, const httplib::Request& req,
               httplib::Response& res) {
  (void)o;
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  const uint64_t it = body.value("iteration_id", 0ull);
  vmon::Span span = BeginSpan(body, "Acq/Trigger");
  VMON_FIELD_I64("frame", "frame_id", static_cast<int64_t>(it));
  VMON_FIELD_OUT("trigger", "trigger_delay_ns",
                 std::to_string(kTriggerDelayNs).c_str());
  span.end();
  res.set_content(json{{"frame_id", it}, {"trigger_delay_ns", kTriggerDelayNs}}
                      .dump(),
                  "application/json");
}

void HandleReceiver(const Options& o, const httplib::Request& req,
                    httplib::Response& res) {
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  const uint64_t it = body.value("iteration_id", 0ull);
  const long long requested = body.value("requested_frame_id", 0LL);
  vmon::Span span = BeginSpan(body, "Receiver/GetFrame");
  long long used = requested;
  if (o.fault == "identity" && it >= 2) used = requested - 1;
  const long long drop_at = o.iterations >= 10 ? 10 : 2;
  if (o.fault == "count" && static_cast<long long>(it) == drop_at) used = 0;
  VMON_FIELD_I64("frame", "requested_frame_id", requested);
  VMON_FIELD_I64("frame", "used_frame_id", used);
  span.end();
  res.set_content(json{{"frame_id", used}}.dump(), "application/json");
}

void HandleAlgo(const Options& o, const httplib::Request& req,
                httplib::Response& res) {
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  const uint64_t it = body.value("iteration_id", 0ull);
  const long long frame_id = body.value("frame_id", 0LL);
  vmon::Span span = BeginSpan(body, "Algo/Process");
  double offset = 1.0;
  const long long glitch_at =
      o.iterations >= 20 ? 20 : (o.iterations > 2 ? o.iterations - 1 : 2);
  if (o.fault == "algo" && static_cast<long long>(it) == glitch_at) offset = 3.7;
  VMON_FIELD_I64("frame", "used_frame_id", frame_id);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", offset);
  VMON_FIELD_OUT("offset", "offset", buffer);
  span.end();
  res.set_content(json{{"offset", offset}}.dump(), "application/json");
}

void HandleCalib(const Options& o, const httplib::Request& req,
                 httplib::Response& res) {
  (void)o;
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  vmon::Span span = BeginSpan(body, "Calib/Transform");
  const double offset = body.value("offset", 0.0);
  const double transformed = offset * 1.02;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", offset);
  VMON_FIELD_IN("offset", "offset", buffer);
  std::snprintf(buffer, sizeof(buffer), "%.3f", transformed);
  VMON_FIELD_OUT("offset", "offset_transformed", buffer);
  span.end();
  res.set_content(json{{"offset", transformed}}.dump(), "application/json");
}

void HandleStore(const Options& o, const httplib::Request& req,
                 httplib::Response& res) {
  (void)o;
  json body = json::parse(req.body.empty() ? "{}" : req.body);
  vmon::Span span = BeginSpan(body, "State/Save");
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", body.value("residual", 0.0));
  VMON_FIELD_OUT("state", "residual", buffer);
  span.end();
  res.set_content(json{{"ok", true}}.dump(), "application/json");
}

int RunController(const Options& o) {
  InitSdk(o);
  httplib::Server server;
  auto wrap = [&](void (*fn)(const Options&, const httplib::Request&,
                             httplib::Response&)) {
    return [&o, fn](const httplib::Request& req, httplib::Response& res) {
      fn(o, req, res);
    };
  };
  if (o.role == "motion-controller") {
    server.Post("/move", wrap(&HandleMotion));
    server.Post("/correct", wrap(&HandleMotion));
  } else if (o.role == "acq-controller") {
    server.Post("/trigger", wrap(&HandleAcq));
  } else if (o.role == "data-receiver") {
    server.Post("/frame", wrap(&HandleReceiver));
  } else if (o.role == "algo-controller") {
    server.Post("/process", wrap(&HandleAlgo));
  } else if (o.role == "calib-service") {
    server.Post("/transform", wrap(&HandleCalib));
  } else if (o.role == "state-store") {
    server.Post("/save", wrap(&HandleStore));
  } else {
    std::fprintf(stderr, "未知角色: %s\n", o.role.c_str());
    return 2;
  }
  if (!server.listen("127.0.0.1", o.port)) {
    std::fprintf(stderr, "端口 %d 监听失败\n", o.port);
    return 2;
  }
  return 0;
}

// ---------------------------------------------------------------- 逻辑层
std::string MakeTraceparent() {
  const vmon::SpanContext ctx = vmon::current_context();
  return vmon::adapters::make_traceparent(ctx.trace_hi, ctx.trace_lo, ctx.span,
                                          true);
}

int RunOrchestrator(const Options& o) {
  InitSdk(o);
  const std::string run_id = "run-cpp-" + std::to_string(::getpid()) + "-" +
                             std::to_string(static_cast<long long>(time(nullptr)));
  vmon::set_run_id(Keep(run_id));
  vmon::set_iteration_id(0);

  // 运行开始上报全部设备状态（G0 状态契约）。
  VMON_STATE("job-orchestrator", "running");
  VMON_STATE("acq-controller", "armed");
  VMON_STATE("data-receiver", "ready");
  VMON_STATE("algo-controller", "idle");
  VMON_STATE("calib-service", "calibrated");
  VMON_STATE("motion-controller", o.fault == "state" ? "disabled" : "homed");
  VMON_STATE("state-store", "open");

  const std::string motion = "127.0.0.1:" + std::to_string(o.motion_port);
  const std::string acq = "127.0.0.1:" + std::to_string(o.acq_port);
  const std::string receiver = "127.0.0.1:" + std::to_string(o.receiver_port);
  const std::string algo = "127.0.0.1:" + std::to_string(o.algo_port);
  const std::string calib = "127.0.0.1:" + std::to_string(o.calib_port);
  const std::string store = "127.0.0.1:" + std::to_string(o.store_port);

  long long trigger_count = 0;
  long long frame_count = 0;
  long long processed_count = 0;

  for (int it = 1; it <= o.iterations; ++it) {
    vmon::set_iteration_id(static_cast<uint64_t>(it));
    vmon::Span root = vmon::Span::create("Job/Iteration", __FILE__, __LINE__);
    const std::string traceparent = MakeTraceparent();
    json base = {{"run_id", run_id},
                 {"iteration_id", it},
                 {"traceparent", traceparent}};

    json reply;
    json move = base;
    move["position"] = it * 10;
    Post(motion, "/move", move, &reply);

    json trig = base;
    Post(acq, "/trigger", trig, &reply);
    const long long requested = reply.value("frame_id", 0LL);
    ++trigger_count;
    VMON_FIELD_I64("frame", "requested_frame_id", requested);

    json getframe = base;
    getframe["requested_frame_id"] = requested;
    Post(receiver, "/frame", getframe, &reply);
    const long long used = reply.value("frame_id", 0LL);
    if (used == 0) {
      root.set_error("E_NO_FRAME");
      root.end();
      continue;
    }
    ++frame_count;

    json process = base;
    process["frame_id"] = used;
    Post(algo, "/process", process, &reply);
    const double offset = reply.value("offset", 0.0);
    ++processed_count;

    json transform = base;
    transform["offset"] = offset;
    Post(calib, "/transform", transform, &reply);
    const double transformed = reply.value("offset", 0.0);

    json correct = base;
    correct["offset"] = transformed;
    Post(motion, "/correct", correct, &reply);

    json save = base;
    save["residual"] = offset > transformed ? offset - transformed
                                            : transformed - offset;
    Post(store, "/save", save, &reply);

    root.end();
  }

  // 计数（G1）。
  VMON_FIELD_I64("count", "trigger_count", trigger_count);
  VMON_FIELD_I64("count", "frame_count", frame_count);
  VMON_FIELD_I64("count", "processed_count", processed_count);
  VMON_FIELD_I64("count", "iteration_count", o.iterations);
  VMON_STATE("job-orchestrator", "finished");

  std::printf("{\"run_id\":\"%s\",\"iterations\":%d,\"fault\":\"%s\"}\n",
              run_id.c_str(), o.iterations, o.fault.c_str());
  vmon::shutdown();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!Parse(argc, argv, &options)) {
    std::fprintf(stderr,
                 "用法: %s --role <role> [--port N] [--agent PATH] "
                 "[--fault identity|timing|count|state|algo|none] "
                 "[--iterations N]\n",
                 argv[0]);
    return 2;
  }
  if (options.role == "orchestrator") return RunOrchestrator(options);
  return RunController(options);
}
