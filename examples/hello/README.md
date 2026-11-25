# hello：最小业务接入示例

> 目标：用最短的代码看清「一个业务请求如何被串成一条 trace」。
> 这是接入 vmon SDK 的起点，建议先跑通本示例，再看
> [multiprocess](../multiprocess/README.md) 和 [faultlab](../faultlab/README.md)。

## 1. 这个示例演示什么

`vmon_hello` 模拟一次电商下单请求：

```
Gateway/Handle
  ├─ Auth/Verify
  ├─ Order/Parse
  ├─ Inventory/Reserve
  │    └─ Cache/Get
  ├─ Payment/Charge        （成功请求）
  └─ Response/Build
```

连续跑两个请求：

| 请求 | 结果 | 说明 |
|---|---|---|
| `demo-request-1` | 失败 | 库存重试 2 次后返回 `E_INVENTORY_TIMEOUT`，根 Span 标记 `E_ORDER_FAILED` |
| `demo-request-2` | 成功 | 完整链路，用于和失败请求对比 |

可以观察到的能力：

- **进程内多级 Span**：瀑布图看到每个阶段的耗时；
- **request_id / task_id 收口**：同一次业务请求的日志与 Span 自动关联；
- **错误码**：首个错误、错误传播、一键诊断都有素材；
- **fail-open**：Agent 不在线时业务照常运行，只是没有维测数据。

## 2. 文件

| 文件 | 作用 |
|---|---|
| `main.cpp` | 示例业务代码（SDK 初始化、Span、日志、错误码） |
| `run_demo.sh` | 一键启动 Server + Agent + 示例进程并查询结果 |
| `CMakeLists.txt` | 构建目标 `vmon_hello` |

## 3. 快速开始

### 3.1 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### 3.2 一键运行（推荐）

```bash
./examples/hello/run_demo.sh
```

脚本会：

1. 用空闲端口启动 `vmon_server`（默认 `18429`）；
2. 启动 `vmon_agent`（UDS 放在临时目录）；
3. 运行 `vmon_hello`，产生两个请求；
4. 轮询 `/api/requests/demo-request-1`，打印 trace、关联日志和 Web UI 地址；
5. 结束进程并清理临时目录。

### 3.3 保留栈，打开 Web UI

```bash
DEMO_KEEP=1 ./examples/hello/run_demo.sh
```

终端会打印：

```
Web UI: http://127.0.0.1:<port>/
WAL:    <work>/agent/wal/
日志:   <work>/server.log  <work>/agent.log
```

按 `Ctrl+C` 结束。

### 3.4 常用环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `PORT` | `18429` | Server 端口 |
| `WORK_DIR` | 临时目录 | 运行目录（WAL、日志） |
| `BUILD_DIR` | `<repo>/build` | 构建产物目录 |
| `DEMO_KEEP` | `0` | 设为 `1` 保留进程和目录 |

示例：

```bash
PORT=19428 WORK_DIR=/tmp/vmon-hello DEMO_KEEP=1 \
  ./examples/hello/run_demo.sh
```

## 4. 预期结果

### 4.1 失败请求 `demo-request-1`

- `summary.has_error=true`；
- `first_error` 指向根 Span `Gateway/Handle`，错误码 `E_ORDER_FAILED`；
- `Inventory/Reserve` Span 带 `E_INVENTORY_TIMEOUT`；
- 日志里能看到：

  ```
  cache miss key=sku-42, fallback to db
  inventory timeout attempt=1/2
  inventory timeout attempt=2/2
  inventory reserve failed code=E_INVENTORY_TIMEOUT
  skip payment because inventory failed
  ```

### 4.2 成功请求 `demo-request-2`

- 无错误码；
- Span 树包含 `Payment/Charge`；
- 日志包含 `payment charged amount=128.00`。

## 5. 代码怎么接

### 5.1 初始化

```cpp
vmon::Config config;
config.service_name = "vmon-hello";
config.instance_id  = "dev-1";
if (!vmon::init(config)) {
  // 初始化失败只影响维测能力，业务照常运行。
  return 0;
}
```

### 5.2 请求入口设置关联 ID

```cpp
vmon::set_request_id(request_id);
vmon::set_task_id(task_id);
```

> SDK 只保存指针，后台发送线程稍后才序列化。`request_id` / `task_id`
> 必须保持有效至少一个 flush 周期（默认 100ms）。示例使用字符串字面量；
> 动态字符串请参考 [http_backend](../http_backend/README.md) 的 `KeepId`。

### 5.3 建 Span 与打日志

```cpp
vmon::Span root = vmon::Span::create("Gateway/Handle", __FILE__, __LINE__);
{
  VMON_SPAN("Auth/Verify");
  SleepMs(2);
  VMON_DEBUG("auth token verified");
}
root.set_error("E_ORDER_FAILED");  // 只接受静态生命周期字符串
root.end();
```

- `VMON_SPAN` 是 RAII 作用域 Span，离开作用域自动结束；
- 需要跨分支标记错误时，用显式 `vmon::Span` 对象；
- 日志宏 `VMON_INFO/WARN/ERROR` 与 `printf` 风格一致。

### 5.4 结束

```cpp
vmon::set_request_id(nullptr);
vmon::set_task_id(nullptr);
vmon::shutdown();
```

## 6. 手工分终端运行

```bash
# 终端 1：Server
./build/vmon_server --listen-host 127.0.0.1 --port 18428 \
  --state-dir /tmp/vmon-server --web-dir web --no-forward

# 终端 2：Agent
./build/vmon_agent --server-url http://127.0.0.1:18428 \
  --uds /tmp/vmon-agent.sock --state-dir /tmp/vmon-agent \
  --node node-1 --scan-interval-ms 2000

# 终端 3：示例进程
VMON_AGENT_ENDPOINT=/tmp/vmon-agent.sock \
  ./build/examples/hello/vmon_hello
```

查询：

```bash
BASE=http://127.0.0.1:18428
curl -s "$BASE/api/requests/demo-request-1" | python3 -m json.tool | head -60
curl -s "$BASE/api/logs?request_id=demo-request-1&limit=20" | python3 -m json.tool
./build/tools/vmonctl --server "$BASE" traces --request demo-request-1
```

然后打开 `http://127.0.0.1:18428/` 搜索 `demo-request-1`。

## 7. 可选的诊断能力

示例支持两个环境变量：

| 变量 | 作用 |
|---|---|
| `VMON_CONTROL_DIR` | 覆盖控制通道目录；启用动态日志级别、抓线程栈 |
| `VMON_CPU_PROFILER=1` | 打开常驻 CPU 采样（需 `-DVMON_ENABLE_CPU_PROFILER=ON` 构建） |

## 8. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 输出 `vmon disabled, business continues` | SDK 初始化失败（如 `service_name` 为空） | 检查配置；业务仍会正常执行 |
| 查不到 trace | Agent 未启动、UDS 路径不一致或上传延迟 | 等待 1 秒再查；检查 `agent.log` 与 WAL 目录 |
| 只有部分日志 | INFO 被采样/去重，或队列满 | 错误日志默认全保留；查看 `dropped_events` |
| 端口被占用 | 默认 18429 已被使用 | 用 `PORT=19429` 覆盖 |
| `request_id` 串到别的请求 | 线程复用后未清理 | 请求结束调用 `set_request_id(nullptr)` |

## 9. 下一步

- [multiprocess](../multiprocess/README.md)：三个真实进程的跨进程链路；
- [http_backend](../http_backend/README.md)：Python/Web 客户端调用 C++ 后端；
- [faultlab](../faultlab/README.md)：复杂故障注入与自动诊断；
- [快速诊断手册](../../docs/quick-diagnosis.md)。
