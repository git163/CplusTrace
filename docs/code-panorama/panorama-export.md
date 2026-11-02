# PROJECT_PANORAMA.md

## 1. 项目元数据
- 项目地址: /tmp/CplusTrace-panorama
- 项目名称: CplusTrace-panorama
- 编程语言: C++
- 生成时间: 2026-11-02T11:27:26.069Z

## 2. 项目概要信息
CplusTrace-panorama（vmon）是一个面向局域网、离线私有部署的 C++ 系统维测（可观测性与排障）平台：通过输入 request_id、trace_id、task_id、错误码或时间窗，自动还原跨进程、跨节点的调用链，关联各环节日志与错误，并给出可疑根因与证据包。项目由 SDK、适配器、Agent、中心 Server、Web UI、工具链及测试组成，数据流为业务进程 SDK → Agent（UDS/分段 WAL）→ Server（HTTP NDJSON）→ Web、CLI、MCP，支持调用链还原、日志聚类、规则告警、SLO、崩溃诊断、性能剖析、证据包与 MCP 接口。

## 3. 项目所有文件列表
- 全量文件数: 184
- 过滤后代码文件数: 145
```text
.claude/settings.json
.gitignore
CMakeLists.txt
README.md
adapters/bpf/weak_edge.h
adapters/grpc/propagation.cpp
adapters/grpc/propagation.h
adapters/grpc/streaming.h
adapters/ipc/socket_header.h
agent/src/agent.cpp
agent/src/agent.h
agent/src/command_runner.cpp
agent/src/command_runner.h
agent/src/control_client.cpp
agent/src/control_client.h
agent/src/crash_collect.cpp
agent/src/crash_collect.h
agent/src/ingest.cpp
agent/src/ingest.h
agent/src/log_tail.cpp
agent/src/log_tail.h
agent/src/main.cpp
agent/src/metrics.cpp
agent/src/metrics.h
agent/src/probe.cpp
agent/src/probe.h
agent/src/process_scan.cpp
agent/src/process_scan.h
agent/src/uploader.cpp
agent/src/uploader.h
agent/src/wal.cpp
agent/src/wal.h
agent/tests/control_command_test.cpp
agent/tests/ingest_test.cpp
agent/tests/log_tail_test.cpp
agent/tests/metrics_test.cpp
agent/tests/probe_test.cpp
agent/tests/process_scan_test.cpp
agent/tests/uploader_crash_test.cpp
agent/tests/wal_test.cpp
clients/python/README.md
clients/python/__pycache__/test_vmon_trace.cpython-314.pyc
clients/python/__pycache__/vmon_trace.cpython-314.pyc
clients/python/test_vmon_trace.py
clients/python/vmon_trace.py
clients/web/README.md
clients/web/test_vmon_trace.js
clients/web/vmon-trace.js
cmake/vmon.cmake
common/json_event.h
common/sha256.cpp
common/sha256.h
deploy/agent.json
deploy/server.json
deploy/systemd/vmon-agent.service
deploy/systemd/vmon-server.service
deploy/tmpfiles-vmon.conf
docs/.DS_Store
docs/client-observability.md
docs/competitive-analysis.md
docs/deployment.md
docs/feature-gap-design.md
docs/guance-benchmark.md
docs/module-design.md
docs/presentation/README.md
docs/presentation/build_deck.py
docs/presentation/render_preview.py
docs/presentation/requirements.txt
docs/presentation/vmon-intro.pptx
docs/production-safety.md
docs/quick-diagnosis.md
docs/scenario-cookbook.md
docs/third-party-deps.md
docs/ui-test-report.md
docs/user-guide.md
docs/verification.md
examples/README.md
examples/hello/CMakeLists.txt
examples/hello/main.cpp
examples/hello/run_demo.sh
examples/http_backend/CMakeLists.txt
examples/http_backend/main.cpp
examples/scenarios/README.md
examples/scenarios/demo.py
plan_v1.0.md
scripts/download-third-party.sh
scripts/gen-deps.sh
scripts/package.sh
scripts/vmon.sh
sdk/include/vmon/vmon.h
sdk/src/control/control_server.cpp
sdk/src/control/control_server.h
sdk/src/core/clock.h
sdk/src/core/config.h
sdk/src/core/event.h
sdk/src/core/ids.h
sdk/src/core/registry.cpp
sdk/src/core/registry.h
sdk/src/core/ring_buffer.h
sdk/src/core/thread_context.h
sdk/src/crash/crash_handler.cpp
sdk/src/crash/crash_handler.h
sdk/src/log/log.cpp
sdk/src/profiler/cpu_profiler.cpp
sdk/src/profiler/cpu_profiler.h
sdk/src/trace/span.cpp
sdk/src/transport/sender.cpp
sdk/src/transport/sender.h
sdk/src/vmon.cpp
server/src/api.cpp
server/src/api.h
server/src/catalog.cpp
server/src/catalog.h
server/src/commands.cpp
server/src/commands.h
server/src/config.cpp
server/src/config.h
server/src/crash.cpp
server/src/crash.h
server/src/dashboard.cpp
server/src/dashboard.h
server/src/diagnostics.cpp
server/src/diagnostics.h
server/src/evidence.cpp
server/src/evidence.h
server/src/forwarder.cpp
server/src/forwarder.h
server/src/issues.cpp
server/src/issues.h
server/src/log_cluster.cpp
server/src/log_cluster.h
server/src/main.cpp
server/src/model.h
server/src/notifier.cpp
server/src/notifier.h
server/src/pipeline.cpp
server/src/pipeline.h
server/src/profiler.cpp
server/src/profiler.h
server/src/rules.cpp
server/src/rules.h
server/src/serialize.cpp
server/src/serialize.h
server/src/silence.cpp
server/src/silence.h
server/src/slo.cpp
server/src/slo.h
server/src/store.cpp
server/src/store.h
server/src/trace_builder.cpp
server/src/trace_builder.h
server/src/util.h
server/tests/api_test.cpp
server/tests/commands_test.cpp
server/tests/dashboard_test.cpp
server/tests/evidence_test.cpp
server/tests/issues_test.cpp
server/tests/log_cluster_test.cpp
server/tests/notifier_test.cpp
server/tests/pipeline_test.cpp
server/tests/profiler_crash_test.cpp
server/tests/rules_test.cpp
server/tests/silence_test.cpp
server/tests/slo_test.cpp
server/tests/store_test.cpp
server/tests/trace_test.cpp
symbolizer/symbolizer.cpp
symbolizer/symbolizer.h
tests/CMakeLists.txt
tests/e2e/__pycache__/run_e2e.cpython-314.pyc
tests/e2e/e2e_client.cpp
tests/e2e/run_e2e.py
tests/propagation_test.cpp
tests/ring_buffer_test.cpp
tests/socket_header_test.cpp
tests/span_test.cpp
tests/test_util.h
tools/CMakeLists.txt
tools/cli/vmonctl.cpp
tools/crash-helper/vmon_crash_helper.cpp
tools/mcp/vmon_mcp.cpp
web/app.js
web/index.html
web/style.css
```

## 4. 函数调用链（JSON）
```json
{
  "maxDepth": 2,
  "entryPoint": "server/src/main.cpp",
  "records": [
    {
      "nodeId": "n_0_1_root_main",
      "functionName": "main",
      "file": "server/src/main.cpp",
      "line": 39,
      "depth": 0,
      "drillFlag": 1,
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/main.cpp",
        "step1-猜测文件-命中: server/src/main.cpp:L39"
      ]
    },
    {
      "nodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "functionName": "vmon::server::load_server_config",
      "file": "server/src/config.cpp",
      "depth": 1,
      "drillFlag": 0,
      "parentNodeId": "n_0_1_root_main",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/config.cpp",
        "step1-猜测文件-命中: server/src/config.cpp"
      ]
    },
    {
      "nodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "functionName": "vmon::server::register_routes",
      "file": "server/src/api.cpp",
      "line": 981,
      "depth": 1,
      "drillFlag": 1,
      "parentNodeId": "n_0_1_root_main",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/api.cpp",
        "step1-猜测文件-命中: server/src/api.cpp:L981"
      ]
    },
    {
      "nodeId": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
      "functionName": "vmon::server::ServerContext::start_background",
      "file": "server/src/api.cpp",
      "line": 954,
      "depth": 1,
      "drillFlag": 1,
      "parentNodeId": "n_0_1_root_main",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/api.cpp",
        "step1-猜测文件-命中: server/src/api.cpp:L954"
      ]
    },
    {
      "nodeId": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
      "functionName": "vmon::server::ServerContext::stop_background",
      "file": "server/src/api.cpp",
      "line": 961,
      "depth": 1,
      "drillFlag": 0,
      "parentNodeId": "n_0_1_root_main",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/api.cpp",
        "step1-猜测文件-命中: server/src/api.cpp:L961"
      ]
    },
    {
      "nodeId": "n_1_6_n_0_1_root_main_httplib-server-listen_after_bind",
      "functionName": "httplib::Server::listen_after_bind",
      "file": "server/src/main.cpp",
      "depth": 1,
      "drillFlag": -1,
      "parentNodeId": "n_0_1_root_main",
      "status": "skipped",
      "locateAttempts": [
        "step1b-同文件兜底-候选(1): server/src/main.cpp",
        "step1b-同文件兜底-未命中(已扫描1个文件)",
        "step2-AI基于文件列表猜测Top3",
        "step2-命中系统/库函数标记: httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。"
      ],
      "error": "系统/库函数，停止下钻：httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。"
    },
    {
      "nodeId": "n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream",
      "functionName": "std::ifstream::ifstream",
      "file": "server/src/config.cpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator",
      "functionName": "nlohmann::json::operator>>",
      "file": "third_party/nlohmann/json.hpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains",
      "functionName": "nlohmann::json::contains",
      "file": "third_party/nlohmann/json.hpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string",
      "functionName": "nlohmann::json::get<std::vector<std::string>>",
      "file": "third_party/nlohmann/json.hpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value",
      "functionName": "nlohmann::json::value",
      "file": "third_party/nlohmann/json.hpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "functionName": "server.Post",
      "file": "server/src/api.cpp",
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): third_party/httplib.h",
        "step1-猜测文件-未命中(已扫描1个文件)",
        "step1b-同文件兜底-候选(1): server/src/api.cpp",
        "step1b-同文件兜底-命中: server/src/api.cpp"
      ]
    },
    {
      "nodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "functionName": "server.Get",
      "file": "server/src/api.cpp",
      "line": 1123,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): third_party/httplib.h",
        "step1-猜测文件-未命中(已扫描1个文件)",
        "step1b-同文件兜底-候选(1): server/src/api.cpp",
        "step1b-同文件兜底-命中: server/src/api.cpp:L1123"
      ]
    },
    {
      "nodeId": "n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete",
      "functionName": "server.Delete",
      "file": "third_party/httplib.h",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options",
      "functionName": "server.Options",
      "file": "third_party/httplib.h",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point",
      "functionName": "server.set_mount_point",
      "file": "third_party/httplib.h",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
      "functionName": "Forwarder::start",
      "file": "server/src/forwarder.cpp",
      "line": 68,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/forwarder.cpp",
        "step1-猜测文件-命中: server/src/forwarder.cpp:L68"
      ]
    },
    {
      "nodeId": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
      "functionName": "Notifier::start",
      "file": "server/src/notifier.cpp",
      "line": 46,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/notifier.cpp",
        "step1-猜测文件-命中: server/src/notifier.cpp:L46"
      ]
    },
    {
      "nodeId": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
      "functionName": "ServerContext::background_loop",
      "file": "server/src/api.cpp",
      "line": 968,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/api.cpp",
        "step1-猜测文件-命中: server/src/api.cpp:L968"
      ]
    },
    {
      "nodeId": "n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join",
      "functionName": "std::thread::join",
      "file": "server/src/api.cpp",
      "depth": 2,
      "drillFlag": -1,
      "parentNodeId": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
      "status": "skipped"
    },
    {
      "nodeId": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
      "functionName": "Notifier::stop",
      "file": "server/src/notifier.cpp",
      "line": 52,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/notifier.cpp",
        "step1-猜测文件-命中: server/src/notifier.cpp:L52"
      ]
    },
    {
      "nodeId": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
      "functionName": "Forwarder::stop",
      "file": "server/src/forwarder.cpp",
      "line": 73,
      "depth": 2,
      "drillFlag": 1,
      "parentNodeId": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
      "status": "done",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/forwarder.cpp",
        "step1-猜测文件-命中: server/src/forwarder.cpp:L73"
      ]
    },
    {
      "nodeId": "n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized",
      "functionName": "authorized",
      "file": "server/src/api.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process",
      "functionName": "ctx.pipeline.process",
      "file": "server/src/diagnostics.h",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "status": "queued"
    },
    {
      "nodeId": "n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest",
      "functionName": "ctx.store.ingest",
      "file": "server/src/diagnostics.h",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "status": "queued"
    },
    {
      "nodeId": "n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event",
      "functionName": "ctx.rules.on_event",
      "file": "server/src/diagnostics.h",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "status": "queued"
    },
    {
      "nodeId": "n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit",
      "functionName": "ctx.forwarder.submit",
      "file": "server/src/diagnostics.h",
      "depth": 3,
      "drillFlag": 0,
      "parentNodeId": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
      "status": "queued"
    },
    {
      "nodeId": "n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard",
      "functionName": "authorized/guard",
      "file": "server/src/api.cpp",
      "depth": 3,
      "drillFlag": 0,
      "parentNodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "status": "queued"
    },
    {
      "nodeId": "n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces",
      "functionName": "ctx.store.query_traces",
      "file": "server/src/store.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "status": "queued"
    },
    {
      "nodeId": "n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs",
      "functionName": "ctx.store.query_logs",
      "file": "server/src/store.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "status": "queued"
    },
    {
      "nodeId": "n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json",
      "functionName": "trace_detail_json",
      "file": "server/src/serialize.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "status": "queued"
    },
    {
      "nodeId": "n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters",
      "functionName": "ctx.store.log_clusters",
      "file": "server/src/store.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
      "status": "queued"
    },
    {
      "nodeId": "n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run",
      "functionName": "Forwarder::run",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
      "status": "queued"
    },
    {
      "nodeId": "n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread",
      "functionName": "std::thread",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange",
      "functionName": "std::atomic<bool>::exchange",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled",
      "functionName": "Notifier::enabled",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange",
      "functionName": "std::atomic::exchange",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread",
      "functionName": "std::thread::thread",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
      "status": "queued"
    },
    {
      "nodeId": "n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate",
      "functionName": "RuleEngine::evaluate",
      "file": "server/src/rule_engine.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
      "status": "queued"
    },
    {
      "nodeId": "n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune",
      "functionName": "Store::prune",
      "file": "server/src/store.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
      "status": "queued"
    },
    {
      "nodeId": "n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns",
      "functionName": "now_ns",
      "file": "server/src/util.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange",
      "functionName": "std::atomic<bool>::exchange",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all",
      "functionName": "std::condition_variable::notify_all",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable",
      "functionName": "std::thread::joinable",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join",
      "functionName": "std::thread::join",
      "file": "server/src/notifier.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
      "status": "queued"
    },
    {
      "nodeId": "n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all",
      "functionName": "std::condition_variable::notify_all",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
      "status": "skipped",
      "error": "系统/库函数，停止下钻：std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。",
      "locateAttempts": [
        "step1-猜测文件-候选(1): server/src/forwarder.cpp",
        "step1-猜测文件-未命中(已扫描1个文件)",
        "step1b-同文件兜底-候选(1): server/src/forwarder.cpp",
        "step1b-同文件兜底-未命中(已扫描1个文件)",
        "step2-AI基于文件列表猜测Top3",
        "step2-命中系统/库函数标记: std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。"
      ]
    },
    {
      "nodeId": "n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join",
      "functionName": "std::thread::join",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": -1,
      "parentNodeId": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
      "status": "skipped"
    },
    {
      "nodeId": "n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once",
      "functionName": "Forwarder::flush_once",
      "file": "server/src/forwarder.cpp",
      "depth": 3,
      "drillFlag": 1,
      "parentNodeId": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
      "status": "queued"
    }
  ],
  "graph": {
    "nodes": [
      {
        "id": "n_0_1_root_main",
        "label": "main",
        "file": "server/src/main.cpp",
        "line": 39,
        "depth": 0,
        "description": "vmon-server 进程入口：解析 CLI、加载并合并配置、初始化 HTTP 服务与路由、启动后台线程、等待信号并优雅关停。"
      },
      {
        "id": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "label": "vmon::server::load_server_config",
        "file": "server/src/config.cpp",
        "depth": 1,
        "description": "从JSON配置文件加载服务端配置到ServerConfig结构体，包含基础字段、pipeline规则和webhook配置解析"
      },
      {
        "id": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "label": "vmon::server::register_routes",
        "file": "server/src/api.cpp",
        "line": 981,
        "depth": 1,
        "description": "向 httplib::Server 注册所有 REST/MCP/Web 路由，并为各路由绑定鉴权与业务处理逻辑"
      },
      {
        "id": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
        "label": "vmon::server::ServerContext::start_background",
        "file": "server/src/api.cpp",
        "line": 954,
        "depth": 1,
        "description": "启动后台服务：通过原子标志防止重复启动，启动转发器和通知器，并创建后台循环线程。"
      },
      {
        "id": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
        "label": "vmon::server::ServerContext::stop_background",
        "file": "server/src/api.cpp",
        "line": 961,
        "depth": 1,
        "description": "停止后台线程与各后台组件，先关运行标志再 join 线程，最后停止通知器与转发器"
      },
      {
        "id": "n_1_6_n_0_1_root_main_httplib-server-listen_after_bind",
        "label": "httplib::Server::listen_after_bind",
        "file": "server/src/main.cpp",
        "depth": 1,
        "description": "系统/库函数，停止下钻：httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。"
      },
      {
        "id": "n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream",
        "label": "std::ifstream::ifstream",
        "file": "server/src/config.cpp",
        "depth": 2,
        "description": "打开配置文件输入流"
      },
      {
        "id": "n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator",
        "label": "nlohmann::json::operator>>",
        "file": "third_party/nlohmann/json.hpp",
        "depth": 2,
        "description": "从输入流解析JSON对象"
      },
      {
        "id": "n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains",
        "label": "nlohmann::json::contains",
        "file": "third_party/nlohmann/json.hpp",
        "depth": 2,
        "description": "检查JSON键是否存在，驱动字段提取流程"
      },
      {
        "id": "n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string",
        "label": "nlohmann::json::get<std::vector<std::string>>",
        "file": "third_party/nlohmann/json.hpp",
        "depth": 2,
        "description": "将noise_patterns字段转换为字符串向量"
      },
      {
        "id": "n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value",
        "label": "nlohmann::json::value",
        "file": "third_party/nlohmann/json.hpp",
        "depth": 2,
        "description": "读取pipeline规则字段并提供默认值"
      },
      {
        "id": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "label": "server.Post",
        "file": "server/src/api.cpp",
        "depth": 2,
        "description": "httplib::Server 的路由注册方法，在 register_routes 中为 /v1/ingest、/v1/profile、/v1/crash、/v1/events 等端点绑定 POST 处理 lambda。"
      },
      {
        "id": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "label": "server.Get",
        "file": "server/src/api.cpp",
        "line": 1123,
        "depth": 2,
        "description": "httplib 框架的 GET 路由注册方法，用于将各个查询接口路径绑定到对应的处理 lambda。"
      },
      {
        "id": "n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete",
        "label": "server.Delete",
        "file": "third_party/httplib.h",
        "depth": 2,
        "description": "注册 DELETE 路由（删除 SLO、规则、静默、仪表板等）"
      },
      {
        "id": "n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options",
        "label": "server.Options",
        "file": "third_party/httplib.h",
        "depth": 2,
        "description": "注册全局 OPTIONS 预检路由并设置 CORS 头"
      },
      {
        "id": "n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point",
        "label": "server.set_mount_point",
        "file": "third_party/httplib.h",
        "depth": 2,
        "description": "若配置了 web_dir，则挂载静态 Web 资源目录"
      },
      {
        "id": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
        "label": "Forwarder::start",
        "file": "server/src/forwarder.cpp",
        "line": 68,
        "depth": 2,
        "description": "启动转发线程，设置运行标志并创建后台线程执行 run"
      },
      {
        "id": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
        "label": "Notifier::start",
        "file": "server/src/notifier.cpp",
        "line": 46,
        "depth": 2,
        "description": "启动通知器：检查启用状态与运行标志，成功后创建后台线程执行 run 循环。"
      },
      {
        "id": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
        "label": "ServerContext::background_loop",
        "file": "server/src/api.cpp",
        "line": 968,
        "depth": 2,
        "description": "后台线程主循环，周期性评估规则并裁剪存储数据"
      },
      {
        "id": "n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join",
        "label": "std::thread::join",
        "file": "server/src/api.cpp",
        "depth": 2,
        "description": "等待后台循环线程结束"
      },
      {
        "id": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
        "label": "Notifier::stop",
        "file": "server/src/notifier.cpp",
        "line": 52,
        "depth": 2,
        "description": "停止通知线程：将 running_ 置为 false，唤醒条件变量并 join 工作线程，实现优雅退出。"
      },
      {
        "id": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
        "label": "Forwarder::stop",
        "file": "server/src/forwarder.cpp",
        "line": 73,
        "depth": 2,
        "description": "停止后台转发线程：原子标记停止、唤醒等待线程、join 线程、执行一次最终批量刷新"
      },
      {
        "id": "n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized",
        "label": "authorized",
        "file": "server/src/api.cpp",
        "depth": 3,
        "description": "校验请求的 Bearer Token 或 X-Vmon-Token，作为 guard 被各 POST 处理器调用。"
      },
      {
        "id": "n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process",
        "label": "ctx.pipeline.process",
        "file": "server/src/diagnostics.h",
        "depth": 3,
        "description": "接入事件入库前的脱敏处理管道，敏感字段不会进入 Store。"
      },
      {
        "id": "n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest",
        "label": "ctx.store.ingest",
        "file": "server/src/diagnostics.h",
        "depth": 3,
        "description": "将规范化后的事件写入事件存储。"
      },
      {
        "id": "n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event",
        "label": "ctx.rules.on_event",
        "file": "server/src/diagnostics.h",
        "depth": 3,
        "description": "事件触发规则引擎评估，是告警/检测核心入口。"
      },
      {
        "id": "n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit",
        "label": "ctx.forwarder.submit",
        "file": "server/src/diagnostics.h",
        "depth": 3,
        "description": "将事件提交给转发器异步发送到外部端点。"
      },
      {
        "id": "n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard",
        "label": "authorized/guard",
        "file": "server/src/api.cpp",
        "depth": 3,
        "description": "鉴权守卫，在业务查询前校验 token，属于请求前置校验桥接节点。"
      },
      {
        "id": "n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces",
        "label": "ctx.store.query_traces",
        "file": "server/src/store.cpp",
        "depth": 3,
        "description": "按 TraceQuery 条件查询 trace 摘要，是 /api/traces 的核心数据源。"
      },
      {
        "id": "n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs",
        "label": "ctx.store.query_logs",
        "file": "server/src/store.cpp",
        "depth": 3,
        "description": "按 LogQuery 条件查询日志记录，是 /api/logs 的核心数据源。"
      },
      {
        "id": "n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json",
        "label": "trace_detail_json",
        "file": "server/src/serialize.cpp",
        "depth": 3,
        "description": "将单个 trace 的详情聚合为 JSON，供 /api/traces/{id} 返回。"
      },
      {
        "id": "n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters",
        "label": "ctx.store.log_clusters",
        "file": "server/src/store.cpp",
        "depth": 3,
        "description": "获取日志聚类结果，是 /api/logs/clusters 的核心数据源。"
      },
      {
        "id": "n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run",
        "label": "Forwarder::run",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "后台线程主循环，按间隔唤醒并批量转发事件"
      },
      {
        "id": "n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread",
        "label": "std::thread",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "创建后台工作线程"
      },
      {
        "id": "n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange",
        "label": "std::atomic<bool>::exchange",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "原子设值并返回旧值，防止重复启动"
      },
      {
        "id": "n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled",
        "label": "Notifier::enabled",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "检查通知器是否配置启用（可能基于 url/token 等配置判断）"
      },
      {
        "id": "n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange",
        "label": "std::atomic::exchange",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "原子地将 running_ 置为 true，防止重复启动"
      },
      {
        "id": "n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread",
        "label": "std::thread::thread",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "创建后台线程运行 Notifier::run 消费队列"
      },
      {
        "id": "n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate",
        "label": "RuleEngine::evaluate",
        "file": "server/src/rule_engine.cpp",
        "depth": 3,
        "description": "按时间窗口对存储数据执行规则评估，生成事件/告警"
      },
      {
        "id": "n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune",
        "label": "Store::prune",
        "file": "server/src/store.cpp",
        "depth": 3,
        "description": "按保留策略清理过期 trace 与日志记录"
      },
      {
        "id": "n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns",
        "label": "now_ns",
        "file": "server/src/util.cpp",
        "depth": 3,
        "description": "获取当前纳秒时间戳"
      },
      {
        "id": "n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange",
        "label": "std::atomic<bool>::exchange",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "原子地将 running_ 置为 false，若已停止则直接返回。"
      },
      {
        "id": "n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all",
        "label": "std::condition_variable::notify_all",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "唤醒所有等待在条件变量上的工作线程。"
      },
      {
        "id": "n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable",
        "label": "std::thread::joinable",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "检查工作线程是否可 join。"
      },
      {
        "id": "n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join",
        "label": "std::thread::join",
        "file": "server/src/notifier.cpp",
        "depth": 3,
        "description": "等待工作线程（Notifier::run 循环）退出。"
      },
      {
        "id": "n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all",
        "label": "std::condition_variable::notify_all",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "系统/库函数，停止下钻：std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。"
      },
      {
        "id": "n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join",
        "label": "std::thread::join",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "等待后台转发线程结束"
      },
      {
        "id": "n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once",
        "label": "Forwarder::flush_once",
        "file": "server/src/forwarder.cpp",
        "depth": 3,
        "description": "线程结束后执行最后一次批量刷新，把队列里剩余事件发出去"
      }
    ],
    "edges": [
      {
        "source": "n_0_1_root_main",
        "target": "n_1_2_n_0_1_root_main_vmon-server-load_server_config"
      },
      {
        "source": "n_0_1_root_main",
        "target": "n_1_3_n_0_1_root_main_vmon-server-register_routes"
      },
      {
        "source": "n_0_1_root_main",
        "target": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background"
      },
      {
        "source": "n_0_1_root_main",
        "target": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background"
      },
      {
        "source": "n_0_1_root_main",
        "target": "n_1_6_n_0_1_root_main_httplib-server-listen_after_bind"
      },
      {
        "source": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "target": "n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream"
      },
      {
        "source": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "target": "n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator"
      },
      {
        "source": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "target": "n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains"
      },
      {
        "source": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "target": "n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string"
      },
      {
        "source": "n_1_2_n_0_1_root_main_vmon-server-load_server_config",
        "target": "n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value"
      },
      {
        "source": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "target": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post"
      },
      {
        "source": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "target": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get"
      },
      {
        "source": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "target": "n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete"
      },
      {
        "source": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "target": "n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options"
      },
      {
        "source": "n_1_3_n_0_1_root_main_vmon-server-register_routes",
        "target": "n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point"
      },
      {
        "source": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
        "target": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start"
      },
      {
        "source": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
        "target": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start"
      },
      {
        "source": "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background",
        "target": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop"
      },
      {
        "source": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
        "target": "n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join"
      },
      {
        "source": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
        "target": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop"
      },
      {
        "source": "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background",
        "target": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop"
      },
      {
        "source": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "target": "n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized"
      },
      {
        "source": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "target": "n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process"
      },
      {
        "source": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "target": "n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest"
      },
      {
        "source": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "target": "n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event"
      },
      {
        "source": "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post",
        "target": "n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit"
      },
      {
        "source": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "target": "n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard"
      },
      {
        "source": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "target": "n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces"
      },
      {
        "source": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "target": "n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs"
      },
      {
        "source": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "target": "n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json"
      },
      {
        "source": "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get",
        "target": "n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters"
      },
      {
        "source": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
        "target": "n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run"
      },
      {
        "source": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
        "target": "n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread"
      },
      {
        "source": "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start",
        "target": "n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange"
      },
      {
        "source": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
        "target": "n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled"
      },
      {
        "source": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
        "target": "n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange"
      },
      {
        "source": "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start",
        "target": "n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread"
      },
      {
        "source": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
        "target": "n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate"
      },
      {
        "source": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
        "target": "n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune"
      },
      {
        "source": "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop",
        "target": "n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns"
      },
      {
        "source": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
        "target": "n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange"
      },
      {
        "source": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
        "target": "n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all"
      },
      {
        "source": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
        "target": "n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable"
      },
      {
        "source": "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop",
        "target": "n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join"
      },
      {
        "source": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
        "target": "n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all"
      },
      {
        "source": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
        "target": "n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join"
      },
      {
        "source": "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop",
        "target": "n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once"
      }
    ]
  }
}
```

## 5. Agent 状态日志
```json
[
  {
    "id": "o1ml72",
    "timestamp": 1793618845642,
    "message": "正在验证本地目录: /tmp/CplusTrace-panorama",
    "type": "info"
  },
  {
    "id": "yi0389",
    "timestamp": 1793618845935,
    "message": "本地目录验证成功",
    "type": "success"
  },
  {
    "id": "e92ir",
    "timestamp": 1793618845935,
    "message": "正在获取文件结构...",
    "type": "info"
  },
  {
    "id": "vo1v7c",
    "timestamp": 1793618846069,
    "message": "成功获取 184 个文件（代码文件 145）",
    "type": "success"
  },
  {
    "id": "8735m",
    "timestamp": 1793618846069,
    "message": "AI Agent: 正在识别主入口文件与入口函数（逐步下钻模式）...",
    "type": "thinking"
  },
  {
    "id": "te3nse",
    "timestamp": 1793618847172,
    "message": "AI 调用完成：入口识别：候选入口文件与主入口函数",
    "type": "thinking",
    "aiTrace": {
      "label": "入口识别：候选入口文件与主入口函数",
      "request": "{\n  \"prompt\": \"\\n你是一位资深软件架构师。下面是项目代码文件列表，请识别主要语言、可能入口文件和主入口函数名称（如果能推断）。\\n\\n代码文件列表：\\n[\\\"adapters/bpf/weak_edge.h\\\",\\\"adapters/grpc/propagation.cpp\\\",\\\"adapters/grpc/propagation.h\\\",\\\"adapters/grpc/streaming.h\\\",\\\"adapters/ipc/socket_header.h\\\",\\\"agent/src/agent.cpp\\\",\\\"agent/src/agent.h\\\",\\\"agent/src/command_runner.cpp\\\",\\\"agent/src/command_runner.h\\\",\\\"agent/src/control_client.cpp\\\",\\\"agent/src/control_client.h\\\",\\\"agent/src/crash_collect.cpp\\\",\\\"agent/src/crash_collect.h\\\",\\\"agent/src/ingest.cpp\\\",\\\"agent/src/ingest.h\\\",\\\"agent/src/log_tail.cpp\\\",\\\"agent/src/log_tail.h\\\",\\\"agent/src/main.cpp\\\",\\\"agent/src/metrics.cpp\\\",\\\"agent/src/metrics.h\\\",\\\"agent/src/probe.cpp\\\",\\\"agent/src/probe.h\\\",\\\"agent/src/process_scan.cpp\\\",\\\"agent/src/pro ...[还有 3458 字节]\"\n}",
      "response": "{\n  \"language\": \"C++\",\n  \"potentialEntryPoints\": [\n    \"server/src/main.cpp\",\n    \"agent/src/main.cpp\",\n    \"sdk/src/vmon.cpp\",\n    \"examples/hello/main.cpp\",\n    \"examples/http_backend/main.cpp\",\n    \"tools/cli/vmonctl.cpp\",\n    \"tools/crash-helper/vmon_crash_helper.cpp\",\n    \"tools/mcp/vmon_mcp.cpp\",\n    \"symbolizer/symbolizer.cpp\",\n    \"tests/e2e/e2e_client.cpp\"\n  ],\n  \"potentialEntryFunctionNames\": [\n    \"main\"\n  ]\n}"
    }
  },
  {
    "id": "k2pvln",
    "timestamp": 1793618847172,
    "message": "识别语言: C++",
    "type": "info"
  },
  {
    "id": "f9ycoa",
    "timestamp": 1793618847172,
    "message": "候选入口文件: server/src/main.cpp, agent/src/main.cpp, sdk/src/vmon.cpp, examples/hello/main.cpp, examples/http_backend/main.cpp",
    "type": "info"
  },
  {
    "id": "rztui",
    "timestamp": 1793618848508,
    "message": "AI 调用完成：入口校验: server/src/main.cpp",
    "type": "thinking",
    "aiTrace": {
      "label": "入口校验: server/src/main.cpp",
      "request": "{\n  \"prompt\": \"\\n你是代码入口识别器。请判断该文件是否是项目入口文件（main 启动、服务启动、React/Vue 挂载、CLI 启动等）。\\n返回 JSON。\\n\\n语言：C++\\n文件路径：server/src/main.cpp\\n文件总行数：170\\n文件内容（按行数规则截取）：\\n- 如果文件少于 2000 行：发送全文\\n- 如果文件在 2001-4000 行：发送前 2000 行\\n- 如果文件超过 4000 行：发送前 2000 行和后 2000 行\\n以下是本次发送内容：\\n// vmon-server 入口：加载配置、注册路由、启动后台规则线程与 HTTP 服务。\\n//\\n// 关停路径：SIGINT/SIGTERM 由主线程 sigwait 接收，先停止接收新连接，\\n// 再停止规则线程与转发线程，保证进程可被 systemd 优雅管理。\\n#include <atomic>\\n#include <chrono>\\n#include <conditi ...[还有 6028 字节]\"\n}",
      "response": "{\n  \"isEntryPoint\": true,\n  \"entryFunctionName\": \"main\",\n  \"reason\": \"包含 main 函数，负责加载配置、注册路由、启动后台线程与 HTTP 服务，并处理信号关停，是典型的服务端入口文件。\"\n}"
    }
  },
  {
    "id": "d798dr",
    "timestamp": 1793618848508,
    "message": "验证候选入口文件: server/src/main.cpp",
    "type": "thinking"
  },
  {
    "id": "vvyvt",
    "timestamp": 1793618848508,
    "message": "确认入口文件: server/src/main.cpp",
    "type": "success"
  },
  {
    "id": "ghtdf",
    "timestamp": 1793618850624,
    "message": "AI 调用完成：项目概要与技术栈生成",
    "type": "thinking",
    "aiTrace": {
      "label": "项目概要与技术栈生成",
      "request": "{\n  \"prompt\": \"\\n你是项目概要分析器。请根据 README、入口文件和清单文件，输出项目中文简介与技术栈（简洁）。\\n\\n仓库：CplusTrace-panorama\\n语言：C++\\n文件内容：{\\\"server/src/main.cpp\\\":\\\"// vmon-server 入口：加载配置、注册路由、启动后台规则线程与 HTTP 服务。\\\\n//\\\\n// 关停路径：SIGINT/SIGTERM 由主线程 sigwait 接收，先停止接收新连接，\\\\n// 再停止规则线程与转发线程，保证进程可被 systemd 优雅管理。\\\\n#include <atomic>\\\\n#include <chrono>\\\\n#include <condition_variable>\\\\n#include <csignal>\\\\n#include <cstdio>\\\\n#include <cstdlib>\\\\n#include <iostream>\\\\n#include <mutex>\\\\n#include <string>\\\\n#include <thread>\\\\n\\\\n#include <CLI/CLI.hpp>\\\\n#include <httplib.h>\\\\n#include <pthread.h>\\\\n\\\\n#include \\\\\\\"api.h\\\\\\\"\\\\n#include  ...[还有 24677 字节]\"\n}",
      "response": "{\n  \"summary\": \"CplusTrace-panorama（vmon）是一个面向局域网、离线私有部署的 C++ 系统维测（可观测性与排障）平台：通过输入 request_id、trace_id、task_id、错误码或时间窗，自动还原跨进程、跨节点的调用链，关联各环节日志与错误，并给出可疑根因与证据包。项目由 SDK、适配器、Agent、中心 Server、Web UI、工具链及测试组成，数据流为业务进程 SDK → Agent（UDS/分段 WAL）→ Server（HTTP NDJSON）→ Web、CLI、MCP，支持调用链还原、日志聚类、规则告警、SLO、崩溃诊断、性能剖析、证据包与 MCP 接口。\",\n  \"techStack\": [\n    \"C++14\",\n    \"CMake\",\n    \"httplib（HTTP）\",\n    \"CLI11（命令行解析）\",\n    \"POSIX 线程/UDS\",\n    \"Python 3（端到端测试与脚本）\",\n    \"静态 Web UI（原生 JS）\",\n    \"python-pptx（文档生成）\"\n  ]\n}"
    }
  },
  {
    "id": "egegjf",
    "timestamp": 1793618850624,
    "message": "【下钻】开始逐级下钻调用链（动态渲染）...",
    "type": "thinking"
  },
  {
    "id": "9oyalw",
    "timestamp": 1793618850624,
    "message": "【下钻】下钻 L0: main()",
    "type": "thinking"
  },
  {
    "id": "em0xx",
    "timestamp": 1793618852988,
    "message": "AI 调用完成：函数下钻分析 L0: main",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L0: main",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n5. 这是入口分析步骤，请把当前函数视为主入口函数。\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并 ...[还有 10315 字节]\"\n}",
      "response": "{\n  \"functionName\": \"main\",\n  \"functionType\": \"function\",\n  \"description\": \"vmon-server 进程入口：解析 CLI、加载并合并配置、初始化 HTTP 服务与路由、启动后台线程、等待信号并优雅关停。\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"vmon::server::load_server_config\",\n      \"type\": \"function\",\n      \"description\": \"从 JSON 文件加载服务配置并校验\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 0,\n      \"possibleFile\": \"server/src/config.cpp\"\n    },\n    {\n      \"name\": \"vmon::server::register_routes\",\n      \"type\": \"function\",\n      \"description\": \"向 httplib::Server 注册所有 API 路由与静态资源\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/api.cpp\"\n    },\n    {\n      \"name\": \"vmon::server::ServerContext::start_background\",\n      \"type\": \"method\",\n      \"description\": \"启动规则线程、转发线程、通知器等后台组件\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/api.cpp\"\n    },\n    {\n      \"name\": \"vmon::server::ServerContext::stop_background\",\n      \"type\": \"method\",\n      \"description\": \"关停规则/转发等后台线程，保证优雅退出\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 0,\n      \"possibleFile\": \"server/src/api.cpp\"\n    },\n    {\n      \"name\": \"httplib::Server::listen_after_bind\",\n      \"type\": \"method\",\n      \"description\": \"在已绑定端口上启动 HTTP 事件循环（框架分发桥接点）\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "77dh36",
    "timestamp": 1793618852988,
    "message": "【下钻】定位成功：main() -> server/src/main.cpp:L39",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/main.cpp",
      "step1-猜测文件-命中: server/src/main.cpp:L39"
    ]
  },
  {
    "id": "3v2gp8",
    "timestamp": 1793618852988,
    "message": "【下钻】发现 5 个关键子调用节点: main()",
    "type": "info"
  },
  {
    "id": "pxmqy",
    "timestamp": 1793618852988,
    "message": "【下钻】下钻 L1: main() -> vmon::server::load_server_config()",
    "type": "thinking"
  },
  {
    "id": "kswhtt",
    "timestamp": 1793618855031,
    "message": "AI 调用完成：函数下钻分析 L1: vmon::server::load_server_config",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L1: vmon::server::load_server_config",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 7199 字节]\"\n}",
      "response": "{\n  \"functionName\": \"vmon::server::load_server_config\",\n  \"functionType\": \"function\",\n  \"description\": \"从JSON配置文件加载服务端配置到ServerConfig结构体，包含基础字段、pipeline规则和webhook配置解析\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"std::ifstream::ifstream\",\n      \"type\": \"method\",\n      \"description\": \"打开配置文件输入流\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"nlohmann::json::operator>>\",\n      \"type\": \"function\",\n      \"description\": \"从输入流解析JSON对象\",\n      \"importance\": \"high\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/nlohmann/json.hpp\"\n    },\n    {\n      \"name\": \"nlohmann::json::contains\",\n      \"type\": \"method\",\n      \"description\": \"检查JSON键是否存在，驱动字段提取流程\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/nlohmann/json.hpp\"\n    },\n    {\n      \"name\": \"nlohmann::json::get<std::vector<std::string>>\",\n      \"type\": \"function\",\n      \"description\": \"将noise_patterns字段转换为字符串向量\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/nlohmann/json.hpp\"\n    },\n    {\n      \"name\": \"nlohmann::json::value\",\n      \"type\": \"method\",\n      \"description\": \"读取pipeline规则字段并提供默认值\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/nlohmann/json.hpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "5yugle",
    "timestamp": 1793618855031,
    "message": "【下钻】定位成功：main() -> vmon::server::load_server_config() -> server/src/config.cpp",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/config.cpp",
      "step1-猜测文件-命中: server/src/config.cpp"
    ]
  },
  {
    "id": "idc23j",
    "timestamp": 1793618855031,
    "message": "【下钻】发现 5 个关键子调用节点: main() -> vmon::server::load_server_config()",
    "type": "info"
  },
  {
    "id": "1gq697",
    "timestamp": 1793618855031,
    "message": "【下钻】下钻 L1: main() -> vmon::server::register_routes()",
    "type": "thinking"
  },
  {
    "id": "0wbcf4",
    "timestamp": 1793618858311,
    "message": "AI 调用完成：函数下钻分析 L1: vmon::server::register_routes",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L1: vmon::server::register_routes",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 82681 字节]\"\n}",
      "response": "{\n  \"functionName\": \"vmon::server::register_routes\",\n  \"functionType\": \"function\",\n  \"description\": \"向 httplib::Server 注册所有 REST/MCP/Web 路由，并为各路由绑定鉴权与业务处理逻辑\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"server.Post\",\n      \"type\": \"method\",\n      \"description\": \"注册 POST 路由（如 /v1/ingest、/v1/crash、/api/diagnose、/mcp 等）\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"third_party/httplib.h\"\n    },\n    {\n      \"name\": \"server.Get\",\n      \"type\": \"method\",\n      \"description\": \"注册 GET 路由（如 /api/overview、/api/traces、/api/services、/ 等）\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"third_party/httplib.h\"\n    },\n    {\n      \"name\": \"server.Delete\",\n      \"type\": \"method\",\n      \"description\": \"注册 DELETE 路由（删除 SLO、规则、静默、仪表板等）\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/httplib.h\"\n    },\n    {\n      \"name\": \"server.Options\",\n      \"type\": \"method\",\n      \"description\": \"注册全局 OPTIONS 预检路由并设置 CORS 头\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/httplib.h\"\n    },\n    {\n      \"name\": \"server.set_mount_point\",\n      \"type\": \"method\",\n      \"description\": \"若配置了 web_dir，则挂载静态 Web 资源目录\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"third_party/httplib.h\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "zspjh",
    "timestamp": 1793618858311,
    "message": "【下钻】定位成功：main() -> vmon::server::register_routes() -> server/src/api.cpp:L981",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/api.cpp",
      "step1-猜测文件-命中: server/src/api.cpp:L981"
    ]
  },
  {
    "id": "1vaplq",
    "timestamp": 1793618858311,
    "message": "【下钻】发现 5 个关键子调用节点: main() -> vmon::server::register_routes()",
    "type": "info"
  },
  {
    "id": "hhn0fw",
    "timestamp": 1793618858311,
    "message": "【下钻】下钻 L1: main() -> vmon::server::ServerContext::start_background()",
    "type": "thinking"
  },
  {
    "id": "g3wvge",
    "timestamp": 1793618860400,
    "message": "AI 调用完成：函数下钻分析 L1: vmon::server::ServerContext::start_background",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L1: vmon::server::ServerContext::start_background",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 82713 字节]\"\n}",
      "response": "{\n  \"functionName\": \"vmon::server::ServerContext::start_background\",\n  \"functionType\": \"function\",\n  \"description\": \"启动后台服务：通过原子标志防止重复启动，启动转发器和通知器，并创建后台循环线程。\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"Forwarder::start\",\n      \"type\": \"method\",\n      \"description\": \"启动事件转发器后台线程\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/forwarder.cpp\"\n    },\n    {\n      \"name\": \"Notifier::start\",\n      \"type\": \"method\",\n      \"description\": \"启动通知器后台线程\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/notifier.cpp\"\n    },\n    {\n      \"name\": \"ServerContext::background_loop\",\n      \"type\": \"method\",\n      \"description\": \"后台循环：周期性执行规则评估与数据清理\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/api.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "8sdqik",
    "timestamp": 1793618860400,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::start_background() -> server/src/api.cpp:L954",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/api.cpp",
      "step1-猜测文件-命中: server/src/api.cpp:L954"
    ]
  },
  {
    "id": "zo8vu4",
    "timestamp": 1793618860400,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::start_background()",
    "type": "info"
  },
  {
    "id": "5i58z",
    "timestamp": 1793618860400,
    "message": "【下钻】下钻 L1: main() -> vmon::server::ServerContext::stop_background()",
    "type": "thinking"
  },
  {
    "id": "3ag7cd",
    "timestamp": 1793618862441,
    "message": "AI 调用完成：函数下钻分析 L1: vmon::server::ServerContext::stop_background",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L1: vmon::server::ServerContext::stop_background",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 82711 字节]\"\n}",
      "response": "{\n  \"functionName\": \"vmon::server::ServerContext::stop_background\",\n  \"functionType\": \"method\",\n  \"description\": \"停止后台线程与各后台组件，先关运行标志再 join 线程，最后停止通知器与转发器\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"std::thread::join\",\n      \"type\": \"method\",\n      \"description\": \"等待后台循环线程结束\",\n      \"importance\": \"high\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"Notifier::stop\",\n      \"type\": \"method\",\n      \"description\": \"停止通知器后台线程与队列\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/notifier.cpp\"\n    },\n    {\n      \"name\": \"Forwarder::stop\",\n      \"type\": \"method\",\n      \"description\": \"停止转发器并刷新剩余数据\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/forwarder.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "vbtx9r",
    "timestamp": 1793618862441,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::stop_background() -> server/src/api.cpp:L961",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/api.cpp",
      "step1-猜测文件-命中: server/src/api.cpp:L961"
    ]
  },
  {
    "id": "nzm76",
    "timestamp": 1793618862441,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::stop_background()",
    "type": "info"
  },
  {
    "id": "6bo7lk",
    "timestamp": 1793618862441,
    "message": "【下钻】下钻 L1: main() -> httplib::Server::listen_after_bind()",
    "type": "thinking"
  },
  {
    "id": "16nh8",
    "timestamp": 1793618863376,
    "message": "AI 调用完成：函数定位猜测Top3: httplib::Server::listen_after_bind",
    "type": "thinking",
    "aiTrace": {
      "label": "函数定位猜测Top3: httplib::Server::listen_after_bind",
      "request": "{\n  \"prompt\": \"\\n你是代码定位助手。请根据函数名、调用上下文与项目代码文件列表，猜测该函数定义最可能所在的 3 个文件（仅返回项目内相对路径）。\\n如果目标函数看起来是系统函数或编程语言标准库/第三方库函数（例如 C 标准库、C++ STL、Java SDK、Node/Python 内置库等），请不要猜测项目文件，直接输出系统库标记。\\n\\n项目语言：C++\\n调用者函数：main\\n调用者文件：server/src/main.cpp\\n目标函数：httplib::Server::listen_after_bind\\n代码文件列表（过滤后）：\\n[\\\"adapters/bpf/weak_edge.h\\\",\\\"adapters/grpc/propagation.cpp\\\",\\\"adapters/grpc/propagation.h\\\",\\\"adapters/grpc/streaming.h\\\",\\\"adapters/ipc/socket_header.h\\\",\\\"agent/src/agent.cpp\\\",\\\"agent/src/agent.h\\\",\\\"agent/src/command_runner.cpp\\\",\\\"age ...[还有 4105 字节]\"\n}",
      "response": "{\n  \"isSystemOrLibraryFunction\": true,\n  \"systemOrLibraryReason\": \"httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。\",\n  \"systemOrLibraryMarker\": \"__SYSTEM_OR_LIBRARY_FUNCTION__\",\n  \"candidateFiles\": [],\n  \"reason\": \"系统或库函数\"\n}"
    }
  },
  {
    "id": "wglei",
    "timestamp": 1793618863376,
    "message": "【下钻】停止下钻（系统/库函数）: main() -> httplib::Server::listen_after_bind()，原因：httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。",
    "type": "thinking"
  },
  {
    "id": "g0t4me",
    "timestamp": 1793618863376,
    "message": "【下钻】下钻 L2: main() -> vmon::server::register_routes() -> server.Post()",
    "type": "thinking"
  },
  {
    "id": "216fik",
    "timestamp": 1793618863410,
    "message": "源码拉取失败: third_party/httplib.h (文件不存在)",
    "type": "info"
  },
  {
    "id": "uj0id9",
    "timestamp": 1793618865462,
    "message": "AI 调用完成：函数下钻分析 L2: server.Post",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: server.Post",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 10715 字节]\"\n}",
      "response": "{\n  \"functionName\": \"server.Post\",\n  \"functionType\": \"method\",\n  \"description\": \"httplib::Server 的路由注册方法，在 register_routes 中为 /v1/ingest、/v1/profile、/v1/crash、/v1/events 等端点绑定 POST 处理 lambda。\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"authorized\",\n      \"type\": \"function\",\n      \"description\": \"校验请求的 Bearer Token 或 X-Vmon-Token，作为 guard 被各 POST 处理器调用。\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"server/src/api.cpp\"\n    },\n    {\n      \"name\": \"ctx.pipeline.process\",\n      \"type\": \"method\",\n      \"description\": \"接入事件入库前的脱敏处理管道，敏感字段不会进入 Store。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/diagnostics.h\"\n    },\n    {\n      \"name\": \"ctx.store.ingest\",\n      \"type\": \"method\",\n      \"description\": \"将规范化后的事件写入事件存储。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/diagnostics.h\"\n    },\n    {\n      \"name\": \"ctx.rules.on_event\",\n      \"type\": \"method\",\n      \"description\": \"事件触发规则引擎评估，是告警/检测核心入口。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/diagnostics.h\"\n    },\n    {\n      \"name\": \"ctx.forwarder.submit\",\n      \"type\": \"method\",\n      \"description\": \"将事件提交给转发器异步发送到外部端点。\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": 0,\n      \"possibleFile\": \"server/src/diagnostics.h\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "fhqgzk",
    "timestamp": 1793618865462,
    "message": "【下钻】定位成功：main() -> vmon::server::register_routes() -> server.Post() -> server/src/api.cpp",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): third_party/httplib.h",
      "step1-猜测文件-未命中(已扫描1个文件)",
      "step1b-同文件兜底-候选(1): server/src/api.cpp",
      "step1b-同文件兜底-命中: server/src/api.cpp"
    ]
  },
  {
    "id": "vwd4h8",
    "timestamp": 1793618865462,
    "message": "【下钻】发现 5 个关键子调用节点: main() -> vmon::server::register_routes() -> server.Post()",
    "type": "info"
  },
  {
    "id": "cb5rct",
    "timestamp": 1793618865462,
    "message": "【下钻】下钻 L2: main() -> vmon::server::register_routes() -> server.Get()",
    "type": "thinking"
  },
  {
    "id": "xsiqjk",
    "timestamp": 1793618865507,
    "message": "源码拉取失败: third_party/httplib.h (文件不存在)",
    "type": "info"
  },
  {
    "id": "v7fkrl",
    "timestamp": 1793618867775,
    "message": "AI 调用完成：函数下钻分析 L2: server.Get",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: server.Get",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 10583 字节]\"\n}",
      "response": "{\n  \"functionName\": \"server.Get\",\n  \"functionType\": \"method\",\n  \"description\": \"httplib 框架的 GET 路由注册方法，用于将各个查询接口路径绑定到对应的处理 lambda。\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"authorized/guard\",\n      \"type\": \"function\",\n      \"description\": \"鉴权守卫，在业务查询前校验 token，属于请求前置校验桥接节点。\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": 0,\n      \"possibleFile\": \"server/src/api.cpp\"\n    },\n    {\n      \"name\": \"ctx.store.query_traces\",\n      \"type\": \"method\",\n      \"description\": \"按 TraceQuery 条件查询 trace 摘要，是 /api/traces 的核心数据源。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/store.cpp\"\n    },\n    {\n      \"name\": \"ctx.store.query_logs\",\n      \"type\": \"method\",\n      \"description\": \"按 LogQuery 条件查询日志记录，是 /api/logs 的核心数据源。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/store.cpp\"\n    },\n    {\n      \"name\": \"trace_detail_json\",\n      \"type\": \"function\",\n      \"description\": \"将单个 trace 的详情聚合为 JSON，供 /api/traces/{id} 返回。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/serialize.cpp\"\n    },\n    {\n      \"name\": \"ctx.store.log_clusters\",\n      \"type\": \"method\",\n      \"description\": \"获取日志聚类结果，是 /api/logs/clusters 的核心数据源。\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/store.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "4zcfi9",
    "timestamp": 1793618867775,
    "message": "【下钻】定位成功：main() -> vmon::server::register_routes() -> server.Get() -> server/src/api.cpp:L1123",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): third_party/httplib.h",
      "step1-猜测文件-未命中(已扫描1个文件)",
      "step1b-同文件兜底-候选(1): server/src/api.cpp",
      "step1b-同文件兜底-命中: server/src/api.cpp:L1123"
    ]
  },
  {
    "id": "dfzoah",
    "timestamp": 1793618867775,
    "message": "【下钻】发现 5 个关键子调用节点: main() -> vmon::server::register_routes() -> server.Get()",
    "type": "info"
  },
  {
    "id": "n63yhj",
    "timestamp": 1793618867775,
    "message": "【下钻】下钻 L2: main() -> vmon::server::ServerContext::start_background() -> Forwarder::start()",
    "type": "thinking"
  },
  {
    "id": "omxjsc",
    "timestamp": 1793618869134,
    "message": "AI 调用完成：函数下钻分析 L2: Forwarder::start",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: Forwarder::start",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 9874 字节]\"\n}",
      "response": "{\n  \"functionName\": \"Forwarder::start\",\n  \"functionType\": \"function\",\n  \"description\": \"启动转发线程，设置运行标志并创建后台线程执行 run\",\n  \"importance\": \"medium\",\n  \"calls\": [\n    {\n      \"name\": \"Forwarder::run\",\n      \"type\": \"method\",\n      \"description\": \"后台线程主循环，按间隔唤醒并批量转发事件\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/forwarder.cpp\"\n    },\n    {\n      \"name\": \"std::thread\",\n      \"type\": \"class\",\n      \"description\": \"创建后台工作线程\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::atomic<bool>::exchange\",\n      \"type\": \"method\",\n      \"description\": \"原子设值并返回旧值，防止重复启动\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "cm60p",
    "timestamp": 1793618869134,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::start_background() -> Forwarder::start() -> server/src/forwarder.cpp:L68",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/forwarder.cpp",
      "step1-猜测文件-命中: server/src/forwarder.cpp:L68"
    ]
  },
  {
    "id": "zt18jf",
    "timestamp": 1793618869134,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::start_background() -> Forwarder::start()",
    "type": "info"
  },
  {
    "id": "g2y53",
    "timestamp": 1793618869134,
    "message": "【下钻】下钻 L2: main() -> vmon::server::ServerContext::start_background() -> Notifier::start()",
    "type": "thinking"
  },
  {
    "id": "wsumdp",
    "timestamp": 1793618870971,
    "message": "AI 调用完成：函数下钻分析 L2: Notifier::start",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: Notifier::start",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 6718 字节]\"\n}",
      "response": "{\n  \"functionName\": \"Notifier::start\",\n  \"functionType\": \"function\",\n  \"description\": \"启动通知器：检查启用状态与运行标志，成功后创建后台线程执行 run 循环。\",\n  \"importance\": \"medium\",\n  \"calls\": [\n    {\n      \"name\": \"Notifier::enabled\",\n      \"type\": \"method\",\n      \"description\": \"检查通知器是否配置启用（可能基于 url/token 等配置判断）\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"server/src/notifier.cpp\"\n    },\n    {\n      \"name\": \"std::atomic::exchange\",\n      \"type\": \"method\",\n      \"description\": \"原子地将 running_ 置为 true，防止重复启动\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::thread::thread\",\n      \"type\": \"method\",\n      \"description\": \"创建后台线程运行 Notifier::run 消费队列\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/notifier.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "o184zh",
    "timestamp": 1793618870971,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::start_background() -> Notifier::start() -> server/src/notifier.cpp:L46",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/notifier.cpp",
      "step1-猜测文件-命中: server/src/notifier.cpp:L46"
    ]
  },
  {
    "id": "bl30sb",
    "timestamp": 1793618870971,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::start_background() -> Notifier::start()",
    "type": "info"
  },
  {
    "id": "ccvf3p",
    "timestamp": 1793618870971,
    "message": "【下钻】下钻 L2: main() -> vmon::server::ServerContext::start_background() -> ServerContext::background_loop()",
    "type": "thinking"
  },
  {
    "id": "9v5u7",
    "timestamp": 1793618872741,
    "message": "AI 调用完成：函数下钻分析 L2: ServerContext::background_loop",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: ServerContext::background_loop",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 10849 字节]\"\n}",
      "response": "{\n  \"functionName\": \"ServerContext::background_loop\",\n  \"functionType\": \"function\",\n  \"description\": \"后台线程主循环，周期性评估规则并裁剪存储数据\",\n  \"importance\": \"medium\",\n  \"calls\": [\n    {\n      \"name\": \"RuleEngine::evaluate\",\n      \"type\": \"method\",\n      \"description\": \"按时间窗口对存储数据执行规则评估，生成事件/告警\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/rule_engine.cpp\"\n    },\n    {\n      \"name\": \"Store::prune\",\n      \"type\": \"method\",\n      \"description\": \"按保留策略清理过期 trace 与日志记录\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/store.cpp\"\n    },\n    {\n      \"name\": \"now_ns\",\n      \"type\": \"function\",\n      \"description\": \"获取当前纳秒时间戳\",\n      \"importance\": \"low\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"server/src/util.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "z7qj2d",
    "timestamp": 1793618872741,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::start_background() -> ServerContext::background_loop() -> server/src/api.cpp:L968",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/api.cpp",
      "step1-猜测文件-命中: server/src/api.cpp:L968"
    ]
  },
  {
    "id": "lyhosg",
    "timestamp": 1793618872741,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::start_background() -> ServerContext::background_loop()",
    "type": "info"
  },
  {
    "id": "qpni3q",
    "timestamp": 1793618872741,
    "message": "【下钻】下钻 L2: main() -> vmon::server::ServerContext::stop_background() -> Notifier::stop()",
    "type": "thinking"
  },
  {
    "id": "e9x238",
    "timestamp": 1793618874661,
    "message": "AI 调用完成：函数下钻分析 L2: Notifier::stop",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: Notifier::stop",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 6716 字节]\"\n}",
      "response": "{\n  \"functionName\": \"Notifier::stop\",\n  \"functionType\": \"function\",\n  \"description\": \"停止通知线程：将 running_ 置为 false，唤醒条件变量并 join 工作线程，实现优雅退出。\",\n  \"importance\": \"medium\",\n  \"calls\": [\n    {\n      \"name\": \"std::atomic<bool>::exchange\",\n      \"type\": \"method\",\n      \"description\": \"原子地将 running_ 置为 false，若已停止则直接返回。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::condition_variable::notify_all\",\n      \"type\": \"method\",\n      \"description\": \"唤醒所有等待在条件变量上的工作线程。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::thread::joinable\",\n      \"type\": \"method\",\n      \"description\": \"检查工作线程是否可 join。\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::thread::join\",\n      \"type\": \"method\",\n      \"description\": \"等待工作线程（Notifier::run 循环）退出。\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/notifier.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "fafmxb",
    "timestamp": 1793618874661,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::stop_background() -> Notifier::stop() -> server/src/notifier.cpp:L52",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/notifier.cpp",
      "step1-猜测文件-命中: server/src/notifier.cpp:L52"
    ]
  },
  {
    "id": "8jsq6",
    "timestamp": 1793618874661,
    "message": "【下钻】发现 4 个关键子调用节点: main() -> vmon::server::ServerContext::stop_background() -> Notifier::stop()",
    "type": "info"
  },
  {
    "id": "6h664v",
    "timestamp": 1793618874661,
    "message": "【下钻】下钻 L2: main() -> vmon::server::ServerContext::stop_background() -> Forwarder::stop()",
    "type": "thinking"
  },
  {
    "id": "4x4aye",
    "timestamp": 1793618876128,
    "message": "AI 调用完成：函数下钻分析 L2: Forwarder::stop",
    "type": "thinking",
    "aiTrace": {
      "label": "函数下钻分析 L2: Forwarder::stop",
      "request": "{\n  \"prompt\": \"\\n你是一个“逐级下钻”的代码调用链分析器，输出必须是 JSON。\\n请分析下面文件中的目标函数（仅输出关键调用节点，忽略琐碎辅助函数）。\\n\\n规则：\\n1. 只关注核心功能流程，输出最重要的关键函数/关键方法调用，最多输出 5 个子节点。\\n2. 每个子节点返回 shouldDrill：-1（叶子或不重要）、0（不确定）、1（值得继续下钻）。\\n3. 如果能根据 import/include 或命名推断出子函数定义文件，请填 possibleFile（项目相对路径）。\\n4. 描述要简短中文。\\n\\n6. 遇到框架启动/分发调用（如 SpringApplication.run、DispatcherServlet、Flask app.run）时，请将其视为“桥接节点”并设置 shouldDrill=1（不要直接作为终点）。\\n\\n项目语言：C++\\n ...[还有 9725 字节]\"\n}",
      "response": "{\n  \"functionName\": \"Forwarder::stop\",\n  \"functionType\": \"function\",\n  \"description\": \"停止后台转发线程：原子标记停止、唤醒等待线程、join 线程、执行一次最终批量刷新\",\n  \"importance\": \"high\",\n  \"calls\": [\n    {\n      \"name\": \"std::condition_variable::notify_all\",\n      \"type\": \"method\",\n      \"description\": \"唤醒等待在条件变量上的转发线程，使其退出循环\",\n      \"importance\": \"medium\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"std::thread::join\",\n      \"type\": \"method\",\n      \"description\": \"等待后台转发线程结束\",\n      \"importance\": \"high\",\n      \"shouldDrill\": -1,\n      \"possibleFile\": \"\"\n    },\n    {\n      \"name\": \"Forwarder::flush_once\",\n      \"type\": \"method\",\n      \"description\": \"线程结束后执行最后一次批量刷新，把队列里剩余事件发出去\",\n      \"importance\": \"high\",\n      \"shouldDrill\": 1,\n      \"possibleFile\": \"server/src/forwarder.cpp\"\n    }\n  ]\n}"
    }
  },
  {
    "id": "qk0bla",
    "timestamp": 1793618876128,
    "message": "【下钻】定位成功：main() -> vmon::server::ServerContext::stop_background() -> Forwarder::stop() -> server/src/forwarder.cpp:L73",
    "type": "thinking",
    "details": [
      "step1-猜测文件-候选(1): server/src/forwarder.cpp",
      "step1-猜测文件-命中: server/src/forwarder.cpp:L73"
    ]
  },
  {
    "id": "3ow75t",
    "timestamp": 1793618876128,
    "message": "【下钻】发现 3 个关键子调用节点: main() -> vmon::server::ServerContext::stop_background() -> Forwarder::stop()",
    "type": "info"
  },
  {
    "id": "5neh7e",
    "timestamp": 1793618876128,
    "message": "【下钻】递归下钻完成，正在整理最终全景图信息...",
    "type": "thinking"
  },
  {
    "id": "ruzz9f",
    "timestamp": 1793618876128,
    "message": "AI Agent: 正在基于完整调用链进行模块划分...",
    "type": "thinking"
  },
  {
    "id": "2od9b9",
    "timestamp": 1793618883455,
    "message": "AI 调用完成：最终模块划分（基于完整调用链）",
    "type": "thinking",
    "aiTrace": {
      "label": "最终模块划分（基于完整调用链）",
      "request": "{\n  \"prompt\": \"\\n你是代码架构模块划分专家。请根据项目概述、文件列表、完整函数调用链，对当前全景图节点进行模块划分。\\n\\n要求：\\n1. 模块按“职责/领域”划分（不是按目录）。\\n2. 模块数量控制在 3~10 个。\\n3. 所有 nodeId 必须分配到一个模块。\\n4. 不要改动 nodeId。\\n5. 返回 JSON。\\n\\n项目元数据：\\n{\\\"repoUrl\\\":\\\"/tmp/CplusTrace-panorama\\\",\\\"repoName\\\":\\\"CplusTrace-panorama\\\",\\\"language\\\":\\\"C++\\\",\\\"generatedAt\\\":\\\"2026-11-02T11:27:26.069Z\\\"}\\n\\n项目概要：\\nCplusTrace-panorama（vmon）是一个面向局域网、离线私有部署的 C++ 系统维测（可观测性与排障）平台：通过输入 request_id、trace_id、task_id、错误码或时间窗，自动还原跨进程、跨节点的调用链，关联各环节日志与错误， ...[还有 54514 字节]\"\n}",
      "response": "{\n  \"modules\": [\n    {\n      \"id\": \"module_bootstrap\",\n      \"name\": \"服务启动与配置加载\"\n    },\n    {\n      \"id\": \"module_http_routing\",\n      \"name\": \"HTTP 路由注册与 Web 托管\"\n    },\n    {\n      \"id\": \"module_ingest\",\n      \"name\": \"事件接入与处理管道\"\n    },\n    {\n      \"id\": \"module_query\",\n      \"name\": \"查询与结果序列化\"\n    },\n    {\n      \"id\": \"module_background\",\n      \"name\": \"后台任务调度与生命周期管理\"\n    },\n    {\n      \"id\": \"module_forwarder\",\n      \"name\": \"事件外发转发器\"\n    },\n    {\n      \"id\": \"module_notifier\",\n      \"name\": \"告警通知器\"\n    },\n    {\n      \"id\": \"module_storage_rules\",\n      \"name\": \"存储维护与规则评估\"\n    },\n    {\n      \"id\": \"module_auth\",\n      \"name\": \"请求鉴权守卫\"\n    }\n  ],\n  \"nodeModuleMap\": {\n    \"n_0_1_root_main\": \"module_bootstrap\",\n    \"n_1_2_n_0_1_root_main_vmon-server-load_server_config\": \"module_bootstrap\",\n    \"n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream\": \"module_bootstrap\",\n    \"n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator\": \"module_bootstrap\",\n    \"n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains\": \"module_bootstrap\",\n    \"n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string\": \"module_bootstrap\",\n    \"n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value\": \"module_bootstrap\",\n    \"n_1_6_n_0_1_root_main_httplib-server-listen_after_bind\": \"module_bootstrap\",\n    \"n_1_3_n_0_1_root_main_vmon-server-register_routes\": \"module_http_routing\",\n    \"n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post\": \"module_http_routing\",\n    \"n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get\": \"module_http_routing\",\n    \"n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete\": \"module_http_routing\",\n    \"n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options\": \"module_http_routing\",\n    \"n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point\": \"module_http_routing\",\n    \"n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process\": \"module_ingest\",\n    \"n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest\": \"module_ingest\",\n    \"n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event\": \"module_ingest\",\n    \"n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit\": \"module_ingest\",\n    \"n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized\": \"module_auth\",\n    \"n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard\": \"module_auth\",\n    \"n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces\": \"module_query\",\n    \"n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs\": \"module_query\",\n    \"n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json\": \"module_query\",\n    \"n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters\": \"module_query\",\n    \"n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background\": \"module_background\",\n    \"n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background\": \"module_background\",\n    \"n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop\": \"module_background\",\n    \"n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join\": \"module_background\",\n    \"n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns\": \"module_background\",\n    \"n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start\": \"module_forwarder\",\n    \"n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop\": \"module_forwarder\",\n    \"n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run\": \"module_forwarder\",\n    \"n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread\": \"module_forwarder\",\n    \"n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange\": \"module_forwarder\",\n    \"n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all\": \"module_forwarder\",\n    \"n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join\": \"module_forwarder\",\n    \"n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once\": \"module_forwarder\",\n    \"n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start\": \"module_notifier\",\n    \"n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop\": \"module_notifier\",\n    \"n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled\": \"module_notifier\",\n    \"n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange\": \"module_notifier\",\n    \"n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread\": \"module_notifier\",\n    \"n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange\": \"module_notifier\",\n    \"n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all\": \"module_notifier\",\n    \"n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable\": \"module_notifier\",\n    \"n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join\": \"module_notifier\",\n    \"n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate\": \"module_storage_rules\",\n    \"n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune\": \"module_storage_rules\"\n  }\n}"
    }
  },
  {
    "id": "a647i7",
    "timestamp": 1793618883455,
    "message": "模块划分完成（AI 基于完整调用链研判）",
    "type": "success"
  },
  {
    "id": "lvyxun",
    "timestamp": 1793618883455,
    "message": "全景图生成完成（逐步下钻模式）",
    "type": "success"
  },
  {
    "id": "mtkhcu",
    "timestamp": 1793618997807,
    "message": "【下钻】手动下钻：main() -> vmon::server::ServerContext::stop_background() -> Forwarder::stop() -> std::condition_variable::notify_all()（L3 -> L4）",
    "type": "thinking"
  },
  {
    "id": "4694gg",
    "timestamp": 1793618998723,
    "message": "AI 调用完成：函数定位猜测Top3: std::condition_variable::notify_all",
    "type": "thinking",
    "aiTrace": {
      "label": "函数定位猜测Top3: std::condition_variable::notify_all",
      "request": "{\n  \"prompt\": \"\\n你是代码定位助手。请根据函数名、调用上下文与项目代码文件列表，猜测该函数定义最可能所在的 3 个文件（仅返回项目内相对路径）。\\n如果目标函数看起来是系统函数或编程语言标准库/第三方库函数（例如 C 标准库、C++ STL、Java SDK、Node/Python 内置库等），请不要猜测项目文件，直接输出系统库标记。\\n\\n项目语言：C++\\n调用者函数：Forwarder::stop\\n调用者文件：server/src/forwarder.cpp\\n目标函数：std::condition_variable::notify_all\\n代码文件列表（过滤后）：\\n[\\\"adapters/bpf/weak_edge.h\\\",\\\"adapters/grpc/propagation.cpp\\\",\\\"adapters/grpc/propagation.h\\\",\\\"adapters/grpc/streaming.h\\\",\\\"adapters/ipc/socket_header.h\\\",\\\"agent/src/agent.cpp\\\",\\\"agent/src/agent.h\\\",\\\"agent/src/command ...[还有 4122 字节]\"\n}",
      "response": "{\n  \"isSystemOrLibraryFunction\": true,\n  \"systemOrLibraryReason\": \"std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。\",\n  \"systemOrLibraryMarker\": \"__SYSTEM_OR_LIBRARY_FUNCTION__\",\n  \"candidateFiles\": [],\n  \"reason\": \"系统或库函数\"\n}"
    }
  },
  {
    "id": "zu4i78",
    "timestamp": 1793618998723,
    "message": "【下钻】停止下钻（系统/库函数）: main() -> vmon::server::ServerContext::stop_background() -> Forwarder::stop() -> std::condition_variable::notify_all()，原因：std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。",
    "type": "thinking"
  }
]
```

## 6. 全景图展示数据
```json
{
  "modules": [
    {
      "id": "module_bootstrap",
      "name": "服务启动与配置加载",
      "color": "#3b82f6"
    },
    {
      "id": "module_http_routing",
      "name": "HTTP 路由注册与 Web 托管",
      "color": "#06b6d4"
    },
    {
      "id": "module_ingest",
      "name": "事件接入与处理管道",
      "color": "#10b981"
    },
    {
      "id": "module_query",
      "name": "查询与结果序列化",
      "color": "#f59e0b"
    },
    {
      "id": "module_background",
      "name": "后台任务调度与生命周期管理",
      "color": "#ef4444"
    },
    {
      "id": "module_forwarder",
      "name": "事件外发转发器",
      "color": "#8b5cf6"
    },
    {
      "id": "module_notifier",
      "name": "告警通知器",
      "color": "#ec4899"
    },
    {
      "id": "module_storage_rules",
      "name": "存储维护与规则评估",
      "color": "#84cc16"
    }
  ],
  "nodeModuleMap": {
    "n_0_1_root_main": "module_bootstrap",
    "n_1_2_n_0_1_root_main_vmon-server-load_server_config": "module_bootstrap",
    "n_1_3_n_0_1_root_main_vmon-server-register_routes": "module_http_routing",
    "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background": "module_background",
    "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background": "module_background",
    "n_1_6_n_0_1_root_main_httplib-server-listen_after_bind": "module_bootstrap",
    "n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream": "module_bootstrap",
    "n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator": "module_bootstrap",
    "n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains": "module_bootstrap",
    "n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string": "module_bootstrap",
    "n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value": "module_bootstrap",
    "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post": "module_http_routing",
    "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get": "module_http_routing",
    "n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete": "module_http_routing",
    "n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options": "module_http_routing",
    "n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point": "module_http_routing",
    "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start": "module_forwarder",
    "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start": "module_notifier",
    "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop": "module_background",
    "n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join": "module_background",
    "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop": "module_notifier",
    "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop": "module_forwarder",
    "n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized": "module_bootstrap",
    "n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process": "module_ingest",
    "n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest": "module_ingest",
    "n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event": "module_ingest",
    "n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit": "module_ingest",
    "n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard": "module_bootstrap",
    "n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces": "module_query",
    "n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs": "module_query",
    "n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json": "module_query",
    "n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters": "module_query",
    "n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run": "module_forwarder",
    "n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread": "module_forwarder",
    "n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange": "module_forwarder",
    "n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled": "module_notifier",
    "n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange": "module_notifier",
    "n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread": "module_notifier",
    "n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate": "module_storage_rules",
    "n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune": "module_storage_rules",
    "n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns": "module_background",
    "n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange": "module_notifier",
    "n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all": "module_notifier",
    "n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable": "module_notifier",
    "n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join": "module_notifier",
    "n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all": "module_forwarder",
    "n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join": "module_forwarder",
    "n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once": "module_forwarder"
  },
  "nodeDescriptions": {
    "n_0_1_root_main": "vmon-server 进程入口：解析 CLI、加载并合并配置、初始化 HTTP 服务与路由、启动后台线程、等待信号并优雅关停。",
    "n_1_2_n_0_1_root_main_vmon-server-load_server_config": "从JSON配置文件加载服务端配置到ServerConfig结构体，包含基础字段、pipeline规则和webhook配置解析",
    "n_1_3_n_0_1_root_main_vmon-server-register_routes": "向 httplib::Server 注册所有 REST/MCP/Web 路由，并为各路由绑定鉴权与业务处理逻辑",
    "n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background": "启动后台服务：通过原子标志防止重复启动，启动转发器和通知器，并创建后台循环线程。",
    "n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background": "停止后台线程与各后台组件，先关运行标志再 join 线程，最后停止通知器与转发器",
    "n_1_6_n_0_1_root_main_httplib-server-listen_after_bind": "系统/库函数，停止下钻：httplib::Server::listen_after_bind 属于第三方库 cpp-httplib（httplib 命名空间），不是项目内定义的函数。",
    "n_2_7_n_1_2_n_0_1_root_main_vmon-server-load_server_config_std-ifstream-ifstream": "打开配置文件输入流",
    "n_2_8_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-operator": "从输入流解析JSON对象",
    "n_2_9_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-contains": "检查JSON键是否存在，驱动字段提取流程",
    "n_2_10_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-get-std-vector-std-string": "将noise_patterns字段转换为字符串向量",
    "n_2_11_n_1_2_n_0_1_root_main_vmon-server-load_server_config_nlohmann-json-value": "读取pipeline规则字段并提供默认值",
    "n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post": "httplib::Server 的路由注册方法，在 register_routes 中为 /v1/ingest、/v1/profile、/v1/crash、/v1/events 等端点绑定 POST 处理 lambda。",
    "n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get": "httplib 框架的 GET 路由注册方法，用于将各个查询接口路径绑定到对应的处理 lambda。",
    "n_2_14_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-delete": "注册 DELETE 路由（删除 SLO、规则、静默、仪表板等）",
    "n_2_15_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-options": "注册全局 OPTIONS 预检路由并设置 CORS 头",
    "n_2_16_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-set_mount_point": "若配置了 web_dir，则挂载静态 Web 资源目录",
    "n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarder-start": "启动转发线程，设置运行标志并创建后台线程执行 run",
    "n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier-start": "启动通知器：检查启用状态与运行标志，成功后创建后台线程执行 run 循环。",
    "n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_servercontext-background_loop": "后台线程主循环，周期性评估规则并裁剪存储数据",
    "n_2_20_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_std-thread-join": "等待后台循环线程结束",
    "n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-stop": "停止通知线程：将 running_ 置为 false，唤醒条件变量并 join 工作线程，实现优雅退出。",
    "n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder-stop": "停止后台转发线程：原子标记停止、唤醒等待线程、join 线程、执行一次最终批量刷新",
    "n_3_23_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_authorized": "校验请求的 Bearer Token 或 X-Vmon-Token，作为 guard 被各 POST 处理器调用。",
    "n_3_24_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-pipeline-process": "接入事件入库前的脱敏处理管道，敏感字段不会进入 Store。",
    "n_3_25_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-store-ingest": "将规范化后的事件写入事件存储。",
    "n_3_26_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-rules-on_event": "事件触发规则引擎评估，是告警/检测核心入口。",
    "n_3_27_n_2_12_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-post_ctx-forwarder-submit": "将事件提交给转发器异步发送到外部端点。",
    "n_3_28_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_authorized-guard": "鉴权守卫，在业务查询前校验 token，属于请求前置校验桥接节点。",
    "n_3_29_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_traces": "按 TraceQuery 条件查询 trace 摘要，是 /api/traces 的核心数据源。",
    "n_3_30_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-query_logs": "按 LogQuery 条件查询日志记录，是 /api/logs 的核心数据源。",
    "n_3_31_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_trace_detail_json": "将单个 trace 的详情聚合为 JSON，供 /api/traces/{id} 返回。",
    "n_3_32_n_2_13_n_1_3_n_0_1_root_main_vmon-server-register_routes_server-get_ctx-store-log_clusters": "获取日志聚类结果，是 /api/logs/clusters 的核心数据源。",
    "n_3_33_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_forwarder-run": "后台线程主循环，按间隔唤醒并批量转发事件",
    "n_3_34_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-thread": "创建后台工作线程",
    "n_3_35_n_2_17_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_forwarde_std-atomic-bool-exchange": "原子设值并返回旧值，防止重复启动",
    "n_3_36_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_notifier-enabled": "检查通知器是否配置启用（可能基于 url/token 等配置判断）",
    "n_3_37_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-atomic-exchange": "原子地将 running_ 置为 true，防止重复启动",
    "n_3_38_n_2_18_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_notifier_std-thread-thread": "创建后台线程运行 Notifier::run 消费队列",
    "n_3_39_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_ruleengine-evaluate": "按时间窗口对存储数据执行规则评估，生成事件/告警",
    "n_3_40_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_store-prune": "按保留策略清理过期 trace 与日志记录",
    "n_3_41_n_2_19_n_1_4_n_0_1_root_main_vmon-server-servercontext-start_background_serverco_now_ns": "获取当前纳秒时间戳",
    "n_3_42_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-atomic-bool-exchange": "原子地将 running_ 置为 false，若已停止则直接返回。",
    "n_3_43_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-condition_variable-notify_all": "唤醒所有等待在条件变量上的工作线程。",
    "n_3_44_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-joinable": "检查工作线程是否可 join。",
    "n_3_45_n_2_21_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_notifier-_std-thread-join": "等待工作线程（Notifier::run 循环）退出。",
    "n_3_46_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-condition_variable-notify_all": "系统/库函数，停止下钻：std::condition_variable::notify_all 是 C++ 标准库中的同步原语方法，不属于项目代码定义。",
    "n_3_47_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_std-thread-join": "等待后台转发线程结束",
    "n_3_48_n_2_22_n_1_5_n_0_1_root_main_vmon-server-servercontext-stop_background_forwarder_forwarder-flush_once": "线程结束后执行最后一次批量刷新，把队列里剩余事件发出去"
  }
}
```

## 7. AI 调用统计
```json
{
  "inputTokens": 118803,
  "outputTokens": 6774,
  "callCount": 18
}
```
