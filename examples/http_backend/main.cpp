// HTTP 后端示例：接收 Python / Web 客户端的 RPC 调用，并续接调用链。
//
// 关键点：
//  - 从 traceparent 头恢复父上下文，创建 server span；
//  - 从 x-request-id / x-task-id 恢复业务关联 ID；
//  - SDK 未初始化或 Agent 不在线时，HTTP 服务照常工作。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <vmon/vmon.h>

namespace {

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

// SDK 的 request_id/task_id 只保存指针，后台发送线程稍后才序列化。
// 业务侧必须保证指针在“请求处理 + 一次 flush”期间有效；这里用有界队列
// 持有字符串，避免栈上临时串在 handler 返回后悬垂。
const char* KeepId(const std::string& id) {
  static std::mutex mutex;
  static std::deque<std::string> ids;
  std::lock_guard<std::mutex> lock(mutex);
  ids.push_back(id);
  while (ids.size() > 4096) ids.pop_front();
  return ids.back().c_str();
}

void HandleEcho(const httplib::Request& req, httplib::Response& res) {
  const std::string request_id = req.get_header_value("x-request-id");
  const std::string task_id = req.get_header_value("x-task-id");
  vmon::set_request_id(request_id.empty() ? nullptr
                                          : KeepId(request_id));
  vmon::set_task_id(task_id.empty() ? nullptr : KeepId(task_id));

  vmon::SpanContext parent;
  const std::string traceparent = req.get_header_value("traceparent");
  const bool has_parent =
      vmon::context_from_traceparent(traceparent.c_str(), &parent);

  {
    // 有上游上下文就续接；没有就新建一条 trace，不能拒绝业务请求。
    vmon::Span span = has_parent
                          ? vmon::create_span_with_parent(
                                parent, "HTTP/echo", __FILE__, __LINE__)
                          : vmon::Span::create("HTTP/echo", __FILE__, __LINE__);
    vmon::heartbeat();
    VMON_INFO("http backend handled request_id=%s", request_id.c_str());

    if (!req.get_header_value("x-vmon-force-error").empty()) {
      span.set_error("E_ECHO");
      VMON_ERROR("http backend forced error request_id=%s", request_id.c_str());
      res.status = 500;
    } else {
      res.status = 200;
    }

    // 模拟业务处理耗时；x-vmon-sleep-ms 用于演示慢调用与关键路径。
    int sleep_ms = 15;
    const std::string sleep_header = req.get_header_value("x-vmon-sleep-ms");
    if (!sleep_header.empty()) {
      const int requested = std::atoi(sleep_header.c_str());
      if (requested >= 0 && requested <= 5000) sleep_ms = requested;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  vmon::set_request_id(nullptr);
  vmon::set_task_id(nullptr);

  nlohmann::json body;
  body["ok"] = res.status == 200;
  body["request_id"] = request_id;
  res.set_content(body.dump(), "application/json");
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 19090;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    }
  }

  vmon::Config config;
  config.service_name = "http-backend";
  config.instance_id = "http-1";
  config.service_version = "0.1.0";
  const std::string agent = Env("VMON_AGENT_ENDPOINT");
  if (!agent.empty()) config.agent_endpoint = agent;
  const std::string control = Env("VMON_CONTROL_DIR");
  if (!control.empty()) config.control_dir = control;
  const std::string crash = Env("VMON_CRASH_DIR");
  if (!crash.empty()) config.crash_report_dir = crash;
  if (!vmon::init(config)) {
    std::printf("vmon disabled, business continues\n");
  }

  httplib::Server server;
  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("{\"ok\":true}", "application/json");
  });
  server.Post("/api/echo", HandleEcho);
  server.Get("/api/echo", HandleEcho);

  std::printf("http backend READY host=%s port=%d\n", host.c_str(), port);
  std::fflush(stdout);
  if (!server.listen(host, port)) {
    std::fprintf(stderr, "http backend listen failed: %s:%d\n", host.c_str(),
                 port);
    vmon::shutdown();
    return 1;
  }
  vmon::shutdown();
  return 0;
}
