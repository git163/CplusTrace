// vmon-agent 入口。
//
// 职责（M1）：
//   1. 监听 UDS，接收 SDK 推送的 JSONL 事件并写入本地 WAL；
//   2. 扫描本节点进程与主机状态，作为 type=process / type=host 事件写入 WAL；
//   3. 跟踪配置的文件日志，作为 type=log 事件写入 WAL；
//   4. 后台把 WAL 推送到中心服务，断网期间本地缓存。
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <CLI/CLI.hpp>

#include "agent.h"
#include "command_runner.h"
#include "crash_collect.h"
#include "ingest.h"
#include "log_tail.h"
#include "metrics.h"
#include "probe.h"
#include "process_scan.h"
#include "uploader.h"
#include "wal.h"
#include "json_event.h"

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

// 把进程快照编码成一条 process 事件。
std::string process_event_json(const vmon::agent::ProcessInfo& info,
                               const std::string& node,
                               const std::string& agent) {
  vmon::common::Event ev;
  ev.type = "process";
  ev.time_ns = vmon::agent::now_wall_ns();
  ev.mono_ns = vmon::agent::now_mono_ns();
  ev.node = node;
  ev.name = info.name;
  ev.pid = info.pid;
  ev.attrs["ppid"] = std::to_string(info.ppid);
  ev.attrs["state"] = info.state;
  ev.attrs["rss_kb"] = std::to_string(info.rss_kb);
  ev.attrs["threads"] = std::to_string(info.threads);
  ev.attrs["cmdline"] = info.cmdline;
  ev.attrs["start_time_ticks"] = std::to_string(info.start_time_ticks);
  ev.attrs["agent"] = agent;
  return vmon::common::to_json(ev).dump();
}

std::string host_event_json(const vmon::agent::HostInfo& info,
                            const std::string& agent) {
  vmon::common::Event ev;
  ev.type = "host";
  ev.time_ns = vmon::agent::now_wall_ns();
  ev.node = info.node;
  ev.attrs["os"] = info.os;
  ev.attrs["kernel"] = info.kernel;
  ev.attrs["arch"] = info.arch;
  ev.attrs["load1"] = std::to_string(info.load1);
  ev.attrs["load5"] = std::to_string(info.load5);
  ev.attrs["load15"] = std::to_string(info.load15);
  ev.attrs["mem_total_kb"] = std::to_string(info.mem_total_kb);
  ev.attrs["mem_available_kb"] = std::to_string(info.mem_available_kb);
  ev.attrs["uptime_seconds"] = std::to_string(info.uptime_seconds);
  ev.attrs["agent"] = agent;
  return vmon::common::to_json(ev).dump();
}

}  // namespace

int main(int argc, char** argv) {
  vmon::agent::AgentConfig config;
  std::string config_path;
  std::vector<std::string> log_file_args;
  std::vector<std::string> probe_args;

  CLI::App app{"vmon-agent：节点维测采集代理", "vmon-agent"};
  app.set_version_flag("--version", "0.1.0");
  app.add_option("-c,--config", config_path, "JSON 配置文件路径");
  app.add_option("--server-url", config.server_url, "中心服务地址");
  app.add_option("--uds", config.listen_uds, "SDK 接入的 Unix Socket 路径");
  app.add_option("--state-dir", config.state_dir, "本地状态与 WAL 目录");
  app.add_option("--node", config.node, "节点名，默认取主机名");
  app.add_option("--token", config.token, "访问中心服务的令牌");
  app.add_option("--control-dir", config.control_dir,
                 "SDK 控制套接字目录，默认 /tmp/vmon-ctrl");
  app.add_option("--crash-dir", config.crash_dir,
                 "SDK 崩溃报告目录，默认 /tmp/vmon-crash");
  app.add_option("--command-interval-ms", config.command_interval_ms,
                 "命令轮询间隔（毫秒）");
  app.add_option("--process-filter", config.process_filters,
                 "进程命令行过滤子串，可重复；为空表示采集全部进程");
  app.add_option("--scan-interval-ms", config.scan_interval_ms,
                 "进程/主机扫描间隔（毫秒）");
  app.add_flag("--process-metrics", config.process_metrics,
               "额外采集进程级 RSS 指标（默认关闭，避免标签基数膨胀）");
  app.add_option("--upload-interval-ms", config.upload_interval_ms,
                 "上传间隔（毫秒）");
  app.add_option("--wal-segment-bytes", config.wal_segment_bytes,
                 "WAL 单段字节数");
  app.add_option("--wal-max-bytes", config.wal_max_bytes,
                 "WAL 总容量上限");
  app.add_option("--log-file", log_file_args,
                 "待采集日志，格式 path[=service]，可重复");
  app.add_option("--probe", probe_args,
                 "HTTP 拨测，格式 name=url[;interval_ms=30000;timeout_ms=3000;status=200]，可重复");
  CLI11_PARSE(app, argc, argv);

  std::string error;
  if (!config_path.empty() &&
      !vmon::agent::load_config(config_path, &config, &error)) {
    std::cerr << "vmon-agent: " << error << std::endl;
    return 1;
  }
  for (const std::string& arg : log_file_args) {
    vmon::agent::LogFileConfig lf;
    const size_t eq = arg.find('=');
    if (eq == std::string::npos) {
      lf.path = arg;
      const size_t slash = arg.rfind('/');
      lf.service = slash == std::string::npos ? arg : arg.substr(slash + 1);
    } else {
      lf.path = arg.substr(0, eq);
      lf.service = arg.substr(eq + 1);
    }
    if (!lf.path.empty()) config.log_files.push_back(lf);
  }
  std::vector<vmon::agent::ProbeConfig> probes;
  for (const std::string& spec : probe_args) {
    vmon::agent::ProbeConfig probe;
    std::string probe_error;
    if (!vmon::agent::parse_probe_spec(spec, &probe, &probe_error)) {
      std::cerr << "vmon-agent: 忽略非法拨测配置: " << probe_error << std::endl;
      continue;
    }
    probes.push_back(probe);
  }
  if (config.node.empty()) config.node = vmon::agent::hostname();
  const std::string agent = vmon::agent::agent_id();

  const std::string state_dir = config.state_dir;
  const std::string wal_dir = state_dir + "/wal";
  if (!vmon::agent::ensure_dir(wal_dir)) {
    std::cerr << "vmon-agent: 无法创建状态目录 " << wal_dir << std::endl;
    return 1;
  }

  vmon::agent::Wal wal;
  if (!wal.open(wal_dir, config.wal_segment_bytes, config.wal_max_bytes)) {
    std::cerr << "vmon-agent: WAL 打开失败" << std::endl;
    return 1;
  }

  vmon::agent::IngestServer ingest;
  ingest.start(config.listen_uds, [&wal](const std::string& line, uint32_t) {
    wal.append(line);
  });

  vmon::agent::Uploader uploader(
      &wal, config.server_url, config.node, agent, config.upload_interval_ms,
      config.max_lines_per_batch, 1024 * 1024);
  uploader.set_token(config.token);
  uploader.start();

  vmon::agent::CommandRunner command_runner(config, config.node, agent);
  command_runner.start();

  vmon::agent::CrashCollector crash_collector(config, config.node, agent);
  crash_collector.start();

  vmon::agent::ProbeRunner probe_runner(
      config.node, agent, probes,
      [&wal](const vmon::common::Event& event) {
        wal.append(vmon::common::to_json(event).dump());
      });
  probe_runner.start();

  vmon::agent::LogTailer tailer;
  for (const auto& lf : config.log_files) {
    tailer.add(lf.path, lf.service);
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  const uint64_t scan_interval_ns =
      static_cast<uint64_t>(config.scan_interval_ms == 0 ? 2000
                                                         : config.scan_interval_ms) *
      1000000ull;
  const uint64_t tail_interval_ns = 500ull * 1000000ull;
  uint64_t last_scan = 0;
  uint64_t last_tail = 0;

  std::cerr << "vmon-agent: node=" << config.node << " uds=" << config.listen_uds
            << " server=" << config.server_url << " state=" << state_dir
            << std::endl;

  while (g_running.load()) {
    const uint64_t now = vmon::agent::now_mono_ns();

    if (now - last_scan >= scan_interval_ns) {
      last_scan = now;
      const auto processes = vmon::agent::scan_processes();
      size_t kept = 0;
      for (const auto& info : processes) {
        if (!vmon::agent::process_matches(info, config.process_filters)) {
          continue;
        }
        ++kept;
        wal.append(process_event_json(info, config.node, agent));
        if (config.process_metrics) {
          wal.append(vmon::common::to_json(vmon::agent::process_metric_event(
                                              info, config.node, agent))
                         .dump());
        }
        if (kept >= 2000) break;
      }
      const vmon::agent::HostInfo host =
          vmon::agent::collect_host_info(config.node);
      wal.append(host_event_json(host, agent));
      // 主机指标走同一 WAL；UI 指标页与 /api/metrics 直接消费这些序列。
      for (const auto& metric : vmon::agent::host_metric_events(
               host, kept, config.node, agent)) {
        wal.append(vmon::common::to_json(metric).dump());
      }
    }

    if (now - last_tail >= tail_interval_ns) {
      last_tail = now;
      const auto lines = tailer.poll(5000);
      for (const auto& line : lines) {
        vmon::common::Event ev;
        ev.type = "log";
        ev.time_ns = line.time_ns;
        ev.node = config.node;
        ev.service = line.service;
        ev.instance = config.node + ":" + line.service;
        ev.file = line.path;
        ev.level = "info";
        ev.msg = line.msg;
        ev.attrs["source"] = "file";
        ev.attrs["agent"] = agent;
        wal.append(vmon::common::to_json(ev).dump());
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cerr << "vmon-agent: 正在退出" << std::endl;
  probe_runner.stop();
  crash_collector.stop();
  command_runner.stop();
  ingest.stop();
  uploader.stop();
  wal.close();
  return 0;
}
