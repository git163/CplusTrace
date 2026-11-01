#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vmon_trace 单元测试，只依赖标准库。

运行：
    python3 -m unittest discover -s clients/python -p 'test_*.py'
"""

import http.server
import threading
import unittest

import vmon_trace


class TraceparentTest(unittest.TestCase):
    def test_round_trip(self):
        context = vmon_trace.new_context()
        header = vmon_trace.make_traceparent(context)
        self.assertEqual(55, len(header))
        parsed = vmon_trace.parse_traceparent(header)
        self.assertIsNotNone(parsed)
        self.assertEqual(context.trace_id, parsed.trace_id)
        self.assertEqual(context.span_id, parsed.span_id)
        self.assertTrue(parsed.sampled)

    def test_invalid_headers(self):
        for value in (None, "", "bad",
                      "01-0123456789abcdef0123456789abcdef-1111222233334444-01",
                      "00-00000000000000000000000000000000-1111222233334444-01",
                      "00-0123456789abcdef0123456789abcdef-0000000000000000-01",
                      "00-zzzz456789abcdef0123456789abcdef-1111222233334444-01"):
            self.assertIsNone(vmon_trace.parse_traceparent(value), value)

    def test_child_keeps_trace_and_changes_span(self):
        parent = vmon_trace.new_context()
        child = vmon_trace.new_context(parent)
        self.assertEqual(parent.trace_id, child.trace_id)
        self.assertNotEqual(parent.span_id, child.span_id)


class SpanTest(unittest.TestCase):
    def setUp(self):
        self.client = vmon_trace.Client(enabled=True, server_url="", token="")
        self.events = []
        self.client._enqueue = self.events.append

    def test_span_emits_begin_and_end(self):
        with self.client.span("Python/Handle", request_id="R1",
                              task_id="T1") as span:
            span.set_attr("stage", "decode")
        self.assertEqual(2, len(self.events))
        begin, end = self.events
        self.assertEqual("span_begin", begin["type"])
        self.assertEqual("span_end", end["type"])
        self.assertEqual(begin["trace"], end["trace"])
        self.assertEqual(begin["span"], end["span"])
        self.assertEqual("R1", end["request_id"])
        self.assertEqual("T1", end["task_id"])
        self.assertEqual("decode", end["attrs"]["stage"])
        self.assertGreaterEqual(int(end["attrs"]["duration_ns"]), 0)

    def test_span_error_is_marked(self):
        with self.client.span("Python/Fail") as span:
            span.set_error("E_PY_TEST")
        end = self.events[-1]
        self.assertEqual("E_PY_TEST", end["error_code"])
        self.assertEqual("true", end["attrs"]["error"])

    def test_nested_span_has_parent(self):
        with self.client.span("outer") as outer:
            with self.client.span("inner") as inner:
                self.assertEqual(outer.context.trace_id, inner.context.trace_id)
                self.assertEqual(outer.context.span_id, inner.parent_span)
        inner_end = [e for e in self.events if e["name"] == "inner"][-1]
        self.assertEqual(outer.context.span_id, inner_end["parent"])

    def test_traceparent_inherits_trace(self):
        parent = vmon_trace.new_context()
        with self.client.span("child", traceparent=vmon_trace.make_traceparent(parent)) as span:
            self.assertEqual(parent.trace_id, span.context.trace_id)
            self.assertEqual(parent.span_id, span.parent_span)


class _Handler(http.server.BaseHTTPRequestHandler):
    status = 500
    seen_headers = {}

    def do_POST(self):
        type(self).seen_headers = dict(self.headers)
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        self.send_response(type(self).status)
        self.end_headers()
        self.wfile.write(b'{"ok":false}')

    def log_message(self, *_args):
        pass


class RequestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.HTTPServer(("127.0.0.1", 0), _Handler)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def test_request_injects_headers_and_marks_5xx(self):
        client = vmon_trace.Client(enabled=True, server_url="", token="")
        with client.span("Python/Call", request_id="R-http") as span:
            status, body = client.request(
                "POST", "http://127.0.0.1:%d/api" % self.port,
                body={"hello": 1}, request_id="R-http")
            self.assertEqual(500, status)
            self.assertEqual(b'{"ok":false}', body)
            self.assertEqual("E_HTTP_500", span.error_code)
            self.assertEqual("500", span.attrs["http.status_code"])
            seen = {key.lower(): value for key, value in _Handler.seen_headers.items()}
            self.assertEqual("R-http", seen.get("x-request-id"))
            self.assertTrue(seen.get("traceparent", "").startswith("00-"))

    def test_request_auto_creates_span(self):
        client = vmon_trace.Client(enabled=True, server_url="", token="")
        status, _ = client.request(
            "POST", "http://127.0.0.1:%d/api" % self.port, body=b"{}")
        self.assertEqual(500, status)


if __name__ == "__main__":
    unittest.main()
