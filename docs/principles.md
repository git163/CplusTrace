# 原理篇：这套维测系统是怎么工作的

> 面向想真正理解系统的开发者。先读 [module-design.md](module-design.md) 的模块边界，
> 再看这篇原理；最后用 [onboarding.md](onboarding.md) 跑起来验证。

## 1. 一句话架构

```
业务进程 SDK  --UDS JSONL-->  Agent  --HTTP NDJSON-->  Server  --HTTP-->  Web / CLI / MCP
     │                          │                        │
     │ 线程本地事件环            │ 分段 WAL + 批量上传      │ 有界 Store + 索引/分析
     │ Span 栈 / 上下文          │ 进程/日志/崩溃/拨测      │ 链路/日志/规则/诊断/证据
```

核心思想：

1. **统一事件模型**：日志、指标、Span、崩溃都是同一种 Event JSON；
2. **单向数据流**：业务只写内存，采集与分析在进程外；
3. **关联优先**：先有 trace_id/request_id，再谈日志和指标；
4. **有界与降级**：任何队列、WAL、Store 都有上限，满了丢弃并计数；
5. **证据可复核**：诊断结论必须能回到原始 trace/日志/指标/证据包。

## 2. 数据模型：为什么所有信号都用 Event

一个 Event 至少包含：

| 字段 | 作用 |
|---|---|
| `type` | `span_begin/span_end/log/metric/crash/process/host/profile` |
| `time_ns` / `mono_ns` | wall clock 跨节点对齐 + monotonic 进程内时序 |
| `trace` / `span` / `parent` | 调用链 |
| `request_id` / `task_id` | 业务/调度关联 |
| `service` / `instance` / `node` / `version` | 资产归属 |
| `pid` / `tid` | 进程/线程 |
| `name` / `file` / `line` | 代码位置 |
| `level` / `msg` / `error_code` | 日志与错误 |
| `attrs` | 低基数扩展字段 |

统一的直接收益：

- Agent 只有一条接入/上传通道；
- Server 只有一套鉴权、限流、索引；
- 新增信号类型不需要新建存储引擎；
- Trace、日志、指标可以按同一组关联键互跳。

代价：Event 必须保持精简；高基数内容不能塞 `attrs`，否则索引会膨胀。

## 3. SDK 原理：热路径只写内存

### 3.1 每线程无锁环

```
业务线程:
  VMON_INFO/VMON_SPAN
    → 读线程本地 request_id/task_id + 当前 Span
    → 组装 POD Event（名称/文件/行号都是静态指针）
    → RingBuffer::push（512 槽，满则 dropped++）
```

- 单生产者/单消费者：业务线程写，后台 sender 读；
- 不锁、不分配、不做 IO；
- 队列满时**丢弃**而不是阻塞业务，这是“不能影响生产”的第一道保证。

### 3.2 Span 栈

- 每个线程一个固定深度（32）的 Span 栈；
- `VMON_SPAN` 是 RAII：进函数创建，出函数结束；
- 父 Span 取栈顶；跨进程时用 `create_span_with_parent`；
- `set_error` 只保存静态错误码指针，`span_end` 时写入事件；
- 同一槽位复用的 Span 会清空 `error_code`，避免错误“传染”。

### 3.3 request_id / task_id 的生命周期

SDK 只保存指针，sender 线程稍后才序列化，所以：

- 指针必须活到事件发送（默认 100ms）；
- 推荐字符串字面量、长生命周期对象，或像 `examples/http_backend` 那样用有界 ID 池；
- 不能指向函数内临时 `std::string`。

### 3.4 发送线程

- 每 100ms 或累计 64KB 批量序列化；
- 序列化遇到非法 UTF-8 使用 `replace`，绝不抛异常终止业务；
- 写 UDS 失败只计数；SDK 不落盘，Agent 不在线时事件会丢（这是已知边界）。

## 4. Agent 原理：本地先落账，再上传

### 4.1 接入

```
SDK --UDS--> IngestServer（单线程 poll）--> wal.append(line)
```

- UDS 权限由部署保证（目录 0770、socket 0660）；
- Agent 不解析业务语义，只按行接收 JSONL。

### 4.2 WAL

- 当前段超过 1 秒且非空，或达到段大小（默认 4MB）→ 封存；
- Uploader 每 200ms~1s 读取封存段，POST `/v1/ingest`；
- 上传成功删段；失败保留重试；
- 总容量超上限（默认 64MB）→ 删除最旧段，优先保错误/崩溃。

这提供了「中心停机不丢最近数据」的本地缓冲，但不是无限队列：容量满仍会丢。

### 4.3 采集器

| 采集器 | 原理 | 边界 |
|---|---|---|
| 进程/主机扫描 | `/proc`（macOS `ps` 兜底） | 只采匹配进程；macOS 字段不全 |
| 文件日志 | inode + offset 跟踪，轮转重开 | 只采配置的文件；单行 64KB；offset 不持久化 |
| 崩溃收集 | 扫描 `crash-<pid>.json` | 需要 SDK 崩溃兜底或 core helper |
| 命令执行 | 轮询 Server 命令，白名单 argv | 不拼 shell；有超时/输出上限 |
| HTTP 拨测 | 周期 GET，记录 up/duration/status | 当前仅 HTTP |

### 4.4 上传可靠性

- 至少一次语义：上传失败会重试，可能重复；
- Server 侧有去重/指纹/时间窗，重复不会无限放大存储；
- Agent 重启会从 WAL 恢复，但文件日志 offset 未持久化，可能重读旧文件。

## 5. Server 原理：有界存储 + 写入侧治理

### 5.1 Store

- 所有容器有上限：trace 2 万、日志 20 万、指标 10 万、事件 5000；
- 索引：trace_id、request_id、task_id、service、node、fingerprint；
- 超限淘汰最旧；重启清空（当前没有长期存储）；
- 统一一把锁，HTTP 线程池与后台规则线程共享。

### 5.2 日志治理

写入路径上完成：

```
log → 错误全留 → INFO 采样 → 同指纹去重 → 噪声模板只计数
    → 模板归一化（数字/hex/UUID/IP/路径/引号内容 → <var>）
    → 指纹 = hash(level|service|error_code|template)
    → 建索引（trace/request/task/fingerprint）
```

这是为什么现场日志量能下降，但错误与代表样本仍在。

### 5.3 链路还原

```
span_begin/span_end
  → 按 trace_id 分桶
  → begin 建节点，end 补耗时/状态
  → 按 parent 组树；父节点缺失时挂根并标注
  → 关键路径：根到最慢叶子的链路
  → 首个错误：最早进入 error 状态的 Span
  → 错误传播链：所有错误 Span + root→first_error 路径
```

事件允许乱序到达：`span_end` 先到也能先记录耗时，查询时最终一致。

### 5.4 线程/进程/系统三维

- **线程时间线**：按 `service/instance/node/tid` 分组，按 start 排序；
  - 相邻 Span 结束到下一个 Span 开始 >100ms → 记为长空档；
  - 多线程/长空档会进入诊断假设；
  - 注意：这是 Span 派生视图，不是真实线程事件环。
- **进程上下文**：trace 里的 node+pid 去 Catalog 查进程快照（cmdline/rss/threads/state）。
- **系统上下文**：trace 时间窗内查 node 的 `host.load1/mem_used_ratio` 与 pid 的
  `process.rss_bytes`，给出 min/max/last。

### 5.5 规则/告警/通知

- 规则在写入路径与后台循环中执行；
- 冷却与同指纹分组避免风暴；
- 静默按规则/类型/服务/节点/级别/时间窗匹配；
- 事件经 Store 的 sink 进入 Notifier 队列（有界 1000），异步 Webhook；
- 只给建议动作，不自动重启/改业务数据。

### 5.6 诊断与证据

- 诊断是**规则化证据汇总**，不是“AI 猜根因”：
  - 首个错误、关键路径、错误链、线程长空档、日志模式、崩溃、剖析；
  - 输出 verdict、hypotheses、checks、remediation；
- 证据包是**可复核的事实集合**：manifest + timeline + request + metrics + logs +
  profiles + dumps + hypotheses，带 SHA-256 与脱敏。

## 6. 立体排障：错误码只是入口

```
错误码聚合（跨 trace/服务）
  → 选一条 trace
  → 错误传播链（谁先错、怎么传）
  → 线程泳道（哪个线程、有没有长空档）
  → 进程（pid/命令行/RSS/线程数）
  → 节点/系统（load/mem/uptime + trace 窗口指标）
  → 诊断建议（抓栈、开 debug、导出证据）
```

关键原则：**错误码不是结论，只是索引**。结论必须能落到“哪条链路、哪个线程、哪个进程、哪台机器、什么时间”。

## 7. 可靠性与背压

| 风险 | 机制 | 代价 |
|---|---|---|
| 业务线程被阻塞 | 无锁环、满则丢弃 | 可能丢事件 |
| Agent 离线 | 本地 WAL | 容量有限，超限删旧 |
| 日志风暴 | 采样/去重/噪声模板 | 低价值日志不全 |
| 中心内存增长 | 有界 Store + 淘汰 | 旧数据清空 |
| 中心停机 | Agent 重试/WAL | 非关键数据可能丢 |
| 崩溃二次伤害 | 信号处理只 write + `_exit` | 不做完整 core 解析 |
| 诊断误操作 | 只读建议，不自动重启 | 需要人工确认 |

## 8. 为什么当前这样取舍

- **不引入外部存储**：试点规模内存 Store 足够，离线部署简单；长期存储是下一步；
- **不做自动重启/回滚**：生产安全优先；建议动作由人确认；
- **eBPF/gRPC streaming 只做接口**：依赖目标内核/gRPC 版本，无法在 macOS 验证；
- **不做全文/DQL**：现有过滤 API 已覆盖排障路径，避免自造查询语言；
- **不常驻 CPU 采样**：默认关闭，诊断版才打开，避免影响生产。

## 9. 哪些能力必须插桩

- 精确 Span 树、时序、错误链：**必须** SDK 创建/传播 Span；
- 线程视图：Span 必须带 `tid`；
- Agent 单独采集：日志（需配置）、指标、进程、崩溃、拨测；
- 零改造调用链：gRPC interceptor/sidecar/eBPF，当前**未真正接入**。

## 10. 建议的验证实验

1. 启动 `./scripts/vmon.sh up`，跑 `demo.py --scenario error --keep`；
2. 在错误码页看同一错误码的两条 Span（E_HTTP_500 → E_ECHO）；
3. 打开 trace，逐层看错误传播链、线程泳道、进程/节点/系统；
4. 停掉后端（或让拨测目标不可达），看 probe.up 变 0；
5. 把 Agent kill 掉，再产生日志，观察 SDK dropped / WAL 行为；
6. 重启中心，观察 Agent WAL 补传；
7. 打开 `tests/e2e/run_e2e.py`，对照每个断言理解数据流。
