#include "crash.h"

#include <algorithm>

#include "symbolizer/symbolizer.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

std::string normalize_frame(const std::string& frame) {
  std::string text = trim(frame);
  // 去掉 "#3 " 这类帧序号与 "pc " 前缀，避免同一栈因序号差异算成不同指纹。
  if (!text.empty() && text[0] == '#') {
    size_t pos = 0;
    while (pos < text.size() && text[pos] != ' ') ++pos;
    text = trim(text.substr(pos));
  }
  if (text.compare(0, 3, "pc ") == 0) text = trim(text.substr(3));
  return text;
}

std::vector<std::string> split_frames(const std::string& text) {
  std::vector<std::string> out;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t newline = text.find('\n', begin);
    std::string line =
        newline == std::string::npos ? text.substr(begin)
                                     : text.substr(begin, newline - begin);
    line = trim(line);
    if (!line.empty()) out.push_back(line);
    if (newline == std::string::npos) break;
    begin = newline + 1;
  }
  return out;
}

}  // namespace

CrashStore::CrashStore(size_t max_records)
    : max_records_(max_records == 0 ? 1000 : max_records) {}

std::string CrashStore::fingerprint_of(const std::string& build_id,
                                       const std::string& signal,
                                       const std::vector<std::string>& frames) {
  std::string key = build_id + "|" + signal;
  const size_t limit = frames.size() > 5 ? 5 : frames.size();
  for (size_t i = 0; i < limit; ++i) {
    key += "|";
    key += normalize_frame(frames[i]);
  }
  return hex_hash(key);
}

std::string CrashStore::add_event(const common::Event& event,
                                  const std::string& node_hint) {
  CrashRecord record;
  record.time_ns = event.time_ns;
  record.node = event.node.empty() ? node_hint : event.node;
  record.service = event.service;
  record.instance = event.instance;
  record.version = event.version;
  record.pid = event.pid;
  record.signal = event.name;
  record.reason = event.msg;

  auto attr = [&event](const char* key, const std::string& fallback = std::string()) {
    auto it = event.attrs.find(key);
    return it == event.attrs.end() ? fallback : it->second;
  };
  record.binary = attr("binary");
  record.build_id = attr("build_id");
  record.report = attr("report");
  record.attrs = event.attrs;
  const std::string frames_text = attr("frames");
  if (!frames_text.empty()) record.raw_frames = split_frames(frames_text);
  return add_record(std::move(record), true);
}

std::string CrashStore::add_json(const nlohmann::json& json,
                                 const std::string& node_hint) {
  CrashRecord record;
  record.time_ns = json.value("time_ns", now_ns());
  record.node = json.value("node", node_hint);
  record.service = json.value("service", std::string());
  record.instance = json.value("instance", std::string());
  record.version = json.value("version", std::string());
  record.pid = json.value("pid", 0);
  record.signal = json.value("signal", std::string());
  record.reason = json.value("reason", std::string());
  record.binary = json.value("binary", std::string());
  record.build_id = json.value("build_id", std::string());
  record.report = json.value("report", std::string());

  if (json.contains("frames") && json["frames"].is_array()) {
    for (const auto& frame : json["frames"]) {
      if (frame.is_string()) record.raw_frames.push_back(frame.get<std::string>());
    }
  }
  if (json.contains("attrs") && json["attrs"].is_object()) {
    for (auto it = json["attrs"].begin(); it != json["attrs"].end(); ++it) {
      record.attrs[it.key()] =
          it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
    }
  }
  return add_record(std::move(record), true);
}

std::string CrashStore::add_record(CrashRecord record, bool symbolize) {
  if (record.build_id.empty() && !record.binary.empty()) {
    symbolizer::Symbolizer tool;
    const symbolizer::BuildId id = tool.build_id_of(record.binary);
    if (id.valid) record.build_id = id.value;
  }

  if (symbolize && !record.binary.empty() && !record.raw_frames.empty()) {
    std::vector<std::string> addresses;
    addresses.reserve(record.raw_frames.size());
    for (const std::string& frame : record.raw_frames) {
      std::string address = symbolizer::Symbolizer::extract_address(frame);
      if (!address.empty()) addresses.push_back(address);
    }
    if (!addresses.empty()) {
      symbolizer::Symbolizer tool;
      std::vector<symbolizer::Frame> resolved;
      std::string error;
      if (tool.symbolize(record.binary, addresses, &resolved, &error)) {
        for (const auto& frame : resolved) {
          CrashFrame out;
          out.address = frame.address;
          out.function = frame.function;
          out.file = frame.file;
          out.line = frame.line;
          out.resolved = frame.resolved;
          record.frames.push_back(out);
        }
      }
    }
  }

  // 未能符号化时保留原始帧文本，方便人工排查。
  if (record.frames.empty()) {
    for (const std::string& raw : record.raw_frames) {
      CrashFrame frame;
      frame.address = symbolizer::Symbolizer::extract_address(raw);
      // 原始帧形如 "0x1000 main+0x10"：保留地址后的符号，避免把地址
      // 和函数名重复塞进 function 字段。
      std::string text = trim(raw);
      const size_t address_pos = frame.address.empty()
                                     ? std::string::npos
                                     : text.find(frame.address);
      if (address_pos != std::string::npos) {
        text = trim(text.substr(address_pos + frame.address.size()));
      }
      if (!text.empty() && text[0] == '+') text.clear();
      frame.function = text;
      record.frames.push_back(frame);
    }
  }

  const std::string fingerprint =
      fingerprint_of(record.build_id, record.signal, record.raw_frames);
  record.id = fingerprint;
  if (record.first_ns == 0) record.first_ns = record.time_ns;
  record.last_ns = record.time_ns;
  record.count = 1;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(fingerprint);
  if (it != records_.end()) {
    CrashRecord& existing = it->second;
    ++existing.count;
    if (record.time_ns > existing.last_ns) {
      existing.last_ns = record.time_ns;
      existing.time_ns = record.time_ns;
      existing.frames = record.frames;
      existing.raw_frames = record.raw_frames;
    }
    if (!record.report.empty()) existing.report = record.report;
    return fingerprint;
  }

  records_.insert(std::make_pair(fingerprint, record));
  order_.push_back(fingerprint);
  while (records_.size() > max_records_ && !order_.empty()) {
    const std::string victim = order_.front();
    order_.pop_front();
    records_.erase(victim);
  }
  return fingerprint;
}

std::vector<CrashRecord> CrashStore::list(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<CrashRecord> out;
  out.reserve(records_.size());
  for (const auto& kv : records_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(),
            [](const CrashRecord& a, const CrashRecord& b) {
              if (a.last_ns != b.last_ns) return a.last_ns > b.last_ns;
              return a.count > b.count;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

bool CrashStore::get(const std::string& id, CrashRecord* out) const {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  *out = it->second;
  return true;
}

}  // namespace server
}  // namespace vmon
