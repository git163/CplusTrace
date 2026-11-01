# 维测能力补齐设计方案（v0.2）

> 日期：2026-11-01
> 状态：设计稿，作为本轮 E2E 扩展、使用手册、Web UI 全量测试与竞品对齐的实施依据。
> 关联文档：[README](../README.md)、[plan_v1.0.md](../plan_v1.0.md)、
> [deployment.md](deployment.md)、[verification.md](verification.md)。

## 1. 背景与目标

M1–M6 已经打通「SDK → Agent → Server → Web/CLI/MCP」的纵向链路，但面向真实
排障仍有四个缺口：

1. 端到端测试只覆盖主链路，查询条件、拓扑、指标、SLO、事件生命周期、规则持久化、
   跨进程父子 Span、UI 依赖接口等缺少断言；
2. 没有面向使用者的最短路径手册，新同学需要读部署文档才能跑起来；
3. Web UI 的每个页面没有逐项做过真实浏览器回归；
4. 与观测云等竞品对照，缺少指标生产者、SLO、拓扑视图、事件闭环、Trace 瀑布图等
   高频维测能力。

本轮目标不是做「大而全平台」，而是把**局域网内最短排障路径**补完整：

> 发现异常 → 看链路/指标 → 定位服务与节点 → 关联日志 → 确认/关闭事件 →
> 需要时导出证据包。

### 1.1 验收口径

- `ctest` 全绿，E2E 场景覆盖下表第 8 节；
- Web UI 14 个页面在真实浏览器（Playwright MCP）中可操作，关键按钮有结果；
- `docs/user-guide.md` 能让使用者在 10 分钟内启动并查询第一条链路；
- `docs/competitive-analysis.md` 明确「已实现 / 本轮补齐 / 非目标」；
- 新增能力默认不改变现有 API 语义，不影响 SDK 业务热路径。

## 2. 现状与差距

| 能力 | 现状 | 差距 | 本轮处理 |
|---|---|---|---|
| 指标 | Store/API/UI 已存在，但无生产者 | 指标页永远为空 | Agent 上报主机指标；进程指标可选 |
| SLO | 无 | 无法量化可用性/延迟目标与错误预算 | 新增 SLO 定义、计算、持久化、API/UI |
| 事件 | 可列表、可 ack | 无 resolve、无筛选 | 增加 resolved 状态、筛选与 UI 操作 |
| 拓扑 | `/api/topology` 已实现 | Web 无入口 | 新增拓扑页面与简单离线 SVG |
| Trace | 树、关键路径已有 | 无时间瀑布图 | 在 Trace 详情中增加瀑布图 |
| 日志 | trace/request/service/level/text/errors | 缺 node/task_id/时间窗 | 补齐过滤条件与 UI |
| 规则 | JSON 编辑 | 使用门槛高 | 增加常用字段表单，保留 JSON 高级编辑 |
| 聚类下钻 | 有竞态 | 结果可能被默认日志查询覆盖 | 已改为路由统一消费 pending fingerprint |
| E2E | 6 类场景 | 覆盖面不足 | 扩展到查询/拓扑/指标/SLO/事件/规则/剖析/UI 接口 |
| 文档 | 部署/验收 | 无使用手册、无竞品对照 | 新增两份文档 |

## 3. 竞品对照与取舍

参考对象：观测云产品页（数据洞察、APM、日志、SLO、告警、事件中心、异常追踪、
智能巡检、Profiling、可用性、指标、统一目录、MCP），以及 Datadog、SigNoz、
Grafana LGTM、夜莺的通用能力。

| 竞品能力 | 本项目定位 | 本轮 | 说明 |
|---|---|---|---|
| 指标采集与趋势 | 采纳 | **补齐** | 主机负载/内存/进程数/uptime；进程 RSS 可选 |
| APM Trace / 服务拓扑 | 核心 | **补齐 UI** | 已有数据结构与 API，补用户入口 |
| 慢调用 / 错误率 / 关键路径 | 核心 | 已有 | 总览 + Trace 详情 |
| 日志检索 / 模板聚类 | 核心 | **增强** | 补 node/task_id/时间窗；修下钻竞态 |
| SLO / 错误预算 | 采纳 | **补齐** | 可用性、延迟两类 SLI |
| 告警规则 / 事件中心 | 采纳 | **增强** | 事件 resolve、筛选、规则表单 |
| 智能巡检 | 简化采纳 | 已有 | 固定只读检查项 |
| Profiling 火焰图 | 采纳数据面 | 已有 | 折叠栈导入与火焰图；不做常驻采样 |
| 可用性拨测（HTTP/TCP） | 二期 | 不做 | 需要外网/目标依赖，当前先做内网诊断 |
| 统一目录 / Owner | 简化采纳 | 部分 | 已有服务/实例/节点；不做 YAML 资产平台 |
| MCP / AI 查询 | 内部版 | **增强** | 增加 trace/topology/SLO 只读工具 |
| RUM / Session Replay / 移动端 | 非目标 | 不做 | 局域网 C++ 后端系统不需要 |
| K8s / 多云 / 云成本 / SIEM | 非目标 | 不做 | 与离线私有部署场景无关 |
| 通知渠道（Webhook/邮件） | 二期 | 不做 | 事件中心先闭环，通知后续接 |

设计原则：

1. **同一事件模型**：指标仍走 `type=metric`，复用 WAL、鉴权、批量上传；
2. **有界资源**：主机指标 9 条/次，进程指标默认关闭；SLO 数量与事件数量有上限；
3. **离线可用**：拓扑图用内联 SVG，不引入前端框架或 CDN；
4. **不碰热路径**：所有新增采集在 Agent 扫描循环，SDK 业务路径不变。

## 4. 总体设计

```
SDK span/log/heartbeat ─┐
Agent 进程/主机扫描 ────┼─> WAL ─> /v1/ingest ─> Store ─┬─> /api/traces
Agent 指标构造 ─────────┘                              ├─> /api/logs
                                                       ├─> /api/metrics
                                                       ├─> /api/topology
                                                       ├─> /api/events
SLO 定义(slos.json) ──> SloManager ──> /api/slos ───────┘
规则(rules.json) ────> RuleEngine ──> 事件中心
```

新增模块：

| 模块 | 文件 | 职责 |
|---|---|---|
| Agent 指标构造 | `agent/src/metrics.{h,cpp}` | HostInfo/ProcessInfo → `type=metric` 事件 |
| SLO 管理 | `server/src/slo.{h,cpp}` | 定义校验、持久化、SLI/错误预算计算 |
| SLO API | `server/src/api.cpp` | `/api/slos` 增删查 |
| 事件生命周期 | `server/src/store.{h,cpp}` | ack / resolve / 筛选 |
| Web 拓扑/瀑布图/SLO | `web/index.html`、`web/app.js` | 无框架渲染 |
| E2E 扩展 | `tests/e2e/*` | 跨进程 trace、指标、SLO、事件、规则等 |

## 5. 详细设计

### 5.1 Agent 主机指标

每次进程/主机扫描（默认 2s）追加以下序列：

| 指标名 | 单位 | 说明 |
|---|---|---|
| `host.load1` / `host.load5` / `host.load15` | 1 | 系统负载 |
| `host.mem_total_bytes` | byte | 物理内存总量 |
| `host.mem_available_bytes` | byte | 可用内存 |
| `host.mem_used_bytes` | byte | total - available |
| `host.mem_used_ratio` | 0..1 | 使用率 |
| `host.process_count` | 个 | 匹配 `--process-filter` 的进程数 |
| `host.uptime_seconds` | s | 主机 uptime |

事件字段：`type=metric`、`node`、`name`、`attrs.value`、`attrs.agent`。
Store 会把除 `value` 外的 attrs 作为标签，并补 `node`。

进程指标（`--process-metrics`，默认关闭）：`process.rss_bytes`，标签
`process`、`pid`、`threads`。**不设置 `service`**：进程名（如 `/bin/zsh`）不是
服务名，写入服务字段会污染服务目录；进程身份只放在标签与 `pid` 字段。默认关闭的
原因：50–200 进程 × 2s 扫描会快速放大序列数和 UI 渲染量。

### 5.2 SLO

#### 数据模型

```json
{
  "id": "order-availability",
  "name": "订单服务可用性",
  "service": "order-service",
  "type": "availability",
  "target": 0.99,
  "window_seconds": 3600,
  "latency_threshold_ns": 0,
  "description": "核心下单接口"
}
```

- `type=availability`：窗口内该服务的 trace 中，无错误的比例；
- `type=latency`：窗口内该服务的 trace 中，`duration <= latency_threshold_ns`
  的比例；
- `service` 为空表示全部服务；
- 校验：`id` 仅允许 `[A-Za-z0-9._-]`，`0 < target < 1`，`window_seconds >= 60`，
  latency 必须提供 `latency_threshold_ns > 0`。

#### 计算

```
total   = 窗口内匹配的 trace 数
bad     = availability ? has_error 的 trace 数
                     : duration > threshold 的 trace 数
sli     = total == 0 ? 0 : (total - bad) / total
budget  = 1 - target
consumed= total == 0 ? 0 : (bad / total) / budget
remaining = max(0, 1 - consumed)
burn    = consumed
status  = total == 0 ? unknown : burn > 2 ? critical : burn > 1 ? warning : healthy
```

返回结构：

```json
{
  "id": "...", "name": "...", "service": "...", "type": "availability",
  "target": 0.99, "window_seconds": 3600,
  "status": "healthy", "total": 120, "bad": 1, "sli": 0.9917,
  "error_budget": 0.01, "budget_consumed": 0.8333,
  "budget_remaining": 0.1667, "burn_rate": 0.8333,
  "window_start_ns": 0, "window_end_ns": 0
}
```

#### 持久化与 API

- `state_dir/slos.json`，写入临时文件后 `rename`，避免半写；
- `GET /api/slos`：返回定义 + 实时状态；
- `POST /api/slos`：按 `id` 新增或覆盖，保存后立即返回状态；
- `DELETE /api/slos/{id}`；
- MCP 只读工具 `list_slos`。

### 5.3 事件生命周期与筛选

状态机：`open → ack → resolved`，`open` 也可直接 `resolved`。重复操作幂等。

- `POST /api/events/{id}/ack`（已有）；
- `POST /api/events/{id}/resolve`（新增）；
- `GET /api/events?status=&severity=&service=&type=&limit=`（增强）；
- Web 事件页增加状态下拉、级别/服务输入、确认/解决按钮。

### 5.4 服务拓扑 UI

数据源：`GET /api/topology`（已有）。页面包含：

1. 边表格：caller、callee、method、count、error_count、avg、max、confidence；
2. 简化 SVG：按调用关系分层布局，节点显示服务名，边宽表示调用量，红色表示错误；
3. `weak` 边用虚线，提示「可能来自推断，不进入 Span 树结论」。

### 5.5 Trace 瀑布图

在 Trace 详情中，把 Span 树拍平为行：

- 横轴：相对 trace 起点的偏移 → span 结束；
- 行内显示服务/方法、耗时、错误标记；
- 窗口宽度用百分比，不需要外部图表库；
- 与 Span 树、关键路径、关联日志并列。

### 5.6 日志过滤增强

新增 `node`、`task_id`、时间窗（15m/1h/6h/全部）过滤；`fingerprint` 下钻后显式
展示当前指纹并提供「清除指纹过滤」。所有条件都落到已有 `LogQuery`，不改存储结构。

### 5.7 规则表单

常用规则支持表单：`id/name/type/service/node/metric/op/threshold/window_ns/
cooldown_ns/severity/enabled`；保留「高级 JSON」作为兜底。表单提交仍走
`POST /api/rules`，保存后写 `rules.json`。

## 6. 测试设计

### 6.1 E2E 场景矩阵

| # | 场景 | 关键断言 |
|---|---|---|
| 1 | 健康检查与鉴权 | `/api/health` 200；无 token 401；静态 `index.html`/`app.js` 200 |
| 2 | 主链路 | request_id 可查 trace + 日志；span/depth/关键路径/首个错误 |
| 3 | 查询条件 | trace_id、request_id、task_id、service、error、min_duration；日志 trace/request/task/service/node/level/text/errors_only |
| 4 | 跨进程链路 | 父进程导出 trace/span，子进程用 `create_span_with_parent`；同一 trace 含两个服务；拓扑出现父子边 |
| 5 | 指标 | `/api/metrics` 出现 `host.load1`、`host.mem_used_ratio` 等；`name` 过滤有效 |
| 6 | 服务/节点/进程 | 服务列表包含测试服务；节点包含进程快照 |
| 7 | 事件 | 规则触发或 `/v1/events` 注入；ack → resolved；筛选生效 |
| 8 | 规则 | 新增规则、列表可见、删除、重启 Server 后持久化 |
| 9 | SLO | 创建可用性/延迟 SLO；有数据时 SLI/预算/状态合理；删除；重启后仍在 |
| 10 | 诊断/巡检 | 按 request_id 返回 checks；`/api/inspection` 摘要完整 |
| 11 | 剖析 | 导入折叠栈后可查 top self / 火焰图 |
| 12 | 证据包 | 校验和、脱敏（IP/邮箱/password/token） |
| 13 | 崩溃 | SIGSEGV 捕获、事件联动、符号化接口错误路径 |
| 14 | MCP | 工具列表包含 trace/topology/SLO；调用返回结构正确 |
| 15 | 断网补传 | 停止 Server → 继续上报 → 重启补传 |

### 6.2 UI 测试

用 Playwright MCP 逐页操作，记录在 `docs/ui-test-report.md`：

- 总览：刷新、自动刷新、巡检；
- 调用链：列表、错误过滤、最小耗时、详情、瀑布图；
- 日志：各过滤器、错误开关、聚类下钻与清除；
- 服务/节点/事件/崩溃/剖析/指标/诊断/规则/拓扑/SLO；
- 令牌保存与 401 提示；
- 所有按钮点击后无 JS 未捕获异常。

### 6.3 单元测试

- Agent：主机指标字段/单位/边界、进程指标标签；
- Server：SLO 校验/计算/持久化、事件 resolve 与筛选、查询条件；
- 原有 SDK/Server/Agent 用例保持通过。

## 7. 兼容性与资源边界

- 全部新增代码保持 C++14；不引入新三方库；
- 指标序列上限沿用 Store：5000 series、每序列 2000 点；
- SLO 上限 200 条；事件沿用 Store 的 5000 条上限；
- 新增 API 均为增量；旧调用行为不变；
- UI 仍为静态资源，无构建步骤、无 CDN、无 Node 运行时。

## 8. 实施阶段

| 阶段 | 内容 | 产出 |
|---|---|---|
| P0 | 修复聚类竞态；Agent 主机指标 + 单测 | 指标页有数据；下钻稳定 |
| P1 | SLO 后端/API/UI；事件 resolve/filter；拓扑/瀑布图/日志过滤/规则表单 | 功能可用 |
| P2 | E2E 扩展到 15 类场景；跨进程父子 Span | `ctest` 全绿 |
| P3 | 使用手册、竞品分析、UI 测试报告 | 文档交付 |
| P4 | 真实浏览器逐页回归；修复发现的问题 | 验收记录更新 |

## 9. 非目标

- 不做公网 SaaS、多租户、权限角色体系；
- 不做 RUM、Session Replay、移动端、云成本、K8s 资产、SIEM；
- 不实现常驻 CPU `SIGPROF` 采样、eBPF、gRPC streaming 拦截；
- 不实现通知渠道（Webhook/邮件/syslog）、拨测、DQL 查询语言；
- 不引入 ClickHouse/VictoriaMetrics 等外部存储，当前内存 Store 面向试点规模。

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 指标基数膨胀 | 默认只开主机指标；进程指标显式开关；序列上限 |
| SLO 计算依赖内存 trace 保留窗口 | 文档注明「SLO 基于当前内存窗口」；后续接长期存储 |
| 拓扑图布局在环/多根时混乱 | 按依赖层级排序 + 表格兜底 |
| E2E 跨进程时序不稳定 | 父进程显式 hold；轮询等待 span 到达；失败 dump 日志 |
| UI 回归依赖浏览器 | Playwright 不可用时以 API 冒烟为主，报告注明环境 |
