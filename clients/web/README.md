# vmon Web 客户端

浏览器端 trace 起点与传播，单文件、无依赖：

```html
<script src="/vmon-trace.js"></script>
<script>
  const tracedFetch = vmonTrace.instrumentFetch(window.fetch, {
    service: "web-console",
    requestId: "REQ-1",
    report: (event) => {
      // 上报给 BFF/网关，不要直连 vmon-server 的写接口
      navigator.sendBeacon("/bff/vmon/events", JSON.stringify(event));
    }
  });
</script>
```

`instrumentFetch` 会：

1. 生成/继承 `trace_id`、`span_id`，注入 `traceparent`、`x-request-id`、`x-task-id`；
2. 统计耗时与 HTTP 状态；
3. 通过 `report` 回调（或 URL）上报一次客户端事件；
4. 5xx 标记 `error_code=E_HTTP_<status>`，网络异常标记 `E_FETCH`；
5. 未命中采样时只透传业务 ID，不注入 `traceparent`、不上报事件。

## 推荐架构

```
浏览器 --traceparent--> BFF/网关 --traceparent--> C++ 后端 / Py 服务
   |                         |
   +--- 事件上报到 BFF ------+---> Agent / Server
```

原因：浏览器直连中心写接口会暴露 token、引入 CORS，并且前端日志噪音大。
BFF 已有会话鉴权，适合统一注入上下文、采样和转发。

## API

| 方法 | 说明 |
|---|---|
| `newContext(parent?)` | 新建上下文；有 parent 时沿用 trace |
| `parseTraceparent(value)` | 解析 W3C 头，非法返回 null |
| `makeTraceparent(ctx, sampled?)` | 生成 55 字符头 |
| `headers(ctx, {requestId, taskId})` | 生成传播头 |
| `instrumentFetch(fetchImpl, options)` | 包装 fetch，自动注入与上报 |

## 测试

```bash
node --test clients/web/test_vmon_trace.js
```
