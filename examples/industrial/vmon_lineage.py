#!/usr/bin/env python3
"""工业控制示例的血缘事件发射器。

不依赖 SDK：直接把 span / lineage / state 事件以 NDJSON 形式 POST 到
vmon-server 的 /v1/ingest，与 C++ SDK 走同一条事件流。

设计要点：
  - 只发值指纹（value_hash），gate 需要的少量字段（计数/身份/时序）额外带 value；
  - run_id + iteration_id 贯穿所有事件，供 Server 按迭代拼接；
  - 一个进程一个 Emitter，内部批量发送。
"""

import json
import os
import socket
import threading
import time
import urllib.error
import urllib.request
import uuid

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = 0xFFFFFFFFFFFFFFFF


def fnv1a64(text):
    h = FNV_OFFSET
    for b in text.encode("utf-8", "replace"):
        h ^= b
        h = (h * FNV_PRIME) & MASK64
    return h


def fingerprint(text):
    return "%016x" % fnv1a64(text)


def now_ns():
    return int(time.time() * 1e9)


def new_span_id():
    return "%016x" % (uuid.uuid4().int & MASK64 or 1)


def new_trace_id():
    return uuid.uuid4().hex


class Emitter:
    def __init__(self, server_url, service, node="node-1", instance=None,
                 batch=64, verbose=False):
        self.server_url = server_url.rstrip("/")
        self.service = service
        self.node = node
        self.instance = instance or ("%s-%d" % (socket.gethostname(), os.getpid()))
        self.batch = batch
        self.verbose = verbose
        self._buf = []
        self._lock = threading.Lock()

    # ---------------------------------------------------------------- 基础
    def _base(self, event_type, name, run_id, iteration_id, trace=None,
              span=None, parent=None):
        ev = {
            "type": event_type,
            "time_ns": now_ns(),
            "mono_ns": time.monotonic_ns(),
            "service": self.service,
            "instance": self.instance,
            "node": self.node,
            "name": name,
        }
        if run_id:
            ev["run_id"] = run_id
        if iteration_id:
            ev["iteration_id"] = iteration_id
        if trace:
            ev["trace"] = trace
        if span:
            ev["span"] = span
        if parent:
            ev["parent"] = parent
        return ev

    def _enqueue(self, ev):
        with self._lock:
            self._buf.append(ev)
            if len(self._buf) >= self.batch:
                self._flush_locked()

    def _flush_locked(self):
        if not self._buf:
            return
        payload = "\n".join(json.dumps(e, ensure_ascii=False) for e in self._buf)
        self._buf = []
        try:
            req = urllib.request.Request(
                self.server_url + "/v1/ingest",
                data=payload.encode("utf-8"),
                headers={"Content-Type": "application/x-ndjson"},
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                resp.read()
        except (urllib.error.URLError, OSError) as exc:
            if self.verbose:
                print("[lineage] 发送失败: %s" % exc, flush=True)

    def flush(self):
        with self._lock:
            self._flush_locked()

    # ---------------------------------------------------------------- span
    def span_begin(self, name, run_id, iteration_id, trace, span, parent=None,
                   kind="internal"):
        ev = self._base("span_begin", name, run_id, iteration_id, trace, span, parent)
        ev["attrs"] = {"kind": kind}
        self._enqueue(ev)

    def span_end(self, name, run_id, iteration_id, trace, span, parent=None,
                 error_code=None, status=None):
        ev = self._base("span_end", name, run_id, iteration_id, trace, span, parent)
        attrs = {}
        if error_code:
            ev["error_code"] = error_code
            attrs["error"] = "true"
        if status:
            attrs["status"] = status
        if attrs:
            ev["attrs"] = attrs
        self._enqueue(ev)

    # ---------------------------------------------------------------- 血缘
    def data(self, run_id, iteration_id, op, items, trace=None, span=None,
             service=None):
        """items: [{object, field, dir, kind, value, raw}]。

        raw=True 时同时上报原文（仅计数/身份/时序等低基数、非敏感字段）。
        """
        ev = self._base("lineage", op, run_id, iteration_id, trace, span)
        if service:
            ev["service"] = service
        refs = []
        for item in items:
            value = item.get("value", "")
            text = str(value)
            ref = {
                "object": item.get("object", ""),
                "field": item.get("field", ""),
                "dir": item.get("dir", "local"),
                "kind": item.get("kind", "str"),
                "value_hash": fingerprint(text),
                "value_len": len(text),
                "op": op,
                "reason": "head",
            }
            if item.get("raw", False):
                ref["value"] = text
            if item.get("shape_hash"):
                ref["shape_hash"] = item["shape_hash"]
            refs.append(ref)
        ev["data"] = refs
        self._enqueue(ev)

    # ---------------------------------------------------------------- 状态
    def state(self, run_id, iteration_id, device, state, source="controller"):
        ev = self._base("state", "state", run_id, iteration_id)
        ev["device"] = device
        ev["state"] = state
        ev["attrs"] = {"source": source}
        self._enqueue(ev)
