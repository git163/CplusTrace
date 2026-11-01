#include "issues.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "crash.h"
#include "store.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

bool valid_status(const std::string& status) {
  return status == "open" || status == "ack" || status == "resolved";
}

}  // namespace

nlohmann::json issue_to_json(const IssueRecord& issue) {
  nlohmann::json j;
  j["id"] = issue.id;
  j["kind"] = issue.kind;
  j["title"] = issue.title;
  j["level"] = issue.level;
  j["service"] = issue.service;
  j["error_code"] = issue.error_code;
  j["status"] = issue.status;
  j["assignee"] = issue.assignee;
  j["note"] = issue.note;
  j["count"] = issue.count;
  j["first_ns"] = issue.first_ns;
  j["first_time"] = iso_time(issue.first_ns);
  j["last_ns"] = issue.last_ns;
  j["last_time"] = iso_time(issue.last_ns);
  j["sample"] = issue.sample;
  j["trace_id"] = issue.trace_id;
  j["request_id"] = issue.request_id;
  j["node"] = issue.node;
  return j;
}

bool IssueStore::load(const std::string& path, std::string* error) {
  std::ifstream in(path);
  if (!in) return true;
  try {
    nlohmann::json json;
    in >> json;
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();
    if (json.is_object()) {
      for (auto it = json.begin(); it != json.end(); ++it) {
        State state;
        state.status = it.value().value("status", std::string("open"));
        state.assignee = it.value().value("assignee", std::string());
        state.note = it.value().value("note", std::string());
        state.updated_ns = it.value().value("updated_ns",
                                            static_cast<uint64_t>(0));
        if (!valid_status(state.status)) state.status = "open";
        states_[it.key()] = state;
      }
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = std::string("Issue 状态解析失败: ") + ex.what();
    return false;
  }
}

bool IssueStore::save(const std::string& path, std::string* error) const {
  nlohmann::json json = nlohmann::json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : states_) {
      json[kv.first] = {{"status", kv.second.status},
                        {"assignee", kv.second.assignee},
                        {"note", kv.second.note},
                        {"updated_ns", kv.second.updated_ns}};
    }
  }
  const std::string tmp = path + ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    if (error) *error = "无法写入 Issue 状态: " + tmp;
    return false;
  }
  out << json.dump(2) << "\n";
  out.close();
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = "无法替换 Issue 状态: " + path;
    return false;
  }
  return true;
}

std::vector<IssueRecord> IssueStore::list(const Store& store,
                                          const CrashStore& crashes,
                                          const IssueQuery& query) const {
  std::unordered_map<std::string, State> states;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    states = states_;
  }

  std::vector<IssueRecord> out;
  for (const LogClusterInfo& cluster : store.log_clusters(5000)) {
    if (!cluster.is_error_level()) continue;
    IssueRecord issue;
    issue.id = "log-" + cluster.id;
    issue.kind = "log";
    issue.title = cluster.tmpl;
    issue.level = cluster.level;
    issue.service = cluster.service;
    issue.error_code = cluster.error_code;
    issue.count = cluster.count;
    issue.first_ns = cluster.first_ns;
    issue.last_ns = cluster.last_ns;
    issue.sample = cluster.last_sample.empty() ? cluster.sample
                                               : cluster.last_sample;
    if (!cluster.nodes.empty()) issue.node = cluster.nodes.front();

    LogQuery log_query;
    log_query.fingerprint = cluster.id;
    log_query.limit = 1;
    const std::vector<LogData> logs = store.query_logs(log_query);
    if (!logs.empty()) {
      issue.trace_id = logs.front().trace;
      issue.request_id = logs.front().request_id;
    }

    auto it = states.find(issue.id);
    if (it != states.end()) {
      issue.status = it->second.status;
      issue.assignee = it->second.assignee;
      issue.note = it->second.note;
    }
    out.push_back(issue);
  }

  for (const CrashRecord& crash : crashes.list(1000)) {
    IssueRecord issue;
    issue.id = "crash-" + crash.id;
    issue.kind = "crash";
    issue.title = crash.signal + (crash.reason.empty() ? "" : " · " + crash.reason);
    issue.level = "critical";
    issue.service = crash.service;
    issue.count = crash.count;
    issue.first_ns = crash.first_ns;
    issue.last_ns = crash.last_ns;
    issue.sample = crash.reason;
    issue.node = crash.node;
    auto it = states.find(issue.id);
    if (it != states.end()) {
      issue.status = it->second.status;
      issue.assignee = it->second.assignee;
      issue.note = it->second.note;
    }
    out.push_back(issue);
  }

  out.erase(std::remove_if(out.begin(), out.end(),
                           [&query](const IssueRecord& issue) {
                             if (!query.status.empty() &&
                                 issue.status != query.status) {
                               return true;
                             }
                             if (!query.service.empty() &&
                                 issue.service != query.service) {
                               return true;
                             }
                             if (!query.level.empty() &&
                                 issue.level != query.level) {
                               return true;
                             }
                             return false;
                           }),
            out.end());

  std::sort(out.begin(), out.end(),
            [](const IssueRecord& a, const IssueRecord& b) {
              return a.last_ns > b.last_ns;
            });
  if (out.size() > query.limit) out.resize(query.limit);
  return out;
}

bool IssueStore::update(const std::string& id, const std::string& status,
                        const std::string& assignee, const std::string& note,
                        std::string* error) {
  if (id.empty()) {
    if (error) *error = "Issue id 不能为空";
    return false;
  }
  if (!status.empty() && !valid_status(status)) {
    if (error) *error = "Issue 状态必须是 open/ack/resolved";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  State& state = states_[id];
  if (!status.empty()) state.status = status;
  // 空值表示不修改；清空负责人/备注请传 "-"（避免误清）。
  if (!assignee.empty()) state.assignee = assignee == "-" ? "" : assignee;
  if (!note.empty()) state.note = note == "-" ? "" : note;
  state.updated_ns = now_ns();
  return true;
}

}  // namespace server
}  // namespace vmon
