# 工业控制闭环示例（C++ 版，走真实 SDK）

与 [Python 版](../industrial/README.md) 行为一致，但每个进程都**链接 vmon SDK**，
事件经 `SDK → Agent(UDS) → Server`，演示真实业务进程的接入形态。

## 1. 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
# 产物：
#   build/vmon_server  build/vmon_agent
#   build/examples/industrial_cpp/vmon_industrial
```

## 2. 运行

```bash
python3 examples/industrial_cpp/run_demo.py \
  --server-bin build/vmon_server \
  --agent-bin build/vmon_agent \
  --client-bin build/examples/industrial_cpp/vmon_industrial \
  --fault identity

# 时序故障
python3 examples/industrial_cpp/run_demo.py \
  --server-bin build/vmon_server --agent-bin build/vmon_agent \
  --client-bin build/examples/industrial_cpp/vmon_industrial \
  --fault timing
```

脚本会自动启动 Server + Agent + 7 个 `vmon_industrial` 进程。

## 3. SDK 埋点长什么样

每个控制器在请求入口恢复父上下文、建 span，然后逐字段记录：

```cpp
vmon::set_run_id(run_id);          // 作业运行
vmon::set_iteration_id(it);        // 迭代序号

vmon::Span span = vmon::create_span_with_parent(parent, "Motion/Move",
                                                __FILE__, __LINE__);
VMON_FIELD_OUT("motion", "settle_time_ns", "13100000");
VMON_FIELD_OUT("motion", "actual_position", "370");
VMON_STATE("motion-controller", "homed");   // 设备状态
span.end();
```

- `VMON_FIELD_*` / `VMON_STATE` 只写线程本地血缘环，**不分配、不加锁、不 IO**；
- `object/field` 必须是静态字符串；值拷贝到固定缓冲（截断）；
- 事件由后台 Sender 批量发往 Agent，业务线程不阻塞。

## 4. 与 Python 版的关系

| | Python 版 | C++ 版（本目录） |
|---|---|---|
| 埋点方式 | 直接 POST `/v1/ingest` | **vmon SDK** |
| 链路 | 直连 Server | **SDK → Agent(WAL) → Server** |
| 用途 | 快速演示 / 跨语言 | **验证 C++ 业务接入** |

两者对同一故障的判定结果一致（identity/timing/count/state/algo/none）。

## 5. 故障场景

与 Python 版相同：`identity`（G2）、`timing`（G3）、`count`（G1）、
`state`（G0）、`algo`（差分）、`none`（对照）。

详见 [../industrial/README.md](../industrial/README.md)。
