# scenarios：常用维测场景一键演示

> 目标：一条命令启动真实 Server + Agent + C++ HTTP 后端，并制造对应场景。
> 更完整的排障方法见 [快速诊断手册](../../docs/quick-diagnosis.md)，
> 场景说明见 [常用场景手册](../../docs/scenario-cookbook.md)。

## 1. 快速开始

```bash
# 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 单个场景（--keep 保留栈，Ctrl+C 结束）
python3 examples/scenarios/demo.py --scenario rpc --keep
python3 examples/scenarios/demo.py --scenario slow --keep
python3 examples/scenarios/demo.py --scenario error --keep
python3 examples/scenarios/demo.py --scenario logstorm --keep
python3 examples/scenarios/demo.py --scenario slo --keep
python3 examples/scenarios/demo.py --scenario crash --keep

# 全部场景
python3 examples/scenarios/demo.py --scenario all --keep
```

脚本会自动：

1. 选择空闲端口，启动 `vmon_server`、`vmon_agent`；
2. 启动 `vmon_http_backend`（真实 SDK 的 C++ HTTP 后端）；
3. 用 Python 客户端（`clients/python/vmon_trace.py`）造请求；
4. 轮询 Server API，打印 trace、日志、诊断结果；
5. `--keep` 时保留进程与临时目录，打印 Web UI 地址。

## 2. 场景清单

| 场景 | 命令 | 重点 | 预期 |
|---|---|---|---|
| 跨语言 RPC | `--scenario rpc` | Python 与 C++ 同一条 trace | Python span 是根，`HTTP/echo` 是子 span |
| 慢调用 | `--scenario slow` | 最慢 Span、关键路径 | 后端 `HTTP/echo` 约 800ms |
| 错误链路 | `--scenario error` | 首个错误、错误码、诊断 | `E_ECHO` / `E_HTTP_500`，verdict=business_error |
| 日志风暴 | `--scenario logstorm` | 模板聚类、采样/去重 | Top 模板收敛，错误样本保留 |
| SLO | `--scenario slo` | SLI、错误预算、燃烧率 | 目标 0.99，出现 critical/燃烧 |
| 崩溃 | `--scenario crash` | SIGSEGV、崩溃指纹 | 退出码 139，崩溃页出现记录 |
| 全部 | `--scenario all` | 依次执行以上场景 | 一条命令完成验收 |

## 3. 每个场景看什么

### 3.1 `rpc`：跨语言链路

- Python 客户端创建 `Python/CallBackend` span；
- 请求头带 `traceparent`，C++ 后端续接为 `HTTP/echo`；
- 两侧日志按同一个 `request_id` 收口。

```bash
python3 examples/scenarios/demo.py --scenario rpc --keep
```

### 3.2 `slow`：慢调用

- 后端通过 `x-vmon-sleep-ms: 800` 模拟慢响应；
- `slowest_span` 指向 `HTTP/echo`；
- 关键路径显示 Python → 后端。

```bash
python3 examples/scenarios/demo.py --scenario slow --keep
```

### 3.3 `error`：错误链路

- 后端返回 500 并标记 `E_ECHO`；
- Python client span 标记 `E_HTTP_500`；
- `POST /api/diagnose` 返回 `verdict=business_error` 与假设。

```bash
python3 examples/scenarios/demo.py --scenario error --keep
```

### 3.4 `logstorm`：日志风暴

- 发送 200 条 INFO + 20 条 ERROR；
- 聚类页出现 `demo tick <num>` 与 `demo storm failure code=<var>`；
- 总览显示 `logs_sampled` / `logs_deduped`。

```bash
python3 examples/scenarios/demo.py --scenario logstorm --keep
```

### 3.5 `slo`：错误预算

- 先制造一条错误 trace，再创建 `demo-python` 的可用性 SLO；
- 目标 0.99，样本很少时会放大燃烧率。

```bash
python3 examples/scenarios/demo.py --scenario slo --keep
```

### 3.6 `crash`：进程崩溃

- 启动 `vmon_e2e_client --mode crash` 触发 SIGSEGV；
- 进程退出码 139；
- Agent 收集崩溃报告，Server 聚合指纹。

```bash
python3 examples/scenarios/demo.py --scenario crash --keep
```

## 4. 手工查询

`--keep` 时脚本会打印 `BASE=http://127.0.0.1:<port>` 与代表 `request_id`：

```bash
BASE=http://127.0.0.1:<port>
RID=<request_id>

curl -s "$BASE/api/requests/$RID" | python3 -m json.tool | head -60
curl -s "$BASE/api/logs?request_id=$RID&limit=20" | python3 -m json.tool
curl -s -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"$RID\"}" "$BASE/api/diagnose" | python3 -m json.tool
```

或打开 `http://127.0.0.1:<port>/`，在调用链页搜索 `request_id`。

## 5. 依赖

- 已构建 `build/`（`vmon_server`、`vmon_agent`、
  `examples/http_backend/vmon_http_backend`、`tests/vmon_e2e_client`）；
- Python 3 标准库；
- `clients/python/vmon_trace.py`（仓库内，无需安装）。

脚本按以下顺序查找二进制：`build/` → `build/examples/http_backend/` →
`build/tests/` → `build/tools/`。

## 6. 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `--scenario` | `all` | 场景名或 `all` |
| `--keep` | 关闭 | 保留进程与临时目录 |

## 7. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 找不到可执行文件 | 未构建或构建目录不同 | 先 `cmake --build build -j` |
| 等待 trace 超时 | Agent 未就绪或上传延迟 | 检查脚本打印的 `server.log` / `agent.log` |
| 端口冲突 | 自动选端口失败（极少） | 重跑；或先释放占用端口 |
| 崩溃场景无记录 | 崩溃上传间隔 2 秒 | 等 2–3 秒再查 `/api/crashes` |
| SLO 显示 unknown | 内存窗口内没有 trace | 先跑 `error` 场景造数据 |

## 8. 相关文档

- [常用场景手册](../../docs/scenario-cookbook.md)：每个场景的观察顺序与产出；
- [快速诊断手册](../../docs/quick-diagnosis.md)：三步定位法；
- [hello](../hello/README.md)：最小业务接入；
- [http_backend](../http_backend/README.md)：Python/Web → C++ 后端；
- [faultlab](../faultlab/README.md)：复杂故障注入与自动诊断。
