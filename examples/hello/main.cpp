// 业务接入示例：模拟一次「网关 → 鉴权 → 订单 → 库存/缓存 → 支付 → 响应」的请求。
//
// 这个示例故意比“打印一行日志”复杂一点，用来展示维测能力：
//   - 进程内多级 Span：瀑布图能看到每个阶段的耗时；
//   - request_id / task_id：日志与链路按同一次业务请求收口；
//   - WARN/ERROR + 错误码：首个错误、错误传播、诊断都有素材；
//   - 两个请求：demo-request-1 失败、demo-request-2 成功，便于对比。
//
// 业务代码只需要关心自己的日志与阶段；SDK 负责把上下文串起来。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <vmon/vmon.h>

namespace {

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? "" : value;
}

void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// 模拟一次下游库存查询；失败时先重试两次，再返回业务错误码。
bool ReserveInventory(bool simulate_failure) {
  vmon::Span inventory =
      vmon::Span::create("Inventory/Reserve", __FILE__, __LINE__);
  {
    VMON_SPAN("Cache/Get");
    SleepMs(4);
    VMON_WARN("cache miss key=sku-42, fallback to db");
  }

  VMON_INFO("inventory query begin sku=sku-42 qty=1");
  SleepMs(12);
  if (!simulate_failure) {
    VMON_INFO("inventory reserved sku=sku-42 qty=1");
    inventory.end();
    return true;
  }

  for (int attempt = 1; attempt <= 2; ++attempt) {
    VMON_WARN("inventory timeout attempt=%d/2", attempt);
    SleepMs(6);
  }
  inventory.set_error("E_INVENTORY_TIMEOUT");
  VMON_ERROR("inventory reserve failed code=E_INVENTORY_TIMEOUT");
  inventory.end();
  return false;
}

// request_id/task_id 使用静态生命周期（示例里是字符串字面量）：SDK 只保存
// 指针，后台发送线程稍后才序列化，不能指向函数内的临时 std::string。
void HandleRequest(const char* request_id, const char* task_id,
                   bool simulate_failure) {
  vmon::set_request_id(request_id);
  vmon::set_task_id(task_id);

  // 根 Span 用显式对象，失败时可以标记错误码，让服务端计算首个错误。
  vmon::Span root = vmon::Span::create("Gateway/Handle", __FILE__, __LINE__);
  VMON_INFO("gateway received request_id=%s task_id=%s", request_id, task_id);
  vmon::heartbeat();

  {
    VMON_SPAN("Auth/Verify");
    SleepMs(2);
    VMON_DEBUG("auth token verified");
  }

  {
    VMON_SPAN("Order/Parse");
    SleepMs(3);
    VMON_INFO("order parsed items=3 amount=128.00");
  }

  const bool inventory_ok = ReserveInventory(simulate_failure);

  if (inventory_ok) {
    vmon::Span payment =
        vmon::Span::create("Payment/Charge", __FILE__, __LINE__);
    SleepMs(8);
    VMON_INFO("payment charged amount=128.00");
    payment.end();
  } else {
    // 失败分支：保留业务决策日志，方便和首个错误对照。
    VMON_WARN("skip payment because inventory failed");
  }

  {
    VMON_SPAN("Response/Build");
    SleepMs(2);
    VMON_INFO("response status=%s request_id=%s",
              inventory_ok ? "ok" : "failed", request_id);
  }

  if (!inventory_ok) {
    root.set_error("E_ORDER_FAILED");
  }
  vmon::heartbeat();
  root.end();

  vmon::set_request_id(nullptr);
  vmon::set_task_id(nullptr);
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
  // 诊断版：配合 -DVMON_ENABLE_CPU_PROFILER=ON 打开常驻采样。
  if (std::string(Env("VMON_CPU_PROFILER")) == "1") {
    config.enable_cpu_profiler = true;
  }

  if (!vmon::init(config)) {
    // 初始化失败只影响维测能力，业务照常运行。
    std::printf("vmon disabled, business continues\n");
    return 0;
  }

  struct DemoRequest {
    const char* request_id;
    const char* task_id;
    bool fail;
  };
  const DemoRequest requests[] = {
      {"demo-request-1", "task-demo-1", true},   // 失败：库存超时
      {"demo-request-2", "task-demo-2", false},  // 成功：完整链路
  };

  for (const DemoRequest& request : requests) {
    HandleRequest(request.request_id, request.task_id, request.fail);
    std::printf("demo request completed request_id=%s status=%s\n",
                request.request_id, request.fail ? "failed" : "ok");
  }

  vmon::shutdown();
  std::printf("vmon example ok（用 demo-request-1 / demo-request-2 查询）\n");
  return 0;
}
