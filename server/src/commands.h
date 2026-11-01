// 跨节点命令队列：Server 下发，Agent 轮询执行并回报。
//
// 典型命令：
//   ping                    —— 检查进程控制通道是否可用；
//   set_log_level           —— 动态调整日志级别（带 TTL）；
//   dump_stacks             —— 抓取全线程栈；
//   run_diagnosis           —— 执行白名单只读诊断命令（Agent 侧实现）。
//
// 命令有 TTL，过期后 Agent 不再执行，避免重启后补执行陈旧的写操作。
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

struct CommandRecord {
  std::string id;
  std::string node;
  std::string agent;
  int32_t pid = 0;
  std::string type;
  nlohmann::json args;
  uint64_t created_ns = 0;
  uint64_t expires_ns = 0;
  uint64_t completed_ns = 0;
  std::string status;  // pending / running / done / failed / expired
  std::string result;
  std::string error;
};

class CommandStore {
 public:
  CommandStore();

  std::string enqueue(const std::string& node, const std::string& agent,
                      int32_t pid, const std::string& type,
                      const nlohmann::json& args, uint64_t ttl_ns);

  // Agent 拉取待执行命令；拉取时标记为 running。
  std::vector<CommandRecord> pending_for(const std::string& node,
                                         const std::string& agent,
                                         size_t limit);

  bool complete(const std::string& id, const std::string& status,
                const std::string& result, const std::string& error);

  bool get(const std::string& id, CommandRecord* out) const;
  std::vector<CommandRecord> list(size_t limit) const;

  size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::deque<std::string> order_;
  std::unordered_map<std::string, CommandRecord> commands_;
  uint64_t seq_ = 0;
};

}  // namespace server
}  // namespace vmon
