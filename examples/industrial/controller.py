#!/usr/bin/env python3
"""工业控制闭环的 7 个进程（每个角色一个独立进程）。

角色：
  orchestrator      逻辑层：迭代循环、收敛判定
  motion-controller 运动：move / correct
  acq-controller    触发采集
  data-receiver     收帧/取帧
  algo-controller   成像算法：由帧算 offset
  calib-service     坐标/标定变换
  state-store       迭代状态持久化

每个非 orchestrator 角色起一个 HTTP 服务，被 orchestrator 调用；
所有角色都把 span / lineage / state 事件发往 vmon-server。
"""

import argparse
import json
import sys
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from vmon_lineage import Emitter, new_span_id, new_trace_id

# 触发延时（固定 10ms）——时序故障的根因所在。
TRIGGER_DELAY_NS = 10_000_000
# 正常 settle 时间。
SETTLE_NS = 8_200_000
# 抖动时的 settle 时间。
SETTLE_JITTER_NS = 13_100_000


def http_post(url, body, timeout=10):
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"}, method="POST"
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8") or "{}")


class Role:
    def __init__(self, args):
        self.role = args.role
        self.port = args.port
        self.emitter = Emitter(args.server, args.role, node=args.node,
                               verbose=args.verbose)
        self.args = args

    # 子类实现：处理一次请求，返回 dict。
    def handle(self, path, body):
        raise NotImplementedError

    def run_server(self):
        role = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, fmt, *a):  # 静默
                pass

            def do_POST(self):
                length = int(self.headers.get("Content-Length", 0))
                raw = self.rfile.read(length) if length else b"{}"
                try:
                    body = json.loads(raw.decode("utf-8") or "{}")
                    result = role.handle(self.path, body)
                    code = 200
                except Exception as exc:  # noqa: BLE001
                    result = {"error": str(exc)}
                    code = 500
                # 每个请求处理完立即发送，避免进程退出时丢最后一批事件。
                role.emitter.flush()
                data = json.dumps(result).encode("utf-8")
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        server = ThreadingHTTPServer(("127.0.0.1", self.port), Handler)
        server.serve_forever()


class MotionController(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        self.emitter.span_begin("Motion/Move", run_id, it, trace, span)
        settle = SETTLE_NS
        # 抖动迭代：默认第 37 次；若总迭代数不足 37，则取最后一次。
        jitter_at = 37 if self.args.iterations >= 37 else max(2, self.args.iterations)
        if path == "/move" and self.args.fault == "timing" and it == jitter_at:
            settle = SETTLE_JITTER_NS
        # 只有 /move 上报 settle 时间；/correct 不覆盖该字段。
        items = [{"object": "motion", "field": "actual_position", "dir": "out",
                  "kind": "int", "value": body.get("position", 0)}]
        if path == "/move":
            items.insert(0, {"object": "motion", "field": "settle_time_ns",
                             "dir": "out", "kind": "int", "value": settle,
                             "raw": True})
        self.emitter.data(run_id, it, "Motion/Move", items,
                          trace=trace, span=span)
        self.emitter.span_end("Motion/Move", run_id, it, trace, span)
        return {"settle_time_ns": settle, "actual_position": body.get("position", 0)}


class AcqController(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        self.emitter.span_begin("Acq/Trigger", run_id, it, trace, span)
        frame_id = it  # 触发第 it 帧
        self.emitter.data(run_id, it, "Acq/Trigger", [
            {"object": "frame", "field": "frame_id", "dir": "out",
             "kind": "int", "value": frame_id},
            {"object": "trigger", "field": "trigger_delay_ns", "dir": "out",
             "kind": "int", "value": TRIGGER_DELAY_NS, "raw": True},
        ], trace=trace, span=span)
        self.emitter.span_end("Acq/Trigger", run_id, it, trace, span)
        return {"frame_id": frame_id, "trigger_delay_ns": TRIGGER_DELAY_NS}


class DataReceiver(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        requested = int(body.get("requested_frame_id", 0))
        self.emitter.span_begin("Receiver/GetFrame", run_id, it, trace, span)
        used = requested
        # 缺陷：按“最新帧”响应。identity 故障时最新帧落后一帧。
        if self.args.fault == "identity" and it >= 2:
            used = requested - 1
        # count 故障：某一帧丢失（取不到帧），用于触发 G1 计数不一致。
        drop_at = 10 if self.args.iterations >= 10 else 2
        if self.args.fault == "count" and it == drop_at:
            used = 0
        self.emitter.data(run_id, it, "Receiver/GetFrame", [
            {"object": "frame", "field": "requested_frame_id", "dir": "in",
             "kind": "int", "value": requested, "raw": True},
            {"object": "frame", "field": "used_frame_id", "dir": "out",
             "kind": "int", "value": used, "raw": True},
        ], trace=trace, span=span)
        self.emitter.span_end("Receiver/GetFrame", run_id, it, trace, span)
        return {"frame_id": used}


class AlgoController(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        frame_id = int(body.get("frame_id", 0))
        self.emitter.span_begin("Algo/Process", run_id, it, trace, span)
        # 正常情况下 offset 稳定为 1.0；algo 故障时某次迭代参数异常。
        offset = 1.0
        glitch_at = 20 if self.args.iterations >= 20 else max(2, self.args.iterations - 1)
        if self.args.fault == "algo" and it == glitch_at:
            offset = 3.7
        self.emitter.data(run_id, it, "Algo/Process", [
            {"object": "frame", "field": "used_frame_id", "dir": "in",
             "kind": "int", "value": frame_id},
            {"object": "offset", "field": "offset", "dir": "out",
             "kind": "float", "value": "%.3f" % offset, "raw": True},
        ], trace=trace, span=span)
        self.emitter.span_end("Algo/Process", run_id, it, trace, span)
        return {"offset": offset}


class CalibService(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        offset = float(body.get("offset", 0.0))
        self.emitter.span_begin("Calib/Transform", run_id, it, trace, span)
        transformed = offset * 1.02
        self.emitter.data(run_id, it, "Calib/Transform", [
            {"object": "offset", "field": "offset", "dir": "in",
             "kind": "float", "value": "%.3f" % offset},
            {"object": "offset", "field": "offset_transformed", "dir": "out",
             "kind": "float", "value": "%.3f" % transformed},
        ], trace=trace, span=span)
        self.emitter.span_end("Calib/Transform", run_id, it, trace, span)
        return {"offset": transformed}


class StateStore(Role):
    def handle(self, path, body):
        run_id = body.get("run_id", "")
        it = body.get("iteration_id", 0)
        trace = body.get("trace") or new_trace_id()
        span = new_span_id()
        self.emitter.span_begin("State/Save", run_id, it, trace, span)
        self.emitter.data(run_id, it, "State/Save", [
            {"object": "state", "field": "residual", "dir": "out",
             "kind": "float", "value": "%.3f" % float(body.get("residual", 0.0))},
        ], trace=trace, span=span)
        self.emitter.span_end("State/Save", run_id, it, trace, span)
        return {"ok": True}


ROLES = {
    "motion-controller": MotionController,
    "acq-controller": AcqController,
    "data-receiver": DataReceiver,
    "algo-controller": AlgoController,
    "calib-service": CalibService,
    "state-store": StateStore,
}

# 启动时上报的状态（满足 G0 状态契约）。
START_STATE = {
    "motion-controller": "homed",
    "acq-controller": "armed",
    "data-receiver": "ready",
    "algo-controller": "idle",
    "calib-service": "calibrated",
    "state-store": "open",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--role", required=True)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--server", default="http://127.0.0.1:18428")
    parser.add_argument("--node", default="node-1")
    parser.add_argument("--fault", default="identity",
                        choices=["none", "identity", "timing", "count", "state", "algo"])
    parser.add_argument("--verbose", action="store_true")
    # orchestrator 用：各控制器端口与迭代次数。
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--motion-port", type=int, default=19101)
    parser.add_argument("--acq-port", type=int, default=19102)
    parser.add_argument("--receiver-port", type=int, default=19103)
    parser.add_argument("--algo-port", type=int, default=19104)
    parser.add_argument("--calib-port", type=int, default=19105)
    parser.add_argument("--store-port", type=int, default=19106)
    args = parser.parse_args()

    if args.role == "orchestrator":
        run_orchestrator(args)
        return

    role_cls = ROLES.get(args.role)
    if role_cls is None:
        print("未知角色: %s" % args.role, file=sys.stderr)
        sys.exit(2)

    role = role_cls(args)
    # 上报启动状态：run_id 用固定值 "__boot__"，iteration_id=0。
    role.emitter.state("__boot__", 0, args.role,
                       START_STATE.get(args.role, "ready"), source="startup")
    role.emitter.flush()
    role.run_server()


# ------------------------------------------------------------------ 逻辑层
def run_orchestrator(args):
    emitter = Emitter(args.server, "job-orchestrator", node=args.node,
                      verbose=args.verbose)
    run_id = "run-" + new_trace_id()[:12]
    # 逻辑层在 run 开始时上报全部设备状态（对应 G0 状态契约）。
    motion_state = "disabled" if args.fault == "state" else "homed"
    for device, state in [
        ("job-orchestrator", "running"),
        ("acq-controller", "armed"),
        ("data-receiver", "ready"),
        ("algo-controller", "idle"),
        ("calib-service", "calibrated"),
        ("motion-controller", motion_state),
        ("state-store", "open"),
    ]:
        emitter.state(run_id, 0, device, state, source="orchestrator")

    peers = {
        "motion": "http://127.0.0.1:%d" % args.motion_port,
        "acq": "http://127.0.0.1:%d" % args.acq_port,
        "receiver": "http://127.0.0.1:%d" % args.receiver_port,
        "algo": "http://127.0.0.1:%d" % args.algo_port,
        "calib": "http://127.0.0.1:%d" % args.calib_port,
        "store": "http://127.0.0.1:%d" % args.store_port,
    }

    iterations = args.iterations
    trigger_count = 0
    frame_count = 0
    processed_count = 0
    for it in range(1, iterations + 1):
        trace = new_trace_id()
        span = new_span_id()
        emitter.span_begin("Job/Iteration", run_id, it, trace, span, kind="server")

        position = it * 10
        # 1) 运动到位置
        http_post(peers["motion"] + "/move",
                  {"run_id": run_id, "iteration_id": it,
                   "trace": trace, "position": position})
        # 2) 触发采集
        acq = http_post(peers["acq"] + "/trigger",
                        {"run_id": run_id, "iteration_id": it, "trace": trace})
        requested = acq["frame_id"]
        trigger_count += 1
        # 逻辑层记录“我请求的是哪一帧”（G2 的参照）
        emitter.data(run_id, it, "Job/Iteration", [
            {"object": "frame", "field": "requested_frame_id", "dir": "out",
             "kind": "int", "value": requested, "raw": True},
        ], trace=trace, span=span)
        # 3) 取帧
        recv = http_post(peers["receiver"] + "/frame",
                         {"run_id": run_id, "iteration_id": it, "trace": trace,
                          "requested_frame_id": requested})
        used = recv.get("frame_id", 0)
        if not used:
            # 丢帧：本次迭代未取到帧，跳过后续处理（触发 G1 计数不一致）。
            emitter.span_end("Job/Iteration", run_id, it, trace, span,
                             error_code="E_NO_FRAME")
            continue
        frame_count += 1
        # 4) 成像算法
        algo = http_post(peers["algo"] + "/process",
                         {"run_id": run_id, "iteration_id": it, "trace": trace,
                          "frame_id": used})
        processed_count += 1
        # 5) 标定变换
        calib = http_post(peers["calib"] + "/transform",
                          {"run_id": run_id, "iteration_id": it, "trace": trace,
                           "offset": algo["offset"]})
        # 6) 运动修正
        http_post(peers["motion"] + "/correct",
                  {"run_id": run_id, "iteration_id": it, "trace": trace,
                   "offset": calib["offset"]})
        # 7) 持久化状态
        residual = abs(algo["offset"] - calib["offset"])
        http_post(peers["store"] + "/save",
                  {"run_id": run_id, "iteration_id": it, "trace": trace,
                   "residual": residual})

        emitter.span_end("Job/Iteration", run_id, it, trace, span)

    # 计数（G1）：触发 / 收帧 / 处理 / 迭代。
    emitter.data(run_id, 0, "Job/Counters", [
        {"object": "count", "field": "trigger_count", "dir": "out",
         "kind": "int", "value": trigger_count, "raw": True},
        {"object": "count", "field": "frame_count", "dir": "out",
         "kind": "int", "value": frame_count, "raw": True},
        {"object": "count", "field": "processed_count", "dir": "out",
         "kind": "int", "value": processed_count, "raw": True},
        {"object": "count", "field": "iteration_count", "dir": "out",
         "kind": "int", "value": iterations, "raw": True},
    ])
    emitter.state(run_id, 0, "job-orchestrator", "finished", source="orchestrator")
    emitter.flush()
    print(json.dumps({"run_id": run_id, "iterations": iterations,
                      "fault": args.fault}))


if __name__ == "__main__":
    main()
