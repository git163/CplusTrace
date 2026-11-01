// vmon-server 入口：加载配置、注册路由、启动后台规则线程与 HTTP 服务。
//
// 关停路径：SIGINT/SIGTERM 由主线程 sigwait 接收，先停止接收新连接，
// 再停止规则线程与转发线程，保证进程可被 systemd 优雅管理。
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <CLI/CLI.hpp>
#include <httplib.h>
#include <pthread.h>

#include "api.h"
#include "config.h"
#include "util.h"

#ifndef VMON_WEB_DIR
#define VMON_WEB_DIR "web"
#endif

namespace {

const char* kVersion = "0.1.0";

bool directory_exists(const std::string& path) {
  struct stat st;
  return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"vmon-server：局域网 C++ 系统维测服务中心", "vmon-server"};
  app.set_version_flag("--version", kVersion);

  std::string config_path;
  std::string listen_host;
  int listen_port = 0;
  std::string web_dir;
  std::string state_dir;
  std::string token;
  std::string vm_url;
  std::string vl_url;
  bool no_forward = false;
  bool disable_evidence = false;
  bool disable_mcp = false;
  std::string evidence_salt;
  std::string webhook_url;
  std::string webhook_token;
  bool redact_sensitive = false;

  app.add_option("-c,--config", config_path, "JSON 配置文件路径");
  app.add_option("--listen-host", listen_host, "监听地址（默认 0.0.0.0）");
  app.add_option("--port", listen_port, "监听端口（默认 18428）");
  app.add_option("--web-dir", web_dir, "Web UI 静态资源目录");
  app.add_option("--state-dir", state_dir, "状态目录（规则、证据包）");
  app.add_option("--token", token, "访问令牌（建议内网也设置）");
  app.add_option("--vm-url", vm_url, "VictoriaMetrics 地址，如 http://127.0.0.1:8428");
  app.add_option("--vl-url", vl_url, "VictoriaLogs 地址，如 http://127.0.0.1:9428");
  app.add_flag("--no-forward", no_forward, "关闭向 VM/VL 的转发");
  app.add_flag("--disable-evidence", disable_evidence, "关闭证据包导出（对外发行版）");
  app.add_flag("--disable-mcp", disable_mcp, "关闭 MCP 接口");
  app.add_option("--evidence-salt", evidence_salt, "证据包假名化盐值");
  app.add_option("--webhook-url", webhook_url,
                 "告警事件 Webhook 地址（http://，内网机器人/网关）");
  app.add_option("--webhook-token", webhook_token,
                 "Webhook 请求头 X-Vmon-Token（可选）");
  app.add_flag("--redact-sensitive", redact_sensitive,
               "入库前脱敏 password/token/email/IP/手机号（默认关闭）");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& ex) {
    return app.exit(ex);
  }

  vmon::server::ServerConfig config;
  if (!config_path.empty()) {
    std::string error;
    if (!vmon::server::load_server_config(config_path, &config, &error)) {
      std::cerr << "配置加载失败: " << error << std::endl;
      return 1;
    }
  }
  if (!listen_host.empty()) config.listen_host = listen_host;
  if (listen_port > 0) config.listen_port = listen_port;
  if (!web_dir.empty()) config.web_dir = web_dir;
  if (!state_dir.empty()) config.state_dir = state_dir;
  if (!token.empty()) config.token = token;
  if (!vm_url.empty()) config.vm_url = vm_url;
  if (!vl_url.empty()) config.vl_url = vl_url;
  if (no_forward) config.forward_enabled = false;
  if (disable_evidence) config.enable_evidence = false;
  if (disable_mcp) config.enable_mcp = false;
  if (!evidence_salt.empty()) config.evidence_salt = evidence_salt;
  if (!webhook_url.empty()) {
    config.webhook_enabled = true;
    config.webhook_url = webhook_url;
  }
  if (!webhook_token.empty()) config.webhook_token = webhook_token;
  if (redact_sensitive) {
    config.pipeline.enabled = true;
    config.pipeline.use_defaults = true;
  }

  if (config.web_dir.empty()) config.web_dir = VMON_WEB_DIR;
  if (!directory_exists(config.web_dir)) {
    std::cerr << "警告: Web 资源目录不存在: " << config.web_dir
              << "，仅提供 API" << std::endl;
    config.web_dir.clear();
  }
  if (!vmon::server::ensure_directory(config.state_dir)) {
    std::cerr << "无法创建状态目录: " << config.state_dir << std::endl;
    return 1;
  }
  if (config.token.empty()) {
    std::cerr << "警告: 未设置访问令牌，任何能访问该端口的主机都能读取维测数据"
              << std::endl;
  }

  // 先屏蔽信号，确保所有线程创建后信号只由主线程 sigwait 消费。
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);

  vmon::server::ServerContext context(config);
  httplib::Server server;
  server.set_payload_max_length(64ull * 1024 * 1024);
  server.set_read_timeout(30, 0);
  server.set_write_timeout(30, 0);
  server.set_idle_interval(0, 200000);
  vmon::server::register_routes(server, context);

  if (!server.bind_to_port(config.listen_host, config.listen_port)) {
    std::cerr << "端口绑定失败: " << config.listen_host << ":"
              << config.listen_port << std::endl;
    return 1;
  }

  context.start_background();
  std::cout << "vmon-server " << kVersion << " 已启动: http://"
            << config.listen_host << ":" << config.listen_port
            << "  state_dir=" << config.state_dir
            << "  转发=" << (context.forwarder.enabled() ? "开" : "关")
            << "  证据包=" << (config.enable_evidence ? "开" : "关")
            << "  脱敏=" << (context.pipeline.enabled() ? "开" : "关")
            << "  Webhook=" << (context.notifier.enabled() ? "开" : "关")
            << std::endl;

  std::thread listener([&server]() { server.listen_after_bind(); });

  int signal_number = 0;
  sigwait(&signals, &signal_number);
  std::cout << "收到信号 " << signal_number << "，正在停止..." << std::endl;
  server.stop();
  if (listener.joinable()) listener.join();
  context.stop_background();
  std::cout << "vmon-server 已停止" << std::endl;
  return 0;
}
