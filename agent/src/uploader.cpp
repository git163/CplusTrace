#include "uploader.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#include "agent.h"
#include "wal.h"

#include <httplib.h>

namespace vmon {
namespace agent {
namespace {

std::vector<std::string> read_lines(const std::string& path, bool* ok) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  if (!in) {
    *ok = false;
    return lines;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) lines.push_back(line);
  }
  *ok = true;
  return lines;
}

std::string url_encode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
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

}  // namespace

Uploader::Uploader(Wal* wal, std::string server_url, std::string node,
                   std::string agent, uint32_t interval_ms, uint32_t max_lines,
                   uint32_t max_bytes)
    : wal_(wal),
      server_url_(std::move(server_url)),
      node_(std::move(node)),
      agent_(std::move(agent)),
      interval_ms_(interval_ms == 0 ? 1000 : interval_ms),
      max_lines_(max_lines == 0 ? 500 : max_lines),
      max_bytes_(max_bytes == 0 ? 1024 * 1024 : max_bytes) {
  // prefix_ 为空时表示服务部署在根路径；不要补 "/"，否则拼出 "//v1/ingest"。
  split_url(server_url_, &host_, &port_, &prefix_);
}

Uploader::~Uploader() { stop(); }

void Uploader::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&Uploader::run, this);
}

void Uploader::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void Uploader::run() {
  // 低流量节点也要及时上传：当前段超过 1 秒就封存，避免数据长期滞留。
  const uint64_t seal_age_ns = 1000000000ull;
  while (running_.load()) {
    if (wal_ != nullptr) wal_->seal_if_older_than(seal_age_ns);
    upload_once();
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(interval_ms_);
    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
}

bool Uploader::upload_once() {
  if (wal_ == nullptr) return false;
  const std::vector<std::string> segments = wal_->sealed_segments();
  bool all_ok = true;
  for (const std::string& path : segments) {
    if (!upload_segment(path)) {
      all_ok = false;
      break;
    }
    wal_->remove_segment(path);
  }
  return all_ok;
}

bool Uploader::upload_segment(const std::string& path) {
  bool read_ok = false;
  const std::vector<std::string> lines = read_lines(path, &read_ok);
  if (!read_ok) return false;
  if (lines.empty()) return true;

  std::string batch;
  size_t batch_lines = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (batch_lines > 0 &&
        (batch_lines >= max_lines_ ||
         batch.size() + lines[i].size() + 1 > max_bytes_)) {
      if (!post_batch(batch)) return false;
      batch.clear();
      batch_lines = 0;
    }
    batch += lines[i];
    batch.push_back('\n');
    ++batch_lines;
  }
  if (batch_lines > 0 && !post_batch(batch)) return false;
  return true;
}

bool Uploader::post_batch(const std::string& body) {
  httplib::Client client(host_, port_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(5, 0);
  client.set_write_timeout(5, 0);

  const std::string path = prefix_ + "/v1/ingest?node=" + url_encode(node_) +
                           "&agent=" + url_encode(agent_);
  httplib::Headers headers;
  headers.emplace("X-Vmon-Node", node_);
  headers.emplace("X-Vmon-Agent", agent_);
  if (!token_.empty()) headers.emplace("X-Vmon-Token", token_);

  auto res = client.Post(path, headers, body, "application/x-ndjson");
  if (!res || res->status < 200 || res->status >= 300) {
    failed_batches_.fetch_add(1);
    return false;
  }
  uploaded_batches_.fetch_add(1);
  uint64_t lines = 0;
  for (char c : body) {
    if (c == '\n') ++lines;
  }
  uploaded_lines_.fetch_add(lines);
  return true;
}

}  // namespace agent
}  // namespace vmon
