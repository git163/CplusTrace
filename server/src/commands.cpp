#include "commands.h"

#include <algorithm>

#include "util.h"

namespace vmon {
namespace server {

CommandStore::CommandStore() = default;

std::string CommandStore::enqueue(const std::string& node,
                                  const std::string& agent, int32_t pid,
                                  const std::string& type,
                                  const nlohmann::json& args,
                                  uint64_t ttl_ns) {
  CommandRecord command;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    command.id = "cmd-" + std::to_string(++seq_) + "-" +
                 hex_hash(node + "|" + agent + "|" + type).substr(0, 8);
    command.node = node;
    command.agent = agent;
    command.pid = pid;
    command.type = type;
    command.args = args;
    command.created_ns = now_ns();
    command.expires_ns = command.created_ns + (ttl_ns == 0 ? 60000000000ull : ttl_ns);
    command.status = "pending";
    commands_[command.id] = command;
    order_.push_back(command.id);
    while (order_.size() > 20000) {
      const std::string victim = order_.front();
      order_.pop_front();
      if (commands_[victim].status != "running") commands_.erase(victim);
    }
  }
  return command.id;
}

std::vector<CommandRecord> CommandStore::pending_for(const std::string& node,
                                                     const std::string& agent,
                                                     size_t limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t now = now_ns();
  std::vector<CommandRecord> out;
  for (auto& kv : commands_) {
    CommandRecord& command = kv.second;
    if (command.status != "pending") continue;
    if (!node.empty() && !command.node.empty() && command.node != node) continue;
    if (!agent.empty() && !command.agent.empty() && command.agent != agent) {
      continue;
    }
    if (now > command.expires_ns) {
      command.status = "expired";
      continue;
    }
    command.status = "running";
    out.push_back(command);
    if (out.size() >= limit) break;
  }
  std::sort(out.begin(), out.end(),
            [](const CommandRecord& a, const CommandRecord& b) {
              return a.created_ns < b.created_ns;
            });
  return out;
}

bool CommandStore::complete(const std::string& id, const std::string& status,
                            const std::string& result,
                            const std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = commands_.find(id);
  if (it == commands_.end()) return false;
  it->second.status = status.empty() ? "done" : status;
  it->second.result = result;
  it->second.error = error;
  it->second.completed_ns = now_ns();
  return true;
}

bool CommandStore::get(const std::string& id, CommandRecord* out) const {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = commands_.find(id);
  if (it == commands_.end()) return false;
  *out = it->second;
  return true;
}

std::vector<CommandRecord> CommandStore::list(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<CommandRecord> out;
  out.reserve(commands_.size());
  for (const auto& kv : commands_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(),
            [](const CommandRecord& a, const CommandRecord& b) {
              return a.created_ns > b.created_ns;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

size_t CommandStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return commands_.size();
}

}  // namespace server
}  // namespace vmon
