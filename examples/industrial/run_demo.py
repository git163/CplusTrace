#!/usr/bin/env python3
"""工业控制闭环演示：启动 7 个进程，复现故障，并用血缘闸门 + RCA 定位。

用法：
  # 已手动启动 vmon_server 时
  python3 examples/industrial/run_demo.py --fault identity

  # 让脚本自己起 Server（需先编译 build/vmon_server 或指定 --server-bin）
  python3 examples/industrial/run_demo.py --start-server --server-bin build/vmon_server

  # 时序故障变体
  python3 examples/industrial/run_demo.py --fault timing
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


def wait_port(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def wait_health(base, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            http_get(base + "/api/health", timeout=1)
            return True
        except Exception:  # noqa: BLE001
            time.sleep(0.2)
    return False


def print_report(run_id, base):
    print("\n" + "=" * 78)
    print("血缘闸门（G0 状态 / G1 计数 / G2 身份 / G3 时序）")
    print("=" * 78)
    gates = http_get(base + "/api/lineage/gates?run_id=" + run_id)
    summary = gates.get("summary", {})
    for gate in ("G0", "G1", "G2", "G3"):
        info = summary.get(gate, {})
        state = "通过" if info.get("passed") else "命中"
        print("  %s  %-4s  失败迭代=%s" % (
            "✔" if info.get("passed") else "✘", gate,
            info.get("failed_iterations", 0)))
    # 打印第一个失败迭代的详情
    for item in gates.get("iterations", []):
        if item.get("first_failed"):
            print("\n  首个失败迭代 %s：" % item["iteration_id"])
            for g in item.get("gates", []):
                mark = "✔" if g.get("passed") else "✘"
                print("    %s %s %s — %s" % (mark, g["gate"], g["check"], g["detail"]))
            break

    print("\n" + "=" * 78)
    print("根因分析（RCA）")
    print("=" * 78)
    rca = http_get(base + "/api/lineage/rca?run_id=" + run_id)
    print("  verdict   : %s" % rca.get("verdict"))
    print("  confidence: %.2f" % rca.get("confidence", 0.0))
    print("  叙述      : %s" % rca.get("narrative", ""))
    print("\n  候选：")
    for c in rca.get("candidates", []):
        print("    - %-18s score=%.2f  %s" % (
            c.get("subject"), c.get("score", 0),
            "; ".join(c.get("reasons", []))))
    dev = rca.get("deviations", [])
    if dev:
        print("\n  偏离基线的字段：")
        for d in dev[:8]:
            print("    - %s/%s = %s（基线占比 %.0f%%，共 %d 次迭代）" % (
                d.get("object"), d.get("field"), d.get("value"),
                d.get("baseline_ratio", 0) * 100, d.get("total", 0)))
    if rca.get("unknowns"):
        print("\n  待确认：")
        for u in rca["unknowns"]:
            print("    - " + u)
    print("\n  Web UI: %s/#lineage   （选 run_id=%s）" % (base, run_id))
    return rca


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default="http://127.0.0.1:18428")
    parser.add_argument("--start-server", action="store_true")
    parser.add_argument("--server-bin", default=os.path.join(ROOT, "build", "vmon_server"))
    parser.add_argument("--port", type=int, default=18428)
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--fault", default="identity",
                        choices=["none", "identity", "timing", "count", "state", "algo"])
    parser.add_argument("--keep", action="store_true", help="保留进程，便于打开 Web UI")
    parser.add_argument("--expect-verdict", default="", help="断言 RCA verdict（用于 CI）")
    args = parser.parse_args()

    base = args.server.rstrip("/")
    procs = []
    state_dir = None
    try:
        if args.start_server:
            if not os.path.exists(args.server_bin):
                print("找不到 Server 可执行文件：%s（先编译，或用 --server-bin）" % args.server_bin)
                return 2
            state_dir = tempfile.mkdtemp(prefix="vmon-industrial-")
            log = open(os.path.join(state_dir, "server.log"), "w")
            procs.append(subprocess.Popen([
                args.server_bin, "--listen-host", "127.0.0.1",
                "--port", str(args.port), "--state-dir", state_dir,
                "--web-dir", os.path.join(ROOT, "web"), "--no-forward",
            ], stdout=log, stderr=subprocess.STDOUT))
            base = "http://127.0.0.1:%d" % args.port
            if not wait_health(base):
                print("Server 启动失败，日志：%s" % os.path.join(state_dir, "server.log"))
                return 2
            print("Server 已启动: %s" % base)
        else:
            if not wait_health(base, timeout=3):
                print("连不上 Server %s；可加 --start-server 让脚本自己起。" % base)
                return 2

        env = dict(os.environ)
        env["PYTHONPATH"] = HERE + os.pathsep + env.get("PYTHONPATH", "")
        for role, port in CONTROLLERS:
            procs.append(subprocess.Popen([
                sys.executable, os.path.join(HERE, "controller.py"),
                "--role", role, "--port", str(port), "--server", base,
                "--fault", args.fault, "--iterations", str(args.iterations),
            ], env=env))
        for _, port in CONTROLLERS:
            if not wait_port(port):
                print("控制器端口 %d 未就绪" % port)
                return 2
        print("7 个进程已就绪（1 个逻辑层 + 6 个控制器）")

        orch = subprocess.run([
            sys.executable, os.path.join(HERE, "controller.py"),
            "--role", "orchestrator", "--server", base,
            "--fault", args.fault, "--iterations", str(args.iterations),
            "--motion-port", "19101", "--acq-port", "19102",
            "--receiver-port", "19103", "--algo-port", "19104",
            "--calib-port", "19105", "--store-port", "19106",
        ], env=env, capture_output=True, text=True)
        if orch.returncode != 0:
            print("orchestrator 失败：\n%s\n%s" % (orch.stdout, orch.stderr))
            return 2
        result = json.loads(orch.stdout.strip().splitlines()[-1])
        run_id = result["run_id"]
        print("作业完成: run_id=%s iterations=%d fault=%s" % (
            run_id, result["iterations"], result["fault"]))

        # 等记录数稳定，避免分析到一半。
        last = -1
        stable = 0
        for _ in range(80):
            try:
                runs = http_get(base + "/api/lineage/runs")
            except Exception:  # noqa: BLE001
                time.sleep(0.2)
                continue
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
        if state_dir and not args.keep:
            shutil.rmtree(state_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
