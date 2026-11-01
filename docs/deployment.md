# 部署与运维手册

> 适用版本：v0.1（2026-11-01）
> 部署形态：局域网内单中心 `vmon-server` + 每节点一个 `vmon-agent`，纯离线。

## 1. 目录规划

推荐安装到 `/opt/vmon`：

```
/opt/vmon/
├── bin/
│   ├── vmon-server          # 中心服务
│   ├── vmon-agent           # 节点采集代理
│   ├── vmonctl              # 命令行客户端
│   ├── vmon_mcp             # MCP stdio 桥（内部版）
│   └── vmon_crash_helper    # core_pattern 上报 helper
├── web/                     # 自研 Web UI 静态资源
└── docs/                    # 本目录
/etc/vmon/
├── server.json              # 中心配置
└── agent.json               # 节点配置
```

状态目录：

| 目录 | 用途 | 默认权限 |
|---|---|---|
| `/var/lib/vmon-server` | 规则、证据包 | `0750 vmon:vmon` |
| `/var/lib/vmon-agent` | WAL 分段、本地状态 | `0750 vmon:vmon` |
| `/var/lib/vmon-crash` | SDK 崩溃报告交换目录 | `0770 root:vmon` |
| `/run/vmon` | SDK 接入 socket | `0770 vmon:vmon` |
| `/run/vmon/ctrl` | 控制通道 socket | `0770 vmon:vmon` |

## 2. 用户、用户组与目录

中心服务与 Agent 使用独立系统用户 `vmon`；**业务进程不切换用户**，只把运行用户加入 `vmon` 组，用于：

- 连接 `/run/vmon/agent.sock` 上报事件；
- 在 `/run/vmon/ctrl` 创建控制 socket，供 Agent 下发动态日志级别等命令；
- 在 `/var/lib/vmon-crash` 写崩溃报告，供 Agent 收取。

```bash
sudo useradd --system --home /var/lib/vmon --shell /usr/sbin/nologin vmon
sudo usermod -aG vmon <业务进程运行用户>
# 业务进程需重启或重新登录后用户组才生效
sudo install -m 0644 deploy/tmpfiles-vmon.conf /etc/tmpfiles.d/vmon.conf
sudo systemd-tmpfiles --create vmon.conf
```

> 如果不方便共享用户组，可让业务进程与 Agent 使用同一用户运行；不要通过放宽为 `0777` 来绕过权限问题。

## 3. 构建与安装

构建机（可离线，依赖见 `scripts/download-third-party.sh`）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 可选诊断能力（默认全关，按需在诊断版打开）
#   -DVMON_ENABLE_CPU_PROFILER=ON   常驻 CPU SIGPROF 采样
#   -DVMON_ENABLE_EBPF=ON           eBPF 弱边（Linux + libbpf）
#   -DVMON_ENABLE_GRPC=ON           gRPC streaming/异步拦截（需目标 gRPC）
cmake --build build -j
ctest --test-dir build --output-on-failure
```

安装：

```bash
sudo install -d /opt/vmon/bin /opt/vmon/web /opt/vmon/docs /etc/vmon
sudo install -m 0755 build/vmon_server build/vmon_agent \
    build/tools/vmonctl build/tools/vmon_mcp build/tools/vmon_crash_helper \
    /opt/vmon/bin/
sudo cp -r web/* /opt/vmon/web/
sudo cp docs/deployment.md /opt/vmon/docs/
sudo install -m 0640 deploy/server.json /etc/vmon/server.json
sudo install -m 0640 deploy/agent.json  /etc/vmon/agent.json
```

## 4. 中心服务配置

`/etc/vmon/server.json` 关键字段：

| 字段 | 说明 | 建议 |
|---|---|---|
| `listen_host` / `listen_port` | HTTP 监听地址 | 仅内网；默认 `18428` |
| `token` | API / Agent 访问令牌 | 必填，随机长串；所有节点一致 |
| `state_dir` | 规则、SLO、静默、Issue、仪表板/快照、证据包目录 | `/var/lib/vmon-server` |
| `max_traces` | 内存中保留的 trace 数 | 默认 20000 |
| `max_log_records` | 内存日志条数上限 | 默认 200000 |
| `retention_seconds` | 内存数据保留窗口 | 默认 6 小时 |
| `info_sample_permyriad` | INFO 日志采样率（万分之一） | `10000` 全采；降噪时调小 |
| `dedup_window_ms` / `dedup_max_per_window` | 同指纹去重窗口与上限 | 默认 30 秒 / 50 条 |
| `noise_patterns` | 已知无害日志模板关键字 | 按现场补充，只计数不存储 |
| `pipeline` | 入库前脱敏规则 | `{"enabled":true,"use_defaults":true}` |
| `webhook` | 告警事件 Webhook | `{"enabled":true,"url":"http://.../vmon/events"}` |
| `vm_url` / `vl_url` / `forward_enabled` | 可选转发 VictoriaMetrics/Logs | 不部署 VM/VL 时保持 `false` |
| `enable_evidence` | 证据包导出（内部版） | 对外发行版设为 `false` |
| `enable_mcp` | MCP 接口 | 仅内部使用；对外发行版设为 `false` |
| `evidence_salt` | 证据包 IP/邮箱等假名化盐 | 随机值，妥善保管 |

启动与检查：

```bash
sudo install -m 0644 deploy/systemd/vmon-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now vmon-server
systemctl status vmon-server
curl -s http://127.0.0.1:18428/api/health
```

`/api/health` 不需要 token，仅返回计数与状态；其余查询与接入接口必须带
`X-Vmon-Token: <token>` 或 `Authorization: Bearer <token>`。

## 5. 节点 Agent 配置

`/etc/vmon/agent.json` 关键字段：

| 字段 | 说明 | 建议 |
|---|---|---|
| `server_url` | 中心地址 | `http://<中心IP>:18428` |
| `listen_uds` | SDK 上报 UDS | `/run/vmon/agent.sock` |
| `state_dir` | WAL 目录 | `/var/lib/vmon-agent` |
| `node` | 节点名 | 留空自动取主机名；建议显式配置 |
| `token` | 与中心一致 | 必填 |
| `control_dir` | SDK 控制 socket 目录 | `/run/vmon/ctrl` |
| `crash_dir` | 崩溃报告交换目录 | `/var/lib/vmon-crash` |
| `scan_interval_ms` | 进程/主机扫描周期 | 默认 2000 |
| `upload_interval_ms` | WAL 上传周期 | 默认 1000 |
| `command_interval_ms` | 命令轮询周期 | 默认 1000 |
| `crash_upload_interval_ms` | 崩溃目录扫描周期 | 默认 2000 |
| `wal_segment_bytes` | 单个 WAL 段大小 | 默认 4 MiB；当前段超过 1 秒会封存上传 |
| `wal_max_bytes` | WAL 总容量 | 默认 64 MiB；超限先删最旧段 |
| `process_filters` | 进程命令行子串，可多个 | 只匹配业务进程，避免采集噪声 |
| `log_files` | 文件日志列表 | 每项 `path` + `service`；当前仅支持具体文件，不支持 glob |

可用性拨测通过命令行重复 `--probe` 配置（不写入 agent.json）：

```bash
vmon_agent ... --probe \
  "order-api=http://127.0.0.1:9000/health;interval_ms=30000;timeout_ms=3000;status=200"
```

部署：

```bash
sudo install -m 0644 deploy/systemd/vmon-agent.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now vmon-agent
systemctl status vmon-agent
ls -l /run/vmon/agent.sock
```

Agent 断网/中心重启时，SDK 事件写入本地 WAL；网络恢复后自动补传。端到端测试
（`tests/e2e/run_e2e.py`）覆盖了“中心停止 → 业务继续上报 → 中心恢复 → 补传”的完整流程。

## 6. 业务进程接入

### 6.1 L1：链接 SDK（推荐核心服务）

如果业务工程与 vmon 一起构建：

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/CplusTrace/cmake")
include(vmon)
vmon_attach(your_target)
```

如果使用预编译 SDK：

```cmake
target_link_libraries(your_target PRIVATE vmon)   # 或直接链接 libvmon.a
target_include_directories(your_target PRIVATE /opt/vmon/sdk/include)
```

启动时初始化，退出时关闭：

```cpp
#include <vmon/vmon.h>

vmon::Config config;
config.service_name = "order-service";
config.instance_id = "order-service-" + std::to_string(getpid());
config.service_version = "2026.11.01";
config.agent_endpoint = "/run/vmon/agent.sock";
config.control_dir = "/run/vmon/ctrl";
config.enable_crash_handler = false;  // 默认关闭，按需开启
vmon::init(config);
...
vmon::shutdown();
```

请求入口设置关联 ID，然后使用 span 与日志宏：

```cpp
vmon::set_request_id(request_id.c_str());
vmon::set_task_id(task_id.c_str());
{
  VMON_SPAN("HandleRequest");
  VMON_INFO("order created request_id=%s", request_id.c_str());
}
vmon::heartbeat();
```

约束：

- SDK fail-open：未初始化、初始化失败、队列满、Agent 不在线时，业务继续正常运行；
- 热路径不分配、不加锁、不做 IO；SDK 不阻塞业务线程；
- 日志正文超过 127 字节会截断；
- 可通过环境变量 `VMON_DISABLE=1` 全局关闭（业务代码无需改动）。

### 6.2 L0：零改造

不改业务代码时，Agent 仍可提供：

- 进程/主机资源快照；
- 文件日志采集与检索（需配置 `log_files`）；
- 崩溃报告收集（依赖 SDK 崩溃处理器或 `core_pattern` helper）；
- 进程存活与重启观测。

精确的跨进程 trace 需要 L1 注入点；这是方案的明确边界。

## 7. 崩溃收集

### 7.1 方案 A：SDK 崩溃处理器（按需开启）

```cpp
config.enable_crash_handler = true;
config.crash_report_dir = "/var/lib/vmon-crash";
```

信号处理器只做 async-signal-safe 的最小写入（信号、pid、故障地址、二进制路径、
时间），随后 `_exit(128+signal)`；不做 malloc、抓栈、符号化。Agent 扫描该目录并上报。

注意：

- 会接管 `SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL`；与平台自带 core 收集并存时需要评估；
- 报告文件权限 `0640`，目录需允许业务用户组写入、Agent 可读可删；
- 生产环境优先使用方案 B，避免改变业务进程信号行为。

### 7.2 方案 B：core_pattern helper

```bash
# 示例：仅上报元数据，core 仍由系统处理
kernel.core_pattern = |/opt/vmon/bin/vmon_crash_helper \
  --server http://127.0.0.1:18428 --token <token> \
  --node "$HOSTNAME" --signal SIGSEGV --pid %p --binary %e

# 或读取完整 JSON 报告
/opt/vmon/bin/vmon_crash_helper --server http://127.0.0.1:18428 \
  --token <token> --payload /path/to/crash.json
```

构建时保留 `-g`、build-id 与独立 debuginfo，服务端才能用 `addr2line` /
`llvm-symbolizer` 符号化。符号化接口：`POST /api/crashes/symbolize`。

## 8. 日常排障入口

```bash
# CLI（推荐）
/opt/vmon/bin/vmonctl --server http://127.0.0.1:18428 --token <token> health
/opt/vmon/bin/vmonctl --server ... overview
/opt/vmon/bin/vmonctl --server ... traces --request <request_id>
/opt/vmon/bin/vmonctl --server ... trace <trace_id>
/opt/vmon/bin/vmonctl --server ... logs --request <request_id>
/opt/vmon/bin/vmonctl --server ... clusters
/opt/vmon/bin/vmonctl --server ... inspect
/opt/vmon/bin/vmonctl --server ... diagnose --request <request_id> --pid <pid>
/opt/vmon/bin/vmonctl --server ... evidence --request <request_id>
```

动态日志级别（带 TTL，到期自动恢复）：

```bash
curl -s -H 'X-Vmon-Token: <token>' -H 'Content-Type: application/json' \
  -d '{"node":"node-1","pid":12345,"level":"debug","ttl_ms":60000}' \
  http://127.0.0.1:18428/api/control/log-level
```

Agent 需要能访问业务进程的 `/run/vmon/ctrl/<pid>.sock`，且业务进程已加入 `vmon` 组。
命令执行结果可在 `/api/commands` 或 `vmonctl commands` 查看。

### 8.1 MCP（内部版）

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

MCP 只暴露只读查询与证据包生成；对外发行版应设置 `"enable_mcp": false`。

## 9. 安全与合规边界

- 仅部署在纯内网；不要暴露到公网，不要在前面挂公网反向代理；
- 必须设置 token；token 泄露等于维测数据泄露；
- 诊断命令为固定白名单（`/proc` 快照、`ss`、`lsof`、`gdb bt`、`dmesg`），不拼接 shell；
- 证据包默认脱敏 IP、邮箱、token/password、home 路径，但**导出仍需人工审批**；
- 对外发行包关闭 `enable_evidence` 与 `enable_mcp`，不包含内部 AI 相关能力；
- 日志与证据包可能包含业务数据，保留期与访问权限按现场制度执行。

## 10. 升级与回滚

1. 备份 `/etc/vmon/*.json` 与 `/var/lib/vmon-server/{rules,slos,silences,issues,dashboards}.json`；
2. 停止 `vmon-agent` 可避免升级期间产生半截数据；中心服务可先升级；
3. 替换 `/opt/vmon/bin` 下二进制与 `/opt/vmon/web`；
4. `systemctl restart vmon-server vmon-agent`；
5. 验证 `/api/health`、Agent 日志、`vmonctl health`；
6. 回滚：恢复旧二进制与配置后重启。WAL 为追加文件，旧版本可继续读取未删除段。

## 11. Linux 兼容性检查清单

代码以 C++14 + POSIX 编写，已在 macOS arm64 跑通全部单元/端到端测试；移植到 Linux 时确认：

- `gcc 7.3.0` 可编译 C++14 子集，不依赖 `std::filesystem`、`std::optional` 等 C++17 特性；
- Agent 进程扫描在 Linux 使用 `/proc`，macOS 使用 `ps` 回退；主机信息在 Linux 读 `/proc`，
  macOS 使用 `sysctl`；
- `ss`、`lsof`、`gdb`、`addr2line`/`llvm-symbolizer` 由现场包管理器安装；缺失时对应用户操作
  返回失败，不影响其他维测能力；
- 节点间需要 NTP 同步，否则跨节点时间线会出现偏差；
- aarch64 上如链接静态库报原子操作未定义，追加 `-latomic`。

## 12. 验收命令

```bash
# 单元测试 + 端到端（构建机）
ctest --test-dir build --output-on-failure

# 中心健康
curl -s http://127.0.0.1:18428/api/health

# Agent 在线与节点资产
/opt/vmon/bin/vmonctl --server http://127.0.0.1:18428 --token <token> nodes

# 一次请求的完整链路
/opt/vmon/bin/vmonctl --server http://127.0.0.1:18428 --token <token> \
  traces --request <request_id>
```
