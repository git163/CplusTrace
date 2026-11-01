#include "profiler.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "util.h"

namespace vmon {
namespace server {
namespace {

std::vector<std::string> split_stack(const std::string& stack) {
  std::vector<std::string> frames;
  size_t begin = 0;
  while (begin <= stack.size()) {
    const size_t pos = stack.find(';', begin);
    std::string frame = pos == std::string::npos
                            ? stack.substr(begin)
                            : stack.substr(begin, pos - begin);
    frame = trim(frame);
    if (!frame.empty()) frames.push_back(frame);
    if (pos == std::string::npos) break;
    begin = pos + 1;
  }
  return frames;
}

struct FlameNode {
  std::string name;
  uint64_t value = 0;
  std::vector<FlameNode> children;
};

FlameNode* child_of(FlameNode* node, const std::string& name) {
  for (FlameNode& child : node->children) {
    if (child.name == name) return &child;
  }
  FlameNode child;
  child.name = name;
  node->children.push_back(child);
  return &node->children.back();
}

nlohmann::json node_to_json(const FlameNode& node) {
  nlohmann::json j;
  j["name"] = node.name;
  j["value"] = node.value;
  if (!node.children.empty()) {
    nlohmann::json children = nlohmann::json::array();
    for (const FlameNode& child : node.children) {
      children.push_back(node_to_json(child));
    }
    j["children"] = children;
  }
  return j;
}

void sort_children(FlameNode* node) {
  std::sort(node->children.begin(), node->children.end(),
            [](const FlameNode& a, const FlameNode& b) {
              return a.value > b.value;
            });
  for (FlameNode& child : node->children) sort_children(&child);
}

}  // namespace

ProfileStore::ProfileStore(size_t max_records)
    : max_records_(max_records == 0 ? 64 : max_records) {}

std::vector<std::pair<std::string, uint64_t>> ProfileStore::parse_folded(
    const std::string& text) {
  std::map<std::string, uint64_t> aggregate;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const size_t space = line.rfind(' ');
    if (space == std::string::npos) continue;
    const std::string stack = trim(line.substr(0, space));
    const uint64_t count =
        static_cast<uint64_t>(to_i64(trim(line.substr(space + 1))));
    if (stack.empty() || count == 0) continue;
    aggregate[stack] += count;
  }
  std::vector<std::pair<std::string, uint64_t>> out;
  out.reserve(aggregate.size());
  for (const auto& kv : aggregate) out.push_back(kv);
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, uint64_t>& a,
               const std::pair<std::string, uint64_t>& b) {
              return a.second > b.second;
            });
  return out;
}

std::string ProfileStore::add(const std::string& folded_text,
                              const std::string& service,
                              const std::string& node,
                              const std::string& instance,
                              const std::string& kind, uint64_t time_ns) {
  ProfileRecord record;
  record.folded = parse_folded(folded_text);
  if (record.folded.empty()) return std::string();
  record.id = hex_hash(service + "|" + node + "|" + kind + "|" +
                       std::to_string(time_ns));
  record.kind = kind.empty() ? "cpu" : kind;
  record.service = service;
  record.node = node;
  record.instance = instance;
  record.time_ns = time_ns;
  for (const auto& entry : record.folded) record.total_samples += entry.second;
  record.start_ns = time_ns;
  record.end_ns = time_ns;

  std::lock_guard<std::mutex> lock(mutex_);
  records_[record.id] = record;
  order_.push_back(record.id);
  while (records_.size() > max_records_ && !order_.empty()) {
    const std::string victim = order_.front();
    order_.pop_front();
    if (victim != record.id) records_.erase(victim);
  }
  return record.id;
}

std::vector<ProfileRecord> ProfileStore::list(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ProfileRecord> out;
  out.reserve(records_.size());
  for (const auto& kv : records_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(),
            [](const ProfileRecord& a, const ProfileRecord& b) {
              return a.time_ns > b.time_ns;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

bool ProfileStore::get(const std::string& id, ProfileRecord* out) const {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  *out = it->second;
  return true;
}

std::vector<std::pair<std::string, uint64_t>> ProfileStore::top_self(
    const ProfileRecord& record, size_t limit) {
  std::unordered_map<std::string, uint64_t> self;
  for (const auto& entry : record.folded) {
    const std::vector<std::string> frames = split_stack(entry.first);
    if (frames.empty()) continue;
    self[frames.back()] += entry.second;
  }
  std::vector<std::pair<std::string, uint64_t>> out(self.begin(), self.end());
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, uint64_t>& a,
               const std::pair<std::string, uint64_t>& b) {
              return a.second > b.second;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

std::vector<std::pair<std::string, uint64_t>> ProfileStore::top_inclusive(
    const ProfileRecord& record, size_t limit) {
  std::unordered_map<std::string, uint64_t> inclusive;
  for (const auto& entry : record.folded) {
    const std::vector<std::string> frames = split_stack(entry.first);
    for (const std::string& frame : frames) inclusive[frame] += entry.second;
  }
  std::vector<std::pair<std::string, uint64_t>> out(inclusive.begin(),
                                                    inclusive.end());
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, uint64_t>& a,
               const std::pair<std::string, uint64_t>& b) {
              return a.second > b.second;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

nlohmann::json ProfileStore::flamegraph(const ProfileRecord& record) {
  FlameNode root;
  root.name = "all";
  for (const auto& entry : record.folded) {
    const std::vector<std::string> frames = split_stack(entry.first);
    FlameNode* node = &root;
    root.value += entry.second;
    for (const std::string& frame : frames) {
      node = child_of(node, frame);
      node->value += entry.second;
    }
  }
  sort_children(&root);
  return node_to_json(root);
}

}  // namespace server
}  // namespace vmon
