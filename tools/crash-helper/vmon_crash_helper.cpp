// vmon-crash-helper：把一份崩溃上下文上报给中心服务。
//
// 两种用法：
//   1) 作为 core_pattern helper 的包装脚本调用：
//        kernel.core_pattern = |/opt/vmon/bin/vmon-crash-helper --server ... \
//            --node "$HOSTNAME" --signal %s --pid %p --binary %e
//      原始 core 仍由系统/包装脚本处理，本工具只上报元数据；
//   2) 读取 JSON 报告（--payload 文件或 stdin）并上报，字段透传。
//
// 工具不解析 core、不执行 shell、不读取任意文件（除 --payload 指定文件）。
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
    std::fprintf(stderr, "vmon-crash-helper: 暂不支持 HTTPS\n");
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

std::string url_encode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

std::string read_all(std::istream& in) {
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::string server_url = "http://127.0.0.1:18428";
  std::string token;
  std::string node;
  std::string service;
  std::string binary;
  std::string build_id;
  std::string signal;
  std::string reason = "core";
  std::string payload_path;
  int pid = 0;
  std::vector<std::string> frames;

  CLI::App app{"vmon-crash-helper：崩溃上下文上报", "vmon-crash-helper"};
  app.set_version_flag("--version", "0.1.0");
  app.add_option("--server", server_url, "中心服务地址");
  app.add_option("--token", token, "访问令牌");
  app.add_option("--node", node, "节点名");
  app.add_option("--service", service, "服务名");
  app.add_option("--binary", binary, "二进制路径");
  app.add_option("--build-id", build_id, "build-id / UUID");
  app.add_option("--signal", signal, "信号名，如 SIGSEGV");
  app.add_option("--reason", reason, "崩溃原因");
  app.add_option("--pid", pid, "崩溃进程 pid");
  app.add_option("--frame", frames, "原始栈帧，可重复");
  app.add_option("--payload", payload_path, "JSON 报告文件；- 表示 stdin");
  CLI11_PARSE(app, argc, argv);

  nlohmann::json body = nlohmann::json::object();
  if (!payload_path.empty()) {
    if (payload_path == "-") {
      body = nlohmann::json::parse(read_all(std::cin));
    } else {
      std::ifstream in(payload_path);
      if (!in) {
        std::fprintf(stderr, "vmon-crash-helper: 无法打开 %s\n",
                     payload_path.c_str());
        return 2;
      }
      body = nlohmann::json::parse(read_all(in));
    }
    if (!body.is_object()) {
      std::fprintf(stderr, "vmon-crash-helper: payload 必须是 JSON 对象\n");
      return 2;
    }
  }

  if (!node.empty() && !body.contains("node")) body["node"] = node;
  if (!service.empty() && !body.contains("service")) body["service"] = service;
  if (!binary.empty() && !body.contains("binary")) body["binary"] = binary;
  if (!build_id.empty() && !body.contains("build_id")) body["build_id"] = build_id;
  if (!signal.empty() && !body.contains("signal")) body["signal"] = signal;
  if (!reason.empty() && !body.contains("reason")) body["reason"] = reason;
  if (pid != 0 && !body.contains("pid")) body["pid"] = pid;
  if (!frames.empty() && !body.contains("frames")) body["frames"] = frames;
  body["time_ns"] = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  std::string host;
  std::string prefix;
  int port = 0;
  if (!parse_url(server_url, &host, &port, &prefix)) return 2;

  httplib::Client client(host, port);
  client.set_connection_timeout(3, 0);
  client.set_read_timeout(10, 0);
  client.set_write_timeout(5, 0);

  httplib::Headers headers;
  headers.emplace("Content-Type", "application/json");
  if (!token.empty()) headers.emplace("X-Vmon-Token", token);
  const std::string node_value = body.value("node", node);
  const std::string path = prefix + "/v1/crash?node=" + url_encode(node_value);
  auto res = client.Post(path, headers, body.dump(), "application/json");
  if (!res || res->status < 200 || res->status >= 300) {
    std::fprintf(stderr, "vmon-crash-helper: 上报失败 %s\n",
                 res ? res->body.c_str() : "(连接失败)");
    return 1;
  }
  std::printf("%s\n", res->body.c_str());
  return 0;
}
