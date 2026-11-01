#!/usr/bin/env python3
"""vmon 端到端测试。

启动真实的 vmon-server、vmon-agent 与使用 SDK 的测试业务进程，覆盖：

1. SDK → UDS → Agent WAL → Server 的 trace / 日志链路；
2. 按 request_id 查询跨进程调用链与日志；
3. 动态日志级别命令（set_log_level）真正在业务进程生效；
4. 日志模板聚类、MCP 查询、证据包导出；
5. 崩溃信号捕获与上传；
6. 中心服务停止期间 Agent 本地缓存，恢复后补传（WAL replay）。

只依赖 Python 3 标准库，退出码 0 表示全部通过。
"""

import argparse
import hashlib
import http.server
import json
import os
import queue
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

TOKEN = "e2e-token"
NODE = "e2e-node"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "clients", "python"))
import vmon_trace  # noqa: E402  (路径注入必须在 import 之前)


class E2EFailure(RuntimeError):
    pass


def log(message):
    print("[e2e] " + message, flush=True)


def now_s():
    return time.time()


def wait_for(predicate, timeout, description, interval=0.2):
    """轮询直到 predicate 返回真值；超时则抛出 E2EFailure。"""
    deadline = now_s() + timeout
    last_error = None
    while now_s() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except E2EFailure:
            raise
        except Exception as error:  # 网络未就绪时继续重试
            last_error = error
        time.sleep(interval)
    suffix = "（最后错误: %s）" % last_error if last_error else ""
    raise E2EFailure("等待超时: %s%s" % (description, suffix))


class LineReader:
    """后台读取子进程 stdout，避免管道写满阻塞。"""

    def __init__(self, stream, name):
        self.lines = queue.Queue()
        self.name = name
        self.thread = threading.Thread(target=self._run, args=(stream,), daemon=True)
        self.thread.start()

    def _run(self, stream):
        try:
            for line in stream:
                self.lines.put(line.rstrip("\n"))
        except Exception:
            pass
        finally:
            self.lines.put(None)

    def wait_line(self, predicate, timeout):
        deadline = now_s() + timeout
        seen = []
        while now_s() < deadline:
            try:
                line = self.lines.get(timeout=max(0.05, deadline - now_s()))
            except queue.Empty:
                continue
            if line is None:
                break
            seen.append(line)
            if predicate(line):
                return line
        raise E2EFailure("%s 未输出期望内容，已收到: %r" % (self.name, seen[-20:]))


class Api:
    def __init__(self, base_url):
        self.base_url = base_url

    def request(self, method, path, body=None, auth=True, timeout=10):
        data = None
        headers = {}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if auth:
            headers["X-Vmon-Token"] = TOKEN
        req = urllib.request.Request(
            self.base_url + path, data=data, headers=headers, method=method
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as response:
                payload = response.read().decode("utf-8")
                return response.status, (json.loads(payload) if payload else None)
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", "replace")
            try:
                parsed = json.loads(payload)
            except ValueError:
                parsed = {"raw": payload}
            return error.code, parsed

    def get(self, path, timeout=10):
        return self.request("GET", path, timeout=timeout)

    def raw_get(self, path, timeout=10):
        """取静态资源或任意响应原文；返回 (status, content_type, text)。"""
        req = urllib.request.Request(self.base_url + path, method="GET")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as response:
                return (response.status, response.headers.get("Content-Type", ""),
                        response.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as error:
            return (error.code, error.headers.get("Content-Type", ""),
                    error.read().decode("utf-8", "replace"))

    def raw_post(self, path, text, content_type="text/plain", timeout=10):
        """发送非 JSON 原文（剖析折叠栈等），返回 (status, parsed_json)。"""
        headers = {"Content-Type": content_type, "X-Vmon-Token": TOKEN}
        req = urllib.request.Request(
            self.base_url + path, data=text.encode("utf-8"),
            headers=headers, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as response:
                payload = response.read().decode("utf-8")
                return response.status, (json.loads(payload) if payload else None)
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", "replace")
            try:
                parsed = json.loads(payload)
            except ValueError:
                parsed = {"raw": payload}
            return error.code, parsed

    def post(self, path, body=None, timeout=10):
        return self.request("POST", path, body or {}, timeout=timeout)

    def ok(self, method, path, body=None, timeout=10):
        status, payload = self.request(method, path, body, timeout=timeout)
        if status != 200:
            raise E2EFailure("%s %s 返回 %s: %r" % (method, path, status, payload))
        return payload


def free_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def read_log(path, tail=80):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
        return "".join(lines[-tail:])
    except OSError:
        return "<无日志>"


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


class ProbeSink:
    """本地 HTTP 目标，用于验证 Agent 可用性拨测。"""

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/health":
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"ok")
            else:
                self.send_response(404)
                self.end_headers()

        def log_message(self, *_args):
            pass

    def __init__(self):
        self.server = http.server.HTTPServer(("127.0.0.1", 0), self.Handler)
        self.port = self.server.server_address[1]
        self.url = "http://127.0.0.1:%d/health" % self.port
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       daemon=True)
        self.thread.start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


class Harness:
    def __init__(self, args):
        self.args = args
        self.work_dir = tempfile.mkdtemp(prefix="vmon-e2e-")
        self.port = free_port()
        self.base_url = "http://127.0.0.1:%d" % self.port
        self.api = Api(self.base_url)
        self.server = None
        self.agent = None
        self.backend = None
        self.clients = []
        self.uds = os.path.join(self.work_dir, "agent.sock")
        self.control_dir = os.path.join(self.work_dir, "ctrl")
        self.crash_dir = os.path.join(self.work_dir, "crash")
        self.agent_state = os.path.join(self.work_dir, "agent-state")
        self.server_state = os.path.join(self.work_dir, "server-state")
        self.server_log = os.path.join(self.work_dir, "server.log")
        self.agent_log = os.path.join(self.work_dir, "agent.log")
        self.backend_port = free_port()
        self.backend_url = "http://127.0.0.1:%d" % self.backend_port
        self.backend_log = os.path.join(self.work_dir, "backend.log")
        self.probe_sink = ProbeSink()
        for path in (self.control_dir, self.crash_dir, self.agent_state,
                     self.server_state):
            os.makedirs(path, exist_ok=True)
        self._server_log_handle = None
        self._agent_log_handle = None
        self._backend_log_handle = None

    # ------------------------------------------------------------ 进程管理
    def server_command(self):
        return [
            self.args.server_bin,
            "--listen-host", "127.0.0.1",
            "--port", str(self.port),
            "--token", TOKEN,
            "--state-dir", self.server_state,
            "--web-dir", self.args.web_dir,
            "--no-forward",
            "--evidence-salt", "e2e-salt",
        ]

    def start_server(self):
        if not self._server_log_handle:
            self._server_log_handle = open(self.server_log, "a", encoding="utf-8")
        self.server = subprocess.Popen(
            self.server_command(),
            stdout=self._server_log_handle,
            stderr=subprocess.STDOUT,
        )
        wait_for(
            lambda: self.api.get("/api/health", timeout=2)[0] == 200,
            15,
            "vmon-server 健康检查",
        )
        log("server 已启动: %s" % self.base_url)

    def stop_server(self, timeout=10):
        if self.server is None:
            return
        if self.server.poll() is None:
            self.server.send_signal(signal.SIGTERM)
            try:
                self.server.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait(timeout=5)
        self.server = None
        wait_for(
            lambda: not self._port_open(),
            10,
            "server 端口关闭",
        )

    def _port_open(self):
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=0.5):
                return True
        except OSError:
            return False

    def start_agent(self):
        self._agent_log_handle = open(self.agent_log, "a", encoding="utf-8")
        command = [
            self.args.agent_bin,
            "--server-url", self.base_url,
            "--uds", self.uds,
            "--state-dir", self.agent_state,
            "--node", NODE,
            "--token", TOKEN,
            "--control-dir", self.control_dir,
            "--crash-dir", self.crash_dir,
            "--command-interval-ms", "100",
            "--scan-interval-ms", "500",
            "--upload-interval-ms", "100",
            "--process-filter", "vmon_e2e_client",
            "--process-metrics",
            "--probe", "e2e-probe=%s;interval_ms=300;timeout_ms=1000"
                       % self.probe_sink.url,
            "--wal-segment-bytes", "65536",
        ]
        self.agent = subprocess.Popen(
            command,
            stdout=self._agent_log_handle,
            stderr=subprocess.STDOUT,
        )
        wait_for(lambda: os.path.exists(self.uds), 15, "Agent UDS 创建")
        log("agent 已启动: uds=%s" % self.uds)

    def stop_agent(self):
        if self.agent is None:
            return
        if self.agent.poll() is None:
            self.agent.send_signal(signal.SIGTERM)
            try:
                self.agent.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.agent.kill()
                self.agent.wait(timeout=5)
        self.agent = None

    def _backend_ready(self):
        try:
            with urllib.request.urlopen(
                    self.backend_url + "/health", timeout=1) as response:
                return 200 <= response.status < 300
        except Exception:
            return False

    def start_backend(self):
        """启动使用真实 SDK 的 HTTP 后端，用于 Python/Web 客户端链路测试。"""
        self._backend_log_handle = open(self.backend_log, "a", encoding="utf-8")
        env = os.environ.copy()
        env["VMON_AGENT_ENDPOINT"] = self.uds
        env["VMON_CONTROL_DIR"] = self.control_dir
        env["VMON_CRASH_DIR"] = self.crash_dir
        self.backend = subprocess.Popen(
            [self.args.backend_bin, "--host", "127.0.0.1",
             "--port", str(self.backend_port)],
            stdout=self._backend_log_handle,
            stderr=subprocess.STDOUT,
            env=env,
        )
        wait_for(self._backend_ready, 15, "HTTP 后端健康检查")
        log("http backend 已启动: %s" % self.backend_url)

    def stop_backend(self):
        if self.backend is None:
            return
        if self.backend.poll() is None:
            self.backend.send_signal(signal.SIGTERM)
            try:
                self.backend.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.backend.kill()
                self.backend.wait(timeout=5)
        self.backend = None

    def start_client(self, request_id, mode="normal", duration_ms=15000,
                     service="e2e-client", trace_id="", parent_span_id="",
                     root_name="", hold_ms=5000):
        command = [
            self.args.client_bin,
            "--uds", self.uds,
            "--control-dir", self.control_dir,
            "--crash-dir", self.crash_dir,
            "--service", service,
            "--request-id", request_id,
            "--task-id", "task-" + request_id,
            "--mode", mode,
            "--duration-ms", str(duration_ms),
            "--hold-ms", str(hold_ms),
        ]
        if root_name:
            command += ["--root-name", root_name]
        if trace_id and parent_span_id:
            command += ["--trace-id", trace_id,
                        "--parent-span-id", parent_span_id]
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        reader = LineReader(process.stdout, "client(%s)" % request_id)
        self.clients.append((process, reader))
        line = reader.wait_line(lambda value: value.startswith("READY"), 10)
        parts = dict(item.split("=", 1) for item in line.split()[1:])
        pid = int(parts["pid"])
        context = {
            "trace_id": parts.get("trace_id", ""),
            "span_id": parts.get("span_id", ""),
        }
        log("client 就绪: request_id=%s pid=%d trace=%s span=%s" % (
            request_id, pid, context["trace_id"], context["span_id"]))
        return process, reader, pid, context

    def wait_client(self, process, timeout):
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            return None

    # ------------------------------------------------------------ 清理
    def cleanup(self):
        for process, _ in self.clients:
            if process.poll() is None:
                process.kill()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        self.stop_backend()
        self.stop_agent()
        self.stop_server()
        self.probe_sink.stop()
        for handle in (self._server_log_handle, self._agent_log_handle,
                       self._backend_log_handle):
            if handle:
                handle.close()
        if self.args.keep:
            log("保留工作目录: %s" % self.work_dir)
        else:
            shutil.rmtree(self.work_dir, ignore_errors=True)

    def dump_logs(self):
        log("--- server.log ---\n%s" % read_log(self.server_log))
        log("--- agent.log ---\n%s" % read_log(self.agent_log))
        log("--- backend.log ---\n%s" % read_log(self.backend_log))


# ---------------------------------------------------------------- 断言辅助
def trace_summary(detail):
    return detail["traces"][0]["summary"]


def walk_spans(node, out):
    out.append(node)
    for child in node.get("children", []):
        walk_spans(child, out)


def find_service_in_trace(detail, service):
    found = []
    for root in detail.get("roots", []):
        walk_spans(root, found)
    return any(item.get("service") == service for item in found)


def collect_spans(detail):
    spans = []
    for root in detail.get("roots", []):
        walk_spans(root, spans)
    return spans


def verify_trace_and_logs(harness, request_id):
    def request_ready():
        data = harness.api.ok(
            "GET", "/api/requests/" + urllib.parse.quote(request_id))
        traces = data.get("traces", [])
        # 接口在数据到达前就会返回空数组，必须等 trace 与日志都齐备。
        if traces and traces[0]["summary"]["span_count"] >= 3 and data.get("logs"):
            return data
        return None

    request = wait_for(request_ready, 30, "按 request_id 查询到完整 trace 与日志")
    traces = request.get("traces", [])
    if not traces:
        raise E2EFailure("request 详情中没有 trace")
    detail = traces[0]
    summary = detail["summary"]
    if summary["span_count"] < 3:
        raise E2EFailure("span 数量不足: %r" % summary)
    if summary["depth"] < 2:
        raise E2EFailure("调用树深度不足: %r" % summary)
    if detail.get("request_id") != request_id:
        raise E2EFailure("trace 未关联 request_id: %r" % detail.get("request_id"))
    if not detail.get("roots"):
        raise E2EFailure("trace 没有根节点")
    if not detail.get("critical_path"):
        raise E2EFailure("trace 没有关键路径")
    if not find_service_in_trace(detail, "e2e-client"):
        raise E2EFailure("trace 中缺少 e2e-client 服务的 Span")

    logs = request.get("logs", [])
    messages = [item.get("msg", "") for item in logs]
    if not any("e2e request started" in item for item in messages):
        raise E2EFailure("按 request_id 未查询到业务日志: %r" % messages[:10])
    if not any("e2e downstream failed" in item for item in messages):
        raise E2EFailure("未查询到错误日志: %r" % messages[:10])
    for item in logs:
        if item.get("request_id") != request_id:
            raise E2EFailure("日志 request_id 关联错误: %r" % item)
    log("PASS trace/log: trace_id=%s spans=%d depth=%d logs=%d" % (
        detail["trace_id"], summary["span_count"], summary["depth"], len(logs)))
    return detail


def verify_http_surface(harness):
    """健康检查免鉴权、业务接口必须鉴权、静态资源可直接访问。"""
    health = harness.api.ok("GET", "/api/health")
    if health.get("status") != "ok":
        raise E2EFailure("健康检查返回异常: %r" % health)
    if health.get("nodes", 0) < 1 or health.get("instances", 0) < 1:
        raise E2EFailure("资产目录未收录节点/实例: %r" % health)

    status, payload = harness.api.request("GET", "/api/overview", auth=False)
    if status != 401:
        raise E2EFailure("未带令牌访问业务接口应返回 401，实际 %s: %r" % (
            status, payload))

    status, _, html = harness.api.raw_get("/")
    if status != 200 or "vmon 维测中心" not in html:
        raise E2EFailure("Web 首页不可用: status=%s len=%d" % (status, len(html)))
    for asset, marker in (("/app.js", "showView"), ("/style.css", "--panel")):
        status, content_type, text = harness.api.raw_get(asset)
        if status != 200 or marker not in text:
            raise E2EFailure("静态资源 %s 异常: status=%s type=%s" % (
                asset, status, content_type))

    status, _ = harness.api.get("/api/traces/ffffffffffffffffffffffffffffffff")
    if status != 404:
        raise E2EFailure("不存在的 trace 应返回 404，实际 %s" % status)
    log("PASS HTTP 面: 健康检查、401、静态资源、404 均符合预期")


def verify_queries(harness, request_id, context, pid):
    """按 trace/request/task/服务/错误/耗时与日志各维度查询。"""
    trace_id = context["trace_id"]
    if not trace_id:
        raise E2EFailure("业务进程未导出 trace_id")

    def traces(params):
        return harness.api.ok(
            "GET", "/api/traces?" + urllib.parse.urlencode(params))["traces"]

    for name, params in (
            ("trace_id", {"trace_id": trace_id}),
            ("request_id", {"request_id": request_id}),
            ("task_id", {"task_id": "task-" + request_id}),
            ("service", {"service": "e2e-client"}),
            ("text", {"q": request_id}),
            ("error", {"error": "1"}),
            ("min_duration", {"min_duration_ns": "1000000"}),
    ):
        found = traces(params)
        if not any(item["trace_id"] == trace_id for item in found):
            raise E2EFailure("按 %s 查询不到目标 trace: %r" % (name, found[:3]))
    target = [item for item in traces({"trace_id": trace_id})][0]
    if not target.get("has_error"):
        raise E2EFailure("SDK set_error 后 trace 未标记为错误: %r" % target)

    def logs(params):
        return harness.api.ok(
            "GET", "/api/logs?" + urllib.parse.urlencode(params))["logs"]

    since_ns = int((time.time() - 3600) * 1000000000)
    log_cases = (
        ("trace", {"trace_id": trace_id}),
        ("request", {"request_id": request_id}),
        ("task", {"task_id": "task-" + request_id}),
        ("service", {"service": "e2e-client"}),
        ("node", {"node": NODE}),
        ("level", {"level": "error"}),
        ("errors_only", {"errors_only": "1"}),
        ("text", {"text": "downstream failed"}),
        ("time_window", {"since_ns": str(since_ns)}),
    )
    for name, params in log_cases:
        found = logs(params)
        if not any("e2e" in item.get("msg", "") for item in found):
            raise E2EFailure("按 %s 查询日志为空: %r" % (name, params))
    if any(item.get("level") != "error" for item in logs({"level": "error"})):
        raise E2EFailure("level=error 过滤返回了非 error 日志")

    services = harness.api.ok("GET", "/api/services")
    match = [item for item in services if item.get("service") == "e2e-client"]
    if not match or match[0].get("alive_instances", 0) < 1:
        raise E2EFailure("服务视图缺少存活实例: %r" % match)
    # 服务性能分析：QPS / 错误率 / P95 来自最近 5 分钟的 Span。
    if match[0].get("span_count_5m", 0) < 1:
        raise E2EFailure("服务缺少 5 分钟 Span 统计: %r" % match[0])
    if "qps_5m" not in match[0] or "p95_ms" not in match[0]:
        raise E2EFailure("服务缺少 QPS/P95 字段: %r" % match[0])
    if pid not in [item.get("pid") for item in match[0].get("instance_details", [])]:
        raise E2EFailure("服务实例中没有目标 pid=%d" % pid)

    def node_ready():
        nodes = harness.api.ok("GET", "/api/nodes")
        node = [item for item in nodes if item.get("name") == NODE]
        if not node or node[0].get("process_count", 0) < 1:
            return None
        if not any(item.get("pid") == pid
                   for item in node[0].get("processes", [])):
            return None
        return node[0]

    # 进程扫描有固定周期，刚启动时快照可能还没包含目标进程。
    node = wait_for(node_ready, 15, "节点进程快照包含 pid=%d" % pid)

    overview = harness.api.ok("GET", "/api/overview")
    if overview.get("trace_count_5m", 0) < 1 or overview.get("log_count", 0) < 1:
        raise E2EFailure("总览统计为空: %r" % overview)
    log("PASS 查询维度: trace/request/task/service/error/duration + 9 类日志过滤")


def verify_metrics(harness):
    """Agent 主机/进程指标应进入指标存储，并可按名字查询。"""
    def host_points():
        points = harness.api.ok(
            "GET", "/api/metrics?name=host.load1&limit=50")
        return points if points else None

    points = wait_for(host_points, 25, "Agent 上报 host.load1 指标")
    labels = points[0].get("labels", {})
    if labels.get("node") != NODE:
        raise E2EFailure("指标缺少节点标签: %r" % labels)
    for name in ("host.mem_used_ratio", "host.process_count",
                 "host.uptime_seconds"):
        if not harness.api.ok("GET", "/api/metrics?name=" + name + "&limit=10"):
            raise E2EFailure("缺少指标 %s" % name)

    process_points = wait_for(
        lambda: harness.api.ok(
            "GET", "/api/metrics?name=process.rss_bytes&limit=50") or None,
        25,
        "Agent 上报 process.rss_bytes 指标",
    )
    process_labels = process_points[0].get("labels", {})
    if process_labels.get("process") in (None, ""):
        raise E2EFailure("进程指标缺少 process 标签: %r" % process_points[0])
    if process_points[0].get("value", 0) <= 0:
        raise E2EFailure("进程 RSS 指标值非法: %r" % process_points[0])
    # 回归：进程名不是服务名，进程指标不能把 /bin/zsh 之类带进服务目录。
    if "service" in process_labels:
        raise E2EFailure("进程指标不应携带服务标签: %r" % process_labels)
    log("PASS 指标: host.* 与 process.rss_bytes 均可按名字查询")


def verify_probes(harness):
    """Agent HTTP 拨测：probe.up / duration / status 指标与汇总接口。"""
    points = wait_for(
        lambda: harness.api.ok("GET", "/api/metrics?name=probe.up&limit=50")
        or None, 25, "Agent 上报 probe.up 指标")
    match = [p for p in points if p.get("labels", {}).get("probe") == "e2e-probe"]
    if not match:
        raise E2EFailure("拨测指标缺少 e2e-probe 标签: %r" % points[:3])
    if match[-1].get("value", 0) < 1:
        raise E2EFailure("拨测目标应为 UP: %r" % match[-1])
    for name in ("probe.duration_ms", "probe.status_code"):
        if not harness.api.ok("GET", "/api/metrics?name=%s&limit=10" % name):
            raise E2EFailure("缺少拨测指标 %s" % name)

    summary = wait_for(
        lambda: harness.api.ok("GET", "/api/probes").get("probes") or None,
        10, "拨测汇总接口")
    target = [item for item in summary if item.get("name") == "e2e-probe"]
    if not target or not target[0].get("up"):
        raise E2EFailure("拨测汇总不是 UP: %r" % summary)
    log("PASS 可用性拨测: probe=e2e-probe url=%s" % target[0].get("url"))


def verify_cross_process(harness):
    """父进程导出上下文，子进程以 create_span_with_parent 续接同一 trace。"""
    parent_request = "e2e-parent-" + uuid.uuid4().hex[:10]
    parent, parent_reader, _, context = harness.start_client(
        parent_request, mode="parent", service="e2e-parent",
        duration_ms=1000, hold_ms=6000, root_name="E2E/ParentRoot")
    if not context["trace_id"] or not context["span_id"]:
        raise E2EFailure("父进程未导出跨进程上下文: %r" % context)

    child_request = "e2e-child-" + uuid.uuid4().hex[:10]
    child, _, _, _ = harness.start_client(
        child_request, service="e2e-child", duration_ms=500,
        trace_id=context["trace_id"], parent_span_id=context["span_id"],
        root_name="E2E/ChildRoot")
    child_code = harness.wait_client(child, 15)
    if child_code != 0:
        raise E2EFailure("子进程退出异常: %r" % child_code)

    trace_id = context["trace_id"]

    def detail_ready():
        status, detail = harness.api.get("/api/traces/" + trace_id)
        if status != 200:
            return None
        services = set()
        for root in detail.get("roots", []):
            nodes = []
            walk_spans(root, nodes)
            services.update(item.get("service") for item in nodes)
        if {"e2e-parent", "e2e-child"} <= services:
            return detail
        return None

    detail = wait_for(detail_ready, 20, "跨进程 trace 同时包含父子服务")
    nodes = []
    for root in detail.get("roots", []):
        walk_spans(root, nodes)
    child_span = [item for item in nodes if item.get("name") == "E2E/ChildRoot"]
    if not child_span:
        raise E2EFailure("trace 中缺少子进程根 span")
    if child_span[0].get("parent_span_id") != context["span_id"]:
        raise E2EFailure("子进程 span 父节点不正确: %r" % child_span[0].get(
            "parent_span_id"))
    if detail["summary"]["service_count"] < 2:
        raise E2EFailure("跨进程 trace 服务数不足: %r" % detail["summary"])

    since_ns = int((time.time() - 3600) * 1000000000)
    topology = harness.api.ok(
        "GET", "/api/topology?since_ns=%d&limit=500" % since_ns)
    edges = [(item["caller"], item["callee"]) for item in topology["edges"]]
    if ("e2e-parent", "e2e-child") not in edges:
        raise E2EFailure("拓扑缺少父子服务边: %r" % edges)
    if not any(item.get("confidence") in ("strong", "weak")
               for item in topology["edges"]):
        raise E2EFailure("拓扑边缺少置信度: %r" % topology["edges"])

    harness.wait_client(parent, 15)
    log("PASS 跨进程链路: trace=%s 两个服务、父子 span 与拓扑边均正确" % trace_id)


def verify_python_rpc(harness):
    """Python 客户端调用 C++ HTTP 后端：同一条 trace、父子 Span、错误标记。"""
    request_id = "e2e-py-" + uuid.uuid4().hex[:10]
    task_id = "task-" + request_id
    client = vmon_trace.Client(
        harness.base_url, TOKEN, service="e2e-python", instance="py-1",
        node=NODE, flush_interval=0.1)
    try:
        with client.span("Python/CallBackend", request_id=request_id,
                         task_id=task_id) as span:
            status, payload = client.request(
                "POST", harness.backend_url + "/api/echo",
                body={"hello": "world"}, request_id=request_id,
                task_id=task_id)
            if status != 200:
                raise E2EFailure("Python 调用后端返回 %s: %r" % (status, payload))
            span.set_attr("client.kind", "python")
        # 第二次调用强制后端报错，验证错误码能从 C++ 回传到整条 trace。
        error_request = request_id + "-err"
        with client.span("Python/CallBackendError", request_id=error_request,
                         task_id=task_id):
            status, _ = client.request(
                "POST", harness.backend_url + "/api/echo",
                body={"hello": "error"}, request_id=error_request,
                task_id=task_id, headers={"x-vmon-force-error": "1"})
            if status != 500:
                raise E2EFailure("强制错误调用应返回 500，实际 %s" % status)
        client.flush()
    finally:
        client.close()

    def ready():
        data = harness.api.ok(
            "GET", "/api/requests/" + urllib.parse.quote(request_id))
        traces = data.get("traces", [])
        if not traces or len(collect_spans(traces[0])) < 2:
            return None
        messages = " ".join(item.get("msg", "") for item in data.get("logs", []))
        return data if "http backend handled" in messages else None

    request = wait_for(ready, 30, "Python + C++ 后端 trace 到达")
    detail = request["traces"][0]
    spans = collect_spans(detail)
    py_spans = [item for item in spans if item.get("service") == "e2e-python"]
    backend_spans = [item for item in spans if item.get("service") == "http-backend"]
    if not py_spans or not backend_spans:
        raise E2EFailure("trace 缺少 Python 或后端服务: %r" % [
            item.get("service") for item in spans])
    root = py_spans[0]
    backend = backend_spans[0]
    if backend.get("trace_id") != root.get("trace_id"):
        raise E2EFailure("Python 与后端 trace_id 不一致")
    if backend.get("parent_span_id") != root.get("span_id"):
        raise E2EFailure("后端 span 的父节点不是 Python span: %r vs %r" % (
            backend.get("parent_span_id"), root.get("span_id")))
    if detail["summary"]["service_count"] < 2:
        raise E2EFailure("Python 链路服务数不足: %r" % detail["summary"])

    log_text = " ".join(item.get("msg", "") for item in detail.get("logs", []))
    if "http backend handled" not in log_text:
        raise E2EFailure("trace 中缺少后端处理日志: %r" % detail.get("logs", [])[:3])

    def error_ready():
        data = harness.api.ok(
            "GET", "/api/requests/" + urllib.parse.quote(request_id + "-err"))
        traces = data.get("traces", [])
        if traces and traces[0]["summary"].get("has_error"):
            return data
        return None

    error_request = wait_for(error_ready, 30, "Python 错误链路到达")
    error_detail = error_request["traces"][0]
    error_code = (error_detail.get("first_error") or {}).get("error_code", "")
    if error_code not in ("E_ECHO", "E_HTTP_500"):
        raise E2EFailure("首个错误码不正确: %r" % error_detail.get("first_error"))
    log("PASS Python RPC: trace=%s Python 与 C++ 后端同一链路，错误码=%s" % (
        root.get("trace_id"), error_code))


def verify_issues(harness):
    """错误日志/崩溃聚合成 Issue，并支持 ack / 指派 / resolve。"""
    def ready():
        data = harness.api.ok("GET", "/api/issues?limit=200")
        issues = data.get("issues", [])
        if not issues:
            return None
        return data

    data = wait_for(ready, 25, "Issue 聚合")
    issues = data["issues"]
    if not any(item.get("kind") == "log" for item in issues):
        raise E2EFailure("Issue 中没有日志异常: %r" % issues[:3])
    issue = issues[0]
    harness.api.ok("POST", "/api/issues/" + urllib.parse.quote(issue["id"]),
                   {"status": "ack", "assignee": "e2e-user"})
    acked = harness.api.ok("GET", "/api/issues?status=ack&limit=200")
    found = [item for item in acked["issues"]
             if item["id"] == issue["id"] and item.get("assignee") == "e2e-user"]
    if not found:
        raise E2EFailure("Issue ack/指派未生效: %r" % acked["issues"][:3])
    harness.api.ok("POST", "/api/issues/" + urllib.parse.quote(issue["id"]),
                   {"status": "resolved"})
    resolved = harness.api.ok("GET", "/api/issues?status=resolved&limit=200")
    if not any(item["id"] == issue["id"] for item in resolved["issues"]):
        raise E2EFailure("Issue resolve 未生效")
    log("PASS 异常追踪: Issue=%s ack→assign→resolve" % issue["id"])


def verify_silences(harness):
    """静默创建/查询/删除；规则事件抑制由单测覆盖。"""
    body = {
        "name": "e2e-silence",
        "service": "e2e-silence-service",
        "duration_minutes": 5,
        "reason": "e2e",
    }
    created = harness.api.ok("POST", "/api/silences", body)
    silence = created["silence"]
    listed = harness.api.ok("GET", "/api/silences")
    if not any(item["id"] == silence["id"] for item in listed["silences"]):
        raise E2EFailure("静默未出现在列表")
    harness.api.ok("DELETE", "/api/silences/" + urllib.parse.quote(silence["id"]))
    status, _ = harness.api.request(
        "DELETE", "/api/silences/" + urllib.parse.quote(silence["id"]))
    if status != 404:
        raise E2EFailure("重复删除静默应返回 404，实际 %s" % status)
    log("PASS 告警静默: 创建/列表/删除与 404")


def verify_dashboards(harness):
    """仪表板 CRUD、数据计算与快照。"""
    dashboard = {
        "id": "e2e-dashboard",
        "name": "E2E 看板",
        "description": "e2e",
        "widgets": [
            {"type": "metric", "title": "负载", "metric": "host.load1"},
            {"type": "traces", "title": "链路", "limit": 3},
            {"type": "text", "title": "说明", "text": "e2e"},
        ],
    }
    harness.api.ok("POST", "/api/dashboards", dashboard)
    data = harness.api.ok("GET", "/api/dashboards/e2e-dashboard")
    if len(data.get("widgets", [])) != 3:
        raise E2EFailure("仪表板 widget 数量不正确: %r" % data)
    if data["widgets"][0].get("count", 0) < 1:
        raise E2EFailure("指标 widget 没有数据: %r" % data["widgets"][0])
    snapshot = harness.api.ok("POST", "/api/dashboards/e2e-dashboard/snapshot")
    snapshot_id = snapshot["snapshot"]["id"]
    loaded = harness.api.ok("GET", "/api/snapshots/" + snapshot_id)
    if len(loaded["data"].get("widgets", [])) != 3:
        raise E2EFailure("快照数据不完整: %r" % loaded)
    harness.api.ok("DELETE", "/api/dashboards/e2e-dashboard")
    status, _ = harness.api.request("GET", "/api/dashboards/e2e-dashboard")
    if status != 404:
        raise E2EFailure("删除仪表板后应返回 404，实际 %s" % status)
    log("PASS 仪表板: 3 个 widget、指标数据、快照与删除")


def verify_events_lifecycle(harness):
    """事件注入 → open → ack → resolved，并验证筛选与幂等。"""
    event_id = "e2e-deploy-" + uuid.uuid4().hex[:8]
    service = "e2e-events-service"
    harness.api.ok("POST", "/v1/events", {
        "id": event_id,
        "type": "deploy",
        "severity": "warning",
        "title": "e2e 发布事件",
        "detail": "version e2e-2",
        "service": service,
        "node": NODE,
    })

    def listed(params):
        return harness.api.ok(
            "GET", "/api/events?" + urllib.parse.urlencode(params))["events"]

    matched = wait_for(
        lambda: [item for item in listed(
            {"status": "open", "type": "deploy", "severity": "warning",
             "service": service, "limit": "100"}) if item["id"] == event_id],
        10,
        "按状态/类型/级别/服务筛选到新事件",
    )
    if matched[0].get("status") != "open":
        raise E2EFailure("新事件初始状态不是 open: %r" % matched[0])

    harness.api.ok("POST", "/api/events/%s/ack" % event_id)
    if not [item for item in listed({"status": "ack"}) if item["id"] == event_id]:
        raise E2EFailure("ack 后事件状态未变为 ack")
    harness.api.ok("POST", "/api/events/%s/resolve" % event_id)
    if not [item for item in listed({"status": "resolved"})
            if item["id"] == event_id]:
        raise E2EFailure("resolve 后事件状态未变为 resolved")

    # 已解决事件再次 ack 不应回退状态。
    harness.api.ok("POST", "/api/events/%s/ack" % event_id)
    current = [item for item in listed({"limit": "100"}) if item["id"] == event_id]
    if not current or current[0]["status"] != "resolved":
        raise E2EFailure("ack 把 resolved 事件回退了: %r" % current)

    status, _ = harness.api.post("/api/events/not-exist/ack")
    if status != 404:
        raise E2EFailure("确认不存在的事件应返回 404，实际 %s" % status)
    log("PASS 事件生命周期: open → ack → resolved，筛选与幂等正确")


def verify_profiles(harness):
    """折叠栈入库、Top 函数与火焰图结构。"""
    folded = "\n".join([
        "main;handle;parse 100",
        "main;handle;io 50",
        "main;handle 30",
    ]) + "\n"
    status, created = harness.api.raw_post(
        "/v1/profile?service=e2e-profile&node=%s&kind=cpu" % NODE, folded)
    if status != 200 or not created.get("id"):
        raise E2EFailure("剖析入库失败: %s %r" % (status, created))
    profile_id = created["id"]

    listing = harness.api.ok(
        "GET", "/api/profiles?service=e2e-profile&limit=10")["profiles"]
    record = [item for item in listing if item["id"] == profile_id]
    if not record:
        raise E2EFailure("剖析列表缺少记录: %r" % listing)
    if record[0].get("total_samples") != 180:
        raise E2EFailure("剖析样本数不正确: %r" % record[0])
    top_self = {item["function"]: item["samples"]
                for item in record[0].get("top_self", [])}
    if top_self.get("parse") != 100 or top_self.get("io") != 50:
        raise E2EFailure("Top self 不正确: %r" % top_self)

    detail = harness.api.ok("GET", "/api/profiles/" + profile_id)
    if detail.get("flamegraph", {}).get("value") != 180:
        raise E2EFailure("火焰图根节点样本数不正确: %r" % detail.get("flamegraph"))
    children = detail["flamegraph"].get("children", [])
    if not children or children[0].get("name") != "main":
        raise E2EFailure("火焰图结构不正确: %r" % children)
    log("PASS 剖析: 总样本 180，Top self 与火焰图结构正确")


def verify_diagnose(harness, request_id):
    """一键诊断与巡检返回结构完整、能定位首个错误。"""
    report = harness.api.ok("POST", "/api/diagnose", {
        "request_id": request_id,
        "since_ns": int((time.time() - 3600) * 1000000000),
    })
    if not report.get("traces"):
        raise E2EFailure("诊断报告没有 trace: %r" % report)
    if not report.get("first_error"):
        raise E2EFailure("诊断报告没有首个错误: %r" % report)
    if not report.get("logs"):
        raise E2EFailure("诊断报告没有日志")
    if not report.get("hypotheses"):
        raise E2EFailure("诊断报告没有假设: %r" % report)
    if not report.get("checks"):
        raise E2EFailure("诊断报告没有检查项")
    if not isinstance(report.get("verdict"), str):
        raise E2EFailure("诊断结论字段缺失: %r" % report)

    inspection = harness.api.ok("GET", "/api/inspection")
    summary = inspection.get("summary", {})
    if not inspection.get("checks"):
        raise E2EFailure("巡检没有检查项")
    if not all(key in summary for key in ("ok", "warn", "fail")):
        raise E2EFailure("巡检汇总字段缺失: %r" % summary)
    log("PASS 诊断/巡检: verdict=%r hypotheses=%d checks=%d" % (
        report.get("verdict"), len(report["hypotheses"]),
        len(report["checks"])))


def verify_slo(harness):
    """SLO 定义、SLI/预算计算、非法输入与 MCP 只读工具。"""
    availability = {
        "id": "e2e-availability",
        "name": "E2E 可用性",
        "service": "e2e-client",
        "type": "availability",
        "target": 0.99,
        "window_seconds": 3600,
    }
    created = harness.api.ok("POST", "/api/slos", availability)
    slo = created.get("slo", {})
    if slo.get("total", 0) < 1:
        raise E2EFailure("SLO 没有统计到 trace: %r" % slo)
    if slo.get("bad", 0) < 1:
        raise E2EFailure("可用性 SLO 未统计到错误 trace: %r" % slo)
    if abs(slo.get("error_budget", 0) - 0.01) > 1e-9:
        raise E2EFailure("错误预算不正确: %r" % slo)
    if slo.get("status") not in ("warning", "critical"):
        raise E2EFailure("错误请求下 SLO 状态异常: %r" % slo)
    if not (0.0 <= slo.get("sli", -1) <= 1.0):
        raise E2EFailure("SLI 超出范围: %r" % slo)

    latency = {
        "id": "e2e-latency",
        "name": "E2E 延迟",
        "service": "e2e-client",
        "type": "latency",
        "target": 0.99,
        "window_seconds": 3600,
        "latency_threshold_ns": 1000,
    }
    created = harness.api.ok("POST", "/api/slos", latency)
    if created.get("slo", {}).get("bad", 0) < 1:
        raise E2EFailure("延迟 SLO 未统计到超阈值 trace: %r" % created)

    invalid = dict(availability, id="e2e-invalid", target=1.5)
    status, payload = harness.api.post("/api/slos", invalid)
    if status != 400:
        raise E2EFailure("非法 SLO 应返回 400，实际 %s: %r" % (status, payload))

    slos = harness.api.ok("GET", "/api/slos")["slos"]
    ids = [item["id"] for item in slos]
    if "e2e-availability" not in ids or "e2e-latency" not in ids:
        raise E2EFailure("SLO 列表缺少定义: %r" % ids)

    called = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0", "id": 9, "method": "tools/call",
        "params": {"name": "list_slos", "arguments": {}},
    })
    payload = json.loads(called["result"]["content"][0]["text"])
    if not any(item["id"] == "e2e-availability"
               for item in payload.get("slos", [])):
        raise E2EFailure("MCP list_slos 未返回 SLO: %r" % payload)

    harness.api.ok("DELETE", "/api/slos/e2e-latency")
    ids = [item["id"] for item in harness.api.ok("GET", "/api/slos")["slos"]]
    if "e2e-latency" in ids:
        raise E2EFailure("删除后 SLO 仍存在: %r" % ids)
    status, _ = harness.api.request("DELETE", "/api/slos/e2e-latency")
    if status != 404:
        raise E2EFailure("删除不存在的 SLO 应返回 404，实际 %s" % status)
    log("PASS SLO: 可用性/延迟 SLI、错误预算、非法输入、删除与 MCP 工具正确")


def verify_mcp_queries(harness, request_id, trace_id):
    """MCP 的 trace/拓扑查询与输入校验。"""
    called = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0", "id": 11, "method": "tools/call",
        "params": {"name": "query_traces",
                   "arguments": {"request_id": request_id}},
    })
    payload = json.loads(called["result"]["content"][0]["text"])
    if not any(item["trace_id"] == trace_id for item in payload.get("traces", [])):
        raise E2EFailure("MCP query_traces 结果不正确: %r" % payload)

    called = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0", "id": 12, "method": "tools/call",
        "params": {"name": "get_topology", "arguments": {}},
    })
    payload = json.loads(called["result"]["content"][0]["text"])
    if "edges" not in payload:
        raise E2EFailure("MCP get_topology 结构不正确: %r" % payload)

    called = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0", "id": 13, "method": "tools/call",
        "params": {"name": "unknown_tool", "arguments": {}},
    })
    if called.get("error", {}).get("code") != -32602:
        raise E2EFailure("MCP 未知工具错误码不正确: %r" % called)
    log("PASS MCP 查询: query_traces/get_topology 与错误码正确")


def verify_rules_and_persistence(harness):
    """规则与 SLO 落盘，重启后仍可用；同时清理测试数据。"""
    rule = {
        "id": "e2e-metric-rule",
        "name": "E2E 指标规则",
        "type": "metric",
        "metric": "host.load1",
        "op": ">=",
        "threshold": 1000000000,
        "window_ns": 60 * 1000000000,
        "cooldown_ns": 300 * 1000000000,
        "severity": "warning",
        "enabled": True,
    }
    harness.api.ok("POST", "/api/rules", rule)

    harness.stop_server()
    harness.start_server()

    rules = harness.api.ok("GET", "/api/rules")["rules"]
    if not any(item.get("id") == "e2e-metric-rule" for item in rules):
        raise E2EFailure("重启后规则丢失: %r" % rules)
    slos = harness.api.ok("GET", "/api/slos")["slos"]
    if not any(item.get("id") == "e2e-availability" for item in slos):
        raise E2EFailure("重启后 SLO 丢失: %r" % slos)
    # 重启后内存中的 trace 已清空，SLO 状态应为 unknown 而不是报错。
    availability = [item for item in slos if item["id"] == "e2e-availability"][0]
    if availability.get("status") != "unknown" or availability.get("total") != 0:
        raise E2EFailure("重启后空数据 SLO 状态不正确: %r" % availability)

    harness.api.ok("DELETE", "/api/rules/e2e-metric-rule")
    harness.api.ok("DELETE", "/api/slos/e2e-availability")
    log("PASS 持久化: 规则与 SLO 重启后仍在，空数据状态为 unknown")


def debug_tick_logs(harness, request_id):
    query = urllib.parse.urlencode({
        "request_id": request_id,
        "text": "e2e debug tick",
        "limit": "50",
    })
    return harness.api.ok("GET", "/api/logs?" + query)


def verify_dynamic_level(harness, request_id, pid):
    before = debug_tick_logs(harness, request_id)["count"]
    if before != 0:
        raise E2EFailure("动态级别开启前不应出现 debug 日志，实际 %d 条" % before)

    response = harness.api.ok("POST", "/api/control/log-level", {
        "pid": pid,
        "node": NODE,
        "level": "debug",
        "ttl_ms": 8000,
    })
    command_ids = response.get("commands", [])
    if len(command_ids) != 1:
        raise E2EFailure("动态级别命令下发失败: %r" % response)
    command_id = command_ids[0]

    def command_done():
        commands = harness.api.ok("GET", "/api/commands?limit=100")["commands"]
        for item in commands:
            if item.get("id") == command_id:
                return item if item.get("status") in ("done", "failed") else None
        return None

    command = wait_for(command_done, 15, "Agent 执行动态级别命令")
    if command["status"] != "done":
        raise E2EFailure("动态级别命令失败: %r" % command)
    try:
        result = json.loads(command.get("result", "{}"))
    except ValueError:
        result = {}
    if result.get("level") != "debug":
        raise E2EFailure("业务进程未确认 debug 级别: %r" % command.get("result"))

    wait_for(
        lambda: debug_tick_logs(harness, request_id)["count"] > 0,
        15,
        "开启 debug 后收到 e2e debug tick 日志",
    )
    clusters = harness.api.ok("GET", "/api/logs/clusters?limit=200")["clusters"]
    if not any("e2e debug tick" in item.get("template", "") for item in clusters):
        raise E2EFailure("debug 日志未被模板聚类: %r" % clusters[:5])
    log("PASS 动态级别: command=%s 已生效并聚类" % command_id)


def verify_mcp(harness, request_id):
    tools = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {},
    })
    names = [item["name"] for item in tools["result"]["tools"]]
    for expected in ("query_logs", "get_trace", "get_request_chain",
                     "create_bundle"):
        if expected not in names:
            raise E2EFailure("MCP 缺少工具 %s: %r" % (expected, names))

    called = harness.api.ok("POST", "/mcp", {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "get_request_chain",
            "arguments": {"request_id": request_id},
        },
    })
    content = called["result"]["content"][0]["text"]
    chain = json.loads(content)
    if chain.get("request_id") != request_id or not chain.get("traces"):
        raise E2EFailure("MCP get_request_chain 返回不完整: %r" % chain)
    log("PASS MCP: %d 个工具，get_request_chain 返回 trace" % len(names))


def verify_evidence(harness, request_id):
    # 注入一条含敏感信息的事件，验证证据包导出侧的脱敏。
    secret_id = "e2e-secret-" + uuid.uuid4().hex[:8]
    harness.api.ok("POST", "/v1/events", {
        "id": secret_id,
        "type": "change",
        "severity": "info",
        "title": "e2e 脱敏检查",
        "detail": ("password=hunter2 email=alice@example.com "
                   "ip=10.20.30.40 path=/home/alice/data"),
        "service": "e2e-secret-service",
        "node": NODE,
    })

    response = harness.api.ok("POST", "/api/evidence", {
        "incident": "e2e",
        "severity": "warning",
        "request_id": request_id,
        "window_ns": 3600 * 1000000000,
        "redact": True,
        "salt": "e2e-salt",
    })
    if not response.get("ok"):
        raise E2EFailure("证据包生成失败: %r" % response)
    pack_dir = response["dir"]
    if not os.path.isdir(pack_dir):
        raise E2EFailure("证据包目录不存在: %s" % pack_dir)
    manifest = response.get("manifest") or {}
    if manifest.get("schema_version") != "vmon-evidence-1":
        raise E2EFailure("证据包 manifest 版本不正确: %r" % manifest)
    if manifest.get("pack_id") != response.get("id"):
        raise E2EFailure("manifest pack_id 不一致")
    files = manifest.get("files", [])
    if not files:
        raise E2EFailure("manifest 文件列表为空")
    for item in files:
        path = os.path.join(pack_dir, item["path"])
        if not os.path.isfile(path):
            raise E2EFailure("证据包缺少文件: %s" % path)
        if os.path.getsize(path) != item["size"]:
            raise E2EFailure("证据包文件大小不一致: %s" % path)
        if len(item["sha256"]) != 64 or file_sha256(path) != item["sha256"]:
            raise E2EFailure("证据包文件校验失败: %s" % path)
    # manifest 自身不参与自校验，但文件必须存在且可解析（response 已包含其内容）。
    manifest_path = os.path.join(pack_dir, "manifest.json")
    if not os.path.isfile(manifest_path):
        raise E2EFailure("证据包缺少 manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        if json.load(handle).get("pack_id") != manifest.get("pack_id"):
            raise E2EFailure("磁盘上的 manifest 内容不一致")

    timeline_path = os.path.join(pack_dir, "timeline.json")
    with open(timeline_path, "r", encoding="utf-8") as handle:
        timeline_text = handle.read()
    for secret in ("hunter2", "alice@example.com", "10.20.30.40", "/home/alice"):
        if secret in timeline_text:
            raise E2EFailure("证据包时间线未脱敏: %s" % secret)
    for marker in ("<redacted>", "<email:", "<ip:", "/home/<user>"):
        if marker not in timeline_text:
            raise E2EFailure("证据包时间线缺少脱敏标记 %s" % marker)
    if not manifest.get("redaction", {}).get("enabled"):
        raise E2EFailure("manifest 未标记已脱敏: %r" % manifest.get("redaction"))
    log("PASS 证据包: %s (%d 个文件，时间线脱敏)" % (
        os.path.basename(pack_dir), len(files)))


def verify_crash(harness):
    request_id = "e2e-crash-" + uuid.uuid4().hex[:10]
    process, _, _, _ = harness.start_client(
        request_id, mode="crash", duration_ms=1000, service="e2e-crash")
    code = harness.wait_client(process, 15)
    # SDK 崩溃处理器写完报告后 _exit(128+signal)；被内核直接终止时为负值。
    if code not in (128 + signal.SIGSEGV, -signal.SIGSEGV):
        raise E2EFailure("崩溃进程退出码异常: %r" % code)

    crashes = wait_for(
        lambda: harness.api.ok("GET", "/api/crashes?service=e2e-crash")["crashes"],
        25,
        "崩溃报告上传",
    )
    crash = crashes[0]
    if crash.get("signal") != "SIGSEGV":
        raise E2EFailure("崩溃信号不正确: %r" % crash)
    if crash.get("node") != NODE:
        raise E2EFailure("崩溃记录节点不正确: %r" % crash)
    events = harness.api.ok("GET", "/api/events?limit=100")["events"]
    if not any(item.get("type") == "crash" for item in events):
        raise E2EFailure("事件中心没有崩溃事件")
    log("PASS 崩溃捕获: signal=%s count=%s" % (crash.get("signal"),
                                                 crash.get("count")))


def verify_offline_replay(harness):
    # 中心服务停止期间业务进程继续写入 Agent，恢复后应补传。
    harness.stop_server()
    time.sleep(0.5)
    request_id = "e2e-offline-" + uuid.uuid4().hex[:10]
    process, _, _, _ = harness.start_client(request_id, duration_ms=2000)
    code = harness.wait_client(process, 20)
    if code != 0:
        raise E2EFailure("离线期间业务进程退出异常: %r" % code)
    # 等待 Agent 封存当前 WAL 段并至少失败重试一次。
    time.sleep(2.5)

    harness.start_server()

    def replayed():
        data = harness.api.ok(
            "GET", "/api/requests/" + urllib.parse.quote(request_id))
        traces = data.get("traces", [])
        if traces and traces[0]["summary"]["span_count"] >= 3:
            return data
        return None

    request = wait_for(replayed, 30, "服务恢复后 WAL 补传")
    traces = request.get("traces", [])
    if not traces or traces[0]["summary"]["span_count"] < 3:
        raise E2EFailure("补传的 trace 不完整: %r" % request)
    log("PASS WAL 补传: request_id=%s spans=%d" % (
        request_id, traces[0]["summary"]["span_count"]))


def main():
    parser = argparse.ArgumentParser(description="vmon 端到端测试")
    parser.add_argument("--server-bin", required=True)
    parser.add_argument("--agent-bin", required=True)
    parser.add_argument("--client-bin", required=True)
    parser.add_argument("--backend-bin", required=True)
    parser.add_argument("--web-dir", required=True)
    parser.add_argument("--keep", action="store_true", help="保留临时目录")
    args = parser.parse_args()

    harness = Harness(args)
    failure = None
    try:
        harness.start_server()
        harness.start_agent()
        harness.start_backend()

        request_id = "e2e-req-" + uuid.uuid4().hex[:10]
        process, _, pid, context = harness.start_client(
            request_id, duration_ms=30000)
        verify_trace_and_logs(harness, request_id)
        verify_queries(harness, request_id, context, pid)
        verify_http_surface(harness)
        verify_metrics(harness)
        verify_probes(harness)
        verify_dynamic_level(harness, request_id, pid)
        verify_mcp(harness, request_id)
        verify_cross_process(harness)
        verify_python_rpc(harness)
        verify_events_lifecycle(harness)
        verify_issues(harness)
        verify_silences(harness)
        verify_dashboards(harness)
        verify_profiles(harness)
        verify_diagnose(harness, request_id)
        verify_evidence(harness, request_id)
        verify_slo(harness)
        verify_mcp_queries(harness, request_id, context["trace_id"])
        verify_rules_and_persistence(harness)
        verify_crash(harness)

        # 正常业务进程验证完毕后退出，再做断网补传。
        if process.poll() is None:
            process.terminate()
        harness.wait_client(process, 10)

        verify_offline_replay(harness)
        log("全部端到端用例通过")
    except E2EFailure as error:
        failure = error
        log("FAIL: %s" % error)
    except Exception as error:  # 未预期异常也要输出现场
        failure = error
        import traceback
        traceback.print_exc()
        log("FAIL(异常): %s" % error)
    finally:
        try:
            harness.dump_logs()
        finally:
            harness.cleanup()

    if failure is not None:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
