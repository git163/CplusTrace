// 一键巡检与一键诊断。
//
// 巡检：节点、Agent、服务实例、错误率、慢调用、崩溃、时钟偏差、磁盘。
// 诊断：按 trace/request/service 汇总链路、日志、崩溃、剖析，给出可疑根因
// 与证据；需要现场线程栈时向 Agent 下发只读命令，结果异步回填。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

class Store;
class CrashStore;
class ProfileStore;
class CommandStore;

struct CheckResult {
  std::string id;
  std::string name;
  std::string status;  // ok / warn / fail / unknown
  std::string detail;
  nlohmann::json evidence = nlohmann::json::object();
};

struct DiagnoseRequest {
  std::string trace_id;
  std::string request_id;
  std::string service;
  std::string instance;
  std::string node;
  int32_t pid = 0;
  uint64_t since_ns = 0;
  bool collect_stack = false;
};

class Diagnostics {
 public:
  static std::vector<CheckResult> inspect(Store& store, CrashStore& crashes,
                                          const std::string& service,
                                          const std::string& state_dir);

  static nlohmann::json diagnose(Store& store, CrashStore& crashes,
                                 ProfileStore& profiles, CommandStore& commands,
                                 const DiagnoseRequest& request);
};

}  // namespace server
}  // namespace vmon
