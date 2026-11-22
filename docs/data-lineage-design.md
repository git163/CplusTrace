# 数据血缘与跨进程参数溯源：设计与实现

> 状态：v0.1，已落地最小可用闭环（事件模型 + Server 血缘存储/闸门/RCA + API/Web/MCP +
> 7 进程工业控制示例 + 单测）。后续项在 §13 标明。
> 关联：[principles.md](principles.md)、[module-design.md](module-design.md)、
> [guance-benchmark.md](guance-benchmark.md)、[examples/industrial/README.md](../examples/industrial/README.md)。

## 0. 框架原理（先懂骨架）

**总（一句话）**：**给数据贴指纹 → 按调用关系连线 → 报错时逆流而上找"第一个变坏的点" →
用"平时正常的样子"当裁判。**

**分（由浅入深）**：

1. **类比·快递追踪**：跨进程调用 = 转运站，参数值 = 包裹，值指纹 = 运单号，报错 = 收件人投诉。
   现有 trace 只知道"哪个站被投诉"，不知道"包裹从哪来、被谁装错"。
2. **现有 trace 缺什么**：只记"谁调谁 / 耗时 / 是否错"，**不记传了什么值**。
3. **三个动作**：① 采集（边界记 字段 + 指纹 + 方向）→ ② 建图（上游 out 指纹 == 下游 in
   指纹则连边）→ ③ 追溯（从错误字段逆流而上）。
4. **判定"变坏"两个判据**：**值溯源**（坏值最早由谁写出）+ **差分溯源**（与同场景成功基线比）。
5. **应对复杂业务**：不预建全图（因果锥裁剪）+ 场景聚类 + 只索引指纹 + 类型图从运行中学习。
6. **两类故障、两套判官**：
   - **A 类·单点坏值**：值血缘 + 单字段差分，能指名道姓；
   - **B 类·值都好但"算/组合/时序"坏了**：计算血缘 + 中间结果 + 组合基线 + 不变式 + 反事实重算。
7. **"谁最后错" ≠ "谁的根因"**：单链靠扇入消歧给候选排序；多链靠跨事件统计归因。
8. **先闸门、后血缘**：状态/使能、计数一致、顺序时序这些 O(1)/O(N) 检查先跑，命中即短路。

**总（收口）**：四个字 **指纹、连边、逆流、基线**；三个不变量 **不预建全图、只索引指纹、
因果序优先于墙钟**。

### 0.1 框架总图

```
┌──────────────────────── ① 采集层（业务进程） ────────────────────────┐
│  显式：VMON_SPAN / set_field / VMON_CHECKPOINT（0~1 行）              │
│  自动：gRPC/HTTP adapter 采 traceparent + 字段（protobuf/JSON）       │
│  声明：lineage/schemas/*.yaml 描述 in/out/敏感                        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ UDS / NDJSON（span + lineage + state）
                               ▼
┌──────────────────────── ② 存储与图（Server） ────────────────────────┐
│  /v1/ingest 分流：span/log → Store；lineage/state → LineageStore      │
│  实例层：run_id + iteration_id + 值指纹 + 值传播边                    │
│  类型层：从实例边累加 support（后续）                                 │
└──────────────────────────────┬───────────────────────────────────────┘
                               ▼
┌──────────────────────── ③ 分析层（确定性，无 LLM） ──────────────────┐
│  G0 状态/使能 → G1 计数 → G2 身份 → G3 时序（命中即短路）             │
│  反向切片 · 值溯源 · 差分溯源 · 扇入消歧 · 跨事件归因                 │
│  输出：候选排序 + 根因链 + evidence + unknowns                        │
└──────────────────────────────┬───────────────────────────────────────┘
                               ▼
┌──────────────────────── ④ 消费层 ───────────────────────────────────┐
│  Web #lineage │ vmonctl │ MCP 工具 │ LLM Agent（可选，读证据）        │
└──────────────────────────────────────────────────────────────────────┘
```

## 1. 背景与问题

多进程 C++ 系统里常见这样一类故障：

> 某个进程出现错误码 E，很可能是调用链路上游进程下发的**某个参数值不对**。
> 需要把「哪个进程、哪个字段、什么时刻写入了这个值 → 经过哪些进程/变换 →
> 最终导致下游报错」这条链自动还原。

现有 vmon 的 `error_chain` 只回答"谁先报错"，不回答"这个值从哪来"。工业控制场景更棘手：
**经常没有任何错误码**，最终表现是整个系统共同作用的结果（迭代不收敛、尺寸超差）。

## 2. 与 ETL / 数仓血缘的区别

| | 数仓血缘（OpenLineage/DataHub） | 本方案 |
|---|---|---|
| 节点 | 表、字段、Job | 进程、算子、参数字段、设备状态 |
| 粒度 | 天/批次 | 单次请求 / 单次迭代 |
| 目标 | 影响分析、治理 | **在线排障、根因定位** |
| 关键信号 | 表依赖 | **值指纹 + 时序 + 状态 + 计数** |
| 时间 | 不敏感 | **因果序优先** |

本方案借用 OpenLineage 的对象命名做导出，但运行时是自研轻量图。

## 3. 目标与非目标

**目标**
- 从"错误码/异常现象"出发，自动还原上游字段来源与传播路径；
- 无错误码的静默失败也能定位（状态/计数/身份/时序/基线）；
- 低成本接入：自动 > 声明式 > 显式，未覆盖处显式标注；
- 结论可复核：候选排序 + 证据 + unknowns，不假装精确。

**非目标**
- 不做全量业务血缘建模，不要求人工先画图；
- 不引入外部存储/服务（离线内网）；
- 不做"LLM 直接读日志猜根因"。

## 4. 开源参考

| 项目 | 借鉴点 |
|---|---|
| **OpenLineage / Marquez** | `Job/Run/Dataset/Facet` 对象命名与 `columnLineage` 结构，作为导出格式 |
| **OpenTelemetry** | span 做时序/错误；多对多用 Span Links；无批准的血缘语义约定 |
| **DataHub / OpenMetadata** | URN/aspect 建模、"原生 API + OL 兼容 API"双接口；不引入 |
| **sqlglot / dbt manifest** | SQL 字段级血缘（可选扩展）；dbt `parent_map/child_map` 纯 JSON 可直接消费 |
| **HolmesGPT / OpenRCA** | 生成式诊断需要结构化证据面；OpenRCA 证明"LLM 直接啃遥测"不可靠 |
| **RCA 算法** | 无成熟开源库；业界做法 = 有界双向遍历 + 状态/时间/跳数排序 |

结论：**只照搬 OpenLineage 的模型命名**，运行时自研；LLM 作为可选推理层，证据层先行。

## 5. 领域模型

### 5.1 事件扩展（`common/json_event.h`）

统一事件新增字段：

```cpp
struct DataRef {            // 一次边界上的字段值指纹
  std::string object;       // frame / motion / count ...
  std::string field;        // frame_id / settle_time_ns ...
  std::string dir;          // in / out / local
  std::string kind;         // str/int/float/bool/bytes/json/opaque
  std::string value_hash;   // 语义指纹
  std::string shape_hash;
  std::string value;        // 可选原文（已脱敏/截断）
  uint32_t value_len;
  bool truncated, redacted, opaque;
  std::string schema_hash, op, reason;
};

struct Event {
  // ... 原有字段
  std::string run_id;        // 一次作业运行
  uint64_t iteration_id;     // 迭代序号（0 表示 run 级）
  std::string device;        // 状态事件：设备
  std::string state;         // 状态事件：当前状态
  std::vector<DataRef> data; // 仅 type=lineage
};
```

新增事件类型：`type=lineage`（字段指纹）、`type=state`（设备状态）。
**不写进 `attrs`**：attrs 约定只放低基数可检索字段，值指纹是超高基数，会撑爆索引。

### 5.2 存储模型（`server/src/lineage.h`）

- `LineageRecord`：run/iteration + 服务/算子 + 对象/字段 + 方向 + 指纹 + 原文；
- `StateRecord`：run/iteration + 设备 + 状态；
- 按 `run_id` 聚合为 `RunData`，有界（`max_runs` / `max_records` / 保留期）。

### 5.3 到 OpenLineage 的映射（导出用，后续）

| OpenLineage | vmon |
|---|---|
| `Job{namespace,name}` | 服务 + 算子 |
| `Run{runId}` | `run_id:iteration_id`（UUIDv5 派生） |
| `Dataset{namespace,name}` | `object` |
| `ColumnLineageDatasetFacet` | `(object, field)` 的 in→out 映射 |
| 值指纹 | 自定义 facet `vmon_value`（OL 核心不建模值） |

## 6. 采集

### 6.1 三级成本阶梯

| 级别 | 业务改造量 | 覆盖内容 |
|---|---|---|
| ① 自动 | 0 行 | 传输边界字段、config/env、序列化 body |
| ② 声明式 | 每场景一份 YAML | 哪些字段 in/out、哪些敏感 |
| ③ 显式 | 每场景 1–3 行 | 算法内部中间结果 |

**关键澄清**：异常边界 ≠ 数据边界。异常已由 span 的 `status/error_code` 覆盖，
**不需要在每个 catch 块插桩**；血缘只需数据边界。

### 6.2 采样与尾部保留

- 默认只发 `value_hash + len + kind`；原文仅错误触发时短时保留；
- 成功链路低频采样（~1%）建基线，否则算不出偏离度；
- 低熵热值（如 `status=OK`）不建倒排、不参与稀有度打分；
- 先脱敏再哈希；二进制/加密标 `opaque` 并降置信。

### 6.3 工业场景特殊字段

- **状态/使能**：`type=state`，设备 `initialized/enabled/homed/calibrated`；
- **计数**：`object=count` 的 `trigger_count/frame_count/processed_count/iteration_count`；
- **时序**：`settle_time_ns`、`trigger_delay_ns`；
- **大对象**：只存 `shape_hash + 统计摘要 + checksum`。

### 6.4 SDK 血缘 API（已实现）

```cpp
vmon::set_run_id(run_id);        // 静态/长生命周期字符串
vmon::set_iteration_id(iter);

VMON_FIELD_IN ("frame", "requested_frame_id", "37");   // 输入字段
VMON_FIELD_OUT("offset", "offset", "1.000");           // 输出字段
VMON_FIELD_I64("count", "trigger_count", 50);
VMON_STATE("motion-controller", "homed");              // 设备状态
```

实现要点：
- 独立 `LineageEvent` 环（64 条/线程），**不撑大**热路径 `Event`；
- 值拷贝到固定缓冲（截断 64B），`object/field` 只存指针，要求静态字符串；
- 后台 Sender 统一发 `type=lineage` / `type=state`；
- 进程退出时做一次最终排空，避免丢最后一批事件。

## 7. 存储与有界资源

- 独立 `LineageStore`（自带锁），不争 `Store::mutex_`；
- 上限：`max_runs=5000`、`max_records=400000`、保留期 6h（可配）；
- 记录只索引值指纹，不建全量实例图；按 `run_id` 惰性聚合。

## 8. 算法

### 8.1 前置闸门（Gate Ladder）

```
G0 状态/使能  ──失败──▶ 直接点名子系统
G1 计数一致   ──失败──▶ 定位"哪一段丢/重"
G2 身份不变式  used_frame_id == requested_frame_id
G3 时序不变式  trigger_delay >= settle_time
G4 值血缘/差分  ──▶ 具体字段/映射
G5 跨事件归因   ──▶ 系统性根因
G6 反事实重放   ──▶ 确认
```

已实现 G0–G3 + 差分；G4–G6 见 §13。

### 8.2 实例血缘图

同一次迭代内，上游 `out` 与下游 `in` 的值指纹相同则连边；节点带服务/对象/字段/方向/值。
用于展示"这个值从哪来、流到哪去"。

### 8.3 差分溯源

对每个 `(object, field)`，统计各迭代取值的多数值；目标迭代偏离多数值即标记，
并给出基线占比。天然唯一的字段（如各类 id）不计入。

### 8.4 跨事件归因（后续）

按 `(错误码, 场景)` 聚合多条故障，用 SBFL（Tarantula/Ochiai）计算组件可疑度：
失败样本常见、正常样本罕见者可疑。

### 8.5 根因叙述

输出 `verdict / confidence / candidates / chain / deviations / evidence / unknowns / narrative`，
扩展 `diagnostics.cpp` 的 verdict 习惯；低证据时明确 `evidence_insufficient`。

## 9. 工业控制案例

详见 [examples/industrial/README.md](../examples/industrial/README.md)。要点：

- 7 个进程（1 逻辑层 + 6 控制器），50 次迭代；
- 两种故障：`identity`（取最新帧导致 `used≠requested`）、`timing`（固定延时触发导致轴未稳定就曝光）；
- 所有进程 200、无错误码、计数一致、状态满足；
- 定位：G0/G1 排除 → G2 或 G3 命中 → 候选排序 → 反事实验证（设计）。

## 10. API / Web / MCP

| 接口 | 用途 |
|---|---|
| `GET /api/lineage/runs` | run 列表 |
| `GET /api/lineage/gates?run_id=` | 闸门结果（含首个失败迭代） |
| `GET /api/lineage/rca?run_id=&iteration_id=` | 根因分析 |
| `GET /api/lineage/graph?run_id=&iteration_id=` | 实例血缘图 |

- Web：导航「数据血缘」（`#lineage`），run 列表 / 闸门 / RCA / 血缘图；
- MCP：`lineage_runs` / `lineage_gates` / `lineage_rca` / `lineage_graph`。

## 11. 使用方式

```bash
# 编译
./scripts/build.sh --no-tests --no-examples -j 8
# 跑示例（自动起 Server）
python3 examples/industrial/run_demo.py --start-server --server-bin build/vmon_server
# 时序故障
python3 examples/industrial/run_demo.py --start-server --server-bin build/vmon_server --fault timing
```

接入自己的业务：提供 `run_id`、`iteration_id` 和要追的字段即可，
用 `emitter.data(...)` / `emitter.state(...)` 或框架的自动/声明式采集。

## 12. 资源边界与生产安全

- 血缘记录独立有界，默认不影响现有 trace/log 路径；
- 只存指纹，原文仅错误时短时保留；
- 敏感字段先脱敏再哈希；
- 未覆盖边界显式标注 `uncovered/opaque`，不假装有数据。

## 13. 当前实现范围与分期

| 已实现 | 后续 |
|---|---|
| 事件模型（data/run_id/iteration_id/state） | 算法内部中间结果 `VMON_CHECKPOINT` |
| LineageStore + 有界淘汰 | 类型层 schema 图（从实例学习） |
| G0 状态 / G1 计数 / G2 身份 / G3 时序 | 自动 adapter 字段提取、YAML schema |
| 实例血缘图 | 跨事件 SBFL 归因 |
| 差分（多数值偏离） | 计算血缘/中间结果/不变式/反事实重放 |
| RCA 候选 + 叙述 + unknowns | OpenLineage 导出、LLM 推理层 |
| API / Web / MCP（含血缘图与跳转 trace） | 证据包 `lineage.jsonl` |
| **C++ SDK 血缘 API**（`VMON_FIELD_*` / `VMON_STATE`，独立事件环） | 自动 adapter 字段抽取 |
| 7 进程工业示例：Python 版 + **C++ 版（走真实 SDK→Agent→Server）** | 更多行业模板 |
| 6 类故障（identity/timing/count/state/algo/none） | — |
| AI 证据包 + 可插拔模型调用（`examples/industrial/ai_diagnose.py`） | 具体模型端点/提示词调优 |

## 14. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 错误未必由"参数值"引起 | 状态/计数/时序/缺失/默认值作一等检查 |
| 不可哈希/加密字段 | opaque + 降置信 + 序列化前后钩子 |
| 语义等价字节不同 | 规范化再哈希 + raw_hash |
| 墙钟不可靠 | 因果序优先，时间仅 tie-break |
| 采样缺上游 | 采样标志传播 + schema-only 事件 |
| 基线污染 | robust 统计 + 排除错误样本 + 定期重建 |
| 场景多、模型复杂 | 因果锥裁剪 + 场景聚类 + 渐进披露 |
| 未覆盖边界 | 显式标注，覆盖率可见可增长 |
