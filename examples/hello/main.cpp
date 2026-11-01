// 最小接入示例：初始化 → 请求关联 ID → 函数级 span → 日志/心跳 → 关闭。
// 业务侧只需要替换日志/初始化位置，不需要改调用流程。
#include <cstdio>
#include <cstdlib>
#include <string>

#include <vmon/vmon.h>

namespace {

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

void HandleRequest(const std::string& request_id) {
  vmon::set_request_id(request_id.c_str());
  {
    VMON_SPAN("HandleRequest");
    VMON_INFO("hello example handling request_id=%s", request_id.c_str());
    vmon::heartbeat();
  }
  vmon::set_request_id(nullptr);
}

}  // namespace

int main() {
  vmon::Config config;
  config.service_name    = "vmon-hello";
  config.instance_id     = "dev-1";
  config.service_version = "0.1.0";

  // 便于本机联调：允许用环境变量覆盖 Agent / 控制通道地址。
  const std::string agent_endpoint = Env("VMON_AGENT_ENDPOINT");
  if (!agent_endpoint.empty()) config.agent_endpoint = agent_endpoint;
  const std::string control_dir = Env("VMON_CONTROL_DIR");
  if (!control_dir.empty()) config.control_dir = control_dir;

  if (!vmon::init(config)) {
    // 初始化失败只影响维测能力，业务照常运行。
    std::printf("vmon disabled, business continues\n");
    return 0;
  }

  HandleRequest("demo-request-1");

  vmon::shutdown();
  std::printf("vmon example ok\n");
  return 0;
}
