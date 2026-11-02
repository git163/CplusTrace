# 快速诊断与定位手册

> 目标：把「多人多机翻日志」变成「一个人、几分钟、按一条请求定位」。
> 关联：[user-guide.md](user-guide.md)、[scenario-cookbook.md](scenario-cookbook.md)、
> [client-observability.md](client-observability.md)。

## 1. 三步法

```
① 收口：拿到 request_id / trace_id / task_id / 服务 + 时间窗
② 定位：看首个错误、最慢 Span、关键路径、拓扑边
③ 证据：关联日志、模板指纹、诊断结论、证据包
```

### 第一步：收口

不要从「所有日志」开始，先确定这次问题对应哪一条请求：

| 手上有什么 | 用什么查 |
|---|---|
| 业务请求号 | `q=<request_id>` 或 `GET /api/requests/{id}` |
| trace_id | `GET /api/traces/{trace_id}` |
| 任务号 | `GET /api/traces?task_id=...` |
| 只知道服务/时间 | `GET /api/traces?service=...&error=1` + 时间窗 |
| 只知道错误码 | `q=<错误码>`，或日志 `text=<错误码>` |

```bash
BASE=http://127.0.0.1:18428
curl -s "$BASE/api/traces?q=REQ-2026-001"
curl -s "$BASE/api/requests/REQ-2026-001"
```

### 第二步：定位

打开 trace 后按固定顺序看四样东西：

1. **首个错误**：`first_error` 的 service / span / error_code；
2. **最慢 Span**：`slowest_span`；
3. **关键路径**：根到最慢叶子的完整链路；
4. **拓扑边**：错误是否集中在一个下游（强边 vs 弱边）。

```bash
curl -s "$BASE/api/traces/<trace_id>" \
  | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d["summary"]); print(d.get("first_error")); print(d.get("slowest_span"))'
```

### 第三步：证据

- **日志**：按 trace_id 自动关联，不需要手工对齐时间；
- **聚类**：同模板聚合，先看次数最多的错误模板；
- **诊断**：`POST /api/diagnose` 返回检查项、假设与结论；
- **证据包**：脱敏 + SHA-256，可交给同事或内部 AI 复核。

```bash
curl -s -H 'Content-Type: application/json' \
  -d '{"request_id":"REQ-2026-001"}' "$BASE/api/diagnose"
curl -s -H 'Content-Type: application/json' \
  -d '{"incident":"INC-001","request_id":"REQ-2026-001","window_ns":3600000000000}' \
  "$BASE/api/evidence"
```

## 2. 常见问题：先看哪里

| 现象 | 第一眼 | 第二眼 | 产出 |
|---|---|---|---|
| 请求失败 | `first_error.error_code` | 同 trace 日志 + 错误模板 | 失败环节与错误码 |
| 请求变慢 | `slowest_span` | 关键路径、火焰图 | 慢在服务/方法/函数 |
| 发布后错误突增 | 服务 + 时间窗 + 错误模板 | 拓扑边、事件中心 | 是否由本次变更引入 |
| 进程崩溃 | 崩溃列表 | 信号、栈帧、build_id | 崩溃指纹与可疑版本 |
| 日志风暴 | 聚类 Top 模板 | 指纹下钻、采样/去重计数 | 噪声源与保留样本 |
| 下游超时 | 错误 Span 的 `peer` | 对端节点状态、重试次数 | 超时发生在哪一跳 |
| 多团队重复错误 | 「异常」页 | 次数、服务、样本、关联 trace | 聚合 Issue 并指派 |
| 告警太吵 | 规则 → 静默 | 静默窗口、事件分组 | 降低重复告警 |
| 接口不可用 | 总览 → 可用性拨测 | `probe.up/duration/status` | 是否可达与哪一步失败 |
| 多线程/长空档 | Trace → 线程视图 | 错误传播链、进程/节点指标、全线程栈 | 锁等待/IO/调度阻塞点 |
| 同一错误码反复出现 | 错误码页 | 涉及服务、传播路径、样本 trace | 受影响链路与根服务 |

## 3. 优势对比（同一件事，路径更短）

| 环节 | 传统做法 | vmon |
|---|---|---|
| 找到请求 | 多台机器 grep 日志、按时间猜 | 输入 request_id / trace_id |
| 还原链路 | 人工拼多进程日志 | 自动 Span 树 + 关键路径 |
| 找根因 | 逐条看错误日志 | 首个错误 + 错误码 + 诊断假设 |
| 看影响面 | 问人、翻拓扑文档 | 服务拓扑 + 强弱边 |
| 复盘 | 截图、口头描述 | 脱敏证据包 + 校验和 |
| 日志量 | 全量收集、越存越多 | 错误全留 + 采样/去重/聚类 |

三个关键设计让路径更短：

1. **错误优先**：错误与慢请求 100% 保留，INFO 才降噪；
2. **强弱边分离**：显式传播进 Span 树，推断边只进拓扑，不误导根因；
3. **证据可带走**：证据包自包含，现场数据不出内网。

## 4. 一键场景演示

不想手工造数据时，直接运行：

```bash
python3 examples/scenarios/demo.py --scenario rpc --keep
python3 examples/scenarios/demo.py --scenario error --keep
python3 examples/scenarios/demo.py --scenario slow --keep
python3 examples/scenarios/demo.py --scenario logstorm --keep
python3 examples/scenarios/demo.py --scenario slo --keep
python3 examples/scenarios/demo.py --scenario crash --keep
python3 examples/scenarios/demo.py --scenario all --keep
```

脚本会启动真实 Server/Agent/HTTP 后端、造出对应场景，并打印 trace_id、
`request_id`、看点和 CLI 命令。`--keep` 保留栈，便于打开 Web UI。

详细场景说明见 [scenario-cookbook.md](scenario-cookbook.md)。

## 5. 使用边界

- SLO 基于中心当前内存窗口的 trace，重启后短暂 `unknown`；
- 证据包与 MCP 属于内部能力，对外发行版应关闭；
- 前端/Python 接入遵循「传播优先」：后端仍是业务真相源；
- 诊断只执行白名单只读命令，写操作需要显式确认与审计。
