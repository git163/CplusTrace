#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""故障注入实验室一键演示：同一个二进制的多角色 + 多故障场景。

用法：
    python3 examples/faultlab/run_demo.py --scenario slow
    python3 examples/faultlab/run_demo.py --scenario lockwait --keep
    python3 examples/faultlab/run_demo.py --scenario all

脚本会：
  1. 启动真实的 vmon-server 与 vmon-agent；
  2. 启动 gateway/order/inventory/payment 四个 C++ 进程（均链接 vmon SDK）；
  3. 按场景注入故障并跑 driver 产生请求；
  4. 通过 Server API 自动定位：调用链、首个错误、错误码传播、日志聚类、
     一键诊断、崩溃记录、证据包；
  5. 打印结构化诊断报告，并写入 <工作目录>/report-<场景>.md。

只依赖 Python 标准库与项目内构建产物。
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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

ROLES = ("gateway", "order", "inventory", "payment")

SCENARIOS = {
    "none": {
        "title": "健康基线（对照组）",
        "iterations": 6,
        "concurrency": 1,
        "worker": False,
        "inject": "不注入故障",
        "expect": "全部请求成功；用于和故障场景对比",
        "look": "调用链完整、无错误码、诊断给出 evidence_insufficient",
        "root_cause": "无故障：四个服务均正常响应",
    },
    "slow": {
        "title": "下游慢调用 + 上游超时 + 重试放大",
        "iterations": 5,
        "concurrency": 1,
        "worker": False,
        "inject": "inventory 每次响应固定 sleep 700ms；order 每次只等 350ms，重试 3 次",
        "expect": "gateway 返回 504，order 记录 E_INVENTORY_TIMEOUT，错误码沿调用链传播",
        "look": "最慢 Span、关键路径、首个错误、重试产生的多个 Inventory/Reserve Span",
        "root_cause": "inventory 单次 700ms 超过 order 350ms 超时阈值；3 次重试后整体失败",
    },
    "payment": {
        "title": "业务错误码跨服务传播",
        "iterations": 9,
        "concurrency": 1,
        "worker": False,
        "inject": "payment 每 3 个请求拒绝一次（E_PAYMENT_DECLINED）",
        "expect": "payment → order → gateway 三层都带同一个业务错误码",
        "look": "错误码页的跨服务聚合与 sample_path、异常追踪 Issue、诊断 verdict=business_error",
        "root_cause": "payment 对迭代号为 3 的倍数的请求返回 E_PAYMENT_DECLINED",
    },
    "lockwait": {
        "title": "锁等待 / 线程阻塞",
        "iterations": 4,
        "concurrency": 4,
        "worker": False,
        "inject": "order 第一个请求持有全局锁 2500ms，其余并发请求排队等待",
        "expect": "并发请求在 gateway 侧全部超时；order 日志出现 lock wait exceeded 800ms",
        "look": "线程时间线长空档、诊断 verdict=lock_contention；Linux 下可再抓全线程栈",
        "root_cause": "order 持锁 2500ms，其余请求排队超过 gateway 600ms 超时阈值",
    },
    "logstorm": {
        "title": "日志风暴 + 队列过载",
        "iterations": 10,
        "concurrency": 1,
        "worker": False,
        "inject": "inventory 每次请求打 300 条 INFO + 10 条 ERROR，隔次返回 503",
        "expect": "日志模板聚类收敛、INFO 被采样/去重、E_INVENTORY_OVERLOADED 传播",
        "look": "日志聚类 Top 模板、采样/去重计数、错误码聚合、Issue 聚合",
        "root_cause": "inventory 日志量过大并隔次返回 E_INVENTORY_OVERLOADED",
    },
    "crash": {
        "title": "后台进程崩溃（SIGSEGV）",
        "iterations": 3,
        "concurrency": 1,
        "worker": True,
        "inject": "worker 启动约 2 秒后模拟空指针解引用，SDK 崩溃处理器落盘最小 JSON",
        "expect": "进程退出码 139，Agent 收集崩溃报告，Server 聚合崩溃指纹",
        "look": "崩溃页 / GET /api/crashes、巡检 checks 中的 crashes=fail、证据包",
        "root_cause": "worker 第 8 个 tick 触发空指针解引用",
    },
}

CONCLUSIONS = {
    "downstream_timeout": "下游调用超时：沿关键路径确认是哪一跳超过 deadline，再看该跳的日志与重试次数",
    "business_error": "业务错误码：沿错误码传播路径定位根服务，业务码本身不是根因，需看根服务日志",
    "lock_contention": "锁等待/线程阻塞：结合线程时间线与全线程栈确认持锁者与等待链",
    "evidence_insufficient": "未命中已知错误模式：检查巡检异常项、崩溃记录，必要时导出证据包",
}

ORDER = ["none", "slow", "payment", "lockwait", "logstorm", "crash"]


def log(message=""):
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
        except Exception as error:  # noqa: BLE001
            last = error
        time.sleep(interval)
    raise RuntimeError("等待超时: %s%s" % (
        description, "（最后错误: %s）" % last if last else ""))


class Stack(object):
    def __init__(self, keep=False, port=0):
        self.keep = keep
        self.work_dir = tempfile.mkdtemp(prefix="vmon-faultlab-")
        self.server_port = port or free_port()
        self.base_url = "http://127.0.0.1:%d" % self.server_port
        self.uds = os.path.join(self.work_dir, "agent.sock")
        self.control_dir = os.path.join(self.work_dir, "ctrl")
        self.crash_dir = os.path.join(self.work_dir, "crash")
        self.server_dir = os.path.join(self.work_dir, "server")
        self.agent_dir = os.path.join(self.work_dir, "agent")
        self.server_log = os.path.join(self.work_dir, "server.log")
        self.agent_log = os.path.join(self.work_dir, "agent.log")
        self.server = None
        self.agent = None
        self.handles = []
        # --keep 单场景时保留的业务进程，由 stop() 统一回收。
        self.extra_procs = {}
        self.extra_handles = []
        for path in (self.control_dir, self.crash_dir, self.server_dir,
                     self.agent_dir):
            os.makedirs(path, exist_ok=True)

    def binary(self, name, *subdirs):
        candidates = [os.path.join(ROOT, "build", name),
                      os.path.join(ROOT, "build-dev", name)]
        for subdir in subdirs:
            candidates.insert(0, os.path.join(ROOT, "build", subdir, name))
            candidates.insert(0, os.path.join(ROOT, "build-dev", subdir, name))
        for path in candidates:
            if os.path.exists(path):
                return path
        raise RuntimeError("找不到 %s，请先构建：cmake --build build -j" % name)

    def start(self, server_bin, agent_bin):
        server_log = open(self.server_log, "a", encoding="utf-8")
        agent_log = open(self.agent_log, "a", encoding="utf-8")
        self.handles = [server_log, agent_log]
        self.server = subprocess.Popen(
            [server_bin, "--listen-host", "127.0.0.1",
             "--port", str(self.server_port),
             "--state-dir", self.server_dir,
             "--web-dir", os.path.join(ROOT, "web"), "--no-forward"],
            stdout=server_log, stderr=subprocess.STDOUT)
        wait_for(lambda: self.get("/api/health")[0] == 200, 15,
                 "vmon-server 启动")
        self.agent = subprocess.Popen(
            [agent_bin, "--server-url", self.base_url, "--uds", self.uds,
             "--state-dir", self.agent_dir, "--node", "faultlab-node",
             "--control-dir", self.control_dir,
             "--crash-dir", self.crash_dir,
             "--command-interval-ms", "100",
             "--scan-interval-ms", "1000",
             "--upload-interval-ms", "100"],
            stdout=agent_log, stderr=subprocess.STDOUT)
        wait_for(lambda: os.path.exists(self.uds), 15, "vmon-agent 启动")
        log("[faultlab] Server : %s/" % self.base_url)
        log("[faultlab] Agent  : %s" % self.uds)
        log("[faultlab] 工作目录: %s" % self.work_dir)

    def get(self, path, timeout=5):
        request = urllib.request.Request(self.base_url + path)
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

    def api(self, method, path, body=None, timeout=15):
        data = None
        headers = {}
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
        if self.extra_procs:
            stop_procs(self.extra_procs, self.extra_handles)
            self.extra_procs = {}
            self.extra_handles = []
        for process in (self.agent, self.server):
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
            log("[faultlab] 保留工作目录: %s" % self.work_dir)


def start_roles(client_bin, stack, fault, ports):
    procs = {}
    handles = []
    try:
        for role in ROLES:
            args = [client_bin, "--role", role, "--port", str(ports[role]),
                    "--agent", stack.uds, "--control-dir", stack.control_dir,
                    "--crash-dir", stack.crash_dir, "--fault", fault,
                    "--order-port", str(ports["order"]),
                    "--inventory-port", str(ports["inventory"]),
                    "--payment-port", str(ports["payment"])]
            handle = open(os.path.join(stack.work_dir, "role-%s.log" % role),
                          "a", encoding="utf-8")
            handles.append(handle)
            procs[role] = subprocess.Popen(args, stdout=handle,
                                           stderr=subprocess.STDOUT)
        for role in ROLES:
            wait_for(lambda role=role: _port_open(ports[role]), 15,
                     "%s 端口就绪" % role)
    except Exception:
        stop_procs(procs, handles)
        raise
    return procs, handles


def _port_open(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.3):
            return True
    except OSError:
        return False


def stop_procs(procs, handles=None):
    for process in procs.values():
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
    for process in procs.values():
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    for handle in handles or []:
        handle.close()


def run_driver(client_bin, ports, scenario, spec):
    args = [client_bin, "--role", "driver",
            "--gateway-port", str(ports["gateway"]),
            "--iterations", str(spec["iterations"]),
            "--concurrency", str(spec["concurrency"]),
            "--request-prefix", scenario]
    completed = subprocess.run(args, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError("driver 失败：\n%s\n%s" % (completed.stdout,
                                                     completed.stderr))
    lines = [line for line in completed.stdout.strip().splitlines() if line]
    if not lines:
        raise RuntimeError("driver 没有输出")
    return json.loads(lines[-1])


def run_crash_worker(client_bin, stack):
    process = subprocess.Popen(
        [client_bin, "--role", "worker", "--fault", "crash",
         "--agent", stack.uds, "--control-dir", stack.control_dir,
         "--crash-dir", stack.crash_dir],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    code = process.wait(timeout=30)
    return code


def collect_spans(detail):
    spans = []

    def walk(node):
        spans.append(node)
        for child in node.get("children", []):
            walk(child)

    for root in detail.get("roots", []):
        walk(root)
    return spans


def wait_trace(stack, request_id, timeout=25):
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
        if not spans:
            return None
        if any(not span.get("end_ns") for span in spans):
            return None
        return data

    return wait_for(ready, timeout, "请求 %s 的完整 trace" % request_id)


def span_tree_lines(detail):
    lines = []

    def walk(node, depth):
        duration_ms = (node.get("duration_ns") or 0) / 1e6
        code = node.get("error_code") or ""
        lines.append("  %s%-14s %-22s %8.1f ms %s" % (
            "  " * depth, node.get("service", ""), node.get("name", ""),
            duration_ms, code))
        for child in node.get("children", []):
            walk(child, depth + 1)

    for root in detail.get("roots", []):
        walk(root, 0)
    return lines


def pick_request(driver):
    for item in driver.get("results", []):
        if not item.get("ok"):
            return item
    results = driver.get("results") or []
    return results[0] if results else {}


def build_report(stack, scenario, spec, driver, crash_code=None):
    """查询 Server API，生成结构化报告（纯文本行 + markdown）。"""
    request = pick_request(driver)
    request_id = request.get("request_id", "")
    lines = []
    md = []

    def add(text=""):
        lines.append(text)
        md.append(text)

    add("=" * 78)
    add("场景：%s（%s）" % (scenario, spec["title"]))
    add("=" * 78)
    add("注入方式：%s" % spec["inject"])
    add("预期现象：%s" % spec["expect"])
    add("")
    add("请求结果%s：成功 %s / 失败 %s（共 %s）" % (
        "（对照组）" if scenario == "crash" else "",
        driver.get("ok", 0), driver.get("failed", 0),
        len(driver.get("request_ids", []))))
    add("代表请求：%s（HTTP %s）" % (
        request_id, request.get("status", 0) or "无响应"))

    detail = None
    if request_id:
        try:
            data = wait_trace(stack, request_id)
            traces = data.get("traces") or []
            detail = traces[0] if traces else None
        except RuntimeError as error:
            add("  ! 未能取到完整 trace：%s" % error)

    if detail:
        summary = detail.get("summary", {})
        add("")
        add("-- 调用链 ------------------------------------------------------")
        add("trace_id   : %s" % detail.get("trace_id"))
        add("Span/服务/深度: %s / %s / %s" % (
            summary.get("span_count"), summary.get("service_count"),
            summary.get("depth")))
        first_error = detail.get("first_error") or {}
        add("首个错误   : %s / %s / %s" % (
            first_error.get("service") or "无",
            first_error.get("name") or "-",
            first_error.get("error_code") or "无"))
        slowest = detail.get("slowest_span") or {}
        if slowest:
            add("最慢 Span  : %s / %s（%.1f ms）" % (
                slowest.get("service"), slowest.get("name"),
                (slowest.get("duration_ns") or 0) / 1e6))
        critical = detail.get("critical_path") or []
        if critical:
            add("关键路径   : %s" % " → ".join(
                "%s/%s" % (item.get("service"), item.get("name"))
                for item in critical))
        add("Span 树：")
        for line in span_tree_lines(detail):
            add(line)

        related = detail.get("logs") or []
        if related:
            add("关联日志（前 5 条）：")
            for item in related[:5]:
                add("  [%s] %s: %s" % (
                    item.get("level"), item.get("service"), item.get("msg")))

    add("")
    add("-- 错误码传播 --------------------------------------------------")
    try:
        codes = stack.ok("GET", "/api/error-codes?limit=20").get(
            "error_codes", [])
    except RuntimeError:
        codes = []
    if not codes:
        add("  无错误码")
    for item in codes[:6]:
        path = " → ".join(
            "%s%s" % (node.get("service"),
                      "[%s]" % node.get("error_code")
                      if node.get("error_code") else "")
            for node in item.get("sample_path", []))
        add("  %-28s traces=%s services=%s" % (
            item.get("code"), item.get("trace_count"),
            ",".join(item.get("services", []))))
        if path:
            add("    传播路径: %s" % path)

    add("")
    add("-- 日志聚类 Top（按次数） --------------------------------------")
    try:
        overview = stack.ok("GET", "/api/overview")
        add("  日志治理：保留=%s 采样=%s 去重=%s 噪声=%s" % (
            overview.get("log_count"), overview.get("logs_sampled"),
            overview.get("logs_deduped"), overview.get("logs_noise")))
    except RuntimeError:
        overview = {}
    try:
        clusters = stack.ok("GET", "/api/logs/clusters?limit=200").get(
            "clusters", [])
    except RuntimeError:
        clusters = []
    clusters = sorted(clusters,
                      key=lambda item: item.get("count", 0), reverse=True)
    if not clusters:
        add("  暂无日志聚类")
    for item in clusters[:8]:
        add("  %6s 次  %-5s %-16s %s" % (
            item.get("count"), item.get("level"), item.get("service"),
            item.get("template")))

    add("")
    add("-- 一键诊断 ----------------------------------------------------")
    diagnose = {}
    try:
        if scenario == "crash":
            status, diagnose = stack.api("POST", "/api/diagnose",
                                         {"service": "faultlab-worker"})
        elif request_id:
            status, diagnose = stack.api("POST", "/api/diagnose",
                                         {"request_id": request_id})
        if status != 200:
            diagnose = {}
    except Exception:  # noqa: BLE001
        diagnose = {}
    if diagnose:
        verdict = diagnose.get("verdict", "")
        add("verdict    : %s" % verdict)
        conclusion = CONCLUSIONS.get(
            verdict, "参考下列假设与建议动作继续排查")
        if scenario == "crash":
            conclusion = ("worker 进程崩溃（SIGSEGV）；崩溃记录与巡检 crashes=fail "
                          "是直接证据")
        add("结论       : %s" % conclusion)
        add("注入根因   : %s" % spec["root_cause"])
        for item in diagnose.get("hypotheses", [])[:4]:
            add("  假设[%s] %s — %s" % (
                item.get("confidence"), item.get("title"),
                item.get("evidence")))
        for item in diagnose.get("remediation", [])[:4]:
            add("  建议[%s] %s：%s" % (
                item.get("risk"), item.get("title"), item.get("detail")))
        checks = diagnose.get("checks") or []
        bad = [check for check in checks if check.get("status") != "ok"]
        if bad:
            add("  巡检异常项：")
            for check in bad[:6]:
                add("    [%s] %s — %s" % (
                    check.get("status"), check.get("name"),
                    check.get("detail")))
    else:
        add("  诊断接口未返回结果")

    if scenario == "crash":
        add("")
        add("-- 崩溃记录 ----------------------------------------------------")
        try:
            crashes = stack.ok(
                "GET", "/api/crashes?service=faultlab-worker").get(
                    "crashes", [])
        except RuntimeError:
            crashes = []
        add("  worker 退出码：%s（139 表示 SIGSEGV）" % crash_code)
        if not crashes:
            add("  尚未收到崩溃记录（可能还在上传）")
        for crash in crashes[:3]:
            add("  %s %s service=%s pid=%s count=%s reason=%s" % (
                crash.get("time"), crash.get("signal"),
                crash.get("service"), crash.get("pid"),
                crash.get("count"), crash.get("reason")))

    add("")
    add("-- 证据包 ------------------------------------------------------")
    try:
        if scenario == "crash":
            status, evidence = stack.api(
                "POST", "/api/evidence",
                {"incident": "FAULTLAB-" + scenario,
                 "service": "faultlab-worker",
                 "window_ns": 300000000000})
        else:
            status, evidence = stack.api(
                "POST", "/api/evidence",
                {"incident": "FAULTLAB-" + scenario,
                 "request_id": request_id,
                 "window_ns": 300000000000})
        if status == 200 and evidence.get("ok"):
            manifest = evidence.get("manifest") or {}
            files = manifest.get("files") or []
            add("  id=%s 文件=%s 目录=%s" % (
                evidence.get("id"), len(files), evidence.get("dir")))
        else:
            add("  证据包未生成：%r" % evidence)
    except Exception as error:  # noqa: BLE001
        add("  证据包接口异常：%s" % error)

    add("")
    add("-- 看这里 ------------------------------------------------------")
    add("Web UI     : %s/  （搜索 %s）" % (stack.base_url, request_id))
    add("CLI        : ./build/tools/vmonctl --server %s traces --request %s"
        % (stack.base_url, request_id))
    add("复现       : python3 examples/faultlab/run_demo.py --scenario %s --keep"
        % scenario)
    add("观察要点   : %s" % spec["look"])
    add("")
    return "\n".join(lines), "\n".join(md)


def run_scenario(stack, client_bin, scenario, keep_roles):
    spec = SCENARIOS[scenario]
    ports = {role: free_port() for role in ROLES}
    procs = {}
    handles = []
    try:
        log("")
        log(">>> 场景 %s：%s" % (scenario, spec["title"]))
        procs, handles = start_roles(client_bin, stack, scenario, ports)
        driver = run_driver(client_bin, ports, scenario, spec)

        crash_code = None
        if spec.get("worker"):
            log("[faultlab] 启动 worker，等待其崩溃 ...")
            crash_code = run_crash_worker(client_bin, stack)
            log("[faultlab] worker 已退出，退出码=%s" % crash_code)
            wait_for(
                lambda: stack.ok(
                    "GET", "/api/crashes?service=faultlab-worker").get(
                        "crashes"),
                25, "崩溃记录上传")

        # 给 SDK/Agent 一点时间完成最后一批上传。
        time.sleep(0.6)
        report, markdown = build_report(stack, scenario, spec, driver,
                                        crash_code)
        log("")
        log(report)
        report_path = os.path.join(stack.work_dir,
                                   "report-%s.md" % scenario)
        with open(report_path, "w", encoding="utf-8") as handle:
            handle.write("# 故障注入实验室报告：%s\n\n" % scenario)
            handle.write("```\n%s\n```\n" % markdown)
        log("[faultlab] 报告已写入: %s" % report_path)
        return report
    finally:
        if not keep_roles:
            stop_procs(procs, handles)
        else:
            # 保留业务进程供 Web UI 现场观察，退出时由 stack.stop() 回收。
            stack.extra_procs = procs
            stack.extra_handles = handles
            log("[faultlab] --keep：%s 进程保留，Ctrl+C 结束" % scenario)


def main():
    parser = argparse.ArgumentParser(
        description="CplusTrace 故障注入实验室（同一二进制 + 多场景）")
    parser.add_argument("--scenario", default="slow",
                        choices=ORDER + ["all"],
                        help="要运行的故障场景，默认 slow")
    parser.add_argument("--server-bin", default="")
    parser.add_argument("--agent-bin", default="")
    parser.add_argument("--client-bin", default="")
    parser.add_argument("--port", type=int, default=0,
                        help="Server 端口，0 表示自动选择")
    parser.add_argument("--keep", action="store_true",
                        help="保留 Server/Agent（单场景时也保留业务进程），"
                             "便于打开 Web UI；Ctrl+C 结束")
    args = parser.parse_args()

    stack = Stack(keep=args.keep, port=args.port)
    try:
        server_bin = args.server_bin or stack.binary("vmon_server")
        agent_bin = args.agent_bin or stack.binary("vmon_agent")
        client_bin = args.client_bin or stack.binary(
            "vmon_faultlab", os.path.join("examples", "faultlab"))
        stack.start(server_bin, agent_bin)

        scenarios = ORDER if args.scenario == "all" else [args.scenario]
        keep_roles = args.keep and args.scenario != "all"
        for scenario in scenarios:
            run_scenario(stack, client_bin, scenario, keep_roles)

        if args.scenario == "all":
            log("")
            log("[faultlab] 全部场景执行完毕。")
        if args.keep:
            log("[faultlab] 栈保持运行：%s/；Ctrl+C 结束" % stack.base_url)
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        log("\n[faultlab] 收到中断，清理中…")
    finally:
        stack.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
