#include "metrics.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "agent.h"

namespace vmon {
namespace agent {
namespace {

std::string number(double value) {
  if (!std::isfinite(value)) return "0";
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return out.str();
}

common::Event make_metric(const std::string& name, double value,
                          const std::string& node, const std::string& agent) {
  common::Event event;
  event.type = "metric";
  event.time_ns = now_wall_ns();
  event.mono_ns = now_mono_ns();
  event.node = node;
  event.name = name;
  event.attrs["value"] = number(value);
  event.attrs["agent"] = agent;
  return event;
}

}  // namespace

std::vector<common::Event> host_metric_events(const HostInfo& info,
                                              size_t process_count,
                                              const std::string& node,
                                              const std::string& agent) {
  const std::string host = node.empty() ? info.node : node;
  const double mem_total = static_cast<double>(info.mem_total_kb) * 1024.0;
  const double mem_available =
      static_cast<double>(info.mem_available_kb) * 1024.0;
  const double mem_used =
      mem_total > mem_available ? mem_total - mem_available : 0.0;

  std::vector<common::Event> events;
  events.push_back(make_metric("host.load1", info.load1, host, agent));
  events.push_back(make_metric("host.load5", info.load5, host, agent));
  events.push_back(make_metric("host.load15", info.load15, host, agent));
  events.push_back(make_metric("host.mem_total_bytes", mem_total, host, agent));
  events.push_back(
      make_metric("host.mem_available_bytes", mem_available, host, agent));
  events.push_back(make_metric("host.mem_used_bytes", mem_used, host, agent));
  events.push_back(make_metric(
      "host.mem_used_ratio", mem_total > 0 ? mem_used / mem_total : 0.0, host,
      agent));
  events.push_back(make_metric("host.process_count",
                               static_cast<double>(process_count), host, agent));
  events.push_back(make_metric("host.uptime_seconds",
                               static_cast<double>(info.uptime_seconds), host,
                               agent));
  return events;
}

common::Event process_metric_event(const ProcessInfo& info,
                                   const std::string& node,
                                   const std::string& agent) {
  // 注意：进程名不是“服务名”，不能写入 event.service，否则会把
  // /bin/zsh 之类进程混进服务目录。进程身份放在标签与 pid 字段里。
  common::Event event = make_metric(
      "process.rss_bytes", static_cast<double>(info.rss_kb) * 1024.0, node,
      agent);
  event.pid = info.pid;
  event.attrs["process"] = info.name;
  event.attrs["pid"] = std::to_string(info.pid);
  event.attrs["threads"] = std::to_string(info.threads);
  return event;
}

}  // namespace agent
}  // namespace vmon
