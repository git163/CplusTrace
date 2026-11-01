#include "probe.h"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <utility>

#include <httplib.h>

#include "agent.h"

namespace vmon {
namespace agent {
namespace {

bool split_url(const std::string& url, std::string* host, std::string* path) {
  std::string rest = url;
  if (rest.compare(0, 7, "http://") != 0) return false;
  rest = rest.substr(7);
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) {
    *host = rest;
    *path = "/";
  } else {
    *host = rest.substr(0, slash);
    *path = rest.substr(slash);
  }
  return !host->empty();
}

uint32_t number(const std::string& value, uint32_t fallback) {
  if (value.empty()) return fallback;
  return static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
}

common::Event metric(const std::string& name, double value,
                     const std::string& probe, const std::string& url,
                     const std::string& node, const std::string& agent,
                     uint64_t now_ns) {
  common::Event event;
  event.type = "metric";
  event.time_ns = now_ns;
  event.mono_ns = now_mono_ns();
  event.node = node;
  event.name = name;
  event.attrs["value"] = std::to_string(value);
  event.attrs["probe"] = probe;
  event.attrs["url"] = url;
  event.attrs["agent"] = agent;
  return event;
}

}  // namespace

bool parse_probe_spec(const std::string& spec, ProbeConfig* out,
                      std::string* error) {
  if (out == nullptr) return false;
  std::vector<std::string> parts;
  size_t begin = 0;
  while (begin <= spec.size()) {
    const size_t pos = spec.find(';', begin);
    parts.push_back(pos == std::string::npos ? spec.substr(begin)
                                             : spec.substr(begin, pos - begin));
    if (pos == std::string::npos) break;
    begin = pos + 1;
  }
  if (parts.empty()) {
    if (error) *error = "拨测配置为空";
    return false;
  }
  const size_t eq = parts[0].find('=');
  if (eq == std::string::npos || eq == 0 || eq + 1 >= parts[0].size()) {
    if (error) *error = "拨测配置格式应为 name=url";
    return false;
  }
  ProbeConfig probe;
  probe.name = parts[0].substr(0, eq);
  probe.url = parts[0].substr(eq + 1);
  if (probe.url.compare(0, 7, "http://") != 0) {
    if (error) *error = "拨测 URL 仅支持 http://";
    return false;
  }
  for (size_t i = 1; i < parts.size(); ++i) {
    const std::string& part = parts[i];
    const size_t sep = part.find('=');
    if (sep == std::string::npos) continue;
    const std::string key = part.substr(0, sep);
    const std::string value = part.substr(sep + 1);
    if (key == "interval_ms") {
      probe.interval_ms = number(value, probe.interval_ms);
    } else if (key == "timeout_ms") {
      probe.timeout_ms = number(value, probe.timeout_ms);
    } else if (key == "status") {
      probe.expected_status = static_cast<int>(number(value, 200));
    }
  }
  if (probe.interval_ms < 100) probe.interval_ms = 100;
  if (probe.timeout_ms < 100) probe.timeout_ms = 100;
  *out = probe;
  return true;
}

ProbeRunner::ProbeRunner(std::string node, std::string agent,
                         std::vector<ProbeConfig> probes, Emit emit)
    : node_(std::move(node)),
      agent_(std::move(agent)),
      probes_(std::move(probes)),
      emit_(std::move(emit)) {}

ProbeRunner::~ProbeRunner() { stop(); }

void ProbeRunner::start() {
  if (probes_.empty() || running_.exchange(true)) return;
  thread_ = std::thread(&ProbeRunner::run, this);
}

void ProbeRunner::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void ProbeRunner::run() {
  std::vector<uint64_t> next(probes_.size(), 0);
  while (running_.load()) {
    const uint64_t now = now_mono_ns();
    for (size_t i = 0; i < probes_.size(); ++i) {
      if (next[i] != 0 && now < next[i]) continue;
      next[i] = now + static_cast<uint64_t>(probes_[i].interval_ms) * 1000000ull;
      for (const common::Event& event : check(probes_[i], now_wall_ns())) {
        if (emit_) emit_(event);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

std::vector<common::Event> ProbeRunner::check(const ProbeConfig& probe,
                                              uint64_t now_ns) const {
  std::vector<common::Event> events;
  std::string host;
  std::string path;
  std::string error;
  int status = 0;
  double duration_ms = 0.0;
  bool up = false;

  if (!split_url(probe.url, &host, &path)) {
    error = "url 仅支持 http://";
  } else {
    httplib::Client client(host);
    client.set_connection_timeout(0, static_cast<int>(probe.timeout_ms) * 1000);
    client.set_read_timeout(0, static_cast<int>(probe.timeout_ms) * 1000);
    const auto start = std::chrono::steady_clock::now();
    auto result = client.Get(path.c_str());
    const auto end = std::chrono::steady_clock::now();
    duration_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count() /
        1000.0;
    if (!result) {
      error = "request failed";
    } else {
      status = result->status;
      up = probe.expected_status == 0
               ? (status >= 200 && status < 300)
               : status == probe.expected_status;
      if (!up) error = "status=" + std::to_string(status);
    }
  }

  common::Event up_event =
      metric("probe.up", up ? 1.0 : 0.0, probe.name, probe.url, node_, agent_,
             now_ns);
  up_event.attrs["status"] = std::to_string(status);
  if (!error.empty()) up_event.attrs["error"] = error;
  events.push_back(up_event);

  events.push_back(metric("probe.duration_ms", duration_ms, probe.name,
                          probe.url, node_, agent_, now_ns));
  events.push_back(metric("probe.status_code", static_cast<double>(status),
                          probe.name, probe.url, node_, agent_, now_ns));

  if (!up) {
    common::Event log_event;
    log_event.type = "log";
    log_event.time_ns = now_ns;
    log_event.mono_ns = now_mono_ns();
    log_event.node = node_;
    log_event.level = "error";
    log_event.name = "probe";
    log_event.msg = "probe failed name=" + probe.name + " url=" + probe.url +
                    " error=" + (error.empty() ? "unknown" : error);
    log_event.attrs["probe"] = probe.name;
    log_event.attrs["url"] = probe.url;
    events.push_back(log_event);
  }
  return events;
}

}  // namespace agent
}  // namespace vmon
