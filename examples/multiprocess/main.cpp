// 多进程调用链示例：Gateway → Order → Inventory。
//
// 三个独立进程各自链接静态 SDK，通过 HTTP 头传播 W3C traceparent：
//   Gateway 创建根 Span，调用 Order；Order 续接父上下文，再调用 Inventory；
//   Inventory 产生业务错误 E_STOCK，错误沿链路回传。
//
// 每个进程只需要做两件事：
//   1) 入口从 traceparent 恢复父上下文，create_span_with_parent；
//   2) 出口用当前 Span 生成 traceparent，放进 HTTP 头。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <vmon/vmon.h>

#include "adapters/grpc/propagation.h"

namespace {

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

// SDK 的 request_id/task_id 只保存指针；sender 稍后才序列化。
// 用有界队列持有字符串，避免 handler 返回后悬垂。
const char* KeepId(const std::string& id) {
  static std::mutex mutex;
  static std::deque<std::string> ids;
  std::lock_guard<std::mutex> lock(mutex);
  ids.push_back(id);
  while (ids.size() > 4096) ids.pop_front();
  return ids.back().c_str();
}

void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Options {
  std::string role;
  int port = 0;
  std::string next;  // host:port，Gateway/Order 使用
};

bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--role" && i + 1 < argc) {
      options->role = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      options->port = std::atoi(argv[++i]);
    } else if (arg == "--next" && i + 1 < argc) {
      options->next = argv[++i];
    }
  }
  return !options->role.empty() && options->port > 0;
}

bool SplitHostPort(const std::string& value, std::string* host, int* port) {
  const size_t colon = value.rfind(':');
  if (colon == std::string::npos || colon == 0) return false;
  *host = value.substr(0, colon);
  *port = std::atoi(value.substr(colon + 1).c_str());
  return *port > 0;
}

// 根据角色执行本进程的业务，并把 traceparent 继续传给下游。
int CallNext(const std::string& next, const std::string& request_id,
             const std::string& task_id, vmon::Span* call_span) {
  std::string host;
  int port = 0;
  if (!SplitHostPort(next, &host, &port)) return 0;

  vmon::SpanContext ctx = vmon::current_context();
  httplib::Headers headers;
  headers.emplace("traceparent",
                  vmon::adapters::make_traceparent(ctx.trace_hi, ctx.trace_lo,
                                                   ctx.span, true));
  headers.emplace("x-request-id", request_id);
  headers.emplace("x-task-id", task_id);

  httplib::Client client(host, port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(5, 0);
  auto result = client.Post("/call", headers, "", "text/plain");
  const int status = result ? result->status : 0;
  if (status != 200) {
    call_span->set_error(status == 0 ? "E_CONNECT" : "E_DOWNSTREAM");
    VMON_ERROR("downstream failed status=%d", status);
  }
  return status;
}

void Handle(const httplib::Request& req, httplib::Response& res,
            const Options& options) {
  const std::string request_id = req.get_header_value("x-request-id");
  const std::string task_id = req.get_header_value("x-task-id");
  vmon::set_request_id(request_id.empty() ? nullptr : KeepId(request_id));
  vmon::set_task_id(task_id.empty() ? nullptr : KeepId(task_id));

  vmon::SpanContext parent;
  const std::string traceparent = req.get_header_value("traceparent");
  const bool has_parent =
      vmon::context_from_traceparent(traceparent.c_str(), &parent);

  const char* root_name = options.role == "gateway"
                              ? "Gateway/Handle"
                              : (options.role == "order" ? "Order/Create"
                                                         : "Inventory/Check");
  vmon::Span root = has_parent
                        ? vmon::create_span_with_parent(parent, root_name,
                                                        __FILE__, __LINE__)
                        : vmon::Span::create(root_name, __FILE__, __LINE__);

  VMON_INFO("%s received request_id=%s", options.role.c_str(),
            request_id.c_str());
  SleepMs(3);

  bool failed = false;
  if (options.role == "gateway") {
    {
      VMON_SPAN("Gateway/Auth");
      SleepMs(2);
      VMON_INFO("auth ok");
    }
    {
      vmon::Span call =
          vmon::Span::create("Gateway/CallOrder", __FILE__, __LINE__);
      const int status = CallNext(options.next, request_id, task_id, &call);
      if (status != 200) {
        failed = true;
        root.set_error("E_GATEWAY");
      }
      call.end();
    }
    {
      VMON_SPAN("Gateway/Respond");
      SleepMs(1);
    }
  } else if (options.role == "order") {
    {
      VMON_SPAN("Order/Validate");
      SleepMs(3);
      VMON_INFO("order validated");
    }
    {
      vmon::Span call =
          vmon::Span::create("Order/CallInventory", __FILE__, __LINE__);
      const int status = CallNext(options.next, request_id, task_id, &call);
      if (status != 200) {
        failed = true;
        root.set_error("E_ORDER_DOWNSTREAM");
      }
      call.end();
    }
  } else {
    {
      VMON_SPAN("Inventory/Query");
      SleepMs(5);
      VMON_WARN("stock is not enough");
    }
    failed = true;
    root.set_error("E_STOCK");
    VMON_ERROR("inventory check failed code=E_STOCK");
  }

  SleepMs(2);
  root.end();
  vmon::set_request_id(nullptr);
  vmon::set_task_id(nullptr);

  nlohmann::json body;
  body["role"] = options.role;
  body["ok"] = !failed;
  body["request_id"] = request_id;
  res.status = failed ? 500 : 200;
  res.set_content(body.dump(), "application/json");
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    std::fprintf(stderr,
                 "用法: %s --role gateway|order|inventory --port N "
                 "[--next host:port]\n",
                 argv[0]);
    return 2;
  }

  vmon::Config config;
  config.service_name = options.role;
  config.instance_id = options.role + "-1";
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
  server.Post("/call", [&options](const httplib::Request& req,
                                  httplib::Response& res) {
    Handle(req, res, options);
  });
  server.Get("/call", [&options](const httplib::Request& req,
                                 httplib::Response& res) {
    Handle(req, res, options);
  });

  std::printf("%s READY port=%d next=%s\n", options.role.c_str(),
              options.port, options.next.c_str());
  std::fflush(stdout);
  if (!server.listen("127.0.0.1", options.port)) {
    std::fprintf(stderr, "%s listen failed\n", options.role.c_str());
    vmon::shutdown();
    return 1;
  }
  vmon::shutdown();
  return 0;
}
