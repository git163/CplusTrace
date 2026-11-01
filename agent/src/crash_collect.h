// 崩溃报告收集：扫描 SDK 信号处理器写下的最小崩溃 JSON，上报中心服务。
//
// 文件约定：<crash_dir>/crash-<pid>.json。正常退出时 SDK 会删除占位文件；
// 崩溃时文件内是一条完整 JSON。Agent 只上报解析成功的非空文件，成功后删除。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "agent.h"

namespace vmon {
namespace agent {

class CrashCollector {
 public:
  CrashCollector(const AgentConfig& config, std::string node,
                 std::string agent);
  ~CrashCollector();

  CrashCollector(const CrashCollector&) = delete;
  CrashCollector& operator=(const CrashCollector&) = delete;

  void start();
  void stop();

  // 扫描一轮，返回成功上报的文件数；供测试直接调用。
  size_t scan_once();

 private:
  void run();
  bool upload(const std::string& body);

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
