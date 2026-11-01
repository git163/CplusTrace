# 观测云能力对标与本项目取舍（2026-11）

> 来源：https://www.guance.com/ 产品页（APM、日志、告警、事件中心、异常追踪、
> 数据洞察、Pipeline、可用性、Profiling、指标、基础设施、MCP、协作、权限）。
> 目的：找出「对局域网 C++ 系统真正重要」的能力，并在当前项目中落地一部分。
> 关联：[competitive-analysis.md](competitive-analysis.md)、
> [feature-gap-design.md](feature-gap-design.md)。

## 1. 观测云的产品主线

从产品页可以归纳出五条主线：

1. **APM**：Trace + 服务拓扑 + 接口耗时/错误/吞吐 + Profiling + 日志关联；
2. **日志**：采集 → Pipeline 解析/脱敏 → 检索/聚类/聚合 → 告警；
3. **告警与事件**：阈值/突变/日志/多数据源检测 → 分级 → 事件中心 → 通知；
4. **异常追踪（Issue）**：告警/错误/日志聚合为 Issue，分派、处理、复盘；
5. **平台能力**：数据洞察（仪表板/查看器/笔记/快照）、权限与脱敏、
   MCP/AI、协作。

它强调的不是「多一个图表」，而是**同一条证据链**：从异常入口出发，
可以连续跳到 Trace、日志、指标、主机、事件和通知。

## 2. 对标矩阵与本项目选择

| 观测云能力 | 对局域网 C++ 的价值 | 本项目状态 | 决策 |
|---|---|---|---|
| Trace / 服务拓扑 | 高：跨进程排障核心 | 已有，支持强弱边 | 保持 |
| 服务性能分析（QPS/错误率/P95） | 高：先判断哪个服务异常 | **本轮补齐**（服务页） | 保持 |
| 代码级 Profiling | 中：需常驻采样，影响生产 | 折叠栈导入 + 可选 SIGPROF（诊断版）+ SVG 条形火焰图 | 内存/锁/IO 与常驻长稳未做 |
| 日志检索 / 聚类 / 关联 | 高：现场日志量大 | 已有 | 保持 |
| Pipeline 解析/脱敏 | 高：敏感信息不能入库 | **本轮补齐**（正则脱敏） | 保持，后续加字段提取 |
| 日志告警 / 阈值 / 突变 | 高：规则引擎已有 | 已有 4 类规则 | 后续加突变检测 |
| 事件中心 | 高：告警需要闭环 | 已有 open/ack/resolved，新增同指纹分组 | 保持 |
| 多渠道通知 | 高：告警要触达 | **本轮补齐 Webhook** | 邮件/syslog 二期 |
| 告警静默/分组 | 高：降低噪音 | **本轮补齐**（静默 + 同指纹分组） | 保持 |
| 异常追踪 Issue | 高：错误聚合与复盘 | **本轮补齐**（聚合/指派/ack/resolve） | 保持 |
| 数据洞察/仪表板/快照 | 中：演示与协作 | **本轮补齐轻量仪表板 + 只读快照** | 笔记/拖拽后续 |
| 可用性拨测（HTTP/TCP/DNS/SSL） | 中高：内网也可用 | **本轮补齐 HTTP 拨测** | TCP/DNS/SSL 二期 |
| RUM / 移动端 / Session | 低：目标是 C++ 后端 | 无 | 非目标 |
| 权限/空间/字段级脱敏 | 中：多团队才需要 | Token 鉴权 + 证据包脱敏 | RBAC 非目标 |
| MCP / AI 上下文 | 高：内部 AI 排障 | 已有只读 MCP | 保持 |
| 协作（快照/笔记） | 中：复盘沉淀 | 证据包可替代一部分 | 暂不做 |
| DQL / PromQL | 中：查询灵活度 | 现有过滤 API | 不自造 DQL |

## 3. 本轮已实现

### 3.1 Pipeline 入库脱敏

- 位置：`server/src/pipeline.*`，在 `/v1/ingest` 写入 Store 前执行；
- 默认规则：password/token/secret、email、IPv4、手机号；
- 自定义规则：配置 `pipeline.rules`；
- 统计：总览 `pipeline_redacted_events`；
- 测试：`server/tests/pipeline_test.cpp`。

### 3.2 事件 Webhook

- 位置：`server/src/notifier.*`，Store 事件 sink 异步入队；
- 有界队列 1000，失败/丢弃计数；
- 配置：`webhook.enabled/url/timeout_ms/token`，或 `--webhook-url`；
- 测试：`server/tests/notifier_test.cpp`（本地 HTTP 服务收包）。

### 3.3 服务性能分析

- `/api/services` 增加 `span_count_5m / error_spans_5m / error_rate_5m /
  qps_5m / p95_ms`；
- Web 服务页展示 QPS、错误率、P95；
- E2E 断言服务统计存在。

### 3.4 异常追踪 Issue

- 位置：`server/src/issues.*`；
- 错误日志模板 + 崩溃指纹派生 Issue，合并持久化的 ack/指派/备注；
- API：`GET /api/issues`、`POST /api/issues/{id}`；
- Web「异常」页支持确认、指派、解决与筛选；
- 测试：`server/tests/issues_test.cpp` + E2E。

### 3.5 告警静默与事件分组

- 位置：`server/src/silence.*` + `Store::add_event`；
- 按规则/类型/服务/节点/级别/时间窗静默，静默不影响已产生事件；
- 同指纹 open 事件 5 分钟窗口内合并，count/last 递增；
- API：`GET/POST/DELETE /api/silences`；
- 测试：`silence_test.cpp` + Store 分组单测 + E2E。

### 3.6 HTTP 可用性拨测

- 位置：`agent/src/probe.*`；
- `--probe name=url[;interval_ms=..;timeout_ms=..;status=..]`；
- 指标 `probe.up / probe.duration_ms / probe.status_code`，失败产生错误日志；
- `/api/probes` 汇总 + 总览展示；
- 测试：`probe_test.cpp` + E2E。

### 3.7 仪表板与快照

- 位置：`server/src/dashboard.*`；
- widget 类型：metric / traces / logs / text；
- 仪表板定义与快照持久化，快照只读；
- API：`/api/dashboards*`、`/api/snapshots*`；
- Web「仪表板」页；
- 测试：`dashboard_test.cpp` + E2E。

### 3.8 客户端与场景

- Python/Web 客户端 trace 接入与 C++ 后端示例；
- 6 类场景 demo 与快速诊断/场景手册。

## 4. 仍未实现的高价值项（建议顺序）

| 优先级 | 能力 | 为什么值得做 | 预估 |
|---|---|---|---|
| P1 | 仪表板拖拽/笔记 | 当前为 JSON 配置 + 只读快照，交互可继续增强 | 3–5 天 |
| P2 | 拨测协议扩展 | 当前 HTTP；TCP/DNS/SSL/WebSocket 待补 | 2–3 天 |
| P2 | 字段提取/类型转换 | Pipeline 从“脱敏”扩展到“结构化” | 2–3 天 |
| P3 | 突变/基线检测 | 比固定阈值更少误报 | 3–5 天 |
| P3 | OTLP 接入 | 兼容其他语言/系统 | 1–2 周 |

> 矩阵中标注“部分/简化”的能力，逐项缺口见
> [competitive-analysis.md 第 6 节](competitive-analysis.md#6-简化实现部分覆盖的具体缺口)，
> 包括 Profiling、eBPF、gRPC streaming、仪表板、拨测、Issue 等。

## 5. 结论

观测云值得借鉴的不是「功能数量」，而是：

1. **从异常出发的连续下钻**：告警 → 事件 → Trace → 日志 → 指标 → 主机；
2. **入库前治理**：Pipeline 让数据可查、可关联、可脱敏；
3. **事件闭环**：告警不只通知，要有状态、负责人和复盘；
4. **服务性能视角**：先看 QPS/错误率/P95，再决定下钻哪条 trace。

本轮把其中最适合当前规模、且不破坏「不影响生产」约束的
**Pipeline 脱敏、Webhook、服务性能分析、Issue、静默/分组、HTTP 拨测、
仪表板/快照**落地；更复杂的拖拽式仪表板、多协议拨测、字段提取、
突变检测与 OTLP 列为下一阶段。这样既不追求大而全，也不会在关键排障路径上缺能力。
