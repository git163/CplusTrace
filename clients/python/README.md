# vmon Python 客户端

让 Python 服务/脚本成为调用链的起点，并把 `traceparent` 传给 C++ 后端。

- 零第三方依赖（只用标准库）；
- 默认 fail-open：中心不可用、token 错误都不会抛到业务；
- 支持嵌套 span、request_id / task_id、HTTP 状态码与错误码；
- 事件可直接上报到 `vmon-server` 的 `/v1/ingest`，也可只做传播不上报。

## 快速使用

```python
from vmon_trace import Client

client = Client(
    server_url="http://127.0.0.1:18428",
    token="dev-token",
    service="py-bff",
    node="node-1",
    flush_interval=1.0,   # 后台批量上报；0 表示不启后台线程
)

with client.span("GET /orders", request_id="REQ-1", task_id="TASK-1") as span:
    status, body = client.request(
        "GET", "http://backend:9000/api/orders",
        request_id="REQ-1", task_id="TASK-1")
    span.set_attr("http.status_code", status)
    if status >= 500:
        span.set_error("E_ORDER")

client.close()   # 停止后台线程并 flush
```

后端 C++ 在入口取头并续接：

```cpp
vmon::SpanContext parent;
if (vmon::context_from_traceparent(
        req.get_header_value("traceparent").c_str(), &parent)) {
  vmon::Span span = vmon::create_span_with_parent(
      parent, "HTTP/Handle", __FILE__, __LINE__);
}
```

## 只传播、不上报

```python
client = Client(service="py-tool")   # server_url/token 为空
headers = client.headers(request_id="REQ-2")
# 把 headers 交给 requests / grpc / urllib
```

## 日志关联

```python
client.log("info", "order created", request_id="REQ-1",
           attrs={"order_id": "12345"})
```

## 注意

- `span()` 使用线程本地栈，异步任务请显式传递 `traceparent`；
- 事件队列满会丢弃并计入 `client.dropped`，不会阻塞业务；
- 浏览器不要持有中心写 token；Web 场景由 BFF 注入上下文并汇总事件。

## 测试

```bash
python3 -m unittest discover -s clients/python -p 'test_*.py'
```
