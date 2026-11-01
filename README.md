# CplusTrace / vmon

局域网、离线、私有部署的 C++ 维测服务。面向多进程、跨节点、以 gRPC/HTTP2 为主的
C++ 系统（10–50 节点、50–200 进程），把「人工翻多机多进程日志」收敛为：

> 输入 `request_id` / `trace_id` / `task_id` / 错误码 / 时间窗 → 自动还原调用链 →
> 关联每个环节的日志与错误 → 给出可疑根因与证据包。

- **使用手册（10 分钟上手）**：[docs/user-guide.md](docs/user-guide.md)
- **快速诊断与定位**：[docs/quick-diagnosis.md](docs/quick-diagnosis.md)
- **常用场景手册**：[docs/scenario-cookbook.md](docs/scenario-cookbook.md)
- 设计方案：[plan_v1.0.md](plan_v1.0.md)
- **详细模块设计**：[docs/module-design.md](docs/module-design.md)
- 本轮能力补齐设计：[docs/feature-gap-design.md](docs/feature-gap-design.md)
- Web/Python 客户端接入设计：[docs/client-observability.md](docs/client-observability.md)
- 项目介绍 PPT：[docs/presentation/](docs/presentation/)
- 竞品对照与取舍：[docs/competitive-analysis.md](docs/competitive-analysis.md)
- 观测云深入对标与本轮落地：[docs/guance-benchmark.md](docs/guance-benchmark.md)
- Web UI 浏览器回归记录：[docs/ui-test-report.md](docs/ui-test-report.md)
- 生产零影响约束：[docs/production-safety.md](docs/production-safety.md)
- 部署与运维：[docs/deployment.md](docs/deployment.md)
- 三方依赖与许可证：[docs/third-party-deps.md](docs/third-party-deps.md)
- 本轮验收记录：[docs/verification.md](docs/verification.md)

## 1. 组成

| 组件 | 说明 |
|---|---|
| `sdk/` | 业务进程内静态库：结构化日志、函数级 span、心跳、控制通道、崩溃兜底 |
| `adapters/` | W3C `traceparent` / request_id / task_id 注入与提取 |
| `agent/` | 节点常驻代理：UDS 接入、分段 WAL、上传、进程/主机扫描、日志采集、命令执行、崩溃收集 |
| `server/` | 中心服务：HTTP API、内存有界存储、调用链还原、日志聚类、规则引擎、诊断、崩溃、剖析、证据包、MCP |
| `web/` | 自研请求级排障 UI（静态资源，无 Node 运行时） |
| `tools/` | `vmonctl` CLI、`vmon_mcp` stdio 桥、`vmon_crash_helper` |
| `tests/` | SDK / Server / Agent 单元测试 + Python 端到端测试 |
| `deploy/` | 示例配置、systemd 单元、tmpfiles |

数据流：

```
业务进程(SDK) --UDS JSONL--> vmon-agent --HTTP NDJSON--> vmon-server --HTTP--> Web / CLI / MCP
                                  |                         |
                                  +-- 分段 WAL（断网续传）  +-- 内存有界存储
                                                             +-- 可选 VM/VL 转发
```

## 2. 里程碑状态

| 里程碑 | 已实现 | 暂未覆盖 / 边界 |
|---|---|---|
| M1 框架底座 | SDK 初始化/配置/日志/span/心跳/控制通道/发送线程；每线程 SPSC 环；traceparent 编解码；Agent UDS 接入、分段 WAL、上传、进程与主机扫描、文件日志采集；Server 接入鉴权、资产目录、查询 API、Web UI；`vmon_attach(target)` 接入入口 | gRPC 拦截器仅有传播层（需在目标工程按实际 gRPC 版本接入）；无平台注册中心 API 对接；trace 存内存，未落 Parquet；未提供日志宏兼容层 |
| M2 日志治理 | 模板归一化、指纹、关键信息提取、INFO 采样、同指纹去重、噪声模板、错误全保留、动态日志级别（TTL）、日志聚类、请求级排障视图 | 线程池/队列/回调 drop-in 包装器与 `causality.id` 自动传递未实现；时序异常识别仅提供基础诊断 |
| M3 全链路 | trace 树、关键路径、服务拓扑与强弱边、按 request/trace/task 查询、跨进程链路与进程内 span 融合、Socket 24 字节 trace 头编解码；gRPC streaming 辅助器与 eBPF 弱边接口（编译开关） | 具体 gRPC/异步 interceptor、Socket 协议、eBPF collector 需目标环境接入；尾部采样未实现 |
| M4 崩溃诊断 | SDK async-signal-safe 崩溃报告、Agent 收集上传、指纹聚合、`addr2line`/`llvm-symbolizer` 离线符号化接口、`core_pattern` helper、事件中心联动 | 未自动关联发布记录；未解析完整 core/minidump |
| M5 性能剖析 | 折叠栈导入/聚合、Top self/inclusive、火焰图 JSON、证据包集成、按需诊断接口；可选 `SIGPROF` 常驻采样（编译开关） | `perf` 采集、内存/锁/IO 剖析未实现；SIGPROF 仅诊断版 |
| M6 告警与内部 AI | 规则引擎（心跳缺失、错误突增、慢 trace、指标阈值）、事件中心、静默/冷却、一键巡检、诊断编排、证据包（脱敏 + SHA-256 清单）、MCP 只读工具、systemd/tmpfiles | 通知渠道（Webhook/邮件/syslog）未实现；无 72 小时长稳与混沌脚本 |
| 本轮补齐 | 主机/进程指标、SLO 与错误预算、事件 resolve/筛选、服务拓扑、Trace 瀑布图、日志时间窗/node/task_id、规则表单、Trace 统一搜索、Python/Web 客户端接入、常用场景 demo、模块/类/时序/原理图、Web UI 全量回归 | 通知渠道、常驻 CPU 采样、eBPF、gRPC streaming 仍为非目标/二期 |

本轮在 M1–M6 纵切之上补齐了高频维测能力：

- Agent 主机指标（`host.load1/5/15`、内存、进程数、uptime）与可选的进程 RSS；
- 服务性能分析：服务页展示近 5 分钟 QPS、错误率、P95；
- SLO（可用性/延迟、错误预算、燃烧率）及持久化；
- 事件生命周期（open → ack → resolved）与多维筛选；
- 服务拓扑、Trace 瀑布图、日志时间窗/node/task_id 过滤；
- 告警规则表单（保留 JSON 高级编辑）；
- Trace 统一搜索 `q`（trace_id / request_id / task_id / 服务 / Span，忽略大小写）；
- Python / Web 客户端 trace 接入与示例 C++ HTTP 后端；
- 入库前 Pipeline 脱敏（password/token/email/IP/手机号）与事件 Webhook 通知；
- 异常追踪 Issue（聚合、指派、ack/resolve）与告警静默/事件分组；
- HTTP 可用性拨测（up/duration/status 指标 + 总览展示）；
- 仪表板与只读快照（指标/链路/日志 widget）；
- 可选 CPU SIGPROF 采样（编译选项 `VMON_ENABLE_CPU_PROFILER`）；
- 常用场景 demo（RPC/慢调用/错误/日志风暴/SLO/崩溃）与快速诊断手册；
- Web UI 16 个视图完成真实浏览器回归。

> 结论：当前版本可作为「可运行、可验证的 M1–M6 纵切」：端到端链路、日志治理、
> 链路还原、崩溃、剖析数据面、告警、证据包与 MCP 都已打通；上表「暂未覆盖」项
> 属于后续加深，不影响本机验收与内网试点。

## 3. 快速开始

### 3.1 依赖

- CMake 3.20+
- 支持 C++14 的编译器（目标环境：Linux x86_64 gcc 12.3 / aarch64 gcc 7.3.0）
- Python 3（仅端到端测试需要）

三方头文件与运行时按需下载（默认走本机代理 `http://127.0.0.1:7890`）：

```bash
./scripts/download-third-party.sh --proxy http://127.0.0.1:7890

# 只下载源码/头文件，或只打印链接
./scripts/download-third-party.sh --sources-only
./scripts/download-third-party.sh --list

# Linux 目标机（移植阶段）
./scripts/download-third-party.sh --os linux --arch amd64
./scripts/download-third-party.sh --os linux --arch arm64
```

许可证策略与完整依赖清单见 [docs/third-party-deps.md](docs/third-party-deps.md)。
本项目当前未声明对外发布许可证；三方依赖均为 Apache-2.0 / MIT / BSD / ISC / zlib /
Public Domain 等宽松许可证。

### 3.2 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

测试目标：

| CTest 名称 | 内容 |
|---|---|
| `vmon_sdk_tests` | 环形队列、span 栈、traceparent 编解码、Socket trace 头、request/task 关联 |
| `vmon_server_tests` | 存储、链路树/关键路径/拓扑、日志聚类、规则、剖析、崩溃、证据包、命令、HTTP API、MCP |
| `vmon_agent_tests` | WAL 轮转/封存/上限、日志 tail、进程扫描、UDS 接入、控制命令、上传与崩溃收集 |
| `vmon_python_client` | Python 客户端 traceparent、Span 父子关系、HTTP 传播与 5xx 错误码 |
| `vmon_web_client` | 浏览器 traceparent、fetch 注入、采样与上报回调 |
| `vmon_e2e` | 真实 Server + Agent + SDK 业务进程 + HTTP 后端的端到端链路 |

`vmon_e2e` 覆盖 21 类场景：HTTP 表面与鉴权、trace/日志多维查询（含统一搜索 `q`）、
动态日志级别真正生效、MCP 工具与错误码、跨进程父子 Span 与拓扑边、Python 客户端
调用 C++ 后端（同一 trace / 错误码）、事件生命周期、异常追踪 Issue、告警静默、
HTTP 可用性拨测、仪表板与快照、剖析、诊断/巡检、指标（主机 + 进程标签回归）、
证据包脱敏与校验和、SLO、规则/SLO 重启持久化、SIGSEGV 捕获上传、中心停止期间
WAL 缓存与恢复补传。
未找到 Python3 时该测试自动跳过。

生成源码发布包（约 400KB，不含三方依赖）：

```bash
./scripts/package.sh                 # 输出 dist/vmon-src-<时间戳>.tar.gz
OUT_DIR=/tmp/release ./scripts/package.sh
```

> 压缩包、`build/`、`third_party/{downloads,src,bin}` 均不进入 git；
> 三方依赖由 `scripts/download-third-party.sh` 按需下载，避免仓库膨胀。

### 3.3 本机管理 Server / Agent

日常开发不需要记参数，用 `scripts/vmon.sh` 管理本地栈：

```bash
./scripts/vmon.sh up        # 启动 Server + Agent，等待健康检查通过
./scripts/vmon.sh status    # 查看进程、健康检查、UDS、日志路径
./scripts/vmon.sh logs agent -f
./scripts/vmon.sh restart
./scripts/vmon.sh down      # 优雅停止；5 秒未退出才强制结束
./scripts/vmon.sh clean     # 停止并删除运行目录（需 FORCE=1）
```

常用环境变量：`PORT`（默认 18428）、`VMON_TOKEN`（默认 dev-token）、
`VMON_HOME`（默认 `.vmon-run`）、`NODE_NAME`、`PROCESS_FILTER`。
脚本只操作自己 PID 文件里的进程，不会按进程名误杀。运行目录下有
`server.log`、`agent.log`、`server/`、`agent/`（WAL 与状态）。

### 3.4 本机跑通一条链路

最简方式是一条命令（自动启动 Server + Agent + 示例进程，查询到数据后清理）。
完整的日常用法见 [docs/user-guide.md](docs/user-guide.md)。

```bash
./examples/hello/run_demo.sh

# 保留进程和临时目录，便于打开 Web UI；按 Ctrl+C 结束
DEMO_KEEP=1 ./examples/hello/run_demo.sh

# 端口被占用或需要固定目录时
PORT=19428 WORK_DIR=/tmp/vmon-demo ./examples/hello/run_demo.sh
```

脚本会打印按 `request_id=demo-request-1` 还原的 trace、关联日志、Web UI 地址和
WAL/组件日志路径。

常用场景（跨语言 RPC、慢调用、错误链路、日志风暴、SLO、崩溃）用一条命令演示：

```bash
python3 examples/scenarios/demo.py --scenario rpc --keep
python3 examples/scenarios/demo.py --scenario all --keep
```

每个场景的观察顺序与产出见 [docs/scenario-cookbook.md](docs/scenario-cookbook.md)。
也可以手动分三个终端运行：

```bash
# 终端 1：中心服务
./build/vmon_server --listen-host 127.0.0.1 --port 18428 \
  --token dev-token --state-dir /tmp/vmon-server-state --no-forward

# 终端 2：Agent
./build/vmon_agent --server-url http://127.0.0.1:18428 \
  --uds /tmp/vmon-agent.sock --state-dir /tmp/vmon-agent-state \
  --node node-1 --token dev-token --scan-interval-ms 2000

# 终端 3：示例业务进程（SDK 已初始化；设置 request_id 后产生 span/日志）
VMON_AGENT_ENDPOINT=/tmp/vmon-agent.sock ./build/examples/hello/vmon_hello
```

查询：

```bash
curl -s -H 'X-Vmon-Token: dev-token' \
  'http://127.0.0.1:18428/api/logs?limit=20'
curl -s -H 'X-Vmon-Token: dev-token' \
  'http://127.0.0.1:18428/api/traces?limit=20'
./build/tools/vmonctl --server http://127.0.0.1:18428 --token dev-token overview
```

浏览器打开 `http://127.0.0.1:18428/` 使用 Web UI。

## 4. 业务接入

CMake 一行接入（`cmake/vmon.cmake` 提供 `vmon_attach`）：

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/CplusTrace/cmake")
include(vmon)
vmon_attach(your_target)
```

```cpp
#include <vmon/vmon.h>

vmon::Config config;
config.service_name = "order-service";
config.instance_id = "order-service-" + std::to_string(getpid());
config.agent_endpoint = "/run/vmon/agent.sock";
config.control_dir = "/run/vmon/ctrl";
config.enable_crash_handler = false;  // 需要时显式开启
vmon::init(config);

void Handle(const char* request_id, const char* task_id) {
  vmon::set_request_id(request_id);
  vmon::set_task_id(task_id);
  {
    VMON_SPAN("Handle");
    VMON_INFO("handling request_id=%s", request_id);
  }
  vmon::heartbeat();
}
```

约束与保证：

- fail-open：未初始化、Agent 离线、队列满时业务照常运行；SDK 永不阻塞业务线程；
- 热路径不分配、不加锁、不做 IO；
- 日志正文截断到 127 字节；
- `VMON_DISABLE=1` 可全局关闭；
- 崩溃处理器默认关闭；开启后接管 `SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL`，
  只写 async-signal-safe 的最小 JSON，然后 `_exit`。与平台 core 收集的取舍见部署手册。

Python / Web 客户端也可以成为 trace 起点：

```python
from vmon_trace import Client

client = Client("http://127.0.0.1:18428", "dev-token",
                service="py-bff", node="node-1")
with client.span("GET /orders", request_id="REQ-1"):
    client.request("GET", "http://backend:9000/api/orders",
                   request_id="REQ-1")
client.close()
```

浏览器使用 `clients/web/vmon-trace.js` 的 `instrumentFetch`；C++ 后端用
`vmon::context_from_traceparent()` 续接。浏览器不要直连中心写接口，事件经 BFF 汇总。
详见 [docs/client-observability.md](docs/client-observability.md)。

完整接入、systemd、用户组与权限、core_pattern、MCP 配置见
[docs/deployment.md](docs/deployment.md)。

## 5. 主要 API

| 接口 | 用途 |
|---|---|
| `POST /v1/ingest` | Agent 批量上报 NDJSON 事件 |
| `POST /v1/crash` / `POST /v1/profile` | 崩溃 / 剖析数据上报 |
| `GET /api/health` | 无鉴权健康检查 |
| `GET /api/overview` | 总览指标、慢 trace、错误 Top |
| `GET /api/requests/{request_id}` | 请求级：trace + 日志 + 聚类 |
| `GET /api/traces/{trace_id}` | trace 树、关键路径、首个错误、关联日志 |
| `GET /api/traces?q=` | 统一搜索（trace_id / request_id / task_id / 服务 / Span） |
| `GET /api/logs` / `GET /api/logs/clusters` | 日志检索（含 node/task_id/时间窗）/ 模板聚类 |
| `GET /api/metrics?name=` | 指标序列查询 |
| `GET /api/topology` | 服务拓扑与强弱边 |
| `GET /api/services` / `GET /api/nodes` | 服务/实例、节点/进程快照 |
| `GET /api/events` / `POST /api/events/{id}/ack` / `resolve` | 事件筛选、生命周期与同指纹分组 |
| `GET /api/issues` / `POST /api/issues/{id}` | 异常追踪（聚合/指派/ack/resolve） |
| `GET/POST/DELETE /api/silences` | 告警静默 |
| `GET /api/probes` | 可用性拨测汇总 |
| `GET/POST/DELETE /api/dashboards` / `GET /api/snapshots` | 仪表板与只读快照 |
| `GET/POST/DELETE /api/slos` | SLO 定义与实时状态 |
| `GET /api/crashes` / `POST /api/crashes/symbolize` | 崩溃列表 / 离线符号化 |
| `GET /api/profiles` | 剖析记录与火焰图 |
| `POST /api/control/log-level` | 动态日志级别（带 TTL） |
| `POST /api/diagnose` / `GET /api/inspection` | 一键诊断 / 巡检 |
| `GET/POST/DELETE /api/rules` | 告警规则 |
| `POST /api/evidence` | 生成脱敏证据包 |
| `POST /mcp` | MCP JSON-RPC（内部版） |

除 `/api/health` 外均需 `X-Vmon-Token` 或 `Authorization: Bearer <token>`。

## 6. 目录结构

```
CplusTrace/
├── sdk/            # 业务进程静态库
├── adapters/       # traceparent / request_id / task_id 传播
├── agent/          # 节点采集代理
├── server/         # 中心服务
├── common/         # 事件模型、SHA-256
├── symbolizer/     # addr2line / llvm-symbolizer
├── clients/        # Python / 浏览器 trace 客户端
├── tools/          # CLI / MCP stdio / crash helper
├── web/            # 静态 Web UI
├── deploy/         # 示例配置、systemd、tmpfiles
├── tests/          # 单元测试与端到端测试
├── docs/           # 设计约束、依赖、部署、验收
└── scripts/        # 本地栈管理（vmon.sh）与三方依赖下载
```

## 7. 平台与安全约束

- 统一 C++14，不使用 C++17/20 特性；避免 gcc 7.3 已知弱点；
- 常驻 CPU SIGPROF、eBPF、gRPC streaming 均为**默认关闭的编译选项**：
  `VMON_ENABLE_CPU_PROFILER` / `VMON_ENABLE_EBPF` / `VMON_ENABLE_GRPC`，
  诊断版按需打开；
- 目标 Linux x86_64 与 aarch64；macOS arm64 为开发验证环境；
- 纯内网、离线部署，不依赖 Grafana / Prometheus / ELK；
- 维测组件独立进程，有内存/CPU/磁盘上限，不影响业务功能与行为；
- 证据包与 MCP 属于内部能力，对外发行版应关闭；
- 诊断只执行固定白名单只读命令，不拼接 shell。
