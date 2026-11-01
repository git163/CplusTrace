#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""常用维测场景演示：一条命令启动真实 Server/Agent/HTTP 后端并造场景。

用法：
    python3 examples/scenarios/demo.py --scenario rpc
    python3 examples/scenarios/demo.py --scenario error --keep
    python3 examples/scenarios/demo.py --scenario all

场景：
    rpc       Python 调用 C++ 后端，验证跨语言同一条 trace
    slow      慢调用：关键路径、最慢 Span
    error     错误链路：首个错误、错误码、一键诊断
    logstorm  日志风暴：模板聚类、采样与去重
    slo       创建可用性 SLO，看错误预算与燃烧率
    crash     SIGSEGV 捕获、崩溃列表
    all       依次运行以上场景

只依赖 Python 标准库与项目内构建产物；--keep 会保留栈和临时目录，便于打开 Web UI。
"""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "clients", "python"))
import vmon_trace  # noqa: E402

TOKEN = "demo-token"
NODE = "demo-node"


def log(message):
    print(message, flush=True)


def free_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def wait_for(predicate, timeout, description, interval=0.2):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except Exception as error:
            last = error
        time.sleep(interval)
    raise RuntimeError("等待超时: %s%s" % (
        description, "（最后错误: %s）" % last if last else ""))


class Stack(object):
    def __init__(self, keep=False):
        self.keep = keep
        self.work_dir = tempfile.mkdtemp(prefix="vmon-demo-")
        self.server_port = free_port()
        self.backend_port = free_port()
        self.base_url = "http://127.0.0.1:%d" % self.server_port
        self.backend_url = "http://127.0.0.1:%d" % self.backend_port
        self.uds = os.path.join(self.work_dir, "agent.sock")
        self.control_dir = os.path.join(self.work_dir, "ctrl")
        self.crash_dir = os.path.join(self.work_dir, "crash")
        self.server_dir = os.path.join(self.work_dir, "server")
        self.agent_dir = os.path.join(self.work_dir, "agent")
        self.server_log = os.path.join(self.work_dir, "server.log")
        self.agent_log = os.path.join(self.work_dir, "agent.log")
        self.backend_log = os.path.join(self.work_dir, "backend.log")
        self.server = None
        self.agent = None
        self.backend = None
        self.handles = []
        for path in (self.control_dir, self.crash_dir, self.server_dir,
                     self.agent_dir):
            os.makedirs(path, exist_ok=True)

    def binary(self, name):
        candidates = [
            os.path.join(ROOT, "build", name),
            os.path.join(ROOT, "build", "examples", "http_backend", name),
            os.path.join(ROOT, "build", "tests", name),
            os.path.join(ROOT, "build", "tools", name),
        ]
        for path in candidates:
            if os.path.exists(path):
                return path
        raise RuntimeError("找不到 %s，请先构建：cmake --build build -j" % name)

    def start(self):
        server_log = open(self.server_log, "a", encoding="utf-8")
        agent_log = open(self.agent_log, "a", encoding="utf-8")
        backend_log = open(self.backend_log, "a", encoding="utf-8")
        self.handles = [server_log, agent_log, backend_log]
        self.server = subprocess.Popen(
            [self.binary("vmon_server"), "--listen-host", "127.0.0.1",
             "--port", str(self.server_port), "--token", TOKEN,
             "--state-dir", self.server_dir,
             "--web-dir", os.path.join(ROOT, "web"), "--no-forward"],
            stdout=server_log, stderr=subprocess.STDOUT)
        wait_for(lambda: self.get("/api/health")[0] == 200, 15,
                 "vmon-server 启动")
        self.agent = subprocess.Popen(
            [self.binary("vmon_agent"), "--server-url", self.base_url,
             "--uds", self.uds, "--state-dir", self.agent_dir,
             "--node", NODE, "--token", TOKEN,
             "--control-dir", self.control_dir,
             "--crash-dir", self.crash_dir,
             "--command-interval-ms", "100",
             "--scan-interval-ms", "500",
             "--upload-interval-ms", "100"],
            stdout=agent_log, stderr=subprocess.STDOUT)
        wait_for(lambda: os.path.exists(self.uds), 15, "vmon-agent 启动")
        env = os.environ.copy()
        env["VMON_AGENT_ENDPOINT"] = self.uds
        env["VMON_CONTROL_DIR"] = self.control_dir
        env["VMON_CRASH_DIR"] = self.crash_dir
        self.backend = subprocess.Popen(
            [self.binary("vmon_http_backend"), "--host", "127.0.0.1",
             "--port", str(self.backend_port)],
            stdout=backend_log, stderr=subprocess.STDOUT, env=env)
        wait_for(lambda: self.get("/health", base=self.backend_url)[0] == 200,
                 15, "HTTP 后端启动")
        log("[demo] Server : %s/" % self.base_url)
        log("[demo] 令牌   : %s" % TOKEN)
        log("[demo] 后端   : %s/api/echo" % self.backend_url)
        log("[demo] 工作目录: %s" % self.work_dir)

    def get(self, path, base=None, timeout=5):
        request = urllib.request.Request((base or self.base_url) + path)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                payload = response.read().decode("utf-8")
                return response.status, (json.loads(payload) if payload else None)
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", "replace")
            try:
                return error.code, json.loads(payload)
            except ValueError:
                return error.code, {"raw": payload}

    def api(self, method, path, body=None, timeout=10):
        data = None
        headers = {"X-Vmon-Token": TOKEN}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            self.base_url + path, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                payload = response.read().decode("utf-8")
                return response.status, (json.loads(payload) if payload else None)
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", "replace")
            try:
                return error.code, json.loads(payload)
            except ValueError:
                return error.code, {"raw": payload}

    def ok(self, method, path, body=None):
        status, payload = self.api(method, path, body)
        if status != 200:
            raise RuntimeError("%s %s -> %s %r" % (method, path, status, payload))
        return payload

    def stop(self):
        for process in (self.backend, self.agent, self.server):
            if process is None or process.poll() is not None:
                continue
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for handle in self.handles:
            handle.close()
        if not self.keep:
            shutil.rmtree(self.work_dir, ignore_errors=True)
        else:
            log("[demo] 保留工作目录: %s" % self.work_dir)


def collect_spans(detail):
    spans = []

    def walk(node):
        spans.append(node)
        for child in node.get("children", []):
            walk(child)

    for root in detail.get("roots", []):
        walk(root)
    return spans


def wait_trace(stack, request_id, min_spans=2, need_error=False,
               need_logs=False, timeout=30):
    def ready():
        status, data = stack.api(
            "GET", "/api/requests/" + urllib.parse.quote(request_id))
        if status != 200 or not data:
            return None
        traces = data.get("traces", [])
        if not traces:
            return None
        detail = traces[0]
        spans = collect_spans(detail)
        if len(spans) < min_spans:
            return None
        # 等待所有 Span 结束，避免把“进行中”的 0ms 当最终耗时。
        if any(not span.get("end_ns") for span in spans):
            return None
        if need_error and not detail["summary"].get("has_error"):
            return None
        if need_logs and not detail.get("logs"):
            return None
        return data

    try:
        return wait_for(ready, timeout, "请求 %s 的 trace 到达" % request_id)
    except RuntimeError:
        overview = stack.ok("GET", "/api/overview")
        log("[demo] 调试总览: %r" % overview)
        raise


def print_trace(detail, show_logs=True):
    summary = detail.get("summary", {})
    spans = collect_spans(detail)
    log("  trace_id     : %s" % detail.get("trace_id"))
    log("  span/服务/深度: %s / %s / %s" % (
        summary.get("span_count"), summary.get("service_count"),
        summary.get("depth")))
    log("  首个错误     : %s" % (
        (detail.get("first_error") or {}).get("error_code") or "无"))
    slow = detail.get("slowest_span") or {}
    if slow:
        log("  最慢 Span    : %s %s（%.1f ms）" % (
            slow.get("service"), slow.get("name"),
            (slow.get("duration_ns") or 0) / 1e6))
    log("  Span 明细：")
    for span in spans:
        log("    - %-14s %-22s %8.1f ms %s" % (
            span.get("service"), span.get("name"),
            (span.get("duration_ns") or 0) / 1e6,
            span.get("error_code") or span.get("status") or ""))
    if show_logs:
        for item in (detail.get("logs") or [])[:5]:
            log("    日志[%s] %s" % (item.get("level"), item.get("msg")))


def print_hint(stack, request_id, title):
    log("  看这里：")
    if stack.keep:
        log("    Web   : %s/  （令牌 %s）" % (stack.base_url, TOKEN))
    else:
        log("    Web   : 栈已停止；加 --keep 可保留并打开 Web UI")
    log("    调用链: 搜索 %s" % request_id)
    log("    CLI   : ./build/tools/vmonctl --server %s --token %s "
        "traces --request %s" % (stack.base_url, TOKEN, request_id))
    log("  场景  : %s" % title)


def new_ids(prefix):
    suffix = "%d" % int(time.time() * 1000)
    return "%s-%s" % (prefix, suffix)


def make_client(stack):
    return vmon_trace.Client(
        stack.base_url, TOKEN, service="demo-python", instance="py-demo",
        node=NODE, flush_interval=0.1)


def scenario_rpc(stack):
    request_id = new_ids("demo-rpc")
    client = make_client(stack)
    try:
        with client.span("Python/CallBackend", request_id=request_id,
                         task_id="task-" + request_id) as span:
            status, _ = client.request(
                "POST", stack.backend_url + "/api/echo",
                body={"scenario": "rpc"}, request_id=request_id,
                task_id="task-" + request_id)
            span.set_attr("demo.scenario", "rpc")
        if status != 200:
            raise RuntimeError("后端返回 %s" % status)
        client.flush()
        log("[demo] python 上报: sent=%d dropped=%d error=%s" % (
            client.sent, client.dropped, client.last_error or "无"))
    finally:
        client.close()
    data = wait_trace(stack, request_id, min_spans=2, need_logs=True)
    log("\n=== 场景：Python → C++ 跨语言链路 ===")
    print_trace(data["traces"][0])
    print_hint(stack, request_id, "同一 trace 里同时看到 demo-python 与 http-backend")
    return request_id


def scenario_slow(stack):
    request_id = new_ids("demo-slow")
    client = make_client(stack)
    try:
        with client.span("Python/CallSlowBackend", request_id=request_id) as span:
            status, _ = client.request(
                "POST", stack.backend_url + "/api/echo",
                body={"scenario": "slow"}, request_id=request_id,
                headers={"x-vmon-sleep-ms": "800"})
            span.set_attr("demo.scenario", "slow")
        client.flush()
    finally:
        client.close()
    data = wait_trace(stack, request_id, min_spans=2)
    detail = data["traces"][0]
    log("\n=== 场景：慢调用（后端 800ms） ===")
    print_trace(detail)
    critical = detail.get("critical_path") or []
    if critical:
        log("  关键路径：%s" % " → ".join(
            "%s/%s" % (item.get("service"), item.get("name"))
            for item in critical))
    print_hint(stack, request_id, "先看最慢 Span，再看关键路径，最后下钻日志")
    return request_id


def scenario_error(stack):
    request_id = new_ids("demo-error")
    client = make_client(stack)
    try:
        with client.span("Python/CallErrorBackend", request_id=request_id) as span:
            status, _ = client.request(
                "POST", stack.backend_url + "/api/echo",
                body={"scenario": "error"}, request_id=request_id,
                headers={"x-vmon-force-error": "1"})
            span.set_attr("demo.scenario", "error")
        client.flush()
    finally:
        client.close()
    data = wait_trace(stack, request_id, min_spans=2, need_error=True)
    detail = data["traces"][0]
    log("\n=== 场景：错误链路（后端强制 E_ECHO） ===")
    print_trace(detail)
    status, diagnose = stack.api("POST", "/api/diagnose",
                                 {"request_id": request_id})
    if status == 200:
        log("  一键诊断：verdict=%s" % diagnose.get("verdict"))
        for item in (diagnose.get("hypotheses") or [])[:3]:
            log("    假设: %s" % (item.get("title") or item.get("summary") or item))
    print_hint(stack, request_id, "首个错误 + 错误码 + 诊断结论")
    return request_id


def scenario_logstorm(stack):
    request_id = new_ids("demo-storm")
    client = make_client(stack)
    try:
        with client.span("Python/LogStorm", request_id=request_id):
            for i in range(200):
                client.log("info", "demo tick %d" % i, request_id=request_id)
            for i in range(20):
                client.log("error", "demo storm failure code=E_STORM",
                           request_id=request_id)
        client.flush()
    finally:
        client.close()
    wait_for(lambda: stack.api("GET", "/api/logs/clusters?limit=5")[0] == 200,
             10, "日志聚类接口")
    clusters = stack.ok("GET", "/api/logs/clusters?limit=5").get("clusters", [])
    overview = stack.ok("GET", "/api/overview")
    log("\n=== 场景：日志风暴（200 条 info + 20 条 error） ===")
    log("  日志统计：保留=%s 采样=%s 去重=%s 噪声=%s" % (
        overview.get("log_count"), overview.get("logs_sampled"),
        overview.get("logs_deduped"), overview.get("logs_noise")))
    log("  Top 模板：")
    for item in clusters[:5]:
        log("    - %5s 次  %-10s %s" % (
            item.get("count"), item.get("level"), item.get("template")))
    print_hint(stack, request_id, "先看模板聚类，再按指纹下钻错误样本")
    return request_id


def scenario_slo(stack, error_request=None):
    if error_request is None:
        error_request = scenario_error(stack)
    slo_id = "demo-availability"
    stack.api("DELETE", "/api/slos/" + slo_id)
    status, payload = stack.api("POST", "/api/slos", {
        "id": slo_id,
        "name": "demo 可用性",
        "service": "demo-python",
        "type": "availability",
        "target": 0.99,
        "window_seconds": 3600,
        "description": "演示：错误预算与燃烧率",
    })
    if status != 200:
        raise RuntimeError("创建 SLO 失败: %r" % payload)
    slo = payload.get("slo") or payload
    log("\n=== 场景：SLO 与错误预算 ===")
    log("  SLI=%.3f  target=%.3f  状态=%s  预算剩余=%.3f  燃烧率=%.2f" % (
        slo.get("sli", 0), slo.get("target", 0),
        slo.get("status"), slo.get("budget_remaining", 0),
        slo.get("burn_rate", 0)))
    log("  样本：total=%s bad=%s" % (slo.get("total"), slo.get("bad")))
    log("  看这里：Web → SLO 页；或 GET /api/slos")
    return slo_id


def scenario_crash(stack):
    request_id = new_ids("demo-crash")
    client_bin = stack.binary("vmon_e2e_client")
    process = subprocess.Popen(
        [client_bin, "--uds", stack.uds,
         "--control-dir", stack.control_dir,
         "--crash-dir", stack.crash_dir,
         "--service", "demo-crash", "--request-id", request_id,
         "--mode", "crash"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    code = process.wait(timeout=20)
    # e2e_client 的 crash 模式固定使用示例服务名 e2e-crash。
    crashes = wait_for(
        lambda: (stack.api("GET", "/api/crashes?service=e2e-crash")[1]
                 or {}).get("crashes") or None,
        30, "崩溃记录上传")
    log("\n=== 场景：进程崩溃（SIGSEGV） ===")
    log("  进程退出码：%s" % code)
    for crash in crashes[:3]:
        log("  %s %s ×%s node=%s reason=%s" % (
            crash.get("time"), crash.get("signal"), crash.get("count"),
            crash.get("node"), crash.get("reason")))
    log("  看这里：Web → 崩溃页；或 GET /api/crashes")
    return request_id


SCENARIOS = {
    "rpc": scenario_rpc,
    "slow": scenario_slow,
    "error": scenario_error,
    "logstorm": scenario_logstorm,
    "slo": scenario_slo,
    "crash": scenario_crash,
}


def main():
    parser = argparse.ArgumentParser(description="vmon 常用维测场景演示")
    parser.add_argument("--scenario", default="all",
                        choices=list(SCENARIOS) + ["all"])
    parser.add_argument("--keep", action="store_true",
                        help="保留栈与临时目录，便于打开 Web UI")
    args = parser.parse_args()

    stack = Stack(keep=args.keep or sys.stdin.isatty())
    try:
        stack.start()
        if args.scenario == "all":
            results = {}
            results["rpc"] = scenario_rpc(stack)
            results["slow"] = scenario_slow(stack)
            results["error"] = scenario_error(stack)
            scenario_slo(stack, results["error"])
            scenario_logstorm(stack)
            scenario_crash(stack)
        else:
            SCENARIOS[args.scenario](stack)
        log("\n[demo] 场景执行完毕。")
        if stack.keep:
            log("[demo] 栈保持运行：%s/（令牌 %s）；Ctrl+C 结束"
                % (stack.base_url, TOKEN))
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        log("\n[demo] 收到中断，清理中…")
    finally:
        stack.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
