// 跨节点命令执行器：轮询 Server 的命令队列，在本地执行后回报结果。
//
// 支持的命令：
//   ping             —— 检查业务进程控制通道；
//   set_log_level    —— 动态日志级别（TTL 由 SDK 控制通道执行）；
//   dump_stacks      —— 抓取全线程栈；
//   get_stats        —— SDK 自监控计数；
//   diagnose         —— 白名单只读诊断（/proc 快照、ss、lsof、gdb bt）。
//
// 安全边界：诊断只执行内置 argv，不拼接 shell，不读取 /proc 以外的任意文件；
// 所有命令有超时与输出上限。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "agent.h"

namespace vmon {
namespace agent {

class CommandRunner {
 public:
  CommandRunner(const AgentConfig& config, std::string node,
                std::string agent);
  ~CommandRunner();

  CommandRunner(const CommandRunner&) = delete;
  CommandRunner& operator=(const CommandRunner&) = delete;

  void start();
  void stop();

  // 执行一条命令并返回结果 JSON；供单元测试直接调用。
  bool execute(const nlohmann::json& command, std::string* result,
               std::string* error);

 private:
  void run();
  void poll_once();
  bool post_result(const std::string& id, const std::string& status,
                   const std::string& result, const std::string& error);

  AgentConfig config_;
  std::string node_;
  std::string agent_;
  std::string host_;
  int port_ = 0;
  std::string prefix_;

  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace agent
}  // namespace vmon
