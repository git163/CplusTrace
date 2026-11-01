# 三方开源依赖清单与许可证策略

> 版本：v0.1（2026-11-01）
> 适用：CplusTrace / vmon 维测服务
> 目标：**许可证友好、纯离线可构建、对生产业务零影响**

## 0. 结论先行

1. **许可证白名单**：默认只引入 `Apache-2.0`、`MIT`、`BSD-2/3-Clause`、`ISC`、`zlib`、`Public Domain`。
2. **许可证红线**：`GPL-2.0/3.0`、`AGPL-3.0`、`SSPL`、`BSL`、`Elastic License`、`Commons Clause` 不链接进产品、不复制进源码树；确需使用只能作为**独立外部工具**，且不随产品分发。
3. **生产进程内零重依赖**：业务进程只链接一个极小的静态 SDK；gRPC 适配器复用目标系统现有 gRPC/protobuf/abseil，**不另带一份**。
4. **重依赖全部外置**：SQLite、HTTP、压缩、正则、符号化、eBPF 等只出现在独立的 Agent / Server 进程中。
5. **默认不引入 Grafana**（AGPL-3.0）；自研请求级排障 UI + uPlot（MIT）替代。
6. 所有下载物锁定版本、记录 SHA256、保留 LICENSE/NOTICE，并纳入 `third_party/deps.tsv` 清单。

> 以下版本为 2026-11-01 查询到的最新稳定版，仅作为下载起点。最终版本必须以目标系统的 OS、glibc、gRPC 版本和编译器能力为准。

## 0.1 已确认的目标环境（2026-11-01）

| 项 | 现状 | 对依赖/设计的影响 |
|---|---|---|
| OS / 架构 | x86_64（gcc 12.3）、aarch64（gcc 7.3.0） | 需要两套构建产物；aarch64 按 C++14 开发（项目统一基线） |
| protobuf | 3.0（具体小版本待确认） | 不能直接套用新版 gRPC/protobuf；适配器随目标工程编译 |
| gRPC | 待确认，可能与 protobuf 3.0 同年代 | 版本过老可能没有 C++ interceptor，需在公共 gRPC 封装层手工注入或走 sidecar |
| 线程池 | Poco 封装 | 复用 POCO；增加 `PocoThreadPoolAdapter` 传递 `causality.id`；POCO 为 BSL-1.0 |
| 数据库 | 有统一数据库 | 中心资产目录复用该库；Agent 本地 WAL 用分段文件，SQLite 可选 |
| 现有监控 | 无 Grafana/Prometheus/ELK | 不集成现成平台；按本方案自建 UI + VictoriaMetrics/VictoriaLogs/Jaeger |

**首期验证环境（2026-11-01 确认）**：先在开发机（macOS arm64）上跑通端到端流程，运行时二进制下载 darwin-arm64 版本；代码保持 Linux 可移植性（POSIX API、CMake 跨平台、不依赖 macOS 专有接口），Linux x86_64/aarch64 的二进制与 `systemd`、`eBPF` 等能力留到移植阶段补齐。

### 0.1.1 aarch64 gcc 7.3.0 编码约束

- 可用：C++14 全部特性，例如 `std::make_unique`、泛型 lambda、`std::shared_timed_mutex`、`constexpr` 放宽、变量模板。
- 避免所有 C++17/C++20 特性：`std::optional`、`std::variant`、`std::string_view`、`if constexpr`、结构化绑定、`std::shared_mutex`、`std::scoped_lock`、`std::filesystem`、`std::format`、`std::span`、`std::jthread`、`std::pmr`、并行算法。
- 避免：`std::to_chars` / `std::from_chars` 的浮点支持（gcc 7 不完整）。
- 避免：`std::pmr`（gcc 9+）、并行算法（需要 TBB）。
- 链接时确认 `_GLIBCXX_USE_CXX11_ABI` 与目标系统一致（gcc 5+ 双 ABI）。
- aarch64 上部分库需要 `-latomic`。
- 如某个三方库在 gcc 7.3 下编译失败，优先降级该库或改用系统包，不要强行升级目标编译器。

## 1. 许可证分级与处理策略

| 等级 | 许可证 | 策略 | 典型组件 |
|---|---|---|---|
| 绿 | Apache-2.0、MIT、BSD-2/3-Clause、ISC、zlib、Public Domain | 可直接链接与分发；保留版权声明、LICENSE 与 NOTICE | gRPC、protobuf、nlohmann/json、SQLite |
| 黄 | MPL-2.0、LGPL-2.1/3.0、EPL-2.0、CDDL | 默认不链接；确需使用时动态链接、不修改源码、独立目录、单独合规评审 | libbpf（可选 BSD-2）、elfutils |
| 红 | GPL-2.0/3.0、AGPL-3.0、SSPL、BSL、Elastic License、Commons Clause | 不链接、不复制；只能作为独立进程的外部工具，且不随产品分发 | Grafana、gdb、perf、binutils |

补充说明：

- **“独立进程调用”不等于“无义务”**：如果离线安装包内打包了 GPL 工具，仍需随包提供该工具的源码与许可证。最稳妥的做法是**不打包**，由现场按 OS 包管理器安装。
- **双许可证组件选择宽松分支**：`zstd` 选 BSD-3-Clause，`libbpf` 选 BSD-2-Clause。
- **传递依赖也要审计**：gRPC 会带入 BoringSSL/OpenSSL、c-ares、re2、zlib、abseil、protobuf，均为宽松许可证；但仍需在 NOTICE 中列全。
- **禁止“看起来开源”的许可证**：SSPL、BSL、Elastic License、Commons Clause 不属于 OSI 认可的开源许可证，不能进入白名单。

## 2. 业务进程内依赖（SDK）

设计目标：**热路径零三方库、零动态分配、零锁、零阻塞**。

| 依赖 | 版本 | 许可证 | 用途 | 是否热路径 | 说明 |
|---|---|---|---|---|---|
| C++14 标准库 | 随编译器 | — | 基础容器、原子、时间 | 是 | 仅用无分配/低分配子集 |
| nlohmann/json | 3.12.0 | MIT | 控制通道、配置解析 | 否 | header-only；只在控制面使用 |
| gRPC C++ | **与目标系统一致** | Apache-2.0 | Client/Server Interceptor | 调用路径 | 必须复用目标系统版本，禁止另带 |
| protobuf | **与目标 gRPC 匹配** | BSD-3-Clause | gRPC 消息 | 调用路径 | 随目标系统，不单独引入 |
| abseil-cpp | **由 gRPC/protobuf 固定** | Apache-2.0 | gRPC 传递依赖 | 调用路径 | 不独立锁版本 |

明确**不放入**业务进程的依赖：

- OpenSSL、zlib、zstd、RE2、SQLite、cpp-httplib、libunwind、GoogleTest、Google Benchmark。
- OpenTelemetry C++ SDK（体积大、依赖链长、版本冲突风险高）。
- 任何 GPL/AGPL/LGPL 库。

> 如果目标程序无法重新编译，则走 L0：业务进程**零链接**，只由 Agent 通过 `/proc`、日志文件、core、eBPF（可选）采集。

## 3. M1 框架阶段依赖（Agent / Server）

这些依赖只进入我们自己的二进制，不进入业务进程。

| 组件 | 建议版本 | 许可证 | 用途 | 交付形态 | 下载链接 |
|---|---|---|---|---|---|
| CMake | 3.25+ | BSD-3-Clause | 构建 | 工具 | [cmake.org/download](https://cmake.org/download/) |
| Ninja | 1.11+ | Apache-2.0 | 构建 | 工具 | [ninja releases](https://github.com/ninja-build/ninja/releases) |
| GCC / Clang | GCC 7.3+ / Clang 5+ | GPL-3.0+runtime exception / Apache-2.0 | C++14 编译 | 工具链 | [GCC](https://gcc.gnu.org/releases.html) / [LLVM](https://github.com/llvm/llvm-project/releases) |
| nlohmann/json | 3.12.0 | MIT | JSON 编解码 | header-only | [v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0) |
| cpp-httplib | 0.64.0 | MIT | HTTP 服务端/客户端 | header-only | [v0.64.0](https://github.com/yhirose/cpp-httplib/releases/tag/v0.64.0) |
| SQLite | 3.45+（amalgamation） | Public Domain | 本地 WAL、资产目录 | 源码 | [sqlite.org/download](https://sqlite.org/download.html) |
| toml++ | 3.4.0 | MIT | 配置解析 | header-only | [v3.4.0](https://github.com/marzer/tomlplusplus/releases/tag/v3.4.0) |
| CLI11 | 2.7.2 | BSD-3-Clause | 命令行工具 | header-only | [v2.7.2](https://github.com/CLIUtils/CLI11/releases/tag/v2.7.2) |
| xxHash | 0.8.4 | BSD-2-Clause | ID、指纹、校验 | header-only 模式 | [v0.8.4](https://github.com/Cyan4973/xxHash/releases/tag/v0.8.4) |
| RE2 | 2025-11-05 | BSD-3-Clause | 日志字段提取（M2） | 源码 | [2025-11-05](https://github.com/google/re2/releases/tag/2025-11-05) |
| zstd | 1.5.7 | BSD-3-Clause | WAL/分段压缩（可选） | 源码 | [v1.5.7](https://github.com/facebook/zstd/releases/tag/v1.5.7) |
| zlib | 1.3.2 | zlib | HTTP 压缩（可选） | 源码 | [v1.3.2](https://github.com/madler/zlib/releases/tag/v1.3.2) |
| OpenSSL | 3.0+（优先 3.5 LTS） | Apache-2.0 | TLS、HMAC 假名化（可选） | 源码/系统包 | [openssl.org/source](https://www.openssl.org/source/) |
| libunwind | 1.8.x | MIT | 按需抓取线程栈（可选） | 源码/系统包 | [libunwind releases](https://github.com/libunwind/libunwind/releases) |
| GoogleTest | 1.12.1 | BSD-3-Clause | 单元测试（不发布）；gcc 7.3 兼容 | 源码 | [release-1.12.1](https://github.com/google/googletest/releases/tag/release-1.12.1) |
| Google Benchmark | 1.10.0 | Apache-2.0 | 开销基准（不发布） | 源码 | [v1.10.0](https://github.com/google/benchmark/releases/tag/v1.10.0) |
| nfpm | 2.x | MIT | 离线包制作（可选） | 二进制 | [nfpm releases](https://github.com/goreleaser/nfpm/releases) |

说明：

- **SQLite 用 amalgamation**：`sqlite3.c` + `sqlite3.h` 两个文件，零构建系统依赖，最适合离线。
- **cpp-httplib**：默认不需要 OpenSSL/zlib；只有启用 HTTPS 或 gzip 时才链接。
- **toml++ 与 yaml-cpp 二选一**：推荐 toml++（header-only、MIT）；如果运维更习惯 YAML，再引 yaml-cpp（MIT）。
- **RE2 会带入 abseil**：只放在 Agent/Server，不要放进业务进程。

## 4. 目标进程内 gRPC 适配（复用，不新增）

| 组件 | 版本要求 | 许可证 | 说明 | 链接 |
|---|---|---|---|---|
| gRPC C++ | **必须与目标系统完全一致** | Apache-2.0 | 使用目标系统已有的 `grpc++` 头文件与库；不要静态链接第二份 | [grpc releases](https://github.com/grpc/grpc/releases) |
| protobuf | 与目标 gRPC 的 pin 一致（当前 3.0） | BSD-3-Clause | 随 gRPC；不要独立升级 | [protobuf releases](https://github.com/protocolbuffers/protobuf/releases) |
| abseil-cpp | 由 gRPC/protobuf 固定 | Apache-2.0 | 不要独立锁版本 | [abseil releases](https://github.com/abseil/abseil-cpp/releases) |
| POCO | 与目标系统一致 | BSL-1.0 | 线程池/任务队列封装；提供 drop-in adapter 传递 `causality.id` | [poco releases](https://github.com/pocoproject/poco/releases) |

**关键风险**：libvmon 如果自带一份 gRPC/protobuf/abseil 并静态链接进业务进程，会与目标系统已有版本产生 ODR/ABI 冲突。正确做法是：

1. gRPC 适配器以**源码或对象库**形式随目标工程编译；
2. 编译时使用目标工程的 gRPC 头文件与编译选项；
3. 通过特性检测/条件编译适配不同 gRPC 版本的 interceptor API；
4. 不把 gRPC 静态库打进 libvmon 的发布包。

## 5. M1 运行时组件（独立进程，不链接进业务）

| 组件 | 版本 | 许可证 | 用途 | 下载链接 |
|---|---|---|---|---|
| VictoriaMetrics | v1.155.0 | Apache-2.0 | 指标存储 | [v1.155.0](https://github.com/VictoriaMetrics/VictoriaMetrics/releases/tag/v1.155.0) |
| VictoriaLogs | v1.53.0 | Apache-2.0 | 日志存储（独立仓库，版本号与 VM 不同） | [VictoriaLogs v1.53.0](https://github.com/VictoriaMetrics/VictoriaLogs/releases/tag/v1.53.0) |
| Jaeger | v2.22.0 | Apache-2.0 | Trace 存储与查询 | [v2.22.0](https://github.com/jaegertracing/jaeger/releases/tag/v2.22.0) |
| LLVM / llvm-symbolizer | 与目标 OS 匹配 | Apache-2.0 with LLVM exception | 离线符号化（M4） | [LLVM releases](https://github.com/llvm/llvm-project/releases) |
| Alertmanager | v0.34.2 | Apache-2.0 | 告警通知（M6，可选） | [v0.34.2](https://github.com/prometheus/alertmanager/releases/tag/v0.34.2) |
| ClickHouse | 26.x stable | Apache-2.0 | 仅日增量 >100 GB 时启用 | [ClickHouse releases](https://github.com/ClickHouse/ClickHouse/releases) |
| Perses | 最新稳定版 | Apache-2.0 | 通用看板（可选，替代 Grafana） | [Perses releases](https://github.com/perses/perses/releases) |

Java 运行时：Jaeger v2 是 Go 二进制，不需要 JVM。若后续引入需要 JVM 的组件，再单独评估。

## 6. 后续阶段 / 可选依赖

| 组件 | 版本 | 许可证 | 阶段 | 用途 | 链接 |
|---|---|---|---|---|---|
| libbpf | v1.8.0 | LGPL-2.1 **OR** BSD-2-Clause | M3 | eBPF 弱边推断；选 BSD-2 分支 | [v1.8.0](https://github.com/libbpf/libbpf/releases/tag/v1.8.0) |
| bpftool | v7.8.1 | GPL-2.0（以仓库为准） | M3 | 外部调试工具；**不打包** | [v7.8.1](https://github.com/libbpf/bpftool/releases/tag/v7.8.1) |
| Apache Arrow | 26.0.0 | Apache-2.0 | 可选 | Parquet 冷存储；依赖重，M1 不启用 | [apache-arrow-26.0.0](https://github.com/apache/arrow/releases/tag/apache-arrow-26.0.0) |
| Crashpad | 最新稳定版 | Apache-2.0 | M4 可选 | minidump 兜底；需从源码构建 | [crashpad](https://github.com/chromium/crashpad) |
| Breakpad | 最新稳定版 | BSD-3-Clause | M4 可选 | minidump 兜底 | [breakpad](https://chromium.googlesource.com/breakpad/breakpad/) |
| cpptrace | v1.0.4 | MIT | 可选 | C++ 栈回溯；**不用于信号处理器** | [v1.0.4](https://github.com/jeremy-rifkin/cpptrace/releases/tag/v1.0.4) |
| backward-cpp | v1.6 | MIT | 可选 | 栈回溯 | [v1.6](https://github.com/bombela/backward-cpp/releases/tag/v1.6) |
| concurrentqueue | v1.0.5 | BSD-2-Clause | 可选 | Agent 内部 MPMC 队列 | [v1.0.5](https://github.com/cameron314/concurrentqueue/releases/tag/v1.0.5) |
| fmt | 12.2.0 | MIT | 可选 | 格式化；C++20 可用 `std::format` 替代 | [12.2.0](https://github.com/fmtlib/fmt/releases/tag/12.2.0) |
| yaml-cpp | 0.9.0 | MIT | 可选 | YAML 配置（与 toml++ 二选一） | [yaml-cpp-0.9.0](https://github.com/jbeder/yaml-cpp/releases/tag/yaml-cpp-0.9.0) |
| uPlot | 1.6.32 | MIT | M2 | 前端时序图 | [1.6.32](https://github.com/leeoniya/uPlot/releases/tag/1.6.32) |
| htmx | 4.0.0 | BSD-2-Clause | 可选 | 轻量前端交互 | [v4.0.0](https://github.com/bigskysoftware/htmx/releases/tag/v4.0.0) |
| OpenTelemetry C++ | v1.30.0 | Apache-2.0 | 可选 | **仅 Agent/Server**；不进 SDK | [v1.30.0](https://github.com/open-telemetry/opentelemetry-cpp/releases/tag/v1.30.0) |
| opentelemetry-proto | v1.11.1 | Apache-2.0 | 可选 | OTLP proto 定义；仅在需要 protobuf 编码时引入 | [v1.11.1](https://github.com/open-telemetry/opentelemetry-proto/releases/tag/v1.11.1) |

## 7. 不建议引入 / 需隔离

| 组件 | 许可证 | 结论 | 替代方案 |
|---|---|---|---|
| Grafana | AGPL-3.0 | 默认不引入；不作为产品内置组件 | 自研 UI + uPlot；或 Perses（Apache-2.0） |
| gdb | GPL-3.0 | 外部工具；默认不 attach 生产进程 | 崩溃用 core + llvm-symbolizer 离线分析 |
| perf | GPL-2.0 | 外部工具；按需、限时、审批 | 自研 `SIGPROF` 采样 + eBPF（可选） |
| binutils（addr2line/objdump） | GPL-3.0 | 外部工具；不链接、不打包 | llvm-symbolizer / llvm-readelf（Apache-2.0） |
| elfutils / libdw | GPL-3.0 / LGPL-3.0 | 不链接 | LLVM 工具链 |
| bpftool | GPL-2.0 | 外部工具；不打包 | 用 libbpf（BSD-2）直接加载 |
| Metabase | AGPL-3.0 | 不引入 | 自研 UI |
| Kibana / OpenSearch Dashboards | Elastic License / Apache-2.0 | 体量与栈不匹配 | 自研 UI |
| Redis | RSALv2/SSPL/AGPL 变化频繁 | 不引入 | SQLite + 本地文件 |
| 任何 “open core” 企业版组件 | 混合许可证 | 不引入 | 只用 Apache-2.0 核心功能 |

## 8. 版本兼容与离线构建注意事项

### 8.1 gRPC 版本是首要约束

- libvmon 的 gRPC 适配器必须与目标系统 **同版本、同编译选项、同 ABI**。
- 先确认目标系统的：
  - `grpc++` 版本（`pkg-config --modversion grpc++` 或源码 tag）；
  - `protoc --version`；
  - 是否使用 BoringSSL 还是 OpenSSL；
  - 是否启用异常/RTTI；
  - 是否使用 custom allocator。
- 如果目标系统 gRPC 版本较老（例如 1.5x），不要用最新 1.85 直接替换；通过条件编译适配 interceptor API 差异。
- 目标 protobuf 为 **3.0**（2016 年版本），与新版 gRPC 不兼容。不要升级目标 protobuf；适配器必须使用目标工程的头文件与库。
- 如果目标 gRPC 版本过老、**没有 C++ interceptor API**，按以下顺序降级：
  1. 在公共 gRPC 封装层手工注入/提取 metadata（仍属 L1，只改一处）；
  2. 节点级 gRPC sidecar/proxy（零代码，但多一跳）；
  3. 只做 L0 的日志序列还原与 eBPF 弱边推断。
- 先用一个小 demo 验证目标 gRPC 是否支持 interceptor，再决定 L1 的实现方式。

### 8.2 不把 OpenTelemetry C++ 放进 SDK

- OTel C++ 依赖 abseil、protobuf、gRPC、curl、nlohmann、prometheus-cpp 等，版本耦合强。
- 建议：**协议保持 OTLP 兼容，但 SDK 不链接 OTel C++**。
- 两条可选路径：
  1. SDK 通过本地 UDS 把紧凑二进制事件发给 Agent，由 Agent 转 OTLP/JSON 或存储原生协议；
  2. Agent/Server 内部使用 `opentelemetry-proto` 的 proto 定义，自行编码，不引入完整 OTel C++ SDK。
- 如果后续确实需要 OTel C++，只放在 Agent/Server，并锁定与 gRPC 兼容的版本。

### 8.3 减少依赖的推荐做法

- 指标上报：VictoriaMetrics 的 `/api/v1/import/prometheus` 文本协议，无需 protobuf。
- 日志上报：VictoriaLogs 的 `/insert/jsonline`，无需 protobuf。
- Trace 上报：Jaeger 的 OTLP/HTTP JSON 接收端，无需 gRPC/protobuf。
- 这样 Agent/Server 可以完全不依赖 gRPC/protobuf，进一步降低构建与许可证风险。
- 如果后续流量增大，再把 Agent→Server 的传输切换为 OTLP/gRPC 或 protobuf，接口不变。

### 8.4 离线打包

建议目录结构：

```
third_party/
├── deps.tsv              # 名称、版本、许可证、URL、SHA256、交付形态
├── downloads/            # 原始压缩包
├── src/                  # 解压后的源码
├── bin/                  # 预编译二进制（VM/VL/Jaeger 等）
├── licenses/             # 每个组件的 LICENSE 原文
└── notices/              # NOTICE 与第三方声明
```

- 所有下载物记录 SHA256；有 GPG 签名的优先校验签名。
- 源码包优先，避免“联网构建时再拉依赖”。
- gRPC 若必须自行构建，用 `git clone --recursive --branch <tag>` 在联网机器上准备完整源码树；GitHub 自动生成的源码包**不包含 submodule**。
- CMake 离线构建使用 `FETCHCONTENT_SOURCE_DIR_<NAME>` 指向 `third_party/src/<name>`，或 `CMAKE_PREFIX_PATH` 指向预编译安装目录。
- 不要把 `/usr/local` 下的系统库混入发布包；用 `ldd` 检查最终二进制的动态依赖。

### 8.5 许可证合规落地

- 每个源文件加 SPDX 头，例如：`SPDX-License-Identifier: Apache-2.0`。
- 仓库根目录保留 `LICENSE`、`THIRD_PARTY_NOTICES.md`、`LICENSES/`。
- Apache-2.0 组件保留其 `NOTICE` 文件。
- CI 增加许可证扫描（如 ScanCode Toolkit，Apache-2.0），阻止红色许可证进入依赖树。
- 外部 GPL 工具（gdb/perf/binutils）只作为现场可选工具，不进入安装包；如必须打包，随包提供源码与许可证。
- 若项目未来开源，建议自身采用 Apache-2.0，与 gRPC、OTel、VictoriaMetrics 生态兼容。

## 9. 对生产环境的影响控制（依赖视角）

| 约束 | 做法 |
|---|---|
| 业务进程不因维测崩溃 | SDK 全部 API `noexcept`；初始化失败自动降级为 disabled |
| 业务进程不因维测阻塞 | 每线程 SPSC 环形队列；队列满丢弃并计数；绝不等待 |
| 业务进程不因维测变慢 | 热路径不 malloc、不加锁、不做 IO、不做 DNS、不做压缩 |
| 业务进程不因维测改变行为 | gRPC 拦截器只追加 metadata；失败时 RPC 继续；不重试、不改 deadline |
| 不引入 ABI 冲突 | gRPC/protobuf/abseil 复用目标系统版本；不静态链接第二份 |
| 不拖垮节点 | Agent/Server 独立 systemd + cgroup 限额；OOM 优先杀 Agent |
| 不占满磁盘 | 本地 WAL 有上限；磁盘水位触发丢弃策略；绝不影响业务写盘 |
| 不干扰日志 | 只读 tail，不加锁、不轮转、不删除业务日志 |
| 可一键回滚 | 全局开关 + 环境变量禁用；卸载 SDK/Agent 后业务行为与接入前一致 |
| 诊断不伤业务 | 默认只读；gdb/ptrace/perf/eBPF 默认关闭，需审批与限时 |

更完整的“生产零影响”约束见 [production-safety.md](production-safety.md)。

## 10. 待确认信息

请优先确认以下信息，我才能把版本和下载清单锁死：

1. 目标系统的 Linux 发行版、CPU 架构、glibc 版本（决定二进制兼容性）。
2. 目标系统的 gRPC C++ 版本、protobuf 版本、abseil 版本；是否使用 BoringSSL/OpenSSL。
3. 目标程序是否可以重新编译；是否存在统一的日志库、gRPC 封装、线程池基础库。
4. 是否允许在节点上部署独立 Agent 进程；是否有 systemd/cgroup 管理。
5. 内核版本是否支持 eBPF/CO-RE；`perf_event_paranoid` 与容器权限现状。
6. 是否已有 Grafana/Prometheus/ELK 等现成组件；是否允许复用。
7. 日志日增量、保留期、中心节点磁盘预算。
8. 项目自身计划采用的许可证（建议 Apache-2.0）。
9. 是否允许对外分发；若只内部使用，GPL 工具的边界可以更宽松，但仍建议按上述白名单执行。

## 11. 下一步

下载脚本已生成：[scripts/download-third-party.sh](scripts/download-third-party.sh)

```bash
# 首期：源码 + 本机（macOS arm64）运行时二进制，本机先跑通
./scripts/download-third-party.sh --proxy http://127.0.0.1:7890

# 后续移植：Linux x86_64 目标机的运行时二进制（源码包共用，不会重复下载）
./scripts/download-third-party.sh --os linux --arch amd64

# 后续移植：Linux aarch64
./scripts/download-third-party.sh --os linux --arch arm64

# 只下载源码/头文件
./scripts/download-third-party.sh --sources

# 只打印链接，不下载
./scripts/download-third-party.sh --dry-run --os linux --arch amd64

# 下载后解压到 third_party/src 与 third_party/bin
./scripts/download-third-party.sh --os linux --arch amd64 --extract
```

`--os` 默认取本机操作系统（macOS → `darwin`，Linux → `linux`），`--arch` 默认取本机架构。源码包与前端资源与 OS/CPU 无关，只下载一次；darwin 与 linux 的二进制分别存放，互不覆盖。

脚本会：
- 下载到 `third_party/downloads/{sources,binaries,frontend}`；
- 对 VM/VictoriaLogs/Jaeger 的官方 checksum 做校验（存在则校验，失败只告警）；
- 生成 `third_party/downloads/SHA256SUMS.local`，供后续锁定版本；
- 不下载 gRPC/protobuf/abseil/POCO（复用目标系统）；
- 不下载 GPL/AGPL 工具（gdb/perf/binutils/bpftool/Grafana）。

已生成：

- `third_party/deps.tsv`：机器可读的版本/许可证/URL/SHA256 清单，由 `scripts/gen-deps.sh` 重新生成；
- `scripts/download-third-party.sh`：下载、校验、解压（支持 `--os` / `--arch`）；
- `third_party/downloads/SHA256SUMS.local`：本地文件哈希，用于离线包校验。

待补充：

- `cmake/Dependencies.cmake`：离线 `find_package` / `FetchContent` 配置；
- `THIRD_PARTY_NOTICES.md` 与 `LICENSES/` 初始文件。
