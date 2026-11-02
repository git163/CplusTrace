# 本机验收记录

> 验收日期：2026-11-01
> 验收环境：macOS 15.7.1 arm64，Apple clang 17.0.0，CMake 4.0.2，Python 3.14
> 目标环境：Linux x86_64 gcc 12.3 / aarch64 gcc 7.3.0（C++14 基线）

## 1. 验收结论

M1–M6 已完成一条可运行、可测试的纵向能力链：

- 真实业务进程通过 SDK 上报 span / 日志 / 心跳，经 Agent WAL 到达中心；
- 按 `request_id` 可还原 trace 树、关键路径与关联日志；
- 动态日志级别命令可下发到业务进程并在 TTL 后自动恢复；
- 日志模板聚类、规则/事件、崩溃捕获、剖析数据面、证据包、MCP 均可用；
- 异常追踪 Issue、告警静默/分组、HTTP 拨测、仪表板/快照已落地；
- 可选 CPU SIGPROF 采样有独立编译开关，默认不进入生产构建；
- 中心服务停机期间 Agent 本地缓存，恢复后自动补传；
- 单元测试、客户端测试与端到端测试全部通过。

完整构建与测试命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

最近一次结果（加入 Python/Web 客户端与 HTTP 后端后）：

```
1/6 Test #1: vmon_e2e ............   Passed  19.21 sec
2/6 Test #2: vmon_python_client ...   Passed   0.58 sec
3/6 Test #3: vmon_web_client ......   Passed   0.05 sec
4/6 Test #4: vmon_sdk_tests .......   Passed   0.00 sec
5/6 Test #5: vmon_server_tests ....   Passed   0.05 sec
6/6 Test #6: vmon_agent_tests .....   Passed   2.38 sec

100% tests passed, 0 tests failed out of 6
```

测试数量：C++ 单元 137 个（SDK 19、Server 79、Agent 39），
Python 客户端 9 个，Web 客户端 5 个，共 151 个测试；
端到端场景 22 类。
Web UI 17 个视图的真实浏览器回归记录见 [ui-test-report.md](ui-test-report.md)。

## 2. 端到端覆盖

`tests/e2e/run_e2e.py` 启动真实 `vmon-server`、`vmon-agent` 与
`tests/e2e/e2e_client.cpp`（链接真实 SDK），执行并断言：

| # | 场景 | 断言 |
|---|---|---|
| 1 | 请求链路 | `/api/requests/{request_id}` 返回完整 trace：span 数 ≥ 3、深度 ≥ 2、有根节点与关键路径、包含业务服务 Span；日志按 request_id 关联且包含错误日志 |
| 2 | 查询条件 | trace 按 trace_id / request_id / task_id / service / 统一搜索 `q` / 仅错误 / 最小耗时；日志按 trace/request/task/service/node/level/text/仅错误；服务视图含 QPS/错误率/P95 |
| 3 | HTTP 表面 | `/api/health` 200；无令牌 `/api/overview` 401；`/`、`/app.js`、`/style.css` 可访问；不存在 trace 返回 404 |
| 4 | 指标 | `host.load1` 带 node 标签；内存/进程数/uptime 可查；`process.rss_bytes` 带 process 标签、值 > 0，且不得携带 service 标签（回归） |
| 5 | 动态日志级别 | 命令前无 debug 日志；`POST /api/control/log-level` 后命令 `done`，业务进程确认 `debug`；中心收到 `e2e debug tick` 且聚类模板可见 |
| 6 | MCP 基础 | `tools/list` 返回工具列表；`tools/call get_request_chain` 返回对应 trace |
| 7 | 跨进程链路 | 父进程导出上下文，子进程 `create_span_with_parent` 续接；同一 trace 含两个服务；子 Span 的 parent 正确；拓扑出现 `e2e-parent → e2e-child` 边且带 confidence |
| 8 | Python/Web RPC | Python 客户端 → C++ HTTP 后端：同一 trace、后端 span 父节点正确、两侧日志可见；强制 500 时首个错误码为 `E_ECHO` / `E_HTTP_500` |
| 9 | 事件生命周期 | 注入 deploy 事件；按 open/type/severity/service 筛选；ack → resolve；resolved 后再 ack 不回退；不存在事件返回 404 |
| 10 | 剖析 | 折叠栈导入后可查 `total_samples=180`、Top self、火焰图根值与 `main` 子节点 |
| 11 | 诊断/巡检 | `/api/diagnose` 返回 traces/first_error/logs/hypotheses/checks/verdict；`/api/inspection` 返回 ok/warn/fail 摘要 |
| 12 | 证据包 | manifest 版本/pack_id 正确；文件大小与 SHA-256 校验一致；日志中的 password/email/IP/home 路径被替换；redaction 规则可见 |
| 13 | SLO | 可用性/延迟 SLO 的 SLI、错误预算、状态；非法 target 400；列表/删除/重复删除 404；MCP `list_slos` |
| 14 | MCP 查询 | `query_traces`、`get_topology` 返回结构正确；未知工具返回 `-32602` |
| 15 | 规则持久化 | 创建指标规则与 SLO；重启 Server 后仍存在；空数据 SLO 为 `unknown`；删除生效 |
| 16 | 崩溃 | 业务进程 `SIGSEGV` 后退出码匹配；`/api/crashes?service=e2e-crash` 可查到；事件中心有 crash 事件 |
| 17 | 断网续传 | 停止中心 → 业务进程继续上报 → 重启中心 → 补传的 trace 完整（span 数 ≥ 3） |
| 18 | 异常追踪 | 错误日志聚类派生成 Issue；ack → 指派 → resolved；状态筛选生效 |
| 19 | 告警静默 | 创建/列表/删除静默；重复删除 404（规则抑制由单测覆盖） |
| 20 | 可用性拨测 | Agent 拨测本地 HTTP 目标，`probe.up/duration/status` 指标与 `/api/probes` 均为 UP |
| 21 | 仪表板/快照 | 指标/链路/文本三类 widget 计算正确；生成快照并可读取；删除后 404 |
| 22 | 错误码跨链路 | `/api/error-codes` 聚合 E_ECHO 的 trace 数、服务与传播路径；`/api/traces?error_code=` 可查；trace 详情含 error_chain/threads/context |

运行方式（也可直接 `ctest -R vmon_e2e`）：

```bash
python3 tests/e2e/run_e2e.py \
  --server-bin build/vmon_server \
  --agent-bin build/vmon_agent \
  --client-bin build/tests/vmon_e2e_client \
  --backend-bin build/examples/http_backend/vmon_http_backend \
  --web-dir web
```

加 `--keep` 可保留临时目录，便于失败时查看 server/agent 日志。

常用场景演示脚本也已逐项实跑：

```bash
python3 examples/scenarios/demo.py --scenario rpc
python3 examples/scenarios/demo.py --scenario slow
python3 examples/scenarios/demo.py --scenario error
python3 examples/scenarios/demo.py --scenario logstorm
python3 examples/scenarios/demo.py --scenario slo
python3 examples/scenarios/demo.py --scenario crash
python3 examples/scenarios/demo.py --scenario all
```

## 3. 本轮发现并修复的问题

| 问题 | 影响 | 修复 |
|---|---|---|
| traceparent 128 位 id 高低 64 位写反 | 生成的 traceparent 无法正确回读，跨进程链路 id 错乱 | `adapters/grpc/propagation.cpp` 按 hi→lo 顺序写入并补测试 |
| 上传 URL 前缀为空时拼出 `//v1/ingest` | Agent 无法上传，数据滞留 WAL | 去掉强制 `/`，由 `split_url` 保持空前缀 |
| WAL 段文件同一毫秒复用 | 进程重启时可能覆盖未上传段，丢数据 | `O_EXCL` + 序号重试，并补单测 |
| 日志正文含 `trace_id` 时未建立 trace 索引 | 日志无法按 trace 查询 | 服务端从消息中提取 `trace_id` |
| 低流量节点当前 WAL 段永不封存 | 数据长期滞留本地，中心看不到 | 当前段超过 1 秒且非空即封存，补单测；`vmon_e2e` 覆盖 |
| 控制 socket 权限 `0600` | 业务用户与 Agent 不同用户时命令无法下发 | 改为目录 `0770`、socket `0660`，部署要求共享 `vmon` 组 |
| 崩溃报告 `0600` | Agent 无法读取其他用户写的报告 | 改为 `0640`，目录按用户组授权 |
| 示例进程把局部 `std::string` 的 `c_str()` 传给 Span | trace 内容出现非法 UTF-8，SDK 序列化抛异常导致业务进程终止 | 改用 `main` 期间有效的 `options.root_name`；发送端改用 `dump(..., replace)` + try/catch，非法字节不再终止业务进程，并正确累计 dropped |
| 未结束 Span 在瀑布图显示巨大负数 | 排障时时间线不可读 | 新增 `spanMs()` 与未结束 Span 回退逻辑，显示「进行中」 |
| 搜索 `request_id`（非 `REQ` 前缀）查不到 trace | 用户无法按业务请求号定位 | 服务端新增 `q` 统一子串搜索（忽略大小写），覆盖 trace_id/request_id/task_id/服务/Span；补单测与 E2E |
| 进程名被写入指标 `service` | `/bin/zsh` 等进程混入服务目录 | 进程指标只写 `process`/`pid`/`threads` 标签；E2E 增加回归断言 |
| MCP 未知工具返回 `-32603` | 与 JSON-RPC 规范不符 | 参数类错误改为 `-32602` |
| 同槽位复用的 Span 继承上一个 Span 的 `error_code` | 正常 Span 被标成错误，影响首个错误与 SLO | 创建 Span 时清空 `frame.error_code`，补单测 |
| `set_request_id` 指向临时 `std::string` | 后台发送线程读取时悬垂，日志 request_id 错乱 | 示例改用字符串字面量；后端用有界 ID 池；文档注明指针需活到发送 |

## 4. 已核验的安全/降级行为

- 无 token / 错误 token 返回 401；`/api/health` 保持公开；
- SDK 未初始化、初始化失败、Agent 不在线时业务继续运行；
- 队列满丢弃事件并计数，不阻塞业务线程；
- 诊断命令白名单拒绝 `passwd` 等非白名单操作；
- 证据包默认脱敏 IP、邮箱、token/password、home 路径；
- 日志 INFO 采样、同指纹去重、噪声模板只计数不存储；
- 规则支持冷却与稳定事件 ID；禁用/限定范围的规则不误报。

## 5. 已知限制（后续加深项）

1. gRPC 拦截器只提供 `traceparent`/request_id/task_id 传播层；实际 client/server
   interceptor 需按目标工程 gRPC 版本接入（旧版 gRPC 可能无 C++ interceptor）。
   `vmon_attach(target)` 已提供业务侧一行链接入口。
2. Socket 仅提供 24 字节 trace 头编解码（`adapters/ipc/socket_header.h`），
   尚未接入具体 Socket 协议与连接级关联；eBPF 弱边、gRPC streaming 已提供
   编译开关与接口层（`adapters/bpf/weak_edge.h`、`adapters/grpc/streaming.h`），
   真正 collector/interceptor 需目标环境接入；尾部采样未实现。
3. 线程池/队列/回调 drop-in 包装器与 `causality.id` 自动传递未实现。
4. 常驻 CPU `SIGPROF` 采样已实现但默认不编译，需 `-DVMON_ENABLE_CPU_PROFILER=ON` 的诊断版；内存/锁/IO 剖析、perf 仍未实现。eBPF/gRPC streaming 仅提供编译开关与接口层，需目标环境验证。
5. Webhook 通知已实现；邮件/syslog 通知未实现。
6. 平台注册中心 API 对接、Parquet 落盘、发布记录关联未实现。
7. 72 小时长稳、磁盘写满、时钟跳变等混沌测试尚未自动化。
8. Linux 目标机上的 gcc 7.3.0 / aarch64 实机编译与 systemd 运行待移植阶段验证；
   代码已按 C++14 与 POSIX 约束编写。

这些限制不影响本机验收结论，但正式生产推广前需按里程碑补齐。
