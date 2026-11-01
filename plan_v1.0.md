# 局域网多进程 C++ 系统「维测服务」设计方案

## 1. 背景与目标（Context）

目标系统是一套运行在**局域网**内的 C++ 系统：

- 多个 C++ 进程由**本地局域网 Web 服务平台**拉起和调度；
- 进程间主要走 **gRPC / HTTP2**，少量走 Socket；
- 这些进程还会调用**另外的局域网进程**接口，调用链存在明确的层级关系；
- 规模约 **10–50 个节点、50–200 个进程**；
- 部署环境为**纯内网、可离线**。

当前排障现状：

- 每个进程组件各自打印日志：监控数据日志、控制命令日志、业务日志、错误日志混在一起，**信息量过大**；
- 出错时只打印错误日志，缺少请求上下文，无法把「一次请求」在多个进程中的日志串起来；
- 开发人员修复 bug 需要在多个进程、多个节点、多个日志文件之间**人工翻找、手动对齐时间线**；
- 调用链层级长，问题定位依赖个人经验，难以复用。

本方案的目标是建设一套**局域网内可离线部署的维测服务**，把「人工翻长链路」变成：

> 输入 `request_id` / `trace_id` / `task_id` / 错误码 / 时间窗 → 自动还原跨进程调用链 → 关联每个环节的关键日志与错误 → 给出可疑根因与证据包。

四段能力：

1. **看得见**：进程/实例/节点健康、资源与业务指标、gRPC 成功率与延迟。
2. **串得起**：跨进程、跨节点的 gRPC/Socket 调用链，进程内函数/线程时序链，请求级日志关联。
3. **定得准**：日志降噪与关键信息提取、崩溃栈、性能剖析、一键诊断。
4. **留得下**：告警与事件、证据包，供现场复盘和内部 AI 加速分析。

预期效果（目标值，需在 M2 验收中校准）：

- 一次跨进程故障从「多人、多小时、人工翻日志」降到「单人、分钟级、按请求定位」；
- 在保留错误与关键证据的前提下，日志总量下降 50% 以上；
- 错误按指纹聚合，重复问题可直接匹配历史处理记录；
- 证据包让内部 AI 参与分析，缩短「定位 → 修复 → 验证」循环；
- 存量程序分级接入：L0 零改造先覆盖全部进程，L1 只改公共库与核心服务，避免逐个程序大改。

**非目标**：公网 SaaS、多租户、RUM/移动端、SIEM、云账单。首期聚焦局域网内这一套系统。

> 代号说明：本文用 `vmon` 作为 SDK/Agent/Server 的占位代号，落地时统一替换为项目正式命名。

## 2. 竞品拆解与取舍

### 2.1 观测云产品地图（参考对象）

观测云把能力分成六组：监控与运维、软件交付与优化、用户与业务分析、安全、AI 观测、平台能力。与本项目相关的部分：

| 观测云能力 | 作用 | 本项目取舍 |
|---|---|---|
| 基础设施监控 | 主机/进程/网络指标与资源拓扑 | **采纳**，收敛为节点/进程/实例视图 |
| APM 应用性能监控 | Trace、服务拓扑、接口耗时、错误率 | **核心采纳**，改造为 gRPC 跨进程链路 |
| 持续 Profiling | CPU/内存/锁/IO 火焰图，Span 关联 | **采纳**，按需 + 低频常驻 |
| 日志监控 | 采集、解析、检索、聚类、告警 | **核心采纳**，重点做降噪与模板聚类 |
| 监控告警 | 阈值/突变/日志/DQL 检测、分级通知 | **采纳**，本地规则引擎 |
| 事件中心 / 异常追踪 | 告警、变更、Issue 生命周期 | **简化采纳**，事件 + 处理状态 |
| SLO 管理 | SLI/SLO/错误预算 | 二期 |
| DataKit | 一体化采集 Agent | **参考架构**，自研 Agent |
| Pipeline | 入库前字段解析、脱敏 | **采纳**，轻量版 |
| GuanceDB 3.1 | Metric/Event 双引擎、存算分离 | **不采纳**，用 VictoriaMetrics 栈 |
| 统一目录 | 服务/主机/数据库资产与关系 | **采纳**，服务-实例-节点-进程模型 |
| DQL & API | 统一查询语言与开放接口 | **简化采纳**，本地查询 API |
| MCP Server / Obsy AI | 让 AI 在授权范围内查询与诊断 | **内部版采纳**，仅内部使用 |

观测云最值得借用的方法论：**统一标签 + 跨信号关联 + 从告警一键下钻 + 证据可回溯**。它的重平台部分不适合本项目，但「一条请求串起日志、链路、指标」的排障路径正是当前最缺的。

### 2.2 竞品定位

| 产品 | 核心优势 | 对本项目的启示 |
|---|---|---|
| Datadog | 集成广、APM/日志/基础设施一体 | 统一标签和跨信号跳转是体验关键 |
| Dynatrace | 拓扑 + 因果分析（Davis AI） | 自动依赖发现与根因收敛值得借鉴 |
| Grafana LGTM | 开源、可自建、生态成熟 | 存储与可视化可复用，但需自己拼装 |
| SigNoz | OTel 原生、开源自托管 | 证明 OTel + ClickHouse 路线可行 |
| 夜莺 n9e | 开源告警引擎、多数据源 | 告警规则与通知层可参考 |
| 观测云 | 国内私有化、全栈信号、AI Agent | 产品分层与排障路径最贴近本项目 |

结论：不做「大而全平台」，只做**局域网内最短的排障路径**：一条请求 → 完整链路 → 关键日志 → 根因线索 → 证据包。

## 3. 总体架构

```
┌──────────────────────────── 局域网（纯内网，10–50 节点） ────────────────────────────┐
│                                                                                      │
│  服务平台 / 调度器                                                                    │
│   ├─ 拉起/停止/升级进程，分配任务                                                      │
│   ├─（可选）注册中心/API：服务名、实例、版本、IP:端口、状态                              │
│   └─ 生命周期事件（发布/重启/扩缩容）──────────────────┐                               │
│                                                         │ webhook/API                   │
│  节点 1..N                                              ▼                               │
│  ┌─────────────────────────────────────────────────────────────────────────────┐      │
│  │ 业务 C++ 进程（gRPC 为主，少量 Socket；可能调用其他 LAN 进程）                │      │
│  │  ├─ libvmon SDK：结构化日志/指标/Trace/心跳/崩溃/控制通道                     │      │
│  │  ├─ 进程内：VMON_SPAN 函数级 span、线程事件环、异步交接 causality.id          │      │
│  │  └─ gRPC Interceptor：注入/提取 traceparent、request_id、task_id              │      │
│  │                                                                             │      │
│  │ vmon-agent：进程发现、procfs、日志采集、崩溃收集、诊断执行、本地缓冲           │      │
│  └──────────────────────────────┬──────────────────────────────────────────────┘      │
│                                 │ OTLP/HTTP2（批量、压缩、断点续传）                     │
│                                 ▼                                                      │
│  ┌──────────────────────── 中心 vmon-server（1 台，可选主备） ───────────────────┐     │
│  │ 接入层：鉴权、限流、采样、去重、Pipeline 解析                                   │     │
│  │ 分析层：链路还原、日志聚类/指纹、关键信息提取、规则引擎、诊断编排                │     │
│  │ 存储层：VictoriaMetrics（指标）/ VictoriaLogs（日志）/ Jaeger 或 Parquet（链路） │     │
│  │ 交互层：Web UI（请求级排障）/ HTTP API / CLI / MCP（内部版）                     │     │
│  └───────────────────────────────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

部署形态：

- **标准**：1 台中心 `vmon-server` + 每节点 1 个 `vmon-agent`，纯离线安装包。
- **小规模合并**：中心与某个 Agent 同机部署，减少一台机器。
- **高可用**：中心主备与存储副本，二期再做。
- **断网/分区**：Agent 本地 WAL 缓存，恢复后续传，不丢关键数据。

## 4. 关键设计

### 4.1 统一数据模型与关联键

所有信号带统一 resource attributes：

| 字段 | 说明 |
|---|---|
| `node.id` / `node.name` | 节点标识与主机名 |
| `host.id` / `boot.id` | machine-id / 本次开机标识 |
| `service.name` / `service.instance.id` | 服务名 / 实例 ID |
| `service.version` / `build.id` / `git.commit` | 版本与构建标识 |
| `process.pid` / `process.start_time` | 进程身份，**必须成对使用** |
| `process.cgroup_id` | 资源隔离与命名空间归属 |
| `program.name` / `program.path` | 可执行文件名与路径 |
| `env` / `deployment` | 环境与部署标识 |

请求级关联字段：

| 字段 | 说明 |
|---|---|
| `trace_id` / `span_id` / `parent_span_id` | W3C Trace Context |
| `request_id` | 业务请求唯一 ID，跨进程必带 |
| `task_id` / `command_id` | 平台调度任务/控制命令 ID |
| `rpc.system` / `rpc.service` / `rpc.method` | gRPC 服务与方法 |
| `rpc.grpc.status_code` | gRPC 状态码 |
| `peer.service` / `peer.address` | 下游服务与地址 |
| `error.code` / `error.type` | 业务错误码与错误类型 |
| `component` / `role` | 组件名与角色（主/备/工作进程） |
| `span.kind` | `server` / `client` / `internal`（进程内） |
| `thread.id` / `thread.name` | 线程标识与名称 |
| `event.seq` / `event.parent_seq` | 线程内事件序号与因果父序号 |
| `causality.id` | 跨线程/异步交接的因果链 ID |
| `stack.hash` / `stack.frames` | WARN/ERROR 时的调用栈指纹与帧 |

关联键：

- 请求关联：`trace_id` → `span_id` → 日志中的 `trace_id`；
- 业务关联：`request_id` / `task_id`；
- 生命周期关联：`boot_id + pid + start_time`（避免重启与 PID 复用）；
- 版本关联：`build.id`（崩溃、火焰图、debuginfo、变更记录都靠它）；
- 拓扑关联：gRPC 边（caller/callee、method、status）与 Socket 边（peer、fd、inode）；
- 时间基准：双时间戳（monotonic + wall-clock）；跨节点对齐依赖 NTP，需监控时钟偏差。

### 4.2 C++ 探针 SDK（`sdk/`）

- **语言与构建**：C++17，静态库优先，头文件 + `.a`；热路径不依赖异常/RTTI 传播。
- **复用**：Trace/Metric/Log 的协议与导出优先薄封装 OpenTelemetry C++ SDK，保证 OTLP 兼容；自研集中在崩溃、心跳、控制通道、gRPC 拦截器和日志治理。
- **结构化日志**：
  - 每线程 SPSC 环形队列（2 的幂、cacheline padding）；
  - 事件用 POD/flat buffer，字符串截断 + intern；
  - 自动注入 `trace_id`/`span_id`/`request_id`/`task_id`；
  - 支持分级、采样、token bucket 限流、去重窗口；
  - 溢出丢弃并累加 drop 计数，SDK 永不阻塞业务线程。
- **指标**：Counter/Gauge/Histogram，标签基数上限保护，禁止把 `request_id` 放进标签。
- **Trace**：W3C `traceparent`；Span 生命周期 RAII；gRPC 拦截器自动创建 client/server span；`VMON_SPAN` 创建进程内 span。
- **进程内时序**：每线程事件环（Breadcrumbs）；线程池/队列/回调包装器传递 `causality.id`；WARN/ERROR 自动附带前序事件与栈指纹。
- **心跳**：业务线程定期 `vmon::heartbeat()`；Agent 区分「进程活着但卡死」和「进程不存在」。
- **崩溃处理**：`SIGSEGV/SIGABRT/SIGBUS/SIGFPE` 处理器只向**预打开 fd/memfd** `write()` 原始上下文后 `_exit`；不在信号上下文里 malloc、格式化、抓栈或符号化。优先 `core_pattern=|/path/crash-helper`，进程内 minidump 仅作兜底。
- **控制通道**：UDS，接收指令：调日志级别、抓全线程栈、启动/停止剖析、导出进程快照。
- **低开销设计**：
  - 后台线程 100 ms / 64 KB 批量 flush；
  - 背压：队列水位 → 采样 → 丢弃；
  - `pthread_atfork` 重置 ring、锁与 pid；exec 通过环境变量传递 trace 上下文；
  - TLS 用 initial-exec 模型；避免每个事件调用 `getpid()`。
- **易踩的坑**：静态初始化顺序（main 前打日志）；SDK 自身日志不得回灌自身管道；共享内存 ring 用 acquire/release + 序号防 ABA；关闭阶段不依赖异常。
- **开销目标**：CPU < 1%，常驻内存 < 15 MB，Counter 自增 p99 < 1 µs，drop 率可观测。

### 4.3 gRPC 与 Socket 链路传播（`adapters/`）

gRPC 是主链路，优先做深：

- **Client Interceptor**：在 `ClientContext` metadata 注入 `traceparent`、`tracestate`、`x-request-id`、`x-task-id`；记录调用开始/结束、`grpc::Status`、peer 地址、超时与重试。
- **Server Interceptor**：从 `ServerContext` 提取上下文，创建 server span；把 `trace_id` 注入该进程的日志上下文；出错时记录 `rpc.grpc.status_code` 与错误码。
- **Streaming RPC**：每条流创建一个 span；按需为消息创建子 span；上下文在流建立时传播一次即可。
- **异步/generic stub**：通过 gRPC interceptor factory 统一接入，不要求业务改调用方式。
- **跨节点**：trace 上下文随 HTTP/2 metadata 自然跨节点，无需额外协议。
- **Socket 链路**：
  - 自定义协议在消息头预留 16–24 字节 trace 字段；
  - 无法改协议时，用连接级上下文 + 业务 `request_id` 关联；
  - 再不行用 eBPF uprobe/kprobe 做弱边推断。
- **零改造选项（L0）**：不改代码时，可用节点级 gRPC sidecar/proxy（能注入 trace 头，但多一跳）或 eBPF uprobe（只能弱关联）。默认优先 L1 代码接入，sidecar 作为无法改代码时的备选。
- **强弱边分离**：gRPC interceptor 与协议适配器产生的边是强边，直接进 Span 树；preload/eBPF 推断的边标记 `confidence`，单独进拓扑图，不污染调用链结论。

### 4.4 维测 Agent（`agent/`）

- **进程发现**：两种模式并存：
  1. **平台 API / 注册中心模式**（推荐）：从平台拉取服务名、实例、版本、IP:端口、状态与生命周期事件；
  2. **进程扫描模式**：从 `/proc`、监听端口、启动参数、配置文件推断服务身份。
- **身份维护**：`boot_id + pid + start_time`，识别重启；与平台实例 ID 做映射。
- **采集**：`/proc/<pid>/{stat,status,io,smaps,fd,limits}`、cgroup、节点 load/CPU/内存/磁盘/网络、`dmesg`（OOM、段错误）。
- **日志采集**：文件 glob + 轮转 + 多行合并；探针直送优先；本地预处理（字段提取、脱敏、限流）。
- **缓冲**：本地磁盘 WAL，容量上限与丢弃策略（先丢低价值日志，保指标、错误、崩溃）。
- **诊断执行器**：白名单命令（`gdb`、`pstack`、`perf`、`ss`、`lsof`、`cat /proc/...`），固定参数模板，禁止 shell 拼接；超时、资源配额、输出截断、审计。
- **多路采集仲裁**：SDK > LD_PRELOAD > eBPF；`producer_id + seq` 去重；进程级 capability 协商。
- **配置**：YAML/TOML，支持热加载与配置版本记录。

### 4.5 中心服务端（`server/`）

- **接入层**：OTLP/HTTP2 接收；鉴权（节点证书/token）、限流、采样、去重；Pipeline 解析与脱敏。
- **资产模型**：服务 → 实例 → 节点 → 进程 → 端口，维护版本与健康状态。
- **分析层**：
  - 链路还原与关键路径；
  - 日志模板聚类、指纹、关键信息提取；
  - 规则引擎与事件中心；
  - 诊断编排（跨节点下发只读诊断任务）。
- **查询层**：按 `trace_id` / `request_id` / `task_id` / 错误码 / 时间窗查询，返回链路 + 日志 + 指标 + 事件的统一视图。
- **交互层**：Web UI、HTTP API、CLI、内部版 MCP。
- **容量**：50–200 进程规模下单中心可承载；预留存储扩容与 ClickHouse 迁移路径。

### 4.6 日志治理与降噪（核心）

当前最大痛点是日志太多。目标不是「全量收集」，而是「**留下能定位问题的部分**」。

先按日志类型分流：

| 日志类型 | 典型内容 | 默认策略 | 用途 |
|---|---|---|---|
| 错误日志 | error/exception/失败返回 | 100% 保留 + 指纹聚合 | 故障定位主证据 |
| 控制命令日志 | 命令下发/执行/回执 | 100% 结构化，带 `command_id`/`task_id` | 操作审计与链路 |
| 监控/心跳日志 | 周期性状态、指标打印 | **转成指标**，日志侧采样或关闭 | 趋势分析，不占日志存储 |
| 业务日志 | 任务状态、订单、关键分支 | 结构化 + INFO 采样 | 业务链路还原 |
| 调试日志 | debug/trace 细节 | 默认关闭，按需开启 | 深度排查 |

治理手段：

1. **结构化**：统一字段（`trace_id`、`request_id`、`task_id`、`component`、`error_code`、`rpc_method`、`peer`），禁止只打自由文本。
2. **降噪**：模板聚类（Drain 类算法）+ 指纹（模板 + 错误码 + 栈哈希）+ 去重窗口（如 60 秒内同指纹计数）+ 按实例/组件限流。
3. **动态级别**：按服务/实例/请求临时开启 DEBUG，带 TTL 自动恢复，避免长期高噪。
4. **关键信息提取**：正则/JSON path 提取错误码、请求 ID、任务 ID、重试次数、耗时、业务主键；高基数字段自动识别并禁止做索引标签。
5. **冷热分层**：热数据 VictoriaLogs 保留 7–14 天；冷数据压缩为 Parquet 保留 30–90 天；错误与崩溃证据长期保留。
6. **双向关联**：日志 → 链路（按 `trace_id`）；链路 → 日志（按 Span 时间窗 + `trace_id`）。
7. **噪声屏蔽**：可配置「已知无害」模板，直接降级或丢弃，但保留计数。
8. **错误上下文**：WARN/ERROR 自动附带进程内 span 路径、最近 N 条线程事件、状态快照与栈指纹，让「一条错误日志」自带前因后果。

预期效果：在不丢失排障证据的前提下，日志量下降 50% 以上，错误日志可聚合到「指纹 + 次数 + 首个/最后一个样本」。

### 4.7 请求级排障与调用链还原（核心）

**入口**：`request_id` / `trace_id` / `task_id` / 错误码 / 时间窗 / 告警事件。

**处理流程**：

1. 用 `trace_id` 找到整棵 Span 树；用 `request_id`/`task_id` 补齐未采样或跨系统段落。
2. 把每个 Span 映射到服务实例、节点、进程与 gRPC 方法。
3. 按 `trace_id` + Span 时间窗拉取各环节日志，挂到对应 Span。
4. 标注：首个错误、最慢 Span、超时、重试、连接失败、队列等待、锁等待。
5. 长链路压缩：折叠重复 RPC、只保留关键路径与异常分支，避免「链路越长越难读」。
6. 生成结论草案：可疑根因（下游超时 / 连接拒绝 / 资源不足 / 锁等待 / OOM / 业务错误码）+ 证据链接。
7. 一键导出证据包。

**三种视图**：

- **Trace 树**：一次请求的完整调用层级；
- **服务拓扑**：服务级依赖与跨节点边，带错误率、p99、调用量；
- **时间线**：日志、指标、事件、发布变更对齐到同一时间轴。

**关键信息提取**：错误码、首个异常日志、Top 慢 Span、重试次数、超时次数、调用深度、跨节点跳数、受影响服务集合。

#### 4.7.1 进程内时序链路与调用链还原（核心补充）

跨进程链路回答「哪个进程、哪一跳」，进程内时序链路回答「进程里哪段执行、哪个线程、哪个状态推进出了问题」。两者用同一个 `trace_id` 融合。

**目标**：把进程内的函数调用、阶段推进、线程/异步交接、WARN/ERROR 发生时的前后事件，串成一条可读的时序链。

**四层采集**：

1. **函数级 Span（插桩增强，主路径）**
   - RAII API：`VMON_SPAN("ParseRequest")`、`VMON_SPAN_ATTR("stage", "decode")`；
   - 进程内 span 作为 gRPC server span 的子 span，沿用同一 `trace_id`；
   - 嵌套深度上限（默认 32）、采样、编译开关；
   - 单次创建目标 < 100 ns；不 malloc、不加锁，使用线程本地 span 栈。

2. **线程事件环（Breadcrumbs）**
   - 每线程固定大小无锁环（默认 256 条）；
   - 记录：monotonic 时间、事件类型、代码位置、关键字段、级别、`event.seq`；
   - WARN/ERROR/异常时自动附带最近 N 条，形成「出错前发生了什么」；
   - 支持 `vmon::state("queue_depth", n)` 记录关键状态快照。

3. **异步/跨线程因果链**
   - 线程池任务包装器：提交时捕获 `trace_id`/`span_id`/`causality.id`，工作线程恢复；
   - 队列包装器：enqueue/dequeue 记录事件，关联生产者与消费者；
   - 回调包装器：定时器、epoll、gRPC async callback 统一包装；
   - 没有这层，异步系统里的链路会在回调处断开。

4. **无插桩兜底（日志序列还原）**
   - 从现有日志按 `thread_id` + `request_id`/`task_id` + 时间戳重建事件序列；
   - 能识别阶段耗时、异常间隔、重试与重复模式；
   - 无法还原函数调用栈，精度低于插桩，作为过渡方案。

**WARN/ERROR 自动上下文**：每条告警/异常日志自动带上：

- 当前进程内 span 路径（函数名、文件:行）；
- 最近 N 条线程事件；
- 关键状态快照；
- `trace_id` / `request_id` / `task_id` / `thread.id`；
- `stack.hash`（调用栈指纹，用于聚合）。

**时序异常识别**：

| 模式 | 判定 | 价值 |
|---|---|---|
| 长间隔 | 相邻事件间隔超过阈值 | 定位阻塞、等锁、等 IO |
| 重试循环 | 同事件/同错误短时间重复 | 定位重试风暴与下游不稳定 |
| 状态跳变 | 状态机出现非法迁移 | 定位业务逻辑缺陷 |
| 事件乱序 | 序号与时间戳矛盾 | 定位并发/同步问题 |
| 阶段耗时异常 | 某阶段耗时显著偏离基线 | 定位性能退化 |

**可视化**：线程泳道时间线（每线程一行）、进程内调用树、WARN/ERROR 标记、阶段耗时条、错误上下文面板。

**开销控制**：span 创建 < 100 ns；线程事件环固定内存；栈只在 WARN/ERROR 或采样时抓取；进程内只记录地址与 build_id，符号化离线完成；支持按模块/函数白名单启停。

**与跨进程链路的融合**：gRPC server span 下的进程内 span 形成「跨节点 → 进程 → 函数 → 事件」的完整树；排障时可以逐层展开，也可以只看异常分支。

### 4.8 崩溃诊断（`symbolizer/` + SDK crash 模块）

- **触发**：以 `core_pattern=|/path/crash-helper` 为主；进程内信号处理器兜底；兼容 `systemd-coredump`。
- **解析时机**：符号化、栈展开、maps 读取全部在 Agent/Server 离线完成，绝不在崩溃进程内做重活。
- **产物**：core/minidump、全线程 backtrace、maps、smaps、status、cmdline、environ（脱敏）、打开 fd、最近日志、最后若干条 trace。
- **符号化**：构建保留 build-id 与独立 debuginfo；Server 维护 build-id → debuginfo 索引；离线 `llvm-symbolizer` / `addr2line`，支持剥离二进制、内联帧、C++ demangle。
- **崩溃指纹**：`build_id + 归一化 Top N 帧`，去重聚合。
- **版本关联**：崩溃 → build_id → git commit → 平台发布记录。
- **安全**：async-signal-safe；预打开输出 fd；限制文件大小；处理器超时由看门狗兜底。

### 4.9 性能剖析（`profiler/`）

- **CPU**：进程内低频 `SIGPROF` 采样（默认 99 Hz）+ 按需 `perf record`；off-CPU 交给 eBPF。
- **火焰图**：折叠栈生成，兼容 FlameGraph/inferno；保存 Top 函数与占比。
- **内存**：RSS/anon/file 趋势；allocator hook 或 malloc 拦截；周期性堆快照差分，输出泄漏候选。
- **锁**：futex 等待采样或 SDK 锁包装器；输出竞争热点与持锁栈。
- **IO**：`/proc/<pid>/io`、块设备延迟、慢系统调用采样。
- **跨节点诊断会话**：Server 下发任务，Agent 执行，结果回传并关联 trace。

### 4.10 健康监控与告警

- **指标**：节点资源、进程存活、心跳年龄、重启次数、线程/fd、gRPC QPS/错误率/p99、队列深度、自定义业务 KPI。
- **规则**：阈值、变化率、心跳缺失、多指标组合、日志指纹突增、gRPC 错误码突增。
- **事件**：告警 + 崩溃 + 重启 + 平台发布/扩缩容变更统一进事件中心。
- **通知**：Web UI、Webhook（飞书/钉钉/企业微信）、邮件（离线 SMTP）、syslog。
- **降噪**：滞回、静默、分组、恢复通知、同指纹合并。

### 4.11 存储与查询

按局域网整体日增量分档：

| 日增量 | 推荐 | 说明 |
|---|---|---|
| < 10 GB | VictoriaMetrics + VictoriaLogs + Parquet（trace） | 最轻，适合首期 |
| 10–100 GB | VM + VL + Jaeger v2 all-in-one 或 Parquet | **默认推荐** |
| > 100 GB | ClickHouse 单机/集群 + VM | 需要更强关联分析时 |

取舍：

- **VictoriaMetrics + VictoriaLogs**：成熟、单二进制、低内存、离线部署简单、Apache 2.0。
- **Trace 存储**：先用 Jaeger v2 all-in-one（Badger）或 Parquet，避免在关键路径上承担 VictoriaTraces 的成熟度风险；跑稳后再评估替换。
- **ClickHouse**：SQL/Join 与压缩强，但单机内存 4–8 GB 起、离线包重，仅在大数据量或复杂关联时启用。
- **Grafana**：可选探索界面，不作硬依赖（AGPL，内部使用可接受，对外分发需评估）；自研 Web UI 聚焦请求级排障。
- 所有常驻组件统一 systemd 内存/磁盘限额；磁盘满时优先保指标、错误日志与崩溃证据。
- **采样策略**：正常请求头部采样 1–10%；错误、超时、慢请求通过 gRPC interceptor 标记后 **100% 保留**（尾部采样在 Server 侧完成）。

### 4.12 诊断编排与一键巡检

- **一键诊断**：输入 pid / 服务实例 / `request_id`，自动执行只读采集（栈、fd、socket、内存、IO、锁、最近日志、最近 trace），输出结构化报告。
- **一键巡检**：进程存活、心跳、资源水位、磁盘、core 配置、时钟同步、gRPC 错误率、日志错误指纹。
- **故障剧本**：卡死、崩溃、内存增长、gRPC 超时、IPC 堆积、CPU 飙高、发布后错误突增。
- **安全边界**：只读优先；写操作（改日志级别、触发 core）需显式确认并审计。

### 4.13 内部 AI 证据包（`tools/evidence-pack/` + `tools/mcp/`，内部版）

目标：把一次故障压缩成自包含、可复核、可脱敏的证据包，交给内部开发者用 Claude Code 等工具快速分析。**不进入对外发布包**。

```
evidence-<incident>-<ts>/
├── manifest.json          # schema_version、pack_id、created_at
│                          # cluster{node_ids,os,kernel}
│                          # window{start,end}
│                          # services[{name,version,build_id,instances[]}]
│                          # incident{type,severity,trigger_rule}
│                          # redaction{rules_version,fields_masked}
│                          # files[{path,sha256,size}]
├── timeline.json          # 告警/崩溃/重启/发布变更/异常点合并时间线
├── request.json           # trace 树 + 进程内时间线 + 跨节点边 + 错误码
├── metrics.json           # 相关指标序列 + 统计摘要 + 异常点
├── logs.jsonl             # 日志模板 + 计数 + 代表样本（含 trace_id/request_id）
├── profiles/              # 折叠栈 + 火焰图 SVG + Top 函数
├── dumps/                 # 符号化栈、maps、寄存器、线程状态
├── diagnostics/           # 诊断命令与输出
├── index.jsonl            # 全量事实索引，便于 AI 检索
└── hypotheses.md          # 事实清单与待验证假设模板（非结论）
```

- **裁剪**：按故障窗口裁剪；日志模板聚类，保留计数与代表样本；目标 < 50 MB。
- **脱敏**：字段级 allowlist 优于黑名单；IP/路径/token/密钥正则在采集侧和导出侧双重执行；需要保留关联性时用 HMAC 假名化；支持 dry-run 对比与校验和。
- **访问控制**：UDS 用 `SO_PEERCRED` + `0660`；Web 仅内网 + token；导出需显式命令、审批与审计；可选签名。
- **MCP Server（内部版）**：只读工具 `query_metrics`、`query_logs`、`get_trace`、`get_request_chain`、`get_process_snapshot`、`run_diagnosis`、`create_bundle`；作用域 token；查询白名单与限速；不提供写操作，禁止直接读磁盘文件。
- **AI 上下文文件**：额外生成一份面向 LLM 的紧凑 Markdown + JSON，减少 token 消耗。
- **隔离**：CMake 选项 `VMON_INTERNAL_AI=ON/OFF`，独立安装包与文档；对外发行版不编译该模块。

### 4.14 典型故障场景与处置路径

| 场景 | 关键信号 | 诊断动作 | 产出 |
|---|---|---|---|
| 请求失败 | `trace_id`/`request_id`、gRPC 状态码、首个错误日志 | 拉整棵 Span 树 + 各环节日志 + 错误码 | 失败点、错误传播路径 |
| 请求变慢 | Span 耗时、p99、CPU/IO profile | 关键路径 + 火焰图 + 等锁/等 IO 拆分 | 慢点归因到服务/方法/函数 |
| 进程内警告/异常 | WARN/ERROR 日志、栈指纹、前序事件 | 展开线程泳道 + 进程内调用树 | 出错前的执行序列与可疑函数 |
| 状态机/重试异常 | 事件序号、状态迁移、重试计数 | 时序模式匹配（长间隔/重试循环/非法迁移） | 异常迁移点与重试风暴 |
| 跨进程超时 | gRPC deadline、重试、下游心跳 | 链路还原 + 对端状态快照 | 超时发生在哪一跳 |
| 日志风暴 | 指纹突增、同模板计数 | 聚类 + 限流 + 动态降级 | 噪声源与保留样本 |
| 进程崩溃 | core、dmesg、最近日志 | core_pattern helper → 符号化 → 版本关联 | 崩溃报告与可疑变更 |
| 进程卡死 | 心跳超时、线程状态、锁等待 | 全线程栈 + futex + syscall | 持锁栈与阻塞点 |
| 内存增长 | RSS/堆快照趋势 | 快照差分 + 分配栈 | 泄漏候选 |
| 发布后异常 | 平台发布事件 + 错误率突增 | 事件时间线关联 | 是否由本次变更引入 |

### 4.15 框架扩展点（「先做框架」的落点）

M1 不只是跑通一条链路，而是把后续能力都挂到稳定接口上：

| 扩展点 | 接口 | 后续能力接入方式 |
|---|---|---|
| RPC 适配器 | `RpcAdapter`：注入/提取上下文、记录状态码、peer 信息 | gRPC、Socket、私有协议各自实现 |
| 进程内插桩 | `SpanScope`：RAII、深度/采样、异步上下文捕获 | 函数级 span、线程池/队列/回调包装 |
| 采集器 | `Collector`：周期采集、事件回调、能力声明 | procfs、日志、崩溃、eBPF 插件化 |
| 存储后端 | `Store`：写入、查询、保留策略 | VM、VL、Jaeger、Parquet、ClickHouse |
| 日志处理器 | `LogProcessor`：解析、脱敏、采样、聚类 | Pipeline 规则与模板库 |
| 诊断脚本 | `DiagnosisStep`：只读命令、超时、输出结构 | 一键诊断、巡检、故障剧本 |
| 通知通道 | `Notifier`：分级、路由、静默 | Webhook、邮件、syslog |
| 分析扩展 | `Analyzer`：输入统一快照，输出结论与证据 | 根因规则、AI RCA |

这样做的好处：M2–M6 每完成一个能力，只新增实现，不动框架；RPC 从 gRPC 扩展到 Socket 或私有协议时也不用改主流程。

### 4.16 接入成本与分级改造（存量程序适配）

结论先说：**需要适配，但不需要所有程序大改**。方案按 L0–L3 分级，存量程序可以先零改造接入，再按价值逐步加深。

| 级别 | 改造内容 | 单进程工作量 | 获得能力 | 适用对象 |
|---|---|---|---|---|
| L0 零改造 | 无代码改动；部署 Agent；保留 debuginfo；日志落文件 | 0 | 进程/资源指标、日志采集与检索、崩溃捕获与符号化、eBPF/sidecar 方法级调用关系、日志序列还原 | 全部存量进程 |
| L1 轻改造 | 启动时 `vmon::init()`；日志宏替换；gRPC interceptor 注册；心跳 | 0.5–1 人日 | 精确跨进程 trace、`request_id`/`task_id` 贯穿、WARN/ERROR 自动上下文 | 核心 10–20 个进程 |
| L2 标准改造 | 关键函数 `VMON_SPAN`；线程池/队列/回调包装；业务字段与状态快照 | 2–5 人日/进程 | 进程内时序链、关键路径、状态异常识别 | 重点模块 |
| L3 深度改造 | 全量函数插桩、分配器 hook、自定义指标 | 按需 | 函数级火焰图、内存泄漏定位 | 1–2 个关键进程 |

**把工作量压下来的关键**：

1. **改公共库，不改每个程序**：日志库、gRPC 封装、线程池、任务队列通常是全系统共用的基础库。在这些库上做一次改造，所有使用方自动获得能力。
2. **CMake 一行接入**：`vmon_attach(target)` 自动链接静态库、加编译选项、生成 build-id。
3. **日志宏兼容层**：保留原有宏名/函数名，内部转发到 vmon，业务代码不用改调用点。
4. **gRPC interceptor 工厂**：在公共 channel/server 创建处注册一次，业务代码零改动。
5. **线程池/队列 drop-in 包装**：提供与原接口兼容的 wrapper，替换头文件即可。
6. **平台统一下发配置**：服务启动参数/环境变量控制开关、采样率、日志级别，不用逐进程改配置。
7. **灰度接入**：按服务/实例逐步开启，先核心链路，后长尾进程。
8. **L0 始终可用**：即使某进程无法重新编译，也能获得基础可观测与崩溃诊断。

**诚实的能力边界**：

- 零改造可以拿到指标、日志、崩溃、粗粒度调用关系；但**精确的端到端 trace 必须有注入点**（代码 L1 或 sidecar 代理）。
- 进程内函数级时序必须有插桩或按需 eBPF，无法完全零改造获得。
- 日志里的 `trace_id` 需要日志库改造；否则只能按时间窗 + `request_id` 做近似关联。

**建议的落地顺序**：

1. 先做 L0，让全部进程「看得见、崩了有栈」；
2. 在公共日志库和 gRPC 封装上做 L1，一次性覆盖大部分进程；
3. 只对 Top 10 错误率高/链路关键的服务做 L2；
4. 1–2 个疑难进程做 L3。

**L0 接入清单（零代码）**：

1. 节点部署 `vmon-agent`；
2. 配置日志路径、进程匹配规则、平台 API（可选）；
3. 构建保留 debuginfo 与 build-id；
4. 配置 `core_pattern` 指向 crash-helper；
5. 平台下发实例元数据（可选，能提升资产准确度）。

**L1 接入清单（单进程 0.5–1 人日）**：

1. CMake 加 `vmon_attach(target)`；
2. 启动处 `vmon::init(cfg)`，退出处 `vmon::shutdown()`；
3. 日志宏替换为兼容层（调用点不变）；
4. gRPC channel/server 创建处注册 interceptor 工厂；
5. 主循环/工作线程定期 `vmon::heartbeat()`；
6. 构建保留 `-g`、build-id 与独立 debuginfo。

这样，50–200 个进程的改造工作量可以收敛到「公共库 2–4 处 + 核心服务 10–20 个」，而不是逐个改 200 个程序。

## 5. 技术选型

| 层 | 选型 | 理由 |
|---|---|---|
| 探针语言 | C++17 | 与目标系统一致，无额外运行时 |
| RPC 接入 | gRPC C++ Interceptor + W3C Trace Context | 不改业务调用方式，跨节点天然传播 |
| Trace/Metric 协议 | OpenTelemetry C++ SDK + OTLP | 复用成熟实现，保留可迁移性 |
| 指标存储 | VictoriaMetrics | 单机低资源、PromQL 兼容 |
| 日志存储 | VictoriaLogs | 单二进制、全文检索、压缩率高 |
| 链路存储 | Jaeger v2 all-in-one 或 Parquet | 成熟、单机简单；VT 跑稳后再评估 |
| 本地元数据 | SQLite | 零运维、单文件 |
| 中心服务 | C++17 + cpp-httplib + 内嵌静态 UI | 与团队技术栈一致，无 Node 运行时依赖 |
| 序列化 | nlohmann/json（控制面）+ protobuf（数据面/OTLP） | 控制面可读，数据面高效 |
| 符号化 | llvm-symbolizer + build-id 索引 | 离线、支持剥离二进制 |
| eBPF | libbpf + CO-RE（可选） | 无侵入兜底；内核不满足时降级 |
| 告警通知 | 自研规则引擎 + Webhook/SMTP/syslog | 离线可用 |
| 可视化 | 自研请求级排障 UI；Grafana 可选 | 聚焦工作流 |

许可证注意：VictoriaMetrics 栈 Apache 2.0；OpenTelemetry Apache 2.0；gRPC Apache 2.0；Grafana AGPL（对外分发需评估）；FlameGraph 脚本许可证引入前确认，优先用 inferno 或自绘 SVG。

## 6. 仓库结构

```
CplusTrace/
├── CMakeLists.txt
├── cmake/                      # 打包、build-id、debuginfo 规则
├── sdk/                        # C++ 探针
│   ├── include/vmon/           # metrics/log/trace/span/breadcrumb/health/crash/control/rpc
│   ├── src/
│   └── tests/
├── adapters/                   # 链路传播适配
│   ├── grpc/                   # gRPC client/server interceptor
│   ├── ipc/                    # Socket/共享内存/消息队列头注入
│   ├── preload/                # LD_PRELOAD 兜底
│   └── bpf/                    # eBPF 弱边推断
├── agent/                      # 节点常驻采集与诊断
│   ├── src/
│   │   ├── discovery/          # 进程发现 + 平台注册中心对接
│   │   ├── collectors/         # procfs/cgroup/host/log/crash
│   │   ├── buffer/             # 本地 WAL
│   │   ├── diagnostics/        # 诊断执行器与剧本
│   │   └── transport/          # OTLP 上报、断点续传
│   └── tests/
├── server/                     # 中心服务
│   ├── src/
│   │   ├── ingest/             # 接入、鉴权、限流、采样、Pipeline
│   │   ├── catalog/            # 服务-实例-节点-进程资产模型
│   │   ├── analysis/           # 链路还原、日志聚类、关键信息提取
│   │   ├── rules/              # 规则引擎、事件中心
│   │   ├── diagnostics/        # 跨节点诊断编排
│   │   └── api/                # HTTP/CLI/MCP
│   └── tests/
├── query/                      # 存储适配：VM / VL / Jaeger / Parquet / SQLite
├── symbolizer/                 # build-id 索引与离线符号化
├── profiler/                   # 采样、折叠栈、火焰图
├── tools/
│   ├── cli/                    # vmonctl
│   ├── evidence-pack/          # 证据包导出与脱敏
│   └── mcp/                    # 内部 AI 接口（可选编译）
├── web/                        # 请求级排障 UI 静态资源
├── deploy/                     # systemd、离线安装包、默认配置
├── examples/                   # 跨节点 gRPC 示例系统与故障注入
├── docs/                       # 设计、接入、运维、排障手册
└── tests/e2e/                  # 端到端与故障注入测试
```

首批关键文件（框架阶段）：

- `sdk/include/vmon/vmon.h`：公共 API 门面（init/metrics/log/trace/heartbeat）。
- `sdk/src/core/ring_buffer.h`：每线程 SPSC 无锁环形队列。
- `sdk/src/trace/propagation.cpp`：W3C traceparent 注入与提取。
- `sdk/src/trace/span_scope.h`：进程内函数级 span 与每线程事件环。
- `adapters/grpc/interceptor.cpp`：gRPC client/server 拦截器。
- `agent/src/discovery/registry_client.cpp`：平台注册中心对接与进程扫描兜底。
- `agent/src/collect/crash_collector.cpp`：core_pattern helper 与离线解析入口。
- `server/src/analysis/trace_builder.cpp`：调用链还原与关键路径。
- `server/src/analysis/log_cluster.cpp`：日志模板聚类与指纹。
- `tools/evidence-pack/pack.cpp`：证据包裁剪、脱敏与校验和。
- `deploy/systemd/vmon-agent.service`：资源限额与重启策略。

## 7. 实施里程碑（框架先行）

按「先做框架，再逐个完成能力」推进。

### M1 框架底座（6–8 周）

- SDK 骨架：初始化、配置、指标、结构化日志、心跳、控制通道。
- 进程内骨架：`VMON_SPAN` RAII、每线程事件环、基础 WARN/ERROR 上下文。
- gRPC Interceptor 骨架：`traceparent` 注入/提取、client/server span、状态码记录。
- Agent：进程发现（平台 API + 进程扫描双模式）、procfs 采集、日志采集、本地 WAL、OTLP 上报。
- Server：接入、鉴权、资产模型、查询 API、Web UI 骨架。
- 接入层：`vmon_attach(target)`、日志宏兼容层、gRPC interceptor 注册工厂、线程池/队列 drop-in 包装。
- 存储：VictoriaMetrics + VictoriaLogs 跑通；trace 先落 Parquet。
- **验收**：2 个节点、3 个 gRPC 服务跑通；输入 `request_id` 能看到跨节点调用链与各环节日志；Agent 断连不丢关键数据；SDK 开销 < 1%；选 2–3 个存量进程完成 L1 试点并记录实际改动人日，其余进程以 L0 接入。

### M2 日志治理与请求级排障（4–6 周）

- 日志分类策略、结构化字段、采样/限流/去重、模板聚类、指纹。
- 动态日志级别（按服务/实例/请求，带 TTL）。
- 关键信息提取（错误码、request_id、task_id、重试、耗时）。
- 进程内时序：完整 Span API、线程事件环、异步/队列/回调包装器、时序异常识别。
- 请求级排障视图：Trace 树 + 进程内时间线 + 日志 + 关键路径 + 首个错误。
- 接入推广：公共日志库与 gRPC 封装完成 L1 改造，覆盖核心 10–20 个进程；其余进程保持 L0。
- **验收**：日志量下降 ≥ 50%；一次失败请求 30 秒内定位到首个错误环节；能从错误日志看到最近 N 条前序事件与函数栈；错误指纹可聚合；核心进程 L1 覆盖完成，单进程改动不超过 1 人日。

### M3 全链路还原与拓扑（4–6 周）

- gRPC streaming、异步 stub、重试与超时的完整覆盖。
- Socket 头注入与连接级关联；eBPF 弱边兜底。
- 服务拓扑、跨节点边、调用层级、缺口检测（有调用无 Span）。
- 跨进程链路与进程内时序融合：同一 `trace_id` 下逐层展开到函数与事件。
- 尾部采样：错误/慢请求 100% 保留。
- **验收**：5 层调用链还原率 > 95%；弱边带 confidence 且不污染 Span 树；跨节点延迟可归因；任一错误 Span 能下钻到进程内时间线。

### M4 崩溃诊断（4–5 周）

- `core_pattern` helper、崩溃上下文收集、build-id 符号库、离线符号化、指纹去重。
- 崩溃与请求链路、发布记录关联。
- **验收**：注入 SIGSEGV 100% 捕获，符号准确，单次处理 < 3 秒；能从崩溃反查到最近发布。

### M5 性能剖析（4–5 周）

- CPU 采样 + 火焰图；内存趋势与堆快照差分；锁与 IO 热点。
- 跨节点按需诊断会话。
- **验收**：火焰图能定位到热点函数；内存泄漏能给出候选分配栈。

### M6 告警、巡检与内部 AI（4–6 周）

- 规则引擎、事件中心、通知渠道、静默与分组。
- 一键巡检、故障剧本。
- 证据包、脱敏、MCP Server、内部版打包与隔离。
- 离线安装、升级回滚、压测与长稳。
- **验收**：72 小时稳定；磁盘满/Agent kill/时钟跳变下优雅降级；故障注入触发告警并生成证据包；内部 AI 基于证据包给出可复核 RCA 草稿；对外发行包不含该模块。

## 8. 验证方案

1. **示例系统**：`examples/` 提供跨节点 gRPC 示例（网关 → 服务 A → 服务 B → 服务 C），包含平台调度模拟、Socket 旁路、故障注入。
2. **端到端脚本**：启动 → 注入故障 → 输入 `request_id` → 断言链路完整、日志关联、错误码正确、证据包生成。
3. **进程内时序验证**：构造异步回调报错、重试循环、锁等待长间隔、状态机非法迁移；断言错误日志带最近 N 条前序事件、函数栈与 `stack.hash`。
4. **日志治理效果**：对比治理前后日志量、错误指纹数量、定位耗时。
5. **开销基准**：SDK 接入前后 CPU、RSS、p99 延迟；目标 CPU < 1%、RSS < 15 MB、Counter p99 < 1 µs、span 创建 < 100 ns。
6. **跨节点验证**：2–5 个节点、10+ 进程；验证 trace 传播、时钟偏差、断网缓存与续传。
7. **离线验证**：断网环境用离线包安装，确认无外部依赖、无外联请求。
8. **安全验证**：节点鉴权、UDS 权限、命令注入防护、诊断白名单、证据包脱敏与审计。
9. **降级验证**：关闭 SDK、关闭 eBPF、禁用符号服务器，确认系统仍可给出可用结论。
10. **混沌与长稳**：kill Agent、磁盘写满、时钟跳变、进程频繁重启、日志风暴，验证 72 小时稳定性。
11. **接入成本验证**：选 2–3 个存量进程实测 L0/L1/L2 的实际改动人日与回归风险，校准推广排期。

## 9. 风险与边界

| 风险 | 影响 | 应对 |
|---|---|---|
| 服务平台无注册中心/API | 实例信息不准 | 进程扫描 + 端口/启动参数推断；后续再接 API |
| gRPC streaming/异步覆盖不全 | 链路缺口 | M3 专项补齐；缺口检测显式标注 |
| 跨节点时钟偏差 | 时间线错乱 | NTP 监控 + 双时间戳；偏差超阈值告警 |
| 日志量仍然过大 | 存储成本与检索变慢 | 分类治理 + 转指标 + 采样 + 模板聚类 + 冷热分层 |
| 采样导致错误请求丢失 | 关键证据缺失 | 错误/超时/慢请求 100% 保留 |
| 未保留 debuginfo | 崩溃栈不可读 | 构建流水线强制归档 `.debug` + build-id |
| Socket 协议不可改 | 链路无法显式传播 | 连接级 + request_id 关联；eBPF 弱边兜底 |
| eBPF 依赖内核/权限 | 兜底能力不稳定 | 明确降级路径；不把 eBPF 作为主路径 |
| 探针开销影响业务 | 影响目标系统 | 采样、批量、背压、全局开关、压测准入 |
| 证据包泄密 | 内部数据外泄 | 默认最小化 + 脱敏 + 审计 + 不对外发布 |
| 中心单点故障 | 采集中断 | Agent 本地 WAL；中心主备二期 |
| 自研范围膨胀 | 工期失控 | 先框架后能力；规则复用成熟组件；先 CLI 后 UI |
| 存量程序改造成本高 | 推广受阻、排期拉长 | 公共库集中改造 + L0 兜底 + 只对核心服务做 L2；先用 2–3 个进程试点量化人日 |
| 进程内插桩覆盖不足 | 时序链断裂 | 关键路径手工插桩优先；eBPF 按需补；日志序列还原兜底 |
| 异步上下文丢失 | 回调后链路断开 | 线程池/队列/回调统一包装器 + `causality.id` |
| 函数级插桩开销 | 影响业务性能 | 白名单、采样、深度上限、编译开关；默认只插关键路径 |

## 10. 待确认问题

1. gRPC C++ 的具体版本、同步/异步/streaming 使用比例，决定 interceptor 覆盖范围。
2. 服务平台是否提供注册中心/API（服务名、实例、IP:端口、版本、状态）；若没有，先走进程扫描模式。
3. 各进程日志日增量与保留期要求，用于确定存储档位。
4. 节点间时钟同步现状（NTP/PTP），是否允许部署时钟偏差监控。
5. 是否存在跨局域网（而非同一局域网）的接口调用；若有，需评估网络与鉴权边界。
6. 中心服务端可用的机器资源（CPU/内存/磁盘）与是否需要主备。
7. 内部 AI 证据包的合规边界：哪些字段绝对禁止导出。
8. 关键函数、线程池、回调框架能否插桩；是否接受编译期自动插桩（`-finstrument-functions`，仅建议开发/诊断版启用）。
9. 现有日志是否已带 `thread_id`、`request_id`、`task_id` 等字段，决定「无插桩日志序列还原」的可用性。
10. 是否存在统一的日志库、gRPC 封装、线程池/任务队列基础库；存量程序能否重新编译。这决定 L1 改造能否集中完成。
11. 是否接受节点级 gRPC sidecar 作为零代码追踪备选（会多一跳，但可以不改业务代码）。
