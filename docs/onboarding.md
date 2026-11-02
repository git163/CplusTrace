# 开发者上手指南（从 0 开始）

> 面向第一次参与本项目的开发人员。目标：**半天跑通、看懂主链路、能独立改一个小功能**。
> 架构细节见 [module-design.md](module-design.md)，使用方式见 [user-guide.md](user-guide.md)，
> 工具生成的代码全景见 [code-panorama-analysis.md](code-panorama-analysis.md)。

## 1. 先建立正确预期

这个项目不是一个普通 Web 服务，而是「探针 + 采集代理 + 中心 + UI」的完整链路：

```
业务进程 SDK ──UDS──> Agent ──HTTP──> Server ──HTTP──> Web/CLI/MCP
```

你至少要同时理解三件事：

1. **事件模型**：所有日志/指标/Span/崩溃都是同一种 Event JSON；
2. **关联键**：trace_id / request_id / task_id / pid+boot / build_id；
3. **可靠性边界**：队列满、WAL 满、中心停机时系统如何降级。

## 2. 环境准备

| 依赖 | 要求 |
|---|---|
| CMake | 3.20+ |
| C++ 编译器 | 支持 C++14（目标 Linux gcc 12.3 / gcc 7.3 aarch64） |
| Python | 3.8+（E2E 与 Python 客户端测试） |
| Node.js | 可选，Web 客户端测试；如果没有，对应 CTest 自动跳过 |
| 三方源码 | `third_party/src` 已入库，clone 后可直接构建 |

```bash
git clone https://github.com/git163/CplusTrace.git
cd CplusTrace
```

> 不需要先运行三方下载脚本；`third_party/src` 已在 git 中。
> `third_party/downloads`、`third_party/bin` 是按平台下载的二进制，不入库。

## 3. 30 分钟跑通

### 3.1 构建 + 测试

```bash
# 一键编译 + 跑测试
./scripts/build.sh --test

# 或手动
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

预期 6 组测试全绿：

- `vmon_sdk_tests`
- `vmon_server_tests`
- `vmon_agent_tests`
- `vmon_python_client`
- `vmon_web_client`
- `vmon_e2e`

### 3.2 启动本地栈

```bash
./scripts/vmon.sh up
./scripts/vmon.sh status
```

会打印局域网地址和本机地址，例如：

```
Web UI : http://192.168.1.3:18428/
          http://127.0.0.1:18428/
```

### 3.3 造一条真实请求

```bash
# 最小业务示例
./examples/hello/run_demo.sh

# 六类场景（RPC/慢调用/错误/日志风暴/SLO/崩溃）
python3 examples/scenarios/demo.py --scenario all --keep
```

打开 Web UI，用脚本打印的 request_id 搜索，按这个顺序看：

1. 总览 KPI；
2. 调用链 → Span 树 → 瀑布图 → 关键路径；
3. 错误传播链 → 线程时间线 → 进程/节点/系统；
4. 日志 → 聚类 → 诊断 → 证据包。

### 3.4 常用调试命令

```bash
./scripts/vmon.sh logs server -f
./scripts/vmon.sh logs agent -f

# 只跑 E2E，并保留现场
cd build && ctest -R vmon_e2e --output-on-failure
python3 ../tests/e2e/run_e2e.py \
  --server-bin vmon_server --agent-bin vmon_agent \
  --client-bin tests/vmon_e2e_client \
  --backend-bin examples/http_backend/vmon_http_backend \
  --web-dir ../web --keep

# CLI
./build/tools/vmonctl --server http://127.0.0.1:18428 \ overview
```

## 4. 代码阅读顺序（重要）

不要从 `server/src/api.cpp` 开始。按这个顺序读，一天能建立完整心智模型：

| 顺序 | 文件 | 要理解什么 |
|---|---|---|
| 1 | [README](../README.md) | 项目定位、构建、API 概览 |
| 2 | [docs/module-design.md](module-design.md) | 分层、模块职责、类图、时序图 |
| 3 | [common/json_event.h](../common/json_event.h) | 统一 Event 模型，所有模块共用 |
| 4 | [sdk/include/vmon/vmon.h](../sdk/include/vmon/vmon.h) | 业务侧公共 API（init/Span/log/heartbeat） |
| 5 | [sdk/src/core/ring_buffer.h](../sdk/src/core/ring_buffer.h) / `thread_context.h` | 每线程无锁队列、Span 栈、request_id |
| 6 | [sdk/src/trace/span.cpp](../sdk/src/trace/span.cpp) | Span 创建/结束、父子关系、错误码 |
| 7 | [sdk/src/transport/sender.cpp](../sdk/src/transport/sender.cpp) | 后台批量序列化、UDS 发送、丢弃计数 |
| 8 | [agent/src/ingest.cpp](../agent/src/ingest.cpp) / `wal.cpp` / `uploader.cpp` | 接入 → WAL → 上传 |
| 9 | [agent/src/main.cpp](../agent/src/main.cpp) | Agent 线程与定时器编排 |
| 10 | [server/src/store.cpp](../server/src/store.cpp) | 有界存储、索引、日志采样/去重 |
| 11 | [server/src/trace_builder.cpp](../server/src/trace_builder.cpp) | Span 树、关键路径、错误链、线程时间线 |
| 12 | [server/src/api.cpp](../server/src/api.cpp) | 路由、鉴权、查询、诊断、证据包 |
| 13 | [web/app.js](../web/app.js) | 17 个视图、瀑布图、SVG 火焰图、错误码视图 |
| 14 | [tests/e2e/run_e2e.py](../tests/e2e/run_e2e.py) | 真实链路的验收方式 |

### 4.1 入口点速查

| 想改什么 | 从哪进 |
|---|---|
| SDK 行为/日志/Span | `sdk/include/vmon/vmon.h`、`sdk/src/` |
| Agent 采集/上传 | `agent/src/main.cpp`、`ingest/wal/uploader` |
| 查询/API/诊断 | `server/src/api.cpp`、`store.cpp`、`diagnostics.cpp` |
| Trace 树/关键路径/线程 | `server/src/trace_builder.cpp` |
| 规则/事件/SLO/静默/Issue | `server/src/rules.cpp`、`slo.cpp`、`silence.cpp`、`issues.cpp` |
| Web UI | `web/index.html`、`web/app.js`、`web/style.css` |
| CLI/MCP | `tools/cli/vmonctl.cpp`、`tools/mcp/vmon_mcp.cpp` |
| 测试 | `tests/`、`server/tests/`、`agent/tests/` |

## 5. 开发一个功能的标准流程

以「新增一个查询接口」为例：

1. **数据层**：在 `server/src/store.*` 增加查询方法，保持有界与线程安全；
2. **聚合层**：需要跨数据源时放 `trace_builder` / `diagnostics` / 新模块；
3. **API**：在 `server/src/api.cpp` 注册路由，套 `guard` 鉴权；
4. **UI**：在 `web/index.html` 加视图/按钮，`web/app.js` 调 API；
5. **测试**：
   - 数据/聚合单测放 `server/tests/`；
   - 真实链路断言放 `tests/e2e/run_e2e.py`；
   - UI 交互用 Playwright MCP 回归；
6. **文档**：更新 README / user-guide / module-design 对应章节；
7. **提交前**：`ctest --output-on-failure` 全绿。

以「新增 Agent 采集器」为例：

1. `agent/src/xxx.h/.cpp` 周期性采集；
2. 转成 `common::Event`，`wal.append(to_json(event).dump())`；
3. Server `Store::ingest` 增加对应 type 的处理/索引；
4. 如需要规则/UI，再接 `rules` 与 `web`；
5. 单测 + E2E。

## 6. 编码约束（必须遵守）

- **C++14**：不要使用 `std::optional`、`std::string_view`、`std::filesystem`、`if constexpr`、结构化绑定；
- **中文注释/文档**：解释性文字用中文；标识符保持英文；
- **不影响业务**：SDK 热路径不分配、不加锁、不做 IO；所有失败 fail-open；
- **指针生命周期**：`set_request_id`/`task_id` 只保存指针，必须活到事件发送；
  Span 名称/错误码使用静态字符串；
- **有界资源**：新增缓存/队列必须设上限，超限计数并丢弃；
- **鉴权**：新增 API 默认套 `guard`，只有 `/api/health` 公开；
- **开源友好**：新增三方依赖必须是 Apache-2.0/MIT/BSD/ISC/zlib/PD，并更新 `third_party/deps.tsv`。

## 7. 常见坑

| 现象 | 原因/处理 |
|---|---|
| 构建找不到头文件 | 确认 `third_party/src` 存在；不要用 `-DVMON_BUILD_TESTS=OFF` 排除 gtest 后又跑测试 |
| E2E 端口冲突/超时 | 用 `--keep` 保留现场，看 `server.log`/`agent.log`；E2E 自动选空闲端口 |
| Web 无法访问 | 确认 `LISTEN_HOST=0.0.0.0`、防火墙放行端口、服务已启动 |
| 局域网打不开 | `LISTEN_HOST=0.0.0.0`、防火墙放行端口、同一网段 |
| Span 出现乱码/悬垂 | Span 名称、request_id 使用了局部 `std::string`；改用静态/长生命周期串 |
| 日志只有 127 字节 | SDK 固定缓冲区截断，详细内容写文件日志或拆分字段 |
| 线程视图只有 tid=0 | Python/Web 客户端未发送 tid；C++ SDK 会带 tid |
| macOS 进程线程数=0 | macOS 用 `ps` 兜底，Linux `/proc` 才完整 |
| 测试跳过 E2E | 未找到 Python3 或 `vmon_http_backend`；检查 CMake 输出 |

## 8. 第一周建议任务

**Day 1**：构建、跑测试、跑 `demo.py --scenario all --keep`，把 17 个视图点一遍。

**Day 2**：按第 4 节顺序读代码；用 `code-panorama` 看 `server/src/main.cpp` 的调用图；
写一份自己的「一次请求从 SDK 到 Web」时序笔记。

**Day 3**：改一个小功能并补测试，例如：

- 给总览加一个统计卡片；
- 给 `/api/error-codes` 加一个过滤参数；
- 给日志视图加一个快捷时间窗。

**第 1 周可选的“好第一 issue”**：

1. Agent 日志采集支持 glob / 目录（当前只支持具体文件）；
2. Agent 持久化日志 offset/inode，避免重启重读；
3. 文件日志多行合并 + 原始时间戳解析；
4. 拆 `server/src/main.cpp`：bootstrap / config / signal / http_server；
5. 拆 `register_routes`：ingest/query/control/admin；
6. 抽 `BackgroundService` 统一 Forwarder/Notifier 生命周期；
7. Python 客户端支持 asyncio 上下文；
8. macOS 进程扫描补齐线程数/完整进程名。

## 9. 调试与排障工具

- `./scripts/vmon.sh status/logs`：最快看进程与日志；
- `vmonctl`：CLI 查询；
- `curl /api/health`：无鉴权健康检查；
- Playwright MCP：UI 回归与截图；
- `lldb` / `gdb`：C++ 单步；E2E `--keep` 保留真实进程；
- code-panorama：入口与调用图（可选，需 LLM Key，Key 不进入仓库）。

## 10. 提交前检查清单

- [ ] `cmake --build build -j` 通过；
- [ ] `ctest --test-dir build --output-on-failure` 全绿；
- [ ] 新增/修改行为有单测或 E2E；
- [ ] 新增 API 有鉴权、参数校验、上限；
- [ ] 文档与截图同步；
- [ ] 没有把 Key、构建产物、运行目录提交进 git；
- [ ] C++14 与“不影响业务”的约束没有被破坏。
