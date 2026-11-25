#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成示例文档用的 SVG 图（只依赖 Python 标准库）。

用法：
    python3 examples/diagrams/build_diagrams.py

输出（与脚本同目录）：
    overall-data-flow.svg       统一采集数据流
    hello-span-tree.svg         hello 示例的 Span 树
    http-backend-sequence.svg   Python/Web → C++ 后端的跨语言时序
    faultlab-topology.svg       故障注入实验室的进程与注入点
    industrial-framework.svg    工业闭环：7 进程 → 四道闸门 → RCA

这些图用于离线阅读、代码评审和 PPT；文档里同时保留 Mermaid 源码，便于修改。
"""

import os
from xml.sax.saxutils import escape

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT = ("'PingFang SC','Hiragino Sans GB','Microsoft YaHei',"
        "'Noto Sans CJK SC','Source Han Sans SC',sans-serif")

COLORS = {
    "gray": "#64748b",
    "blue": "#2563eb",
    "teal": "#0d9488",
    "green": "#16a34a",
    "orange": "#ea580c",
    "red": "#dc2626",
    "purple": "#7c3aed",
    "slate": "#334155",
}


def text(x, y, value, size=13, weight="normal", fill="#0f172a",
         anchor="start", halo=False):
    halo_attr = (' stroke="#ffffff" stroke-width="4" paint-order="stroke" '
                 'stroke-linejoin="round"') if halo else ""
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"'
            f'{halo_attr}>{escape(str(value))}</text>')


def label(x, y, value, size=12, fill="#475569", anchor="middle",
          weight="normal"):
    return text(x, y, value, size, weight, fill, anchor, halo=True)


def node(x, y, w, h, title, sub="", fill="#f8fafc", stroke="#334155",
         title_fill="#0f172a", sub_fill="#475569", dash=None, radius=10):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    parts = [
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="1.6"{dash_attr}/>'
    ]
    cx = x + w / 2.0
    subs = [sub] if isinstance(sub, str) else list(sub or [])
    if not subs:
        parts.append(text(cx, y + h / 2.0 + 5, title, 15, "600", title_fill,
                          "middle"))
        return "\n".join(parts)
    total = 19 + 17 * (len(subs) - 1)
    start = y + h / 2.0 - total / 2.0 + 15
    parts.append(text(cx, start, title, 15, "600", title_fill, "middle"))
    for index, line in enumerate(subs):
        parts.append(text(cx, start + 18 + 16 * index, line, 12, "normal",
                          sub_fill, "middle"))
    return "\n".join(parts)


def arrow(x1, y1, x2, y2, color="gray", dash=None, width=1.8):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
            f'stroke="{COLORS[color]}" stroke-width="{width}"{dash_attr} '
            f'marker-end="url(#m-{color})"/>')


def polyline(points, color="gray", dash=None, width=1.8):
    path = "M " + " L ".join(f"{x},{y}" for x, y in points)
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<path d="{path}" fill="none" stroke="{COLORS[color]}" '
            f'stroke-width="{width}"{dash_attr} marker-end="url(#m-{color})"/>')


def marker_defs():
    lines = []
    for name, color in COLORS.items():
        lines.append(
            f'<marker id="m-{name}" viewBox="0 0 10 10" refX="9" refY="5" '
            f'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
            f'<path d="M0,0 L10,5 L0,10 Z" fill="{color}"/></marker>')
    return "\n".join(lines)


def svg(width, height, title, body):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" style="max-width:100%;height:auto;background:#ffffff" role="img" aria-label="{escape(title)}">
<title>{escape(title)}</title>
<defs>
{marker_defs()}
</defs>
{body}
</svg>
'''


def write(name, content):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(content)
    print("wrote", path)


# ---------------------------------------------------------------- 1. 统一数据流
def build_overall_data_flow():
    body = []
    body.append(text(40, 44, "CplusTrace 示例：统一采集数据流", 21, "700"))
    body.append(text(40, 70, "所有示例共用同一条链路：业务线程只写本地环，"
                             "后台线程批量上报", 13, fill="#475569"))

    body.append(node(40, 110, 220, 130, "业务进程",
                     ["hello / multiprocess", "faultlab / industrial"],
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(node(350, 110, 210, 130, "vmon-agent",
                     ["UDS 接入 · WAL", "进程/主机采集 · 控制通道"],
                     fill="#ecfeff", stroke=COLORS["teal"]))
    body.append(node(650, 110, 210, 130, "vmon-server",
                     ["内存存储 · 链路还原", "日志聚类 · 诊断/证据包"],
                     fill="#f0fdf4", stroke=COLORS["green"]))
    body.append(node(910, 84, 170, 58, "Web UI", "浏览器排障",
                     fill="#faf5ff", stroke=COLORS["purple"]))
    body.append(node(910, 156, 170, 58, "vmonctl / MCP", "CLI / 内部 AI",
                     fill="#fff7ed", stroke=COLORS["orange"]))
    body.append(node(910, 228, 170, 58, "证据包 / 告警", "脱敏 + 校验和",
                     fill="#fef2f2", stroke=COLORS["red"]))

    body.append(arrow(260, 175, 350, 175, "blue"))
    body.append(label(305, 164, "UDS JSONL"))
    body.append(arrow(560, 175, 650, 175, "teal"))
    body.append(label(605, 164, "HTTP NDJSON"))

    for target_y in (113, 185, 257):
        body.append(polyline([(860, 175), (885, 175), (885, target_y),
                              (910, target_y)], "green"))

    body.append(f'<rect x="40" y="300" width="1040" height="72" rx="12" '
                f'fill="#f8fafc" stroke="#cbd5e1" stroke-width="1.4"/>')
    body.append(text(62, 330, "fail-open：Agent 离线 / 队列满时业务继续运行；"
                              "WAL 断网续传，恢复后补传。", 13, fill="#334155"))
    body.append(text(62, 354, "错误与慢请求 100% 保留；INFO 采样 / 去重 / "
                              "模板聚类，先降噪再定位。", 13, fill="#334155"))
    return svg(1120, 400, "CplusTrace 示例统一数据流", "\n".join(body))


# ---------------------------------------------------------------- 2. hello Span 树
def build_hello_span_tree():
    body = []
    body.append(text(40, 44, "hello：demo-request-1 的 Span 树", 21, "700"))
    body.append(text(40, 70, "库存重试 2 次后失败；根 Span 标记 E_ORDER_FAILED，"
                             "错误码沿树向上传播", 13, fill="#475569"))

    body.append(node(40, 120, 230, 62, "Gateway/Handle",
                     "E_ORDER_FAILED · 24 ms",
                     fill="#fef2f2", stroke=COLORS["red"]))
    rows = [
        (105, "Auth/Verify", "2 ms", "#f8fafc", COLORS["slate"], None),
        (165, "Order/Parse", "3 ms", "#f8fafc", COLORS["slate"], None),
        (225, "Inventory/Reserve", "E_INVENTORY_TIMEOUT · 24 ms",
         "#fef2f2", COLORS["red"], None),
        (300, "Payment/Charge", "成功分支才执行",
         "#f1f5f9", "#94a3b8", "5 4"),
        (365, "Response/Build", "2 ms", "#f8fafc", COLORS["slate"], None),
    ]
    for y, title, sub, fill, stroke, dash in rows:
        body.append(node(380, y, 220, 54, title, sub, fill=fill, stroke=stroke,
                         dash=dash))
        body.append(polyline([(270, 151), (325, 151), (325, y + 27),
                              (380, y + 27)], "slate"))

    body.append(node(690, 225, 170, 54, "Cache/Get", "4 ms",
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(polyline([(600, 252), (645, 252), (645, 252), (690, 252)],
                         "blue"))
    body.append(label(645, 292, "缓存未命中后回源", 12, "#475569"))
    return svg(900, 450, "hello 示例的 Span 树", "\n".join(body))


# ---------------------------------------------------------------- 3. http_backend 时序
def build_http_backend_sequence():
    width, height = 1000, 500
    actors = [
        (70, "Python / 浏览器"),
        (230, "BFF（可选）"),
        (430, "vmon_http_backend"),
        (620, "vmon-agent"),
        (780, "vmon-server"),
        (930, "Web UI"),
    ]
    body = [text(30, 38, "http_backend：跨语言链路续接", 21, "700")]
    for x, name in actors:
        box_w = 160 if name == "vmon_http_backend" else 130
        body.append(node(x - box_w / 2, 58, box_w, 46, name,
                         fill="#f8fafc", stroke=COLORS["slate"], radius=8))
        body.append(f'<line x1="{x}" y1="104" x2="{x}" y2="440" '
                    f'stroke="#cbd5e1" stroke-width="1.4" '
                    f'stroke-dasharray="5 5"/>')

    def message(x1, x2, y, value, color="slate", dash=None):
        body.append(arrow(x1, y, x2, y, color, dash))
        body.append(label((x1 + x2) / 2.0, y - 8, value, 12, "#334155"))

    message(70, 230, 150, "HTTP + traceparent + x-request-id", "purple")
    message(230, 430, 195, "转发同一 traceparent", "purple")
    # 自身处理：小回环
    body.append(polyline([(430, 240), (500, 240), (500, 272), (430, 272)],
                         "blue"))
    body.append(label(510, 261, "context_from_traceparent → HTTP/echo Span",
                      12, "#334155", anchor="start"))
    message(430, 620, 315, "SDK 事件（UDS）", "teal")
    message(620, 780, 355, "HTTP NDJSON", "green")
    message(780, 930, 395, "trace / 日志", "green")
    message(930, 780, 425, "按 request_id 查询", "gray", "5 4")

    body.append(f'<rect x="30" y="460" width="940" height="30" rx="8" '
                f'fill="#f8fafc" stroke="#cbd5e1"/>')
    body.append(text(48, 480, "后端不新建 trace：把上游 Span 作为父节点；"
                              "解析失败才降级为新根 trace，业务请求不被拒绝。",
                     12, fill="#334155"))
    return svg(width, height, "http_backend 跨语言链路时序", "\n".join(body))


# ---------------------------------------------------------------- 4. faultlab 拓扑
def build_faultlab_topology():
    body = []
    body.append(text(40, 44, "faultlab：进程拓扑与故障注入点", 21, "700"))
    body.append(text(40, 70, "一个二进制、六种角色；同一个 --fault 参数控制"
                             "各角色的故障行为", 13, fill="#475569"))

    body.append(node(40, 110, 150, 56, "driver", "压测客户端",
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(node(250, 110, 160, 56, "gateway", "入口 / 状态码",
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(node(470, 110, 160, 56, "order", "编排 + 重试",
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(node(720, 110, 210, 64, "inventory",
                     ["slow / logstorm", "隔次 503"], fill="#fff7ed",
                     stroke=COLORS["orange"]))
    body.append(node(720, 186, 210, 64, "payment", "payment 拒绝",
                     fill="#fef2f2", stroke=COLORS["red"]))
    body.append(node(470, 268, 160, 56, "worker", "后台任务",
                     fill="#faf5ff", stroke=COLORS["purple"]))
    body.append(node(720, 268, 210, 56, "SIGSEGV", "崩溃处理器落盘",
                     fill="#fef2f2", stroke=COLORS["red"], dash="6 4"))

    body.append(arrow(190, 138, 250, 138, "blue"))
    body.append(label(220, 127, "HTTP"))
    body.append(arrow(410, 138, 470, 138, "blue"))
    body.append(label(440, 127, "traceparent"))
    body.append(polyline([(630, 138), (675, 138), (675, 142), (720, 142)],
                         "orange"))
    body.append(label(690, 104, "重试 3 次"))
    body.append(polyline([(630, 150), (675, 150), (675, 218), (720, 218)],
                         "red"))
    body.append(label(700, 178, "业务调用", anchor="start"))
    body.append(arrow(630, 296, 720, 296, "red", "6 4"))
    body.append(label(675, 285, "crash 注入"))

    # 业务进程分组 + 一条 SDK 事件出口，避免连线穿过方框。
    body.append(f'<rect x="30" y="96" width="940" height="240" rx="16" '
                f'fill="none" stroke="#cbd5e1" stroke-width="1.4" '
                f'stroke-dasharray="6 4"/>')
    body.append(polyline([(500, 336), (500, 374), (175, 374), (175, 400)],
                         "teal", "6 4"))
    body.append(label(360, 366, "所有进程：SDK 事件（UDS）", 12, "#475569",
                      anchor="start"))

    body.append(node(60, 400, 230, 70, "vmon-agent",
                     ["WAL · 采集 · 控制", "UDS 接入"],
                     fill="#ecfeff", stroke=COLORS["teal"]))
    body.append(node(350, 400, 230, 70, "vmon-server",
                     ["存储 · 还原 · 诊断", "证据包 · 告警"],
                     fill="#f0fdf4", stroke=COLORS["green"]))
    body.append(node(640, 400, 260, 70, "Web / 诊断 / 证据包",
                     "按 trace_id / request_id 查询", fill="#faf5ff",
                     stroke=COLORS["purple"]))
    body.append(arrow(290, 435, 350, 435, "green"))
    body.append(label(320, 424, "HTTP NDJSON"))
    body.append(arrow(580, 435, 640, 435, "green"))
    return svg(1120, 510, "faultlab 进程拓扑与故障注入点", "\n".join(body))


# ---------------------------------------------------------------- 5. 工业闭环框架
def build_industrial_framework():
    body = []
    body.append(text(40, 44, "industrial：7 进程 → 四道闸门 → 根因分析", 21, "700"))
    body.append(text(40, 70, "所有进程都返回 200，问题藏在数据血缘与时序里；"
                             "闸门先排除、再点名", 13, fill="#475569"))

    body.append(node(440, 92, 260, 56, "job-orchestrator",
                     "迭代循环 · 收敛判定",
                     fill="#eff6ff", stroke=COLORS["blue"]))
    body.append(arrow(570, 148, 570, 180, "blue"))
    body.append(label(605, 168, "HTTP + traceparent", 12, "#475569",
                      anchor="start"))

    body.append(f'<rect x="40" y="180" width="1080" height="126" rx="14" '
                f'fill="#f8fafc" stroke="#cbd5e1" stroke-width="1.4" '
                f'stroke-dasharray="6 4"/>')
    body.append(text(62, 204, "6 个控制器进程（每个都链接 SDK，"
                              "按 run_id + iteration_id 上报血缘）",
                     13, fill="#475569"))
    controllers = [
        (65, "motion-controller", "settle_time_ns"),
        (240, "acq-controller", "frame_id"),
        (415, "data-receiver", "requested / used"),
        (590, "algo-controller", "offset"),
        (765, "calib-service", "offset_transformed"),
        (940, "state-store", "residual"),
    ]
    for x, title, sub in controllers:
        body.append(node(x, 226, 160, 58, title, sub, fill="#ffffff",
                         stroke=COLORS["slate"], radius=8))

    body.append(arrow(570, 306, 570, 340, "slate"))
    body.append(label(640, 328, "血缘事件：run_id + iteration_id + 值指纹",
                      12, "#475569", anchor="start"))

    gates = [
        (40, "G0 状态闸门"),
        (250, "G1 计数闸门"),
        (460, "G2 身份闸门"),
        (670, "G3 时序闸门"),
        (880, "值差分检测"),
    ]
    for x, title in gates:
        body.append(node(x, 340, 190, 48, title, "", fill="#fff7ed",
                         stroke=COLORS["orange"], radius=8))
    for x in (230, 440, 650, 860):
        body.append(arrow(x + 10, 364, x + 20, 364, "orange"))
    body.append(arrow(570, 388, 570, 430, "orange"))

    body.append(node(400, 430, 340, 64, "RCA 根因分析",
                     ["候选 + 置信度", "叙述 + 证据 + unknowns"],
                     fill="#fef2f2", stroke=COLORS["red"]))
    body.append(arrow(570, 494, 570, 530, "red"))
    body.append(node(400, 530, 340, 54, "Web：血缘图 + 调用链下钻",
                     "从数据流直接跳到 Span 树",
                     fill="#faf5ff", stroke=COLORS["purple"]))
    return svg(1160, 620, "industrial 工业闭环原理框架", "\n".join(body))


def main():
    write("overall-data-flow.svg", build_overall_data_flow())
    write("hello-span-tree.svg", build_hello_span_tree())
    write("http-backend-sequence.svg", build_http_backend_sequence())
    write("faultlab-topology.svg", build_faultlab_topology())
    write("industrial-framework.svg", build_industrial_framework())


if __name__ == "__main__":
    main()
