#include "log_cluster.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util.h"

namespace vmon {
namespace server {
namespace {

bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool all_hex(const std::string& s) {
  if (s.size() < 8) return false;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool is_uuid(const std::string& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  return true;
}

bool looks_like_number(const std::string& s) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[i] == '-' || s[i] == '+') ++i;
  bool digit = false;
  bool dot = false;
  for (; i < s.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) {
      digit = true;
    } else if (s[i] == '.' && !dot) {
      dot = true;
    } else {
      return false;
    }
  }
  return digit;
}

bool is_ip_like(const std::string& s) {
  // 简单判定：数字与 '.' 组成且至少三个点，或带端口。
  int dots = 0;
  bool digit = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '.') {
      ++dots;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      digit = true;
    } else if (c == ':' && dots >= 3) {
      break;
    } else if (c == '%') {
      break;
    } else {
      return false;
    }
  }
  return digit && dots >= 3;
}

std::string normalize_value(const std::string& key, std::string value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  if (value.empty()) return "<empty>";
  if (key == "request_id" || key == "requestid" || key == "req_id" ||
      key == "trace_id" || key == "task_id" || key == "taskid" ||
      key == "command_id" || key == "span_id" || key == "session_id" ||
      key == "user_id" || key == "uid" || key == "order_id" ||
      key == "error_code" || key == "errcode" || key == "errno" ||
      key == "code" || key == "peer" || key == "addr" || key == "url" ||
      key == "path" || key == "file" || key == "method" ||
      key == "rpc_method" || key == "pid" || key == "tid" || key == "fd" ||
      key == "port" || key == "ip" || key == "host" || key == "thread") {
    return "<var>";
  }
  if (all_digits(value)) return "<num>";
  if (looks_like_number(value)) return "<num>";
  if (is_uuid(value)) return "<uuid>";
  if (is_ip_like(value)) return "<ip>";
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X') &&
      all_hex(value.substr(2))) {
    return "<hex>";
  }
  if (all_hex(value)) return "<hex>";
  return value;
}

std::string normalize_token(const std::string& token) {
  if (token.empty()) return token;
  // key=value / key:value 形式：保留 key，归一化 value。
  const size_t eq = token.find('=');
  if (eq != std::string::npos && eq > 0 && eq + 1 < token.size() &&
      token[eq + 1] != '=') {
    std::string key = token.substr(0, eq);
    // key 前后可能带标点，去掉非字母数字前缀。
    while (!key.empty() && !std::isalnum(static_cast<unsigned char>(key[0]))) {
      key.erase(0, 1);
    }
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!lower.empty()) {
      return key + "=" + normalize_value(lower, token.substr(eq + 1));
    }
  }
  if (token.size() >= 18 && token.find("://") != std::string::npos) return "<url>";
  if (token[0] == '/' && token.size() > 1) return "<path>";
  if (is_uuid(token)) return "<uuid>";
  if (is_ip_like(token)) return "<ip>";
  if (all_digits(token)) return "<num>";
  if (looks_like_number(token)) return "<num>";
  if (token.size() > 2 && token[0] == '0' &&
      (token[1] == 'x' || token[1] == 'X') && all_hex(token.substr(2))) {
    return "<hex>";
  }
  if (all_hex(token) && token.size() >= 12) return "<hex>";
  // 带单位的数字：120ms、3.5s、10us、2ns
  const std::string units[] = {"ms", "us", "ns", "s"};
  for (const std::string& unit : units) {
    if (token.size() > unit.size() &&
        token.compare(token.size() - unit.size(), unit.size(), unit) == 0) {
      const std::string number = token.substr(0, token.size() - unit.size());
      if (looks_like_number(number) || all_digits(number)) return "<dur>";
    }
  }
  // 尾部标点去掉后再判断一次（例如 "123," )
  size_t end = token.size();
  while (end > 0 && (token[end - 1] == ',' || token[end - 1] == ';' ||
                     token[end - 1] == ')' || token[end - 1] == ']' ||
                     token[end - 1] == '}')) {
    --end;
  }
  if (end < token.size()) {
    const std::string core = normalize_token(token.substr(0, end));
    if (core != token.substr(0, end)) return core + token.substr(end);
  }
  return token;
}

bool key_interesting(const std::string& key) {
  static const char* keys[] = {
      "request_id", "requestid", "req_id", "trace_id", "task_id", "taskid",
      "command_id", "error_code", "errcode", "errno", "code", "cost",
      "cost_ms", "latency", "duration", "elapsed", "retry", "retries",
      "timeout", "status", "rpc_method", "method", "peer", "url", "fd",
      "pid", "tid", "thread", "queue", "queue_depth"};
  for (const char* candidate : keys) {
    if (key == candidate) return true;
  }
  return false;
}

}  // namespace

std::string LogClusterer::make_template(const std::string& msg) {
  std::string text = trim(msg);
  if (text.empty()) return "<empty>";

  std::vector<std::string> tokens;
  size_t begin = 0;
  while (begin < text.size()) {
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
      ++begin;
    }
    size_t end = begin;
    while (end < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[end]))) {
      ++end;
    }
    if (end > begin) tokens.push_back(text.substr(begin, end - begin));
    begin = end;
  }
  if (tokens.empty()) return "<empty>";

  std::string out;
  const size_t limit = tokens.size() > 16 ? 16 : tokens.size();
  for (size_t i = 0; i < limit; ++i) {
    if (i > 0) out.push_back(' ');
    out += normalize_token(tokens[i]);
  }
  if (tokens.size() > limit) out += " ...";
  return out;
}

std::string LogClusterer::make_fingerprint(const std::string& level,
                                           const std::string& service,
                                           const std::string& error_code,
                                           const std::string& tmpl) {
  return hex_hash(level + "|" + service + "|" + error_code + "|" + tmpl);
}

std::unordered_map<std::string, std::string> LogClusterer::extract_keys(
    const std::string& msg) {
  std::unordered_map<std::string, std::string> out;
  const size_t size = msg.size();
  size_t i = 0;
  while (i < size) {
    // 跳过非键起始字符。
    if (!std::isalpha(static_cast<unsigned char>(msg[i])) && msg[i] != '_') {
      ++i;
      continue;
    }
    const size_t key_begin = i;
    while (i < size && (std::isalnum(static_cast<unsigned char>(msg[i])) ||
                        msg[i] == '_')) {
      ++i;
    }
    std::string key = msg.substr(key_begin, i - key_begin);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t pos = i;
    while (pos < size && (msg[pos] == ' ' || msg[pos] == '"' || msg[pos] == '\'')) {
      ++pos;
    }
    if (pos >= size || msg[pos] != '=') continue;
    ++pos;
    while (pos < size && (msg[pos] == ' ' || msg[pos] == '"' || msg[pos] == '\'')) {
      ++pos;
    }
    const size_t value_begin = pos;
    while (pos < size && !std::isspace(static_cast<unsigned char>(msg[pos])) &&
           msg[pos] != ',' && msg[pos] != ';' && msg[pos] != ')' &&
           msg[pos] != ']' && msg[pos] != '}' && msg[pos] != '"' &&
           msg[pos] != '\'') {
      ++pos;
    }
    if (key_interesting(key) && pos > value_begin && out.size() < 16) {
      out[key] = msg.substr(value_begin, pos - value_begin);
    }
    i = pos > i ? pos : i + 1;
  }
  return out;
}

LogClusterer::LogClusterer(size_t max_clusters)
    : max_clusters_(max_clusters == 0 ? 5000 : max_clusters) {}

LogClusterInfo LogClusterer::annotate(const std::string& msg,
                                      const std::string& level,
                                      const std::string& service,
                                      const std::string& node,
                                      const std::string& error_code,
                                      uint64_t time_ns) {
  const std::string tmpl = make_template(msg);
  const std::string id = make_fingerprint(level, service, error_code, tmpl);

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = clusters_.find(id);
  if (it == clusters_.end()) {
    Entry entry;
    entry.info.id = id;
    entry.info.tmpl = tmpl;
    entry.info.level = level;
    entry.info.service = service;
    entry.info.error_code = error_code;
    entry.info.sample = msg;
    entry.info.last_sample = msg;
    entry.info.count = 0;
    entry.info.first_ns = time_ns;
    entry.info.last_ns = time_ns;
    entry.last_touch = time_ns;
    it = clusters_.insert(std::make_pair(id, entry)).first;
    order_.push_back(id);

    while (clusters_.size() > max_clusters_ && !order_.empty()) {
      const std::string victim = order_.front();
      order_.pop_front();
      auto victim_it = clusters_.find(victim);
      if (victim_it != clusters_.end() && victim_it != it) {
        clusters_.erase(victim_it);
      }
    }
  }

  LogClusterInfo& info = it->second.info;
  ++info.count;
  if (info.first_ns == 0 || time_ns < info.first_ns) info.first_ns = time_ns;
  if (time_ns > info.last_ns) {
    info.last_ns = time_ns;
    info.last_sample = msg;
  }
  it->second.last_touch = time_ns;
  if (!node.empty() &&
      std::find(info.nodes.begin(), info.nodes.end(), node) == info.nodes.end() &&
      info.nodes.size() < 16) {
    info.nodes.push_back(node);
  }
  if (!service.empty() &&
      std::find(info.services.begin(), info.services.end(), service) ==
          info.services.end() &&
      info.services.size() < 16) {
    info.services.push_back(service);
  }
  return info;
}

std::vector<LogClusterInfo> LogClusterer::top(size_t limit) const {
  std::vector<LogClusterInfo> out;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out.reserve(clusters_.size());
    for (const auto& kv : clusters_) out.push_back(kv.second.info);
  }
  std::sort(out.begin(), out.end(),
            [](const LogClusterInfo& a, const LogClusterInfo& b) {
              if (a.count != b.count) return a.count > b.count;
              return a.last_ns > b.last_ns;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

std::vector<LogClusterInfo> LogClusterer::list(size_t limit) const {
  std::vector<LogClusterInfo> out;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out.reserve(clusters_.size());
    for (const auto& kv : clusters_) out.push_back(kv.second.info);
  }
  std::sort(out.begin(), out.end(),
            [](const LogClusterInfo& a, const LogClusterInfo& b) {
              return a.last_ns > b.last_ns;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

size_t LogClusterer::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return clusters_.size();
}

}  // namespace server
}  // namespace vmon
