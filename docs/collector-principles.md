# 采集器原理

> 本文讲 Agent 里每个采集器「采什么、怎么采、什么时候采、满了怎么办」。
> 模块边界见 [module-design.md](module-design.md)，整体原理见 [principles.md](principles.md)。

## 1. 总览：所有采集器最终只做一件事

```
采集器（定时/事件触发）
  → 组装 common::Event
  → wal.append(to_json(event).dump())
  → Uploader 批量 POST /v1/ingest
  → Server Store 索引/分析
```

统一约定：

- **有界**：每次采集有上限（进程数、行数、单行大小）；
- **不阻塞**：采集失败只记录/跳过，不影响其他采集器；
- **可观测**：丢弃、WAL 上限、队列满都有计数；
- **不解析业务**：Agent 尽量保持原始行/原始字段，语义交给 Server。

Agent 由一个主循环 + 若干后台线程组成：

| 组件 | 触发方式 | 默认周期 |
|---|---|---|
| UDS 接入 | 事件驱动（SDK 写入） | 实时 |
| 进程/主机扫描 | 主循环定时器 | 2s |
| 文件日志 | 主循环定时器 | 500ms |
| 崩溃目录扫描 | 独立线程 | 2s |
| 命令轮询 | 独立线程 | 1s |
| HTTP 拨测 | 独立线程 | 每任务 30s（可配） |
| 上传 | 独立线程 | 200ms~1s |

## 2. SDK 事件接入（UDS）

代码：`agent/src/ingest.*`

原理：

```
SDK 写入 UDS（JSONL）
→ poll 监听多个客户端
→ 每个客户端维护 buffer，按 '\n' 切行
→ 每行交给 handler：wal.append(line)
```

- 单线程 poll，避免连接多时线程爆炸；
- 不解析 JSON，只保证“一行一个事件”；
- 拿到对端 pid（`SO_PEERCRED`）用于审计/诊断；
- 客户端断开时清空 buffer，不保留半行；
- Agent 崩溃/重启后由 SDK 重新连接。

## 3. 进程与主机采集

代码：`agent/src/process_scan.*`

### 3.1 Linux（目标环境）

遍历 `/proc/<pid>/`：

| 文件 | 取什么 |
|---|---|
| `stat` | ppid、state、start_time_ticks |
| `status` | VmRSS、Threads |
| `cmdline` | 命令行（NUL 转空格） |

进程名从 `cmdline` 的 argv0 取 basename；命令行用于过滤与展示。

### 3.2 macOS（开发环境）

没有 `/proc`，用：

```bash
ps -axo pid=,ppid=,rss=,state=,comm=,args=
```

局限：

- `comm` 可能被截断；
- 没有线程数，`threads=0`；
- 内存可用量/uptime 字段不完整。

这就是为什么文档标注“macOS 字段可能不完整，Linux 才完整”。

### 3.3 过滤与上限

```text
--process-filter 子串1,子串2
```

- 匹配 `cmdline` 或 `name`；
- 为空表示全部进程；
- 每次最多保留 2000 个匹配进程；
- 不采进程的历史指标，只采当前快照。

### 3.4 进程身份

`pid + start_time_ticks + node` 才能唯一标识一次进程生命周期，避免 PID 复用。

## 4. 主机指标

代码：`agent/src/metrics.*`

每次扫描后生成 9 条 `type=metric`：

| 指标 | 来源 |
|---|---|
| `host.load1/5/15` | `getloadavg` |
| `host.mem_total_bytes` | `/proc/meminfo` 或 `hw.memsize` |
| `host.mem_available_bytes` | `/proc/meminfo` |
| `host.mem_used_bytes` | total - available |
| `host.mem_used_ratio` | used / total |
| `host.process_count` | 本次匹配的进程数 |
| `host.uptime_seconds` | `/proc/uptime` |

标签：`node`、`agent`。Store 会把除 `value` 外的 attrs 作为标签。

**进程 RSS 指标默认关闭**（`--process-metrics`）：

- 50–200 进程 × 每 2s 一条 → 序列数增长很快；
- 需要按进程排查内存时再打开。

## 5. 文件日志采集

代码：`agent/src/log_tail.*`

### 5.1 跟踪方式

每个文件维护：

```
path / service / inode / offset / pending / known
```

每次 poll：

1. `stat(path)` 拿 inode；
2. 首次打开、inode 变化、文件变短 → 从头开始；
3. 从 offset 读最多 4MB；
4. 按换行切完整行；
5. 半行留在 `pending`，下次继续；
6. 单行超过 64KB 截断；`pending` 超过 64KB 直接丢弃（防异常长行吃内存）。

### 5.2 时间与字段

- `time_ns` 是 **Agent 读取时间**，不是日志原始时间；
- `service` 来自启动参数 `--log-file path=service`；
- 不自动提取 `request_id/trace_id/tid`，服务端会从正文做轻量提取。

### 5.3 已知边界

- 只支持具体文件，不支持 glob/目录；
- 不采 stdout/stderr、journald、容器日志；
- 不合并多行堆栈；
- offset 不持久化，Agent 重启会从头重读（可能重复）；
- 轮转瞬间可能漏掉一小段；
- 文件日志没有 `tid`，线程视图只能靠 SDK 日志/Span。

## 6. 崩溃采集

代码：`agent/src/crash_collect.*` + `sdk/src/crash/*`

- SDK 信号处理器在信号上下文只做 `write()`，写 `crash-<pid>.json`，然后 `_exit`；
- Agent 周期扫描 `--crash-dir`；
- 解析成功的 JSON → `POST /v1/crash`；
- 上传成功后删除文件；
- 符号化在 Server 离线完成，不在崩溃进程里做。

## 7. 命令与诊断采集

代码：`agent/src/command_runner.*` + `control_client.*`

```
Server 命令队列
← Agent 轮询 /api/commands
→ 按类型执行 ping/set_log_level/dump_stacks/get_stats/diagnose
→ 连接 SDK 控制 socket <control_dir>/<pid>.sock
→ POST 结果 /api/commands/{id}/result
```

安全边界：

- 只执行内置 argv，不拼 shell；
- 有超时与输出上限；
- 诊断只读优先。

## 8. HTTP 拨测

代码：`agent/src/probe.*`

- 每个 probe 有自己的 interval/timeout/expected status；
- `httplib::Client` GET，记录耗时；
- 生成 `probe.up`、`probe.duration_ms`、`probe.status_code`；
- 失败额外生成一条 error 日志；
- 单线程轮询，失败不影响其他 probe；
- 当前只支持 `http://`，多协议未实现。

## 9. WAL 与上传：采集器的“下水道”

代码：`agent/src/wal.*` + `uploader.*`

```
wal.append(line)
  → 当前段累计 >= segment_bytes 或超过 1s 且有数据 → 封存
  → uploader 读取封存段
  → POST /v1/ingest
  → 成功删段；失败保留重试
  → 总容量 > wal_max_bytes → 删除最旧段
```

这是采集器可靠性的核心：

- 中心停机/网络抖动时，数据先落本地；
- 容量满时按“最旧优先”丢弃；
- 上传是至少一次语义，可能重复，Server 侧去重。

## 10. 失败与降级

| 场景 | 行为 |
|---|---|
| 进程扫描失败 | 跳过本轮，下次重试 |
| 日志文件不存在 | 跳过；文件出现后下次开始读 |
| 崩溃 JSON 非法 | 不上传，保留文件（便于人工检查） |
| 命令 socket 连不上 | 返回错误，不影响 Agent 主循环 |
| 中心不可达 | WAL 累积，恢复后补传 |
| WAL 满 | 删除最旧段 |
| SDK 队列满 | SDK 丢弃并计数 |

## 11. 扩展一个采集器

推荐模式：

1. 新文件 `agent/src/xxx_collector.h/.cpp`；
2. 提供 `start/stop` 或 `poll_once`；
3. 每次采集限制条数/大小；
4. 组装 `common::Event`，设置 `type/node/service/attrs`；
5. 通过注入的 `wal.append` 写出，不直接发 HTTP；
6. 单元测试 `poll_once` 的边界（空目录、超限、异常文件）；
7. E2E 增加真实数据断言。

## 12. 采集器能力边界（当前）

- 无容器/cgroup/K8s 采集；
- 无 stdout/journald/容器日志；
- 无日志 glob、offset 持久化、多行合并；
- 无 eBPF/网络连接采集（只有接口）；
- macOS 进程字段不完整；
- 不采业务自定义指标（需要 SDK 或后续扩展）。
