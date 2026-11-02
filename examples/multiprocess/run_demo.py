#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""多进程调用链演示：Gateway → Order → Inventory。

一条命令启动 Server、Agent、三个真实业务进程，发起一次请求，
等待 trace 到达后打印 ASCII 树并生成 SVG 树图。

用法：
    python3 examples/multiprocess/run_demo.py
    python3 examples/multiprocess/run_demo.py --keep   # 保留栈，打开 Web UI
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
sys.path.insert(0, os.path.join(ROOT, "examples", "multiprocess"))
from draw_tree import print_tree, render_svg  # noqa: E402

NODE = "mp-node"


def log(message):
    print("[mp] " + message, flush=True)


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


def get_json(url, timeout=3):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def post_json(url, body, headers=None, timeout=10):
    data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(
        url, data=data, method="POST",
        headers={"Content-Type": "application/json", **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


class Stack:
    def __init__(self, keep):
        self.keep = keep
        self.work = tempfile.mkdtemp(prefix="vmon-mp-")
        self.server_port = free_port()
        self.base = "http://127.0.0.1:%d" % self.server_port
        self.uds = os.path.join(self.work, "agent.sock")
        self.ctrl = os.path.join(self.work, "ctrl")
        self.crash = os.path.join(self.work, "crash")
        self.server_dir = os.path.join(self.work, "server")
        self.agent_dir = os.path.join(self.work, "agent")
        for path in (self.ctrl, self.crash, self.server_dir, self.agent_dir):
            os.makedirs(path, exist_ok=True)
        self.handles = []
        self.processes = []

    def binary(self, *names):
        for name in names:
            for path in (os.path.join(ROOT, "build", name),
                         os.path.join(ROOT, "build", "examples",
                                      "multiprocess", name)):
                if os.path.exists(path):
                    return path
        raise RuntimeError("找不到构建产物: %s，请先 ./scripts/build.sh" %
                           " / ".join(names))

    def start(self):
        server_log = open(os.path.join(self.work, "server.log"), "w",
                          encoding="utf-8")
        agent_log = open(os.path.join(self.work, "agent.log"), "w",
                         encoding="utf-8")
        self.handles = [server_log, agent_log]
        self.processes.append(subprocess.Popen(
            [self.binary("vmon_server"), "--listen-host", "127.0.0.1",
             "--port", str(self.server_port), "--state-dir", self.server_dir,
             "--web-dir", os.path.join(ROOT, "web"), "--no-forward"],
            stdout=server_log, stderr=subprocess.STDOUT))
        wait_for(lambda: get_json(self.base + "/api/health").get("status") == "ok",
                 15, "Server 启动")
        self.processes.append(subprocess.Popen(
            [self.binary("vmon_agent"), "--server-url", self.base,
             "--uds", self.uds, "--state-dir", self.agent_dir,
             "--node", NODE, "--control-dir", self.ctrl,
             "--crash-dir", self.crash,
             "--scan-interval-ms", "1000",
             "--upload-interval-ms", "100",
             "--command-interval-ms", "100",
             "--process-filter", "vmon_multiprocess"],
            stdout=agent_log, stderr=subprocess.STDOUT))
        wait_for(lambda: os.path.exists(self.uds), 15, "Agent 启动")

        inventory_port = free_port()
        order_port = free_port()
        gateway_port = free_port()
        env = os.environ.copy()
        env["VMON_AGENT_ENDPOINT"] = self.uds
        env["VMON_CONTROL_DIR"] = self.ctrl
        env["VMON_CRASH_DIR"] = self.crash
        clients = [
            ("inventory", inventory_port, ""),
            ("order", order_port, "127.0.0.1:%d" % inventory_port),
            ("gateway", gateway_port, "127.0.0.1:%d" % order_port),
        ]
        for role, port, next_addr in clients:
            command = [self.binary("vmon_multiprocess"),
                       "--role", role, "--port", str(port)]
            if next_addr:
                command += ["--next", next_addr]
            log_file = open(os.path.join(self.work, role + ".log"), "w",
                            encoding="utf-8")
            self.handles.append(log_file)
            self.processes.append(subprocess.Popen(
                command, stdout=log_file, stderr=subprocess.STDOUT, env=env))
            wait_for(
                lambda p=port: get_json("http://127.0.0.1:%d/health" % p).get("ok"),
                15, "%s 启动" % role)
        self.gateway_port = gateway_port
        log("Server : %s/" % self.base)
        log("Gateway: http://127.0.0.1:%d/call" % gateway_port)
        log("工作目录: %s" % self.work)

    def stop(self):
        for process in reversed(self.processes):
            if process.poll() is not None:
                continue
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for handle in self.handles:
            try:
                handle.close()
            except Exception:
                pass
        if not self.keep:
            shutil.rmtree(self.work, ignore_errors=True)
        else:
            log("保留工作目录: %s" % self.work)


def main():
    parser = argparse.ArgumentParser(description="多进程链路还原演示")
    parser.add_argument("--keep", action="store_true",
                        help="保留进程与工作目录，便于打开 Web UI")
    parser.add_argument("--output", default="/tmp/vmon-trace-tree.svg",
                        help="SVG 输出路径")
    args = parser.parse_args()

    stack = Stack(keep=args.keep)
    try:
        stack.start()
        request_id = "mp-%d" % int(time.time() * 1000)
        task_id = "task-" + request_id
        status, body = post_json(
            "http://127.0.0.1:%d/call" % stack.gateway_port,
            {"hello": "world"},
            headers={"x-request-id": request_id, "x-task-id": task_id})
        log("Gateway 返回: status=%s body=%s" % (status, body))
        if status != 500:
            raise RuntimeError("预期 Inventory 错误回传 500，实际 %s" % status)

        def ready():
            data = get_json("%s/api/requests/%s" % (
                stack.base, urllib.parse.quote(request_id)))
            traces = data.get("traces", [])
            if traces and traces[0]["summary"].get("span_count", 0) >= 8:
                return traces[0]
            return None

        detail = wait_for(ready, 30, "多进程 trace 到达")
        print("\n===== ASCII Span 树 =====")
        print_tree(detail)
        render_svg(detail, args.output)
        print("\nSVG 树图: %s" % args.output)
        log("Web UI: %s/  搜索 request_id=%s" % (stack.base, request_id))
        if stack.keep:
            log("栈保持运行，Ctrl+C 结束")
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        log("收到中断，清理中…")
    finally:
        stack.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
