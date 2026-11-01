# vmon 使用手册

> 面向第一次接触本系统的人。目标：**10 分钟内启动、查清一条请求、定位一个错误**。
> 部署、权限、systemd、core_pattern 等运维细节见 [deployment.md](deployment.md)。
>
> 想直接看「怎么快速定位」：读 [quick-diagnosis.md](quick-diagnosis.md)。
> 想按场景跑 demo：读 [scenario-cookbook.md](scenario-cookbook.md)。

## 1. 三分钟跑起来（本机演示）

前提：已按 [README](../README.md) 构建出 `build/`。

**日常最常用的是本地栈脚本**（启动、停止、查看状态都只需要一条命令）：

```bash
./scripts/vmon.sh up        # 启动 Server + Agent
./scripts/vmon.sh status    # 看进程、健康检查、日志路径
./scripts/vmon.sh logs agent -f
./scripts/vmon.sh down      # 停止
```

`up` 成功后会打印 Web UI 地址（默认 `http://127.0.0.1:18428/`）和令牌
（默认 `dev-token`）。想改端口或目录：

```bash
PORT=19428 VMON_TOKEN=mytoken VMON_HOME=/tmp/vmon \
  ./scripts/vmon.sh up
```

只想看“一条完整请求长什么样”，用一条命令的演示脚本：

```bash
./examples/hello/run_demo.sh
```

想看跨语言、慢调用、错误、日志风暴、SLO、崩溃等常用场景：

```bash
python3 examples/scenarios/demo.py --scenario all --keep
```

脚本会启动真实 Server/Agent/HTTP 后端并造数据，打印 trace_id、request_id
和 Web/CLI 查看方式。场景说明见 [scenario-cookbook.md](scenario-cookbook.md)。

脚本自动完成：启动 Server → 启动 Agent → 运行示例业务进程 → 按 `request_id`
查询并打印 trace 与日志 → 清理。

想自己打开 Web UI 看看：

```bash
DEMO_KEEP=1 ./examples/hello/run_demo.sh
# 按脚本打印的地址打开，例如 http://127.0.0.1:18429/
# 令牌就是脚本使用的 token（默认 demo-token）
```

> Web UI 左上角的「令牌」输入框必须与服务端 `--token` 一致，否则所有查询都会提示
> 「未授权」。令牌只存在浏览器 localStorage，不上传。

## 2. 真实环境怎么部署

| 角色 | 做什么 | 一条命令（示意） |
|---|---|---|
| 中心 | 一台机器跑 `vmon_server` | `vmon_server --port 18428 --token <token> --state-dir /var/lib/vmon/server --web-dir /opt/vmon/web --no-forward` |
| 节点 | 每台业务机跑一个 `vmon_agent` | `vmon_agent --server-url http://<中心>:18428 --uds /run/vmon/agent.sock --state-dir /var/lib/vmon/agent --node <节点名> --token <token>` |
| 业务进程 | 链接 SDK，指向 Agent 的 UDS | 见 [README 第 4 节](../README.md#4-业务接入) |

Agent 与业务进程不在同一用户时，把双方加入同一个组，并保证 UDS 目录 `0770`、
socket `0660`。完整清单见 [deployment.md](deployment.md)。

## 3. 第一次排障：从一个错误开始

### 3.1 打开总览，先看「有没有事」

`http://<中心>:<端口>/` → 总览：

- KPI：节点数、在线 Agent、服务/实例、近 5 分钟 QPS、错误率、P95/P99；
- 最近慢调用、错误日志模板 Top 5、最近事件；
- 「执行巡检」给出 `ok / warn / fail` 清单（节点、错误率、慢调用、错误日志、
  崩溃、时钟偏差、磁盘）。

巡检出现 `fail` 或错误率升高时，按下面的顺序下钻。

### 3.2 用调用链接管一条请求

进入「调用链」：

1. 搜索框输入任意一项：`trace_id`、`request_id`、`task_id`、服务名、Span 名、
   错误码，甚至 `error`、`50ms` 这类关键词。搜索是**忽略大小写的子串匹配**。
2. 勾选「仅错误」、填写最小耗时，可以快速收敛。
3. 点击一条 trace，右侧展示：
   - **Span 树**：父子层级、每个 Span 的服务/实例/节点/耗时/状态；
   - **瀑布图**：以 trace 起点为基准的时间条，未结束的 Span 标注「进行中」；
   - **关键路径**：最慢的一条链路；
   - **首个错误**：第一个 `error` 状态的 Span 及错误码；
   - **关联日志**：自动带上该 trace 的日志，点击可继续看上下文。

> 跨进程链路：父进程把 `trace_id`/`span_id` 通过 `vmon::SpanContext` 传给子进程，
> 子进程用 `create_span_with_parent` 续接，就能在同一个 trace 里看到两个服务。

### 3.3 看日志，但不要被日志淹没

「日志」页支持：

| 过滤 | 用途 |
|---|---|
| trace / request / task | 按一次请求或任务收口 |
| service / node | 按服务或机器收口 |
| level / 仅错误 | 只看 error / warn |
| 文本 | 关键词搜索（错误码、订单号等） |
| 时间窗 | 15 分钟 / 1 小时 / 6 小时 / 全部 |

「日志聚类」把这些日志按模板聚合（`<var>` 表示变化部分）：

- 先看哪个模板次数最多、哪个模板带 error；
- 点指纹下钻，只看这一类日志；
- 点「清除指纹」回到全量。

### 3.4 确认影响面：服务拓扑与节点

- 「服务拓扑」：谁调用谁、方法名、调用量、错误数、平均/最大耗时；
  虚线表示推断出的弱边，不进入 Span 树结论；
- 「服务」：服务 → 实例 → 版本；
- 「节点」：OS/内核/架构/内存，以及进程快照（pid、RSS、线程数、命令行）。

### 3.5 需要更深的证据时

「诊断」页四块能力：

1. **诊断**：填 `trace_id` / `request_id` / 服务 / 节点 / pid，勾选「需要全线程栈」，
   返回结构化 JSON：匹配的 trace、首个错误、相关日志、可疑假设、检查项、结论。
2. **证据包**：填事件名（如 `INC-2026-001`）和目标 trace/request，生成一个目录：
   `manifest.json`、`timeline.json`、`request.json`、`metrics.json`、`logs.jsonl`、
   `profiles/`、`dumps/`、`index.jsonl`、`hypotheses.md`。IP、邮箱、token/password、
   home 路径默认脱敏，`manifest.json` 带每个文件的 SHA-256。
3. **动态日志级别**：按服务/节点/pid 临时开 `debug`，带 TTL（默认 5 分钟），
   到期自动恢复；命令记录可看到 `running → done` 与执行结果。
4. **命令记录**：所有下发命令的状态与结果，便于审计。

### 3.6 事件闭环

「事件」页：

- 按状态（open/ack/resolved）、级别、类型、服务筛选；
- 「确认」表示已知晓，「解决」表示处理完毕；
- 规则命中的告警、崩溃、平台事件都会进入这里。

「规则」页支持表单新建常用规则（心跳缺失、错误日志突增、慢 trace、指标阈值），
也保留高级 JSON 编辑。默认规则不可删除，自定义规则可删除。

### 3.7 SLO 与错误预算

「SLO」页：

- 可用性型：窗口内该服务无错误 trace 的比例；
- 延迟型：窗口内耗时不超过阈值的 trace 比例；
- 展示 SLI、错误预算剩余、燃烧率、健康状态；
- 状态含义：`healthy` / `warning`（燃烧率 > 1）/ `critical`（> 2）/
  `unknown`（窗口内没有数据）。

> SLO 基于中心当前内存窗口内的 trace 计算；重启会清空 trace，因此重启后可能
> 短暂显示 `unknown`，有新请求后恢复。

### 3.8 Python / Web 客户端

前端或 Python 服务调用后端时，不要让它们成为链路盲区：

- Python：`clients/python/vmon_trace.py`，`client.span()` + `client.request()`；
- 浏览器：`clients/web/vmon-trace.js`，`instrumentFetch()` 自动注入 `traceparent`；
- C++ 后端：`vmon::context_from_traceparent()` + `create_span_with_parent()` 续接。

```python
from vmon_trace import Client

client = Client("http://127.0.0.1:18428", "dev-token",
                service="py-bff", node="node-1")
with client.span("GET /orders", request_id="REQ-1"):
    client.request("GET", "http://backend:9000/api/orders",
                   request_id="REQ-1")
client.close()
```

浏览器不要直连中心写接口；由 BFF/网关统一鉴权、注入上下文、采样上报。
完整说明见 [client-observability.md](client-observability.md)。

### 3.9 入库脱敏与告警通知

**入库脱敏（Pipeline）**：默认关闭；开启后 password/token/email/IP/手机号在进入
Store 前被替换，日志、trace 属性、证据包都不会出现原文。

```bash
# 快捷开关：使用内置规则
vmon_server ... --redact-sensitive

# 或配置文件里自定义规则
"pipeline": {
  "enabled": true,
  "use_defaults": true,
  "rules": [
    {"name": "order", "pattern": "order-[0-9]+", "replace": "order-<id>"}
  ]
}
```

总览里能看到 `pipeline_redacted_events` 计数。

**告警 Webhook**：事件中心新增事件时异步 POST 到内网 Webhook（飞书/钉钉/企微
机器人或自建网关），队列有界，失败只计数不阻塞规则。

```bash
vmon_server ... --webhook-url http://127.0.0.1:9000/vmon/events \
                --webhook-token optional
```

请求体：

```json
{"source": "vmon", "event": {"id": "...", "type": "alert",
 "severity": "critical", "title": "...", "service": "...", "status": "open"}}
```

总览里能看到 `webhook_sent / webhook_failed / webhook_dropped`。

## 4. 常用查询速查

### HTTP

```bash
TOKEN=<token>
BASE=http://<中心>:18428

# 调用链：统一搜索
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/traces?q=REQ-123&limit=20"

# 仅错误 + 最慢（耗时单位纳秒）
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/traces?error=1&min_duration_ns=500000000"

# 一条 trace / 一次请求
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/traces/<trace_id>"
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/requests/<request_id>"

# 日志
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/logs?request_id=R1&errors_only=1&limit=50"

# 指标
curl -s -H "X-Vmon-Token: $TOKEN" "$BASE/api/metrics?name=host.load1&limit=200"
```

### CLI

```bash
vmonctl --server "$BASE" --token "$TOKEN" overview
vmonctl --server "$BASE" --token "$TOKEN" traces --request REQ-123 --error
vmonctl --server "$BASE" --token "$TOKEN" trace <trace_id>
vmonctl --server "$BASE" --token "$TOKEN" logs --text timeout
vmonctl --server "$BASE" --token "$TOKEN" inspect
vmonctl --server "$BASE" --token "$TOKEN" diagnose --request REQ-123
vmonctl --server "$BASE" --token "$TOKEN" evidence --incident INC-001 --trace <trace_id>
vmonctl --server "$BASE" --token "$TOKEN" log-level --service order --level debug --ttl 60000
```

### MCP（内部版，只读）

```json
{
  "mcpServers": {
    "vmon": {
      "command": "/opt/vmon/bin/vmon_mcp",
      "args": ["--server", "http://127.0.0.1:18428", "--token", "<token>"]
    }
  }
}
```

工具包括 `query_traces`（支持 `q` 统一搜索）、`get_trace`、`get_request_chain`、
`query_logs`、`list_log_clusters`、`get_topology`、`list_slos`、`get_process_snapshot`、
`run_diagnosis`、`create_bundle` 等。对外发行版应设置 `"enable_mcp": false`。

## 5. 常见问题

| 现象 | 原因与处理 |
|---|---|
| 页面全是「未授权」 | 左上角令牌与服务端 `--token` 不一致；重新填写并保存 |
| 调用链列表为空 | 业务进程未初始化 SDK、Agent 未启动或 UDS 路径不一致；先看「节点」是否有该 Agent、Agent 日志是否连接成功 |
| 有 trace 但没有日志 | 日志未带 `trace_id`/`request_id`；SDK 的 `VMON_INFO` 会自动注入当前 span 上下文，确认日志宏走的是 vmon |
| 指标页为空 | Agent 未开启扫描或还没到上传周期（默认 2 秒扫描、200ms 上传）；主机指标默认开启，进程指标需 `--process-metrics` |
| 服务列表出现奇怪名字 | 旧版本会把进程名当服务名；升级到当前版本后重启 Agent/Server，历史内存数据随之清空 |
| 崩溃只有地址没有函数名 | 未保留 debuginfo 或未安装 `addr2line`/`llvm-symbolizer`；构建时保留 `-g` 与独立 debuginfo |
| SLO 显示 `unknown` | 窗口内没有匹配 trace；确认服务名、时间窗，并产生新请求 |
| 证据包很大 | 按故障窗口裁剪后再导出；指标文件通常最大，可缩小时间窗 |

## 6. 数据边界

- 中心 Store 为内存有界存储（trace、日志、指标、事件均有上限），重启清空；
- Agent 本地 WAL 断网续传，容量上限与丢弃策略可配置；
- 证据包与 MCP 属于内部能力，不进入对外发行包；
- 诊断只执行固定白名单只读命令，不拼接 shell。
