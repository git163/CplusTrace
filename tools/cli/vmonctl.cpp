// vmonctl：中心服务的命令行客户端，适合脚本与现场排障。
//
// 所有查询走 HTTP API；输出默认是便于人读的摘要，--json 输出原始 JSON。
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace {

struct Endpoint {
  std::string host;
  int port = 80;
  std::string prefix;
};

bool parse_url(const std::string& url, Endpoint* out) {
  if (out == nullptr) return false;
  std::string rest = url;
  if (rest.compare(0, 7, "http://") == 0) {
    rest = rest.substr(7);
  } else if (rest.compare(0, 8, "https://") == 0) {
    std::fprintf(stderr, "vmonctl: 暂不支持 HTTPS，请使用内网 HTTP 或反向代理\n");
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    out->prefix = rest.substr(slash);
    rest = rest.substr(0, slash);
  }
  const size_t colon = rest.find(':');
  if (colon == std::string::npos) {
    out->host = rest;
    out->port = 80;
  } else {
    out->host = rest.substr(0, colon);
    out->port = std::atoi(rest.c_str() + colon + 1);
  }
  while (!out->prefix.empty() && out->prefix.back() == '/') out->prefix.pop_back();
  return !out->host.empty() && out->port > 0;
}

std::string url_encode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~' || c == '/') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

struct Client {
  Endpoint endpoint;
  std::string token;
  bool raw_json = false;

  httplib::Headers headers() const {
    httplib::Headers h;
    if (!token.empty()) h.emplace("X-Vmon-Token", token);
    return h;
  }

  nlohmann::json get(const std::string& path) const {
    httplib::Client client(endpoint.host, endpoint.port);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(15, 0);
    auto res = client.Get(endpoint.prefix + path, headers());
    if (!res) {
      std::fprintf(stderr, "vmonctl: 连接失败 %s:%d\n", endpoint.host.c_str(),
                   endpoint.port);
      std::exit(2);
    }
    if (res->status < 200 || res->status >= 300) {
      std::fprintf(stderr, "vmonctl: HTTP %d %s\n", res->status, res->body.c_str());
      std::exit(1);
    }
    return nlohmann::json::parse(res->body);
  }

  nlohmann::json post(const std::string& path, const nlohmann::json& body) const {
    httplib::Client client(endpoint.host, endpoint.port);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(60, 0);
    auto res = client.Post(endpoint.prefix + path, headers(), body.dump(),
                           "application/json");
    if (!res) {
      std::fprintf(stderr, "vmonctl: 连接失败 %s:%d\n", endpoint.host.c_str(),
                   endpoint.port);
      std::exit(2);
    }
    if (res->status < 200 || res->status >= 300) {
      std::fprintf(stderr, "vmonctl: HTTP %d %s\n", res->status, res->body.c_str());
      std::exit(1);
    }
    return nlohmann::json::parse(res->body);
  }
};

void print_json(const nlohmann::json& json) {
  std::cout << json.dump(2) << std::endl;
}

std::string value_or(const nlohmann::json& json, const char* key,
                     const std::string& fallback = "-") {
  auto it = json.find(key);
  if (it == json.end() || it->is_null()) return fallback;
  if (it->is_string()) return it->get<std::string>();
  return it->dump();
}

}  // namespace

int main(int argc, char** argv) {
  std::string server_url = "http://127.0.0.1:18428";
  std::string token;
  bool raw_json = false;

  CLI::App app{"vmonctl：vmon 维测服务中心命令行", "vmonctl"};
  app.set_version_flag("--version", "0.1.0");
  app.add_option("--server", server_url, "中心服务地址");
  app.add_option("--token", token, "访问令牌");
  app.add_flag("--json", raw_json, "输出原始 JSON");

  auto* health = app.add_subcommand("health", "健康检查");
  auto* overview = app.add_subcommand("overview", "总览");
  app.add_subcommand("services", "服务与实例");
  app.add_subcommand("nodes", "节点与进程");
  auto* traces = app.add_subcommand("traces", "查询调用链");
  std::string traces_request, traces_service;
  bool traces_error = false;
  int traces_limit = 50;
  traces->add_option("--request", traces_request, "request_id");
  traces->add_option("--service", traces_service, "服务名");
  traces->add_flag("--error", traces_error, "仅错误");
  traces->add_option("--limit", traces_limit, "条数");

  auto* trace = app.add_subcommand("trace", "查看一条调用链");
  std::string trace_id;
  trace->add_option("trace_id", trace_id, "trace_id")->required();

  auto* logs = app.add_subcommand("logs", "查询日志");
  std::string log_trace, log_request, log_service, log_level, log_text;
  int log_limit = 100;
  logs->add_option("--trace", log_trace, "trace_id");
  logs->add_option("--request", log_request, "request_id");
  logs->add_option("--service", log_service, "服务");
  logs->add_option("--level", log_level, "级别");
  logs->add_option("--text", log_text, "正文关键字");
  logs->add_option("--limit", log_limit, "条数");

  app.add_subcommand("clusters", "日志聚类");
  app.add_subcommand("events", "事件中心");
  app.add_subcommand("crashes", "崩溃列表");
  app.add_subcommand("profiles", "剖析列表");
  app.add_subcommand("inspect", "一键巡检");
  app.add_subcommand("rules", "告警规则");
  app.add_subcommand("commands", "命令记录");

  auto* diagnose = app.add_subcommand("diagnose", "一键诊断");
  std::string diag_trace, diag_request, diag_service, diag_node;
  int diag_pid = 0;
  bool diag_stack = false;
  diagnose->add_option("--trace", diag_trace, "trace_id");
  diagnose->add_option("--request", diag_request, "request_id");
  diagnose->add_option("--service", diag_service, "服务");
  diagnose->add_option("--node", diag_node, "节点");
  diagnose->add_option("--pid", diag_pid, "pid");
  diagnose->add_flag("--stack", diag_stack, "抓取全线程栈");

  auto* evidence = app.add_subcommand("evidence", "生成证据包");
  std::string ev_incident = "manual", ev_trace, ev_request, ev_service;
  int ev_hours = 1;
  evidence->add_option("--incident", ev_incident, "事件名");
  evidence->add_option("--trace", ev_trace, "trace_id");
  evidence->add_option("--request", ev_request, "request_id");
  evidence->add_option("--service", ev_service, "服务");
  evidence->add_option("--hours", ev_hours, "时间窗（小时）");

  auto* level = app.add_subcommand("log-level", "动态日志级别");
  std::string ll_level = "debug", ll_service, ll_node;
  int ll_pid = 0;
  int ll_ttl = 300000;
  level->add_option("--level", ll_level, "级别")->required();
  level->add_option("--service", ll_service, "服务");
  level->add_option("--node", ll_node, "节点");
  level->add_option("--pid", ll_pid, "pid");
  level->add_option("--ttl", ll_ttl, "TTL 毫秒");

  if (argc <= 1) {
    std::cout << app.help() << std::endl;
    return 0;
  }
  CLI11_PARSE(app, argc, argv);

  Client client;
  client.token = token;
  client.raw_json = raw_json;
  if (!parse_url(server_url, &client.endpoint)) return 2;

  if (*health) {
    const nlohmann::json data = client.get("/api/health");
    if (raw_json) return print_json(data), 0;
    std::cout << "status=" << value_or(data, "status")
              << " version=" << value_or(data, "version")
              << " uptime=" << value_or(data, "uptime_seconds") << "s\n"
              << "nodes=" << value_or(data, "nodes")
              << " instances=" << value_or(data, "instances")
              << " traces=" << value_or(data, "traces")
              << " logs=" << value_or(data, "logs") << "\n";
    return 0;
  }
  if (*overview) {
    const nlohmann::json data = client.get("/api/overview");
    if (raw_json) return print_json(data), 0;
    std::cout << "nodes=" << value_or(data, "node_count")
              << " services=" << value_or(data, "service_count")
              << " instances=" << value_or(data, "instance_count")
              << " traces(5m)=" << value_or(data, "trace_count_5m")
              << " error_rate=" << value_or(data, "error_rate") << "%\n"
              << "p50=" << value_or(data, "p50_ms") << "ms"
              << " p95=" << value_or(data, "p95_ms") << "ms"
              << " p99=" << value_or(data, "p99_ms") << "ms\n";
    for (const auto& trace : data.value("slow_traces", nlohmann::json::array())) {
      std::cout << "  slow " << value_or(trace, "trace_id")
                << " " << value_or(trace, "root_service")
                << " " << value_or(trace, "root_name")
                << " " << value_or(trace, "duration_ms") << "ms\n";
    }
    return 0;
  }
  if (app.got_subcommand("services")) {
    const nlohmann::json data = client.get("/api/services");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data) {
      std::cout << value_or(item, "service") << "  instances="
                << value_or(item, "alive_instances") << "/"
                << value_or(item, "instances") << " errors="
                << value_or(item, "error_count") << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("nodes")) {
    const nlohmann::json data = client.get("/api/nodes");
    if (raw_json) return print_json(data), 0;
    for (const auto& node : data) {
      std::cout << value_or(node, "name") << " os=" << value_or(node, "os")
                << " arch=" << value_or(node, "arch")
                << " load1=" << value_or(node, "load1")
                << " processes=" << node.value("processes", nlohmann::json::array()).size()
                << "\n";
    }
    return 0;
  }
  if (*traces) {
    std::string path = "/api/traces?limit=" + std::to_string(traces_limit);
    if (!traces_request.empty()) path += "&request_id=" + url_encode(traces_request);
    if (!traces_service.empty()) path += "&service=" + url_encode(traces_service);
    if (traces_error) path += "&error=1";
    const nlohmann::json data = client.get(path);
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("traces", nlohmann::json::array())) {
      std::cout << value_or(item, "trace_id") << "  "
                << (item.value("has_error", false) ? "ERROR" : "OK") << "  "
                << value_or(item, "root_service") << "/"
                << value_or(item, "root_name") << "  "
                << value_or(item, "duration_ms") << "ms  "
                << value_or(item, "time") << "\n";
    }
    return 0;
  }
  if (*trace) {
    const nlohmann::json data = client.get("/api/traces/" + url_encode(trace_id));
    if (raw_json) return print_json(data), 0;
    const auto summary = data.value("summary", nlohmann::json::object());
    std::cout << "trace " << value_or(data, "trace_id")
              << " request=" << value_or(data, "request_id")
              << " duration=" << value_or(summary, "duration_ns") << "ns"
              << " spans=" << value_or(summary, "span_count")
              << " services=" << value_or(summary, "service_count") << "\n";
    for (const auto& span : data.value("critical_path", nlohmann::json::array())) {
      std::cout << "  " << value_or(span, "service") << " / "
                << value_or(span, "name") << " " << value_or(span, "duration_ns")
                << "ns";
      if (span.contains("error_code")) std::cout << " error=" << span["error_code"];
      std::cout << "\n";
    }
    return 0;
  }
  if (*logs) {
    std::string path = "/api/logs?limit=" + std::to_string(log_limit);
    if (!log_trace.empty()) path += "&trace_id=" + url_encode(log_trace);
    if (!log_request.empty()) path += "&request_id=" + url_encode(log_request);
    if (!log_service.empty()) path += "&service=" + url_encode(log_service);
    if (!log_level.empty()) path += "&level=" + url_encode(log_level);
    if (!log_text.empty()) path += "&text=" + url_encode(log_text);
    const nlohmann::json data = client.get(path);
    if (raw_json) return print_json(data), 0;
    for (const auto& log : data.value("logs", nlohmann::json::array())) {
      std::cout << value_or(log, "time") << " [" << value_or(log, "level") << "] "
                << value_or(log, "service") << " " << value_or(log, "msg") << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("clusters")) {
    const nlohmann::json data = client.get("/api/logs/clusters?limit=200");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("clusters", nlohmann::json::array())) {
      std::cout << "[" << value_or(item, "count") << "x] "
                << value_or(item, "level") << " "
                << value_or(item, "service") << " "
                << value_or(item, "template") << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("events")) {
    const nlohmann::json data = client.get("/api/events?limit=100");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("events", nlohmann::json::array())) {
      std::cout << value_or(item, "time") << " [" << value_or(item, "severity")
                << "] " << value_or(item, "title") << " ("
                << value_or(item, "status") << ")\n";
    }
    return 0;
  }
  if (app.got_subcommand("crashes")) {
    const nlohmann::json data = client.get("/api/crashes?limit=100");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("crashes", nlohmann::json::array())) {
      std::cout << value_or(item, "last") << " " << value_or(item, "service")
                << " " << value_or(item, "signal") << " x"
                << value_or(item, "count") << " id=" << value_or(item, "id")
                << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("profiles")) {
    const nlohmann::json data = client.get("/api/profiles?limit=50");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("profiles", nlohmann::json::array())) {
      std::cout << value_or(item, "time") << " " << value_or(item, "service")
                << " " << value_or(item, "kind") << " samples="
                << value_or(item, "total_samples") << " id="
                << value_or(item, "id") << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("inspect")) {
    const nlohmann::json data = client.get("/api/inspection");
    if (raw_json) return print_json(data), 0;
    for (const auto& check : data.value("checks", nlohmann::json::array())) {
      std::cout << "[" << value_or(check, "status") << "] "
                << value_or(check, "id") << " " << value_or(check, "detail")
                << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("rules")) {
    const nlohmann::json data = client.get("/api/rules");
    if (raw_json) return print_json(data), 0;
    for (const auto& rule : data.value("rules", nlohmann::json::array())) {
      std::cout << value_or(rule, "id") << " type=" << value_or(rule, "type")
                << " threshold=" << value_or(rule, "threshold")
                << " severity=" << value_or(rule, "severity") << "\n";
    }
    return 0;
  }
  if (app.got_subcommand("commands")) {
    const nlohmann::json data = client.get("/api/commands?limit=100");
    if (raw_json) return print_json(data), 0;
    for (const auto& item : data.value("commands", nlohmann::json::array())) {
      std::cout << value_or(item, "id") << " " << value_or(item, "type")
                << " " << value_or(item, "status") << " "
                << value_or(item, "error", "") << "\n";
    }
    return 0;
  }
  if (*diagnose) {
    nlohmann::json body;
    body["trace_id"] = diag_trace;
    body["request_id"] = diag_request;
    body["service"] = diag_service;
    body["node"] = diag_node;
    body["pid"] = diag_pid;
    body["collect_stack"] = diag_stack;
    return print_json(client.post("/api/diagnose", body)), 0;
  }
  if (*evidence) {
    nlohmann::json body;
    body["incident"] = ev_incident;
    body["trace_id"] = ev_trace;
    body["request_id"] = ev_request;
    body["service"] = ev_service;
    body["window_ns"] = static_cast<uint64_t>(ev_hours) * 3600ull * 1000000000ull;
    const nlohmann::json data = client.post("/api/evidence", body);
    if (raw_json) return print_json(data), 0;
    std::cout << (data.value("ok", false) ? "ok" : "失败") << " "
              << value_or(data, "dir") << "\n";
    return data.value("ok", false) ? 0 : 1;
  }
  if (*level) {
    nlohmann::json body;
    body["level"] = ll_level;
    body["service"] = ll_service;
    body["node"] = ll_node;
    body["pid"] = ll_pid;
    body["ttl_ms"] = ll_ttl;
    return print_json(client.post("/api/control/log-level", body)), 0;
  }
  std::cout << app.help() << std::endl;
  return 0;
}
