# Web / Python 客户端接入的可维测设计

> 日期：2026-11-01
> 问题：后续可能由 Web 或 Python 通过 RPC 调用后端，业务逻辑仍在后端。
> 这部分要不要纳入可维测范围？怎么做？
> 关联：[plan_v1.0.md](../plan_v1.0.md)、[user-guide.md](user-guide.md)。

## 1. 结论

**要支持，但只支持“起点与传播”，不把业务逻辑或完整采集器搬到前端。**

一句话原则：

> 后端仍是业务真相源；Web / Python 只负责**把 trace 起点传进来**，
> 以及（可选）上报少量**客户端体验指标**。

原因很直接：一次请求现在从 Python 服务或浏览器发起。如果上游不带
`trace_id` / `request_id`，后端链路再完整，也只是一段“没有开头的链路”，
无法回答“这次慢/错是用户哪次操作触发的”。所以上游必须接入，但接入面要足够小。

## 2. 三种接入形态

| 形态 | 典型场景 | 接入方式 | 采集内容 |
|---|---|---|---|
| Python 服务 | 脚本、定时任务、BFF、内部工具调用后端 RPC | 中间件/装饰器注入 `traceparent`、`x-request-id`、`x-task-id` | client span、RPC 状态码、耗时、重试次数 |
| Web 前端 | 浏览器页面调用 BFF 或后端 HTTP API | 由 BFF/网关注入；浏览器可选 OTel JS | 页面/操作 span、API 耗时、前端错误（采样） |
| 第三方/异构系统 | Java/Go 服务、其他团队接口 | OTLP 或 HTTP header 传播 | 入口 span 与错误码 |

### 2.1 Python：传播优先，SDK 可选

最小方案（推荐先做）：

```python
# 伪代码：在每次调用后端前生成/透传上下文
trace_id = new_trace_id()
span_id = new_span_id()
headers = {
    "traceparent": f"00-{trace_id}-{span_id}-01",
    "x-request-id": request_id,
    "x-task-id": task_id,
}
rpc_call(headers=headers, ...)
```

后端 C++ SDK 已有 `create_span_with_parent` 与 `parse_traceparent`，只要从
HTTP/gRPC metadata 提取 `traceparent` 即可续接。这样：

- 不要求 Python 安装重型 SDK；
- 不需要新增服务端接口；
- 失败时业务行为不变（header 只是附加信息）。

第二阶段再提供 `vmon-py` 轻量包：封装 `requests` / grpc 拦截器，自动生成
client span、记录状态码与耗时，并可选把少量事件发到 BFF 或 Agent。

### 2.2 Web：不要让浏览器直连中心写接口

浏览器直接向 `vmon-server` 写数据有三个问题：

1. token 会暴露在浏览器，写接口可被滥用；
2. CORS 与内网策略复杂；
3. 前端日志噪音大，容易污染错误优先的治理策略。

推荐路径：

```
浏览器 → BFF/网关（已有鉴权）→ 后端业务服务
                 │
                 └─ 注入 traceparent / request_id；可选聚合前端事件
```

BFF 已经在内网、已有会话鉴权，由它统一注入上下文，最安全也最容易落地。
浏览器侧只建议采集：页面加载、关键操作、API 失败，采样后经 BFF 汇总。

### 2.3 第三方系统：OTLP 兜底

如果对方已有 OpenTelemetry，可直接把 OTLP 发到 Agent（新增 OTLP/HTTP receiver）
或中心。此方案复用标准协议，但会引入 protobuf 依赖与额外端口，放在二期。

## 3. 需要补的能力

| 能力 | 现状 | 需要做什么 | 优先级 |
|---|---|---|---|
| HTTP header 提取 `traceparent` | SDK/适配层已有编解码 | 在业务 HTTP 入口调用提取并 `create_span_with_parent` | P0 |
| gRPC metadata 提取 | 适配层有传播函数 | 按目标 gRPC 版本接入 server interceptor | P0 |
| `x-request-id` / `x-task-id` 透传 | 已有 `set_request_id` / `set_task_id` | 在入口统一读取并设置 | P0 |
| Python 轻量客户端 | 无 | `vmon-py`：requests/grpc 包装 + 采样 + 失败静默 | P1 |
| 前端事件上报 | 无 | BFF 聚合接口，限流 + 采样 + 字段白名单 | P2 |
| OTLP 接入 | 无 | Agent 增加 OTLP/HTTP receiver | P2 |

## 4. 不建议做的事

- **不要把业务逻辑或数据组装放到前端**：维测只“观察”，不参与业务决策；
- **不要让浏览器持有中心写 token**：写入口只开给 BFF/Agent；
- **不要为 Python 重造一套完整 SDK**：先传播，再按需补 client span；
- **不要全量采集前端日志**：只保留错误、慢请求和关键操作，且必须采样；
- **不要改变现有 API 语义**：header 缺失时后端照常工作，链路从后端根 Span 开始。

## 5. 分阶段落地

| 阶段 | 内容 | 产出 | 预估 |
|---|---|---|---|
| P0 | 约定 header；C++ 入口提取示例；文档与联调 demo | 一条 trace 从 Python 打进后端 C++ | 1–2 天 |
| P1 | `vmon-py` 轻量包（client span + 失败静默） | Python 侧自动埋点 | 3–5 天 |
| P2 | BFF 聚合前端事件；OTLP receiver | Web/第三方接入 | 1–2 周 |
| P3 | 前端 UX 指标、会话关联（可选） | 页面级体验视图 | 按需 |

## 6. 验收标准

1. Python 发起一次调用，中心能看到完整 trace：Python client span → 后端
   server span → 下游服务；
2. `request_id`、`task_id`、错误码在两侧一致，日志可按同一 request 查询；
3. header 缺失、格式错误、中心不可用时，Python/前端业务不受影响；
4. 浏览器不直接持有中心写 token；
5. 前端事件默认关闭或低采样，不改变后端日志治理策略。

## 7. 对现有架构的影响

- **SDK**：只需在公开 API 上补一个“从 header 提取上下文”的便捷函数（或文档示例）；
- **Agent/Server**：P0/P1 无改动；P2 才新增 OTLP 接入；
- **Web UI**：无改动；
- **存储**：沿用现有事件模型，前端事件也走 `type=log/metric/span`，不新增存储引擎。

这也是当前分层的一个好处：接入面在 SDK/入口层，后面的采集、存储、分析、展示
都不需要为“前端接入”重写。
