# 常用维测场景手册

> 每个场景都是「一条命令造数据 + 固定观察顺序 + 明确产出」。
> 运行器：[examples/scenarios/demo.py](../examples/scenarios/demo.py)；
> 方法论：[quick-diagnosis.md](quick-diagnosis.md)。

## 0. 公共前提

```bash
# 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 任意场景（--keep 保留栈，Ctrl+C 结束）
python3 examples/scenarios/demo.py --scenario <name> --keep
```

脚本会启动真实 `vmon-server`、`vmon-agent` 和 `examples/http_backend`
（使用真实 SDK 的 C++ HTTP 后端），打印 Web UI 地址、trace_id、
request_id 与 CLI 命令。

| 场景 | 命令 | 重点 |
|---|---|---|
| 跨语言 RPC | `--scenario rpc` | Python 与 C++ 同一条 trace |
| 慢调用 | `--scenario slow` | 最慢 Span、关键路径 |
| 错误链路 | `--scenario error` | 首个错误、错误码、诊断 |
| 日志风暴 | `--scenario logstorm` | 模板聚类、采样/去重 |
| SLO | `--scenario slo` | SLI、错误预算、燃烧率 |
| 崩溃 | `--scenario crash` | SIGSEGV、崩溃指纹 |
| 全部 | `--scenario all` | 依次执行以上场景 |

## 1. 跨语言 RPC：Python / Web → C++ 后端

**目的**：验证上游不是「链路的盲区」，而是 trace 的起点。

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario rpc --keep
```

**预期看到**：

- 同一条 trace 中两个服务：`demo-python`（client span）与 `http-backend`（server span）；
- 后端 span 的 `parent_span_id` 等于 Python span 的 `span_id`；
- 日志 `http backend handled request_id=...` 自动挂在 trace 上。

**观察顺序**：

1. Web → 调用链，搜索打印出的 request_id；
2. 看 Span 树：Python 是根，HTTP/echo 是子节点；
3. 看瀑布图：Python 总耗时包含后端耗时；
4. 看日志：两侧日志按同一个 request_id 收口。

**手工接入**：

- Python：`clients/python/vmon_trace.py`，见 [client-observability.md](client-observability.md)；
- 浏览器：`clients/web/vmon-trace.js`，`instrumentFetch` 自动注入头；
- C++ 后端：`vmon::context_from_traceparent()` + `create_span_with_parent()`。

## 2. 慢调用：先看最慢 Span，再看关键路径

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario slow --keep
```

后端会 sleep 800ms。预期：

- `slowest_span` 指向后端 `HTTP/echo`（或包含它的上游）；
- `critical_path` 显示 `demo-python → http-backend`；
- 瀑布图里后端条占总时长的大部分。

**定位顺序**：

```
最慢 Span → 关键路径 → 该 Span 的日志 → 火焰图（如有）
```

如果最慢 Span 在下游，继续看下游自己的 trace/日志；如果耗时在本地，考虑锁等待、IO、CPU。

## 3. 错误链路：首个错误 + 错误码 + 诊断

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario error --keep
```

后端返回 500 并标记 `E_ECHO`，Python client span 标记 `E_HTTP_500`。预期：

- `summary.has_error=true`；
- `first_error` 带 service / span / error_code；
- `POST /api/diagnose` 返回 `verdict=business_error` 与假设；
- 日志里能看到后端 error 与上游 info 的先后关系。

**CLI**：

```bash
./build/tools/vmonctl --server <url> traces --request <id> --error
./build/tools/vmonctl --server <url> diagnose --request <id>
```

## 4. 日志风暴：模板聚类与降噪

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario logstorm --keep
```

脚本发送 200 条 info + 20 条 error。预期：

- 聚类页出现 `demo tick <num>` 与 `demo storm failure code=<var>`；
- 错误模板保留样本，INFO 按策略采样/去重；
- 总览能看到 `logs_sampled` / `logs_deduped` 计数。

**定位顺序**：Top 模板 → 错误模板 → 指纹下钻 → 代表样本。不要逐条翻日志。

## 5. SLO：错误预算与燃烧率

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario slo --keep
```

脚本先制造一条错误 trace，再创建服务 `demo-python` 的可用性 SLO（target 0.99）。
预期：`status=critical`、`burn_rate=100`、`budget_remaining=0`（样本很少时会放大）。

**注意**：SLO 基于内存窗口；重启后没有数据会显示 `unknown`。

## 6. 崩溃：信号、栈帧、build_id

**造数据**：

```bash
python3 examples/scenarios/demo.py --scenario crash --keep
```

预期：

- 进程退出码 139（SIGSEGV）；
- 崩溃页出现 `e2e-crash` / `SIGSEGV`；
- 详情能看到信号、pid、node、原因；有 debuginfo 时可符号化。

## 7. 异常、静默、拨测与仪表板

这些能力不依赖额外 demo 参数，可直接在 `--keep` 栈上验证：

```bash
# 异常：错误日志聚合为 Issue
curl -s \
  "http://127.0.0.1:<port>/api/issues?limit=10"

# 静默：发布窗口内抑制规则事件
curl -s -H 'Content-Type: application/json' \
  -d '{"name":"发布窗口","service":"demo-python","duration_minutes":30}' \
  "http://127.0.0.1:<port>/api/silences"

# 拨测：Agent 启动时增加 --probe，然后看总览/指标
vmon_agent ... --probe \
  'demo-api=http://127.0.0.1:19090/health;interval_ms=5000'

# 仪表板：创建设置后打开 Web →「仪表板」，可生成只读快照
curl -s -H 'Content-Type: application/json' \
  -d '{"id":"dash-1","name":"值班看板","widgets":[
       {"type":"metric","title":"负载","metric":"host.load1"},
       {"type":"traces","title":"链路"},
       {"type":"logs","title":"错误模板","limit":5}]}' \
  "http://127.0.0.1:<port>/api/dashboards"
```

## 8. 全部场景

```bash
python3 examples/scenarios/demo.py --scenario all --keep
```

适合演示或验收：一条命令依次跑完，最后保留栈，按打印的 Web 地址逐页看。

## 9. 手工造数据（不用 demo 脚本）

```bash
# 启动栈
./scripts/vmon.sh up

# 启动 HTTP 后端
VMON_AGENT_ENDPOINT=.vmon-run/agent.sock \
VMON_CONTROL_DIR=.vmon-run/ctrl \
VMON_CRASH_DIR=.vmon-run/crash \
./build/examples/http_backend/vmon_http_backend --port 19090

# Python 调用
python3 - <<'PY'
import sys; sys.path.insert(0, "clients/python")
from vmon_trace import Client
c = Client("http://127.0.0.1:18428", service="py-demo")
with c.span("Python/Manual", request_id="manual-1") as s:
    print(c.request("POST", "http://127.0.0.1:19090/api/echo",
                    body={"x": 1}, request_id="manual-1"))
c.close()
PY
```

然后搜索 `manual-1`。
