#!/usr/bin/env python3
"""C++ 版工业控制示例：Server + Agent + 7 个 C++ 进程（走真实 SDK）。

用法：
  python3 examples/industrial_cpp/run_demo.py \
    --server-bin build/vmon_server --agent-bin build/vmon_agent \
    --client-bin build/examples/industrial_cpp/vmon_industrial \
    --fault identity
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
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

CONTROLLERS = [
    ("motion-controller", 19101),
    ("acq-controller", 19102),
    ("data-receiver", 19103),
    ("algo-controller", 19104),
    ("calib-service", 19105),
    ("state-store", 19106),
]


def http_get(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8") or "{}")


def wait_health(base, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            http_get(base + "/api/health", timeout=1)
            return True
        except Exception:  # noqa: BLE001
            time.sleep(0.2)
    return False


def wait_port(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def wait_path(path, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.1)
    return False


def print_report(run_id, base):
    print("\n" + "=" * 78)
    print("血缘闸门（G0 状态 / G1 计数 / G2 身份 / G3 时序）")
    print("=" * 78)
    gates = http_get(base + "/api/lineage/gates?run_id=" + run_id)
    for gate in ("G0", "G1", "G2", "G3"):
        info = gates.get("summary", {}).get(gate, {})
        print("  %s  %-4s 失败迭代=%s" % (
            "✔" if info.get("passed") else "✘", gate,
            info.get("failed_iterations", 0)))
    for item in gates.get("iterations", []):
        if item.get("first_failed"):
            print("\n  首个失败迭代 %s：" % item["iteration_id"])
            for g in item.get("gates", []):
                print("    %s %s %s — %s" % (
                    "✔" if g.get("passed") else "✘", g["gate"], g["check"],
                    g["detail"]))
            break

    print("\n" + "=" * 78)
    print("根因分析（RCA）")
    print("=" * 78)
    rca = http_get(base + "/api/lineage/rca?run_id=" + run_id)
    print("  verdict   : %s" % rca.get("verdict"))
    print("  confidence: %.2f" % rca.get("confidence", 0.0))
    print("  叙述      : %s" % rca.get("narrative", ""))
    for c in rca.get("candidates", []):
        print("    - %-18s score=%.2f  %s" % (
            c.get("subject"), c.get("score", 0),
            "; ".join(c.get("reasons", []))))
    return rca


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-bin", default=os.path.join(ROOT, "build", "vmon_server"))
    parser.add_argument("--agent-bin", default=os.path.join(ROOT, "build", "vmon_agent"))
    parser.add_argument("--client-bin",
                        default=os.path.join(ROOT, "build", "examples",
                                             "industrial_cpp", "vmon_industrial"))
    parser.add_argument("--port", type=int, default=18428)
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--fault", default="identity",
                        choices=["none", "identity", "timing", "count", "state", "algo"])
    parser.add_argument("--expect-verdict", default="")
    parser.add_argument("--debug-iteration", type=int, default=0)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    for path in (args.server_bin, args.agent_bin, args.client_bin):
        if not os.path.exists(path):
            print("找不到可执行文件：%s（先编译 examples）" % path)
            return 2

    base = "http://127.0.0.1:%d" % args.port
    state_dir = tempfile.mkdtemp(prefix="vmon-industrial-cpp-")
    uds = os.path.join(state_dir, "agent.sock")
    procs = []
    try:
        server_log = open(os.path.join(state_dir, "server.log"), "w")
        procs.append(subprocess.Popen([
            args.server_bin, "--listen-host", "127.0.0.1",
            "--port", str(args.port), "--state-dir", os.path.join(state_dir, "server"),
            "--web-dir", os.path.join(ROOT, "web"), "--no-forward",
        ], stdout=server_log, stderr=subprocess.STDOUT))
        if not wait_health(base):
            print("Server 启动失败：%s" % os.path.join(state_dir, "server.log"))
            return 2

        agent_log = open(os.path.join(state_dir, "agent.log"), "w")
        procs.append(subprocess.Popen([
            args.agent_bin, "--server-url", base, "--uds", uds,
            "--state-dir", os.path.join(state_dir, "agent"),
            "--node", "node-1", "--scan-interval-ms", "2000",
            "--upload-interval-ms", "200",
        ], stdout=agent_log, stderr=subprocess.STDOUT))
        if not wait_path(uds):
            print("Agent 启动失败：%s" % os.path.join(state_dir, "agent.log"))
            return 2

        for role, port in CONTROLLERS:
            procs.append(subprocess.Popen([
                args.client_bin, "--role", role, "--port", str(port),
                "--agent", uds, "--fault", args.fault,
                "--iterations", str(args.iterations),
            ]))
        for _, port in CONTROLLERS:
            if not wait_port(port):
                print("控制器端口 %d 未就绪" % port)
                return 2
        print("7 个 C++ 进程已就绪（SDK → Agent → Server）")

        orch = subprocess.run([
            args.client_bin, "--role", "orchestrator", "--agent", uds,
            "--fault", args.fault, "--iterations", str(args.iterations),
            "--motion-port", "19101", "--acq-port", "19102",
            "--receiver-port", "19103", "--algo-port", "19104",
            "--calib-port", "19105", "--store-port", "19106",
        ], capture_output=True, text=True)
        if orch.returncode != 0:
            print("orchestrator 失败：\n%s\n%s" % (orch.stdout, orch.stderr))
            return 2
        result = json.loads(orch.stdout.strip().splitlines()[-1])
        run_id = result["run_id"]
        print("作业完成: run_id=%s iterations=%d fault=%s" % (
            run_id, result["iterations"], result["fault"]))
        time.sleep(0.5)  # 等 SDK/Agent 首批上传
        # 等记录数稳定（Agent 批量上传有间隔），避免分析到一半。
        last = -1
        stable = 0
        for _ in range(80):
            runs = http_get(base + "/api/lineage/runs")
            records = 0
            for r in runs:
                if r.get("run_id") == run_id:
                    records = r.get("records", 0)
                    break
            if records > 0 and records == last:
                stable += 1
                if stable >= 3:
                    break
            else:
                stable = 0
            last = records
            time.sleep(0.2)

        rca = print_report(run_id, base)
        if args.debug_iteration:
            graph = http_get(base + "/api/lineage/graph?run_id=" + run_id +
                             "&iteration_id=%d" % args.debug_iteration)
            print("\n[debug] iteration %d nodes:" % args.debug_iteration)
            for n in graph.get("nodes", []):
                print("  %-20s %-8s %-18s %-4s %s" % (
                    n.get("service"), n.get("object"), n.get("field"),
                    n.get("dir"), n.get("value")))
        if args.expect_verdict and rca.get("verdict") != args.expect_verdict:
            print("期望 verdict=%s，实际=%s" % (args.expect_verdict, rca.get("verdict")))
            return 1
        if args.keep:
            print("\n--keep：进程保留，Ctrl+C 结束。")
            while True:
                time.sleep(1)
        return 0
    finally:
        for p in procs:
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        for p in procs:
            try:
                p.wait(timeout=3)
            except Exception:  # noqa: BLE001
                p.kill()
        if not args.keep:
            shutil.rmtree(state_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
