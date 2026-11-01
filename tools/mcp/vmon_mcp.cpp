// vmon-mcp：把中心服务的 MCP HTTP 接口桥接为 stdio MCP Server。
//
// Claude Code / 其他 MCP 客户端通过 stdio 启动本程序；每一行 JSON-RPC
// 请求原样转发到 vmon-server 的 /mcp，再把响应写回 stdout。
// 注意：stdout 只能输出协议数据，日志一律写 stderr。
#include <cstdio>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace {

bool parse_url(const std::string& url, std::string* host, int* port,
               std::string* prefix) {
  std::string rest = url;
  if (rest.compare(0, 7, "http://") == 0) {
    rest = rest.substr(7);
  } else if (rest.compare(0, 8, "https://") == 0) {
    std::fprintf(stderr, "vmon-mcp: 暂不支持 HTTPS\n");
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    *prefix = rest.substr(slash);
    rest = rest.substr(0, slash);
  }
  const size_t colon = rest.find(':');
  if (colon == std::string::npos) {
    *host = rest;
    *port = 80;
  } else {
    *host = rest.substr(0, colon);
    *port = std::atoi(rest.c_str() + colon + 1);
  }
  while (!prefix->empty() && prefix->back() == '/') prefix->pop_back();
  return !host->empty() && *port > 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string server_url = "http://127.0.0.1:18428";
  std::string token;

  CLI::App app{"vmon-mcp：MCP stdio 桥接", "vmon-mcp"};
  app.set_version_flag("--version", "0.1.0");
  app.add_option("--server", server_url, "中心服务地址");
  app.add_option("--token", token, "访问令牌");
  CLI11_PARSE(app, argc, argv);

  std::string host;
  std::string prefix;
  int port = 0;
  if (!parse_url(server_url, &host, &port, &prefix)) return 2;

  httplib::Client client(host, port);
  client.set_connection_timeout(3, 0);
  client.set_read_timeout(60, 0);
  client.set_write_timeout(10, 0);

  httplib::Headers headers;
  headers.emplace("Content-Type", "application/json");
  if (!token.empty()) headers.emplace("X-Vmon-Token", token);

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    nlohmann::json request;
    try {
      request = nlohmann::json::parse(line);
    } catch (const std::exception& ex) {
      nlohmann::json error = {{"jsonrpc", "2.0"},
                              {"id", nullptr},
                              {"error", {{"code", -32700}, {"message", ex.what()}}}};
      std::cout << error.dump() << std::endl;
      continue;
    }

    // JSON-RPC 通知没有 id，不应返回响应。
    const bool notification = request.value("method", std::string())
                                  .compare(0, 14, "notifications/") == 0;
    auto res = client.Post(prefix + "/mcp", headers, request.dump(),
                           "application/json");
    if (notification) continue;
    if (!res) {
      nlohmann::json error = {
          {"jsonrpc", "2.0"},
          {"id", request.contains("id") ? request["id"] : nlohmann::json()},
          {"error", {{"code", -32000}, {"message", "无法连接 vmon-server"}}}};
      std::cout << error.dump() << std::endl;
      continue;
    }
    std::string body = res->body;
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
      body.pop_back();
    }
    if (body.empty()) {
      nlohmann::json error = {
          {"jsonrpc", "2.0"},
          {"id", request.contains("id") ? request["id"] : nlohmann::json()},
          {"error", {{"code", -32000}, {"message", "空响应"}}}};
      std::cout << error.dump() << std::endl;
      continue;
    }
    std::cout << body << std::endl;
  }
  return 0;
}
