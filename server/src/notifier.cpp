#include "notifier.h"

#include <chrono>
#include <utility>

#include <httplib.h>

#include "serialize.h"

namespace vmon {
namespace server {
namespace {

const size_t kMaxQueue = 1000;

bool split_url(const std::string& url, std::string* host, std::string* path,
               std::string* error) {
  std::string rest = url;
  if (rest.compare(0, 7, "http://") == 0) {
    rest = rest.substr(7);
  } else {
    if (error) *error = "Webhook 仅支持 http://（内网地址）";
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) {
    *host = rest;
    *path = "/";
  } else {
    *host = rest.substr(0, slash);
    *path = rest.substr(slash);
  }
  if (host->empty()) {
    if (error) *error = "Webhook URL 缺少主机";
    return false;
  }
  return true;
}

}  // namespace

Notifier::Notifier(const NotifierConfig& config) : config_(config) {}

Notifier::~Notifier() { stop(); }

void Notifier::start() {
  if (!enabled()) return;
  if (running_.exchange(true)) return;
  thread_ = std::thread(&Notifier::run, this);
}

void Notifier::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void Notifier::enqueue(const EventRecord& event) {
  if (!enabled()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxQueue) {
      queue_.pop_front();
      dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    queue_.push_back(event);
  }
  cv_.notify_one();
}

void Notifier::run() {
  while (running_.load()) {
    EventRecord event;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(200), [this]() {
        return !running_.load() || !queue_.empty();
      });
      if (!running_.load() && queue_.empty()) break;
      if (queue_.empty()) continue;
      event = std::move(queue_.front());
      queue_.pop_front();
    }
    if (!post(event)) {
      failed_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

bool Notifier::post(const EventRecord& event) {
  std::string host;
  std::string path;
  std::string error;
  if (!split_url(config_.url, &host, &path, &error)) {
    return false;
  }

  nlohmann::json payload;
  payload["source"] = "vmon";
  payload["event"] = event_to_json(event);
  const std::string body = payload.dump();

  httplib::Client client(host);
  const uint32_t timeout_ms = config_.timeout_ms == 0 ? 3000 : config_.timeout_ms;
  client.set_connection_timeout(0, static_cast<int>(timeout_ms) * 1000);
  client.set_read_timeout(0, static_cast<int>(timeout_ms) * 1000);
  client.set_write_timeout(0, static_cast<int>(timeout_ms) * 1000);

  httplib::Headers headers;
  headers.emplace("X-Vmon-Event", event.id);
  if (!config_.token.empty()) headers.emplace("X-Vmon-Token", config_.token);

  auto result = client.Post(path.c_str(), headers, body, "application/json");
  if (!result) return false;
  const bool ok = result->status >= 200 && result->status < 300;
  if (ok) sent_.fetch_add(1, std::memory_order_relaxed);
  return ok;
}

}  // namespace server
}  // namespace vmon
