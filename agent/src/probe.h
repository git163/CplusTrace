// HTTP 可用性拨测：从 Agent 周期访问关键接口，记录 up / 耗时 / 状态码。
//
// 约束：
//   - 只支持 http://（内网地址）；https 需要目标环境自带 TLS 支持；
//   - 单线程轮询，拨测失败只产生事件/指标，不影响 Agent 主循环；
//   - 结果走 WAL，与其它指标同一条上报链路。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "json_event.h"

namespace vmon {
namespace agent {

struct ProbeConfig {
  std::string name;
  std::string url;
  uint32_t interval_ms = 30000;
  uint32_t timeout_ms = 3000;
  int expected_status = 200;  // 0 表示任意 2xx
};

// 解析 "name=http://host/path;interval_ms=5000;timeout_ms=2000;status=200"
bool parse_probe_spec(const std::string& spec, ProbeConfig* out,
                      std::string* error);

class ProbeRunner {
 public:
  typedef std::function<void(const common::Event&)> Emit;

  ProbeRunner(std::string node, std::string agent,
              std::vector<ProbeConfig> probes, Emit emit);
  ~ProbeRunner();

  ProbeRunner(const ProbeRunner&) = delete;
  ProbeRunner& operator=(const ProbeRunner&) = delete;

  void start();
  void stop();
  bool running() const { return running_.load(); }

  // 执行一次拨测并返回要上报的事件；供单元测试直接调用。
  std::vector<common::Event> check(const ProbeConfig& probe,
                                   uint64_t now_ns) const;

 private:
  void run();

  std::string node_;
  std::string agent_;
  std::vector<ProbeConfig> probes_;
  Emit emit_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace agent
}  // namespace vmon
