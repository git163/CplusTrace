#include "forwarder.h"

#include <chrono>

#include <httplib.h>

#include "util.h"

namespace vmon {
namespace server {
namespace {

struct Endpoint {
  std::string host;
  int port = 0;
  std::string prefix;
  bool valid = false;
};

Endpoint parse_endpoint(const std::string& url) {
  Endpoint out;
  std::string rest = url;
  if (starts_with(rest, "http://")) {
    rest = rest.substr(7);
  } else if (starts_with(rest, "https://")) {
    return out;  // 局域网离线场景默认 HTTP；HTTPS 需在部署层用反代
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    out.prefix = rest.substr(slash);
    rest = rest.substr(0, slash);
  }
  const size_t colon = rest.find(':');
  if (colon == std::string::npos) {
    out.host = rest;
    out.port = 80;
  } else {
    out.host = rest.substr(0, colon);
    out.port = static_cast<int>(to_i64(rest.substr(colon + 1), 80));
  }
  out.valid = !out.host.empty() && out.port > 0;
  return out;
}

std::string escape_label(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\\' || c == '"' || c == '\n') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string attr(const common::Event& event, const char* key) {
  auto it = event.attrs.find(key);
  return it == event.attrs.end() ? std::string() : it->second;
}

}  // namespace

Forwarder::Forwarder(const ForwarderConfig& config) : config_(config) {
  enabled_ = !config.vm_url.empty() || !config.vl_url.empty();
}

Forwarder::~Forwarder() { stop(); }

void Forwarder::start() {
  if (!enabled_ || running_.exchange(true)) return;
  thread_ = std::thread(&Forwarder::run, this);
}

void Forwarder::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  flush_once();
}

void Forwarder::submit(const common::Event& event) {
  if (!enabled_) return;
  if (event.type != "log" && event.type != "metric") return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.size() >= config_.queue_limit) {
    dropped_.fetch_add(1);
    return;
  }
  queue_.push_back(event);
  cv_.notify_one();
}

std::vector<common::Event> Forwarder::take_batch() {
  std::vector<common::Event> batch;
  std::lock_guard<std::mutex> lock(mutex_);
  while (!queue_.empty() && batch.size() < config_.batch_size) {
    batch.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }
  return batch;
}

void Forwarder::flush_once() {
  if (!enabled_) return;
  while (true) {
    std::vector<common::Event> batch = take_batch();
    if (batch.empty()) break;
    bool ok = true;
    if (!config_.vm_url.empty()) ok = send_vm(batch) && ok;
    if (!config_.vl_url.empty()) ok = send_vl(batch) && ok;
    if (ok) {
      forwarded_.fetch_add(batch.size());
    } else {
      failed_.fetch_add(batch.size());
    }
  }
}

void Forwarder::run() {
  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(config_.flush_interval_ms),
                   [this] { return !running_.load() || !queue_.empty(); });
    }
    flush_once();
  }
}

bool Forwarder::send_vm(const std::vector<common::Event>& events) {
  const Endpoint endpoint = parse_endpoint(config_.vm_url);
  if (!endpoint.valid) return false;

  std::string body;
  size_t count = 0;
  for (const common::Event& event : events) {
    if (event.type != "metric" || event.name.empty()) continue;
    body += event.name;
    std::string labels;
    auto add_label = [&labels](const std::string& key, const std::string& value) {
      if (value.empty()) return;
      if (!labels.empty()) labels += ",";
      labels += key + "=\"" + escape_label(value) + "\"";
    };
    add_label("service", event.service);
    add_label("node", event.node);
    add_label("instance", event.instance);
    for (const auto& kv : event.attrs) {
      if (kv.first == "value") continue;
      add_label(kv.first, kv.second);
    }
    if (!labels.empty()) body += "{" + labels + "}";
    std::string value = attr(event, "value");
    if (value.empty()) value = "0";
    const uint64_t ms = event.time_ns / 1000000ull;
    body += " " + value + " " + std::to_string(ms) + "\n";
    ++count;
  }
  if (count == 0) return true;

  httplib::Client client(endpoint.host, endpoint.port);
  client.set_connection_timeout(0, config_.timeout_ms * 1000);
  client.set_read_timeout(0, config_.timeout_ms * 1000);
  client.set_write_timeout(0, config_.timeout_ms * 1000);
  const std::string path = endpoint.prefix + "/api/v1/import/prometheus";
  auto res = client.Post(path, body, "text/plain");
  return res && res->status >= 200 && res->status < 300;
}

bool Forwarder::send_vl(const std::vector<common::Event>& events) {
  const Endpoint endpoint = parse_endpoint(config_.vl_url);
  if (!endpoint.valid) return false;

  std::string body;
  size_t count = 0;
  for (const common::Event& event : events) {
    if (event.type != "log") continue;
    nlohmann::json line;
    line["_time"] = iso_time(event.time_ns);
    line["_msg"] = event.msg;
    if (!event.service.empty()) line["service"] = event.service;
    if (!event.node.empty()) line["node"] = event.node;
    if (!event.instance.empty()) line["instance"] = event.instance;
    if (!event.level.empty()) line["level"] = event.level;
    if (!event.trace.empty()) line["trace_id"] = event.trace;
    if (!event.span.empty()) line["span_id"] = event.span;
    if (!event.request_id.empty()) line["request_id"] = event.request_id;
    if (!event.task_id.empty()) line["task_id"] = event.task_id;
    if (!event.error_code.empty()) line["error_code"] = event.error_code;
    if (!event.file.empty()) line["file"] = event.file;
    if (event.line != 0) line["line"] = event.line;
    if (event.pid != 0) line["pid"] = event.pid;
    for (const auto& kv : event.attrs) {
      if (!line.contains(kv.first)) line[kv.first] = kv.second;
    }
    body += line.dump();
    body.push_back('\n');
    ++count;
  }
  if (count == 0) return true;

  httplib::Client client(endpoint.host, endpoint.port);
  client.set_connection_timeout(0, config_.timeout_ms * 1000);
  client.set_read_timeout(0, config_.timeout_ms * 1000);
  client.set_write_timeout(0, config_.timeout_ms * 1000);
  const std::string path =
      endpoint.prefix +
      "/insert/jsonline?_stream_fields=service,node&_msg_field=_msg&_time_field=_time";
  auto res = client.Post(path, body, "application/stream+json");
  return res && res->status >= 200 && res->status < 300;
}

}  // namespace server
}  // namespace vmon
