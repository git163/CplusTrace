// vmon-trace.js 单元测试：node --test clients/web/test_vmon_trace.js
"use strict";

const test = require("node:test");
const assert = require("node:assert");
const vmonTrace = require("./vmon-trace.js");

test("traceparent 往返与非法输入", () => {
  const context = vmonTrace.newContext();
  const header = vmonTrace.makeTraceparent(context);
  assert.strictEqual(header.length, 55);
  const parsed = vmonTrace.parseTraceparent(header);
  assert.strictEqual(parsed.traceId, context.traceId);
  assert.strictEqual(parsed.spanId, context.spanId);
  assert.strictEqual(parsed.sampled, true);

  assert.strictEqual(vmonTrace.parseTraceparent("bad"), null);
  assert.strictEqual(vmonTrace.parseTraceparent(
    "00-00000000000000000000000000000000-1111222233334444-01"), null);
});

test("子上下文沿用 trace，生成新 span", () => {
  const parent = vmonTrace.newContext();
  const child = vmonTrace.newContext(parent);
  assert.strictEqual(child.traceId, parent.traceId);
  assert.notStrictEqual(child.spanId, parent.spanId);
});

test("instrumentFetch 注入传播头并上报事件", async () => {
  let seen = null;
  const reports = [];
  const fakeFetch = (url, init) => {
    seen = init;
    return Promise.resolve({ status: 200, ok: true });
  };
  const traced = vmonTrace.instrumentFetch(fakeFetch, {
    service: "web-console",
    requestId: "REQ-1",
    taskId: "TASK-1",
    report: (event) => reports.push(event)
  });

  const response = await traced("/api/orders", { method: "GET" });
  assert.strictEqual(response.status, 200);
  assert.ok(seen.headers.traceparent.startsWith("00-"));
  assert.strictEqual(seen.headers["x-request-id"], "REQ-1");
  assert.strictEqual(seen.headers["x-task-id"], "TASK-1");

  assert.strictEqual(reports.length, 1);
  assert.strictEqual(reports[0].service, "web-console");
  assert.strictEqual(reports[0].status, "ok");
  assert.strictEqual(reports[0].request_id, "REQ-1");
  assert.ok(reports[0].duration_ns >= 0);
});

test("instrumentFetch 上报 5xx 与异常", async () => {
  const reports = [];
  const traced = vmonTrace.instrumentFetch(
    () => Promise.resolve({ status: 503, ok: false }),
    { report: (event) => reports.push(event) });
  await traced("/api/fail");
  assert.strictEqual(reports[0].status, "error");
  assert.strictEqual(reports[0].error_code, "E_HTTP_503");

  const failing = vmonTrace.instrumentFetch(
    () => Promise.reject(new Error("network down")),
    { report: (event) => reports.push(event) });
  await assert.rejects(() => failing("/api/fail"), /network down/);
  assert.strictEqual(reports[1].error_code, "E_FETCH");
});

test("未命中采样时只透传业务 ID", async () => {
  let seen = null;
  const reports = [];
  const traced = vmonTrace.instrumentFetch(
    (url, init) => {
      seen = init;
      return Promise.resolve({ status: 200, ok: true });
    },
    { requestId: "REQ-2", sample: 0, report: (event) => reports.push(event) });
  await traced("/api/orders");
  assert.strictEqual(seen.headers.traceparent, undefined);
  assert.strictEqual(seen.headers["x-request-id"], "REQ-2");
  assert.strictEqual(reports.length, 0);
});
