#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vmon Python 客户端：让 Python 服务/脚本成为调用链的起点。

设计目标：
- 只依赖 Python 标准库，离线可用；
- 默认 fail-open：中心不可用、token 错误、序列化失败都不影响业务；
- 传播 W3C traceparent / x-request-id / x-task-id，后端 C++ SDK 可直接续接；
- 可选把 Python 侧的 client span 直接上报到 vmon-server（/v1/ingest）。

最小用法：

    from vmon_trace import Client

    client = Client("http://127.0.0.1:18428", "dev-token",
                    service="py-bff", node="node-1")
    with client.span("GET /orders", request_id="REQ-1") as span:
        status, body = client.request(
            "GET", "http://backend:9000/api/orders",
            request_id="REQ-1")
        span.set_attr("http.status_code", status)
    client.close()

后端（C++）只需要从 HTTP 头里取 traceparent，然后：

    vmon::SpanContext parent;
    if (vmon::context_from_traceparent(traceparent, &parent)) {
      vmon::Span span = vmon::create_span_with_parent(parent, "Handle", __FILE__, __LINE__);
    }

注意：浏览器端不要直接持有中心写 token；Web 场景应由 BFF 注入上下文并汇总事件。
"""

import json
import os
import queue
import random
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

__all__ = [
    "TraceContext", "Span", "Client",
    "new_context", "parse_traceparent", "make_traceparent",
]


def _now_ns():
    try:
        return time.time_ns()
    except AttributeError:  # Python < 3.7 兜底
        return int(time.time() * 1e9)


def _hex(value, width):
    return format(value, "0%dx" % width)


def _random_hex(width):
    """生成 width 个十六进制字符，且不全为 0。"""
    while True:
        value = random.getrandbits(width * 4)
        if value:
            return _hex(value, width)


class TraceContext(object):
    """一次调用链上下文：trace_id 32 hex，span_id 16 hex。"""

    __slots__ = ("trace_id", "span_id", "sampled")

    def __init__(self, trace_id, span_id, sampled=True):
        self.trace_id = trace_id
        self.span_id = span_id
        self.sampled = bool(sampled)

    def __repr__(self):
        return "TraceContext(trace=%s, span=%s)" % (
            self.trace_id, self.span_id)


def new_context(parent=None):
    """基于父上下文创建新 span；没有父上下文时新建 trace。"""
    if parent is None:
        return TraceContext(_random_hex(32), _random_hex(16))
    return TraceContext(parent.trace_id, _random_hex(16), parent.sampled)


def _is_hex(value):
    if not value:
        return False
    for ch in value:
        if ch not in "0123456789abcdefABCDEF":
            return False
    return True


def parse_traceparent(value):
    """解析 W3C traceparent；非法返回 None，调用方应新建 trace。"""
    if not value or len(value) != 55:
        return None
    if value[2] != "-" or value[35] != "-" or value[52] != "-":
        return None
    if value[:2] not in ("00",):
        return None
    trace_id = value[3:35].lower()
    span_id = value[36:52].lower()
    flags = value[53:55]
    if not _is_hex(trace_id) or not _is_hex(span_id) or not _is_hex(flags):
        return None
    if int(trace_id, 16) == 0 or int(span_id, 16) == 0:
        return None
    return TraceContext(trace_id, span_id, int(flags, 16) != 0)


def make_traceparent(context, sampled=None):
    """生成 W3C traceparent 头。"""
    if sampled is None:
        sampled = context.sampled
    return "00-%s-%s-%s" % (
        context.trace_id, context.span_id, "01" if sampled else "00")


class Span(object):
    """Python 侧 client span；作为上下文管理器使用。"""

    def __init__(self, client, name, context, parent_span, request_id=None,
                 task_id=None, attrs=None):
        self.client = client
        self.name = name
        self.context = context
        self.parent_span = parent_span or ""
        self.request_id = request_id or ""
        self.task_id = task_id or ""
        self.attrs = dict(attrs or {})
        self.error_code = ""
        self.start_ns = _now_ns()
        self.end_ns = 0
        self._ended = False

    def set_attr(self, key, value):
        self.attrs[str(key)] = str(value)

    def set_error(self, error_code):
        self.error_code = str(error_code)

    def __enter__(self):
        self.client._push(self)
        self.client._emit_begin(self)
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc is not None and not self.error_code:
            self.set_error("E_PY_EXCEPTION")
        self.end()
        self.client._pop(self)
        return False

    def end(self):
        if self._ended:
            return
        self._ended = True
        self.end_ns = _now_ns()
        self.client._emit_end(self)

    def headers(self):
        headers = {
            "traceparent": make_traceparent(self.context),
        }
        if self.request_id:
            headers["x-request-id"] = self.request_id
        if self.task_id:
            headers["x-task-id"] = self.task_id
        return headers


class Client(object):
    """轻量 Python 客户端：trace 上下文 + 可选事件上报。"""

    def __init__(self, server_url="", token="", service="python-client",
                 instance="", node="", service_version="",
                 flush_interval=1.0, queue_size=4096, enabled=True):
        self.server_url = (server_url or "").rstrip("/")
        self.token = token or ""
        self.service = service or "python-client"
        self.instance = instance or "%s-%d" % (socket.gethostname(), os.getpid())
        self.node = node or ""
        self.service_version = service_version or ""
        self.enabled = bool(enabled)
        self.flush_interval = max(0.05, float(flush_interval or 0))
        self._queue = queue.Queue(maxsize=max(64, int(queue_size)))
        self._dropped = 0
        self._sent = 0
        self._last_error = ""
        self._local = threading.local()
        self._stop = threading.Event()
        self._worker = None
        if self.enabled and self.server_url:
            self._worker = threading.Thread(
                target=self._flush_loop, name="vmon-python-flush", daemon=True)
            self._worker.start()

    # ------------------------------------------------------------ 上下文
    def _stack(self):
        stack = getattr(self._local, "stack", None)
        if stack is None:
            stack = []
            self._local.stack = stack
        return stack

    def current(self):
        stack = self._stack()
        return stack[-1] if stack else None

    def _push(self, span):
        self._stack().append(span)

    def _pop(self, span):
        stack = self._stack()
        if stack and stack[-1] is span:
            stack.pop()
        elif span in stack:
            stack.remove(span)

    def span(self, name, traceparent=None, request_id=None, task_id=None,
             attrs=None):
        """创建 span；traceparent 为空时继承当前线程的 span。"""
        parent = None
        if traceparent:
            parent = parse_traceparent(traceparent)
        if parent is None:
            current = self.current()
            parent = current.context if current is not None else None
        context = new_context(parent)
        return Span(self, name, context, parent.span_id if parent else "",
                    request_id=request_id, task_id=task_id, attrs=attrs)

    def headers(self, traceparent=None, request_id=None, task_id=None):
        """生成要传给后端的传播头；不在 span 内时新建一个临时上下文。"""
        context = None
        if traceparent:
            context = parse_traceparent(traceparent)
        if context is None and self.current() is not None:
            context = self.current().context
        if context is None:
            context = new_context()
        headers = {"traceparent": make_traceparent(context)}
        if request_id:
            headers["x-request-id"] = request_id
        if task_id:
            headers["x-task-id"] = task_id
        return headers

    # ------------------------------------------------------------ HTTP 调用
    def request(self, method, url, body=None, headers=None, request_id=None,
                task_id=None, timeout=10, span_name=None):
        """带 trace 传播的 HTTP 调用，返回 (status, body_bytes)。

        没有外层 span 时自动创建一个 client span。
        """
        headers = dict(headers or {})
        current = self.current()
        if current is None:
            with self.span(span_name or ("%s %s" % (method, url)),
                           request_id=request_id, task_id=task_id) as span:
                return self._do_request(method, url, body, headers,
                                        request_id, task_id, timeout, span)
        return self._do_request(method, url, body, headers, request_id,
                                task_id, timeout, current)

    def _do_request(self, method, url, body, headers, request_id, task_id,
                    timeout, span):
        # 只在调用处补充缺省头，已有值不覆盖（允许调用方自定义父链路）。
        if "traceparent" not in headers:
            headers["traceparent"] = make_traceparent(span.context)
        if request_id and "x-request-id" not in headers:
            headers["x-request-id"] = request_id
        if task_id and "x-task-id" not in headers:
            headers["x-task-id"] = task_id
        data = body
        if isinstance(body, dict):
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            headers.setdefault("Content-Type", "application/json")
        elif isinstance(body, str):
            data = body.encode("utf-8")
        request = urllib.request.Request(
            url, data=data, headers=headers, method=method)
        status = 0
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                status = response.status
                payload = response.read()
        except urllib.error.HTTPError as error:
            status = error.code
            payload = error.read()
            error.close()
        except Exception:
            span.set_error("E_HTTP_EXCEPTION")
            raise
        span.set_attr("http.status_code", status)
        span.set_attr("http.method", method)
        span.set_attr("http.url", url)
        if status >= 500:
            span.set_error("E_HTTP_%d" % status)
        return status, payload

    def log(self, level, message, request_id=None, task_id=None, attrs=None):
        """发送一条与当前 span 关联的日志；没有 span 时只带业务 ID。"""
        current = self.current()
        event = {
            "type": "log",
            "time_ns": _now_ns(),
            "service": self.service,
            "instance": self.instance,
            "level": level,
            "msg": message,
        }
        if self.node:
            event["node"] = self.node
        if self.service_version:
            event["version"] = self.service_version
        if current is not None:
            event["trace"] = current.context.trace_id
            event["span"] = current.context.span_id
            if current.parent_span:
                event["parent"] = current.parent_span
        if request_id:
            event["request_id"] = request_id
        if task_id:
            event["task_id"] = task_id
        if attrs:
            event["attrs"] = {str(k): str(v) for k, v in attrs.items()}
        self._enqueue(event)

    # ------------------------------------------------------------ 事件上报
    def _base_event(self, kind, span):
        event = {
            "type": kind,
            "time_ns": _now_ns(),
            "trace": span.context.trace_id,
            "span": span.context.span_id,
            "service": self.service,
            "instance": self.instance,
            "name": span.name,
        }
        if self.node:
            event["node"] = self.node
        if self.service_version:
            event["version"] = self.service_version
        if span.parent_span:
            event["parent"] = span.parent_span
        if span.request_id:
            event["request_id"] = span.request_id
        if span.task_id:
            event["task_id"] = span.task_id
        if span.attrs:
            event["attrs"] = {str(k): str(v) for k, v in span.attrs.items()}
        return event

    def _emit_begin(self, span):
        event = self._base_event("span_begin", span)
        event["time_ns"] = span.start_ns
        self._enqueue(event)

    def _emit_end(self, span):
        event = self._base_event("span_end", span)
        event["time_ns"] = span.end_ns or _now_ns()
        attrs = event.setdefault("attrs", {})
        attrs["duration_ns"] = str(max(0, event["time_ns"] - span.start_ns))
        if span.error_code:
            event["error_code"] = span.error_code
            attrs["error"] = "true"
        self._enqueue(event)

    def _enqueue(self, event):
        if not self.enabled or not self.server_url:
            return
        try:
            self._queue.put_nowait(json.dumps(event, ensure_ascii=False))
        except queue.Full:
            self._dropped += 1

    def flush(self, timeout=5):
        """同步发送队列中的事件；失败不抛异常。"""
        if not self.server_url:
            return True
        lines = []
        while True:
            try:
                lines.append(self._queue.get_nowait())
            except queue.Empty:
                break
        if not lines:
            return True
        data = ("\n".join(lines) + "\n").encode("utf-8")
        url = self.server_url + "/v1/ingest"
        if self.node:
            url += "?node=" + urllib.parse.quote(self.node)
        headers = {
            "Content-Type": "application/x-ndjson",
            "X-Vmon-Agent": "python-client",
        }
        if self.token:
            headers["X-Vmon-Token"] = self.token
        request = urllib.request.Request(
            url, data=data, method="POST", headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                response.read()
            self._sent += len(lines)
            return True
        except Exception as error:  # fail-open：只记录，不影响业务
            self._last_error = str(error)
            return False

    def _flush_loop(self):
        while not self._stop.wait(self.flush_interval):
            self.flush()

    @property
    def dropped(self):
        return self._dropped

    @property
    def sent(self):
        return self._sent

    @property
    def last_error(self):
        return self._last_error

    def close(self):
        self._stop.set()
        if self._worker is not None:
            self._worker.join(timeout=2)
            self._worker = None
        self.flush()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
