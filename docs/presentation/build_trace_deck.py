#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成《链路时序还原》专题 PPT。

用法：
    .venv/bin/python docs/presentation/build_trace_deck.py
输出：
    docs/presentation/trace-timeline.pptx
"""

import os
import sys

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import PP_ALIGN
from pptx.util import Inches

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_deck as base  # noqa: E402

LEFT = base.LEFT
WIDTH = base.WIDTH
RIGHT = base.RIGHT
NAVY = base.NAVY
INK = base.INK
MUTED = base.MUTED
BLUE = base.BLUE
TEAL = base.TEAL
AMBER = base.AMBER
RED = base.RED
LINE = base.LINE
BG = base.BG
WHITE = base.WHITE
SOFT_BLUE = base.SOFT_BLUE
SOFT_TEAL = base.SOFT_TEAL
SOFT_AMBER = base.SOFT_AMBER
SOFT_RED = base.SOFT_RED
SOFT_GRAY = base.SOFT_GRAY

FOOTER = "vmon · 链路时序还原"


def text(slide, x, y, w, h, value, size=16, bold=False, color=INK,
         align=PP_ALIGN.LEFT, spacing=1.12, name=None):
    kwargs = {}
    if name:
        kwargs["name"] = name
    return base.add_text(slide, x, y, w, h, value, size=size, bold=bold,
                         color=color, align=align, spacing=spacing, **kwargs)


def title(slide, value, subtitle=None):
    base.add_title(slide, value, subtitle)


def footer(slide, n):
    base.add_footer(slide, n, text=FOOTER)


def rect(slide, x, y, w, h, fill, line=None, radius=0.08, shape=None):
    return base.add_rect(slide, x, y, w, h, fill, line,
                         shape or MSO_SHAPE.ROUNDED_RECTANGLE, radius)


def code(slide, x, y, w, h, value, size=10):
    base.add_code(slide, x, y, w, h, value, size=size)


def pill(slide, x, y, w, value, color=BLUE, fill=SOFT_BLUE, size=10):
    base.add_pill(slide, x, y, w, value, color=color, fill=fill, size=size)


def arrow(slide, x, y, w, h, color=BLUE, direction="right"):
    base.add_arrow(slide, x, y, w, h, color=color, direction=direction)


def notes(slide, value):
    slide.notes_slide.notes_text_frame.text = value


def new_slide(prs, dark=False):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    base.add_bg(slide, NAVY if dark else BG)
    return slide


def slide_cover(prs):
    slide = new_slide(prs, dark=True)
    rect(slide, 9.0, 0, 4.33, 7.5, RGBColor(0x12, 0x2A, 0x52), radius=0.1)
    # 事件装饰
    code(slide, 9.35, 1.15, 3.65, 2.4,
         '{"type":"span_begin",\n "trace":"7e22...",\n "span":"fa1b...",\n'
         ' "parent":"",\n "name":"Gateway/Handle"}', size=8)
    code(slide, 9.35, 3.8, 3.65, 1.75,
         '{"type":"span_begin",\n "trace":"7e22...",\n "span":"17f3...",\n'
         ' "parent":"fa1b...",\n "name":"HTTP/echo"}', size=8)
    text(slide, 0.9, 1.1, 7.8, 0.35, "vmon · 专题", size=13, bold=True,
         color=RGBColor(0x7D, 0xB0, 0xFF))
    text(slide, 0.86, 1.55, 8.2, 1.6, "链路时序还原\n进程间 + 进程内",
         size=42, bold=True, color=WHITE, spacing=1.08)
    text(slide, 0.9, 3.25, 7.6, 0.7,
         "从两条事件到一棵时间树：谁调谁、谁先错、谁最慢",
         size=19, color=RGBColor(0xC8, 0xD6, 0xE8))
    for i, (value, color) in enumerate([
            ("traceparent", BLUE), ("Span 栈", TEAL),
            ("线程泳道", AMBER), ("关键路径", RED)]):
        pill(slide, 0.9 + i * 1.9, 4.45, 1.72, value, color=WHITE,
             fill=RGBColor(0x1B, 0x35, 0x5F), size=10)
    text(slide, 0.9, 6.5, 7.0, 0.3, "2026-11 · 原理与实现", size=11,
         color=RGBColor(0x7E, 0x90, 0xAA))
    footer(slide, 1)
    notes(slide, "这套 PPT 只讲一件事：Span 事件怎么还原成跨进程、跨线程的时序。"
                 "先讲数据，再讲算法，最后看真实案例和边界。")


def slide_problem(prs, n):
    slide = new_slide(prs)
    title(slide, "为什么链路时序难还原？",
          "不是没有日志，而是日志之间没有“父子关系”和统一时间基准")
    items = [
        ("跨进程", "每个进程只看得到自己\ncaller 不知道 callee 的耗时", BLUE),
        ("多线程", "同一请求被线程池/队列拆开\ntid 不同，事件乱序", TEAL),
        ("异步/重试", "回调、定时器、重试\n父子关系容易断", AMBER),
        ("时间基准", "不同节点墙钟有偏差\n同一进程也有 wall/mono 两套", RED),
    ]
    for i, (head, body, color) in enumerate(items):
        col, row = i % 2, i // 2
        base.add_card(slide, LEFT + col * 6.05, 1.65 + row * 2.0, 5.75, 1.75,
                      head, body, color, title_size=18, body_size=13)
    rect(slide, LEFT, 5.95, WIDTH, 0.62, SOFT_BLUE)
    text(slide, LEFT + 0.3, 6.12, WIDTH - 0.6, 0.3,
         "目标：同一 trace_id 下，还原“谁调谁、谁先错、谁最慢、在哪个线程”",
         size=14, bold=True, color=base.BLUE_DARK)
    footer(slide, n)
    notes(slide, "先让听众认同问题：跨进程、跨线程、异步和时间基准，"
                 "四个因素叠加，人工对齐几乎不可能。")


def slide_event(prs, n):
    slide = new_slide(prs)
    title(slide, "还原的原材料：begin / end 两条事件",
          "所有时序都来自 Span 的开始、结束，以及 trace/span/parent/tid")
    code(slide, LEFT, 1.5, 6.85, 4.65,
         '{"type":"span_begin",\n'
         ' "trace":"7e22ec...", "span":"fa1b9c...", "parent":"",\n'
         ' "name":"Gateway/Handle", "service":"gateway",\n'
         ' "tid":101, "time_ns":1793540000100000000}\n\n'
         '{"type":"span_begin",\n'
         ' "trace":"7e22ec...", "span":"17f39b...", "parent":"fa1b9c...",\n'
         ' "name":"Order/Create", "service":"order",\n'
         ' "tid":102, "time_ns":1793540000120000000}\n\n'
         '{"type":"span_end",\n'
         ' "trace":"7e22ec...", "span":"17f39b...",\n'
         ' "error_code":"E_DB", "time_ns":1793540000180000000}', size=9)
    cards = [
        ("trace_id", "一条链路共用\n由根 Span 创建", BLUE),
        ("span_id / parent", "节点与边\nparent 为空即根", TEAL),
        ("tid", "线程维度\n多线程/异步的关键", AMBER),
        ("time_ns", "开始/结束\nduration = end - start", RED),
    ]
    for i, (head, body, color) in enumerate(cards):
        y = 1.5 + i * 1.16
        rect(slide, 7.75, y, 4.8, 1.0, WHITE, LINE)
        text(slide, 7.95, y + 0.11, 4.3, 0.26, head, size=11, bold=True,
             color=color)
        text(slide, 7.95, y + 0.4, 4.3, 0.48, body, size=9.5, color=MUTED,
             spacing=1.15)
    rect(slide, LEFT, 6.35, WIDTH, 0.45, SOFT_GRAY)
    text(slide, LEFT, 6.45, WIDTH, 0.28,
         "begin 建节点、end 补耗时；事件可乱序到达，查询时最终一致。",
         size=11, bold=True, color=MUTED, align=PP_ALIGN.CENTER)
    footer(slide, n)
    notes(slide, "强调：时序不是额外采集的，就是 begin/end 两个时间戳。"
                 "tid 和 parent 决定“在哪个线程、挂在谁下面”。")


def slide_cross_process(prs, n):
    slide = new_slide(prs)
    title(slide, "进程间：trace_id 不变，span_id 每跳新建",
          "traceparent 只带 trace/span，request_id 走独立 header")
    boxes = [
        (0.9, 2.3, "Gateway", "span G（根）", BLUE),
        (4.35, 2.3, "Order", "span O，parent=G", TEAL),
        (7.8, 2.3, "Inventory", "span I，parent=O", AMBER),
    ]
    for x, y, head, span, color in boxes:
        rect(slide, x, y, 2.6, 1.05, WHITE, color, radius=0.1)
        text(slide, x + 0.15, y + 0.15, 2.3, 0.3, head, size=15, bold=True,
             color=INK)
        text(slide, x + 0.15, y + 0.55, 2.3, 0.3, span, size=9, color=color,
             bold=True)
    arrow(slide, 3.55, 2.7, 0.65, 0.22, BLUE)
    arrow(slide, 7.0, 2.7, 0.65, 0.22, TEAL)
    text(slide, 3.35, 2.25, 1.05, 0.3, "traceparent", size=8, color=MUTED)
    text(slide, 6.8, 2.25, 1.05, 0.3, "traceparent", size=8, color=MUTED)
    text(slide, 0.9, 3.7, 10.5, 0.3,
         "同一 trace_id：7e22ec...    每个进程：新建自己的 span_id，parent=上游 span_id",
         size=11, bold=True, color=base.BLUE_DARK)
    code(slide, 0.9, 4.25, 6.4, 1.8,
         "// send side\nstd::string tp = make_traceparent(\n"
         "    trace_hi, trace_lo, current_span, true);\n"
         "// receive side\nSpanContext parent;\n"
         "context_from_traceparent(header, &parent);\n"
         "create_span_with_parent(parent, \"Handle\", ...);", size=9)
    code(slide, 7.55, 4.25, 5.0, 1.8,
         "// request_id headers\nX-Vmon-Request-Id: REQ-1\n"
         "X-Vmon-Task-Id: TASK-1\n\n"
         "// receive side\nvmon::set_request_id(header);", size=9)
    text(slide, 0.9, 6.35, WIDTH - 0.2, 0.55,
         "关键：caller 的 span_id 成为 callee 的 parent；trace_id 由根决定，绝不改。",
         size=12, bold=True, color=MUTED)
    footer(slide, n)
    notes(slide, "这一页讲跨进程规则。可以理解为：快递单号（trace）不变，"
                 "每个站点打自己的扫描号（span），并记录上一站是谁（parent）。")


def slide_intra_process(prs, n):
    slide = new_slide(prs)
    title(slide, "进程内：Span 栈自动维护父子关系",
          "VMON_SPAN 是 RAII；栈顶就是父 Span，tid 由线程本地上下文提供")
    code(slide, LEFT, 1.5, 6.2, 3.8,
         "vmon::set_request_id(\"REQ-1\");\n"
         "{\n"
         "  VMON_SPAN(\"Gateway/Handle\");   // span=A, parent=0\n"
         "  {\n"
         "    VMON_SPAN(\"Order/Parse\");    // span=B, parent=A\n"
         "  }\n"
         "  vmon::Span db = vmon::Span::create(\"DB/Query\", ...);\n"
         "  db.set_error(\"E_DB\");          // span=C, parent=A\n"
         "  db.end();\n"
         "}", size=10)
    # 栈示意
    stack_x = 7.4
    text(slide, stack_x, 1.45, 4.0, 0.3, "线程本地 Span 栈", size=12,
         bold=True, color=MUTED)
    slots = [
        ("stack[0]", "Gateway/Handle", "A", BLUE),
        ("stack[1]", "Order/Parse", "B", TEAL),
        ("stack[1] reused", "DB/Query", "C", RED),
    ]
    for i, (slot, name, sid, color) in enumerate(slots):
        y = 1.9 + i * 1.2
        rect(slide, stack_x, y, 4.6, 0.9, WHITE, color)
        text(slide, stack_x + 0.15, y + 0.08, 1.4, 0.25, slot, size=9,
             color=MUTED, name="Menlo")
        text(slide, stack_x + 0.15, y + 0.38, 2.5, 0.3, name, size=11,
             bold=True, color=INK)
        text(slide, stack_x + 3.6, y + 0.3, 0.8, 0.3, sid, size=14, bold=True,
             color=color)
    text(slide, LEFT, 5.55, WIDTH, 0.3,
         "parent 规则：跨进程用传入 parent；进程内用 stack[depth-1]；根 Span parent=0。",
         size=11, bold=True, color=base.BLUE_DARK)
    text(slide, LEFT, 6.05, WIDTH, 0.55,
         "tid 也来自线程本地上下文：同一线程的 Span 共享 tid；线程池/回调需要显式传递上下文。",
         size=10.5, color=MUTED)
    footer(slide, n)
    notes(slide, "进程内不需要手写 parent：RAII 入栈/出栈自动完成。"
                 "注意栈槽复用要清 error_code，这是实现细节。")


def slide_algorithm(prs, n):
    slide = new_slide(prs)
    title(slide, "还原算法：从事件到时间树",
          "六步，事件可以乱序到达，查询时保证最终一致")
    steps = [
        ("01", "按 trace_id 分桶", "同一条链路的事件放一起"),
        ("02", "begin 建节点", "span_begin 创建 SpanData"),
        ("03", "end 补耗时/状态", "span_end 写 end_ns、error_code"),
        ("04", "按 parent 组树", "父节点缺失时挂根并标注孤儿"),
        ("05", "算关键路径", "根到最慢叶子的链路"),
        ("06", "出错误链/线程视图", "first_error、root→error、按 tid 分组"),
    ]
    for i, (num, head, body) in enumerate(steps):
        col, row = i % 3, i // 3
        x = LEFT + col * 4.05
        y = 1.55 + row * 2.05
        rect(slide, x, y, 3.8, 1.75, WHITE, LINE)
        text(slide, x + 0.2, y + 0.14, 0.6, 0.3, num, size=13, bold=True,
             color=BLUE)
        text(slide, x + 0.2, y + 0.48, 3.4, 0.3, head, size=14, bold=True,
             color=INK)
        text(slide, x + 0.2, y + 0.9, 3.4, 0.6, body, size=10.5, color=MUTED,
             spacing=1.15)
    rect(slide, LEFT, 5.9, WIDTH, 0.6, SOFT_BLUE)
    text(slide, LEFT + 0.3, 6.05, WIDTH - 0.6, 0.3,
         "关键假设：trace_id/span_id/parent 必须齐全；缺失时只能降级为日志时间线。",
         size=11.5, bold=True, color=base.BLUE_DARK)
    footer(slide, n)
    notes(slide, "算法不复杂，难在数据质量和边界：乱序、缺失 parent、重复事件。"
                 "这些都在 Store 写入侧做了兜底。")


def slide_cross_timeline(prs, n):
    slide = new_slide(prs)
    title(slide, "进程间时序：瀑布图怎么画",
          "横轴 = start_ns - trace.first_ns；每一行一个 Span")
    origin_x = 2.6
    span_w = 8.8
    rows = [
        ("Gateway/Handle", 0, 120, BLUE, True),
        ("Order/Create", 10, 85, TEAL, True),
        ("Inventory/Reserve", 20, 40, AMBER, False),
        ("Cache/Get", 30, 10, RED, False),
    ]
    text(slide, origin_x, 1.45, span_w, 0.25, "0 ms → 120 ms", size=9,
         color=MUTED, align=PP_ALIGN.RIGHT)
    for i, (name, start, duration, color, critical) in enumerate(rows):
        y = 1.8 + i * 1.0
        text(slide, 0.75, y + 0.05, 1.7, 0.28, name, size=9.5, color=INK)
        track_x = origin_x
        rect(slide, track_x, y, span_w, 0.34, RGBColor(0x0D, 0x14, 0x20), LINE,
             radius=0.2)
        bx = track_x + (start / 120.0) * span_w
        bw = max(0.12, (duration / 120.0) * span_w)
        rect(slide, bx, y + 0.04, bw, 0.26, color, radius=0.2)
        text(slide, bx + bw + 0.08, y + 0.03, 1.1, 0.25,
             "%d ms" % duration, size=8.5, color=color, bold=critical)
    # 关键路径标注
    line_y = 6.05
    rect(slide, LEFT, line_y, WIDTH, 0.55, SOFT_RED)
    text(slide, LEFT + 0.25, line_y + 0.12, WIDTH - 0.5, 0.3,
         "关键路径 = 根到最慢叶子：Gateway → Order → Inventory（红色高亮）",
         size=11, bold=True, color=RED)
    text(slide, LEFT, 6.7, WIDTH, 0.25,
         "父 Span 的 duration 覆盖子 Span；端到端耗时 = 根 Span duration。",
         size=9.5, color=MUTED)
    footer(slide, n)
    notes(slide, "瀑布图不是按日志时间，而是按 Span 的相对偏移。"
                 "父条覆盖子条，最外层就是端到端耗时。")


def slide_thread_lane(prs, n):
    slide = new_slide(prs)
    title(slide, "进程内时序：按 tid 画线程泳道",
          "同一 trace 下，线程并行的 Span 分开画，长空档一眼可见")
    lanes = [
        ("gateway / tid 101", [("Handle", 0, 90, BLUE), ("Parse", 10, 20, TEAL)]),
        ("order / tid 102", [("Create", 8, 52, TEAL), ("Wait", 70, 12, AMBER)]),
        ("inventory / tid 103", [("Reserve", 20, 35, AMBER), ("DB", 28, 10, RED)]),
    ]
    x0, lane_w = 3.0, 8.0
    for i, (label, bars) in enumerate(lanes):
        y = 1.75 + i * 1.35
        text(slide, 0.7, y + 0.05, 2.2, 0.3, label, size=9, color=MUTED,
             name="Menlo")
        rect(slide, x0, y, lane_w, 0.38, RGBColor(0x0D, 0x14, 0x20), LINE,
             radius=0.2)
        for name, start, duration, color in bars:
            bx = x0 + (start / 100.0) * lane_w
            bw = max(0.15, (duration / 100.0) * lane_w)
            rect(slide, bx, y + 0.04, bw, 0.3, color, radius=0.2)
            text(slide, bx + 0.05, y + 0.08, max(0.5, bw - 0.1), 0.2, name,
                 size=7.5, color=WHITE, bold=True)
    # 空档标记：Create 结束到 Wait 开始之间的 10% 时间片
    rect(slide, x0 + (60 / 100.0) * lane_w, 3.1 + 0.04, (10 / 100.0) * lane_w,
         0.3, SOFT_AMBER, AMBER, radius=0.1)
    text(slide, x0 + 7.2, 5.55, 2.2, 0.3, "橙色 = >100ms 空档", size=9,
         color=AMBER, bold=True)
    text(slide, LEFT, 5.9, WIDTH, 0.55,
         "线程分组键：service + instance + node + tid；\n"
         "排序后计算相邻 Span 的间隔，超过 100ms 标记为长空档。",
         size=11, color=MUTED, spacing=1.2)
    footer(slide, n)
    notes(slide, "线程泳道回答“在哪个线程、有没有卡住”。"
                 "需要 Span 带 tid；文件日志通常没有 tid，所以只能靠 SDK。")


def slide_error_chain(prs, n):
    slide = new_slide(prs)
    title(slide, "错误时序：first_error 与错误传播链",
          "错误码不是结论，要沿 parent 回溯到根，按时间排序所有错误 Span")
    rows = [
        ("0.0 ms", "demo-python / Python/CallErrorBackend", "E_HTTP_500", "0", "15.7 ms"),
        ("0.4 ms", "http-backend / HTTP/echo", "E_ECHO", "7768490", "15.1 ms"),
    ]
    base.add_table(slide, LEFT, 1.55, 11.8, 1.5, [
        ["偏移", "服务 / Span", "错误码", "tid", "耗时"],
        *[list(row) for row in rows],
    ], col_widths=[1.3, 5.0, 1.5, 1.3, 1.5], size=10, row_height=0.5)
    text(slide, LEFT, 3.4, WIDTH, 0.3, "错误传播路径（root → first_error）",
         size=11, bold=True, color=MUTED)
    # 路径
    nodes = [
        ("demo-python", "Python/CallErrorBackend", "E_HTTP_500", RED),
        ("http-backend", "HTTP/echo", "E_ECHO", AMBER),
    ]
    x = 0.9
    for i, (svc, name, code, color) in enumerate(nodes):
        rect(slide, x, 3.85, 4.3, 1.1, WHITE, color)
        text(slide, x + 0.18, 3.98, 3.9, 0.25, svc, size=9, color=MUTED)
        text(slide, x + 0.18, 4.22, 3.9, 0.3, name, size=11, bold=True,
             color=INK)
        text(slide, x + 0.18, 4.58, 3.9, 0.25, code, size=10, bold=True,
             color=color, name="Menlo")
        x += 4.75
    arrow(slide, 5.3, 4.25, 0.5, 0.22, RED)
    text(slide, LEFT, 5.3, WIDTH, 0.45,
         "first_error = 最早进入 error 状态的 Span；first_path 由父指针回溯得到。\n"
         "多个错误 Span 按 start 排序，形成“错误传播链”，区分首个错误与后续传播。",
         size=11, color=MUTED, spacing=1.25)
    rect(slide, LEFT, 6.3, WIDTH, 0.55, SOFT_RED)
    text(slide, LEFT + 0.3, 6.42, WIDTH - 0.6, 0.3,
         "若 client 包装层也标记错误，first_error 可能是 E_HTTP_500，而根因在后端的 E_ECHO。",
         size=10.5, bold=True, color=RED)
    footer(slide, n)
    notes(slide, "错误码可能被上层包装。只看 first_error 不够，要看整条错误传播链，"
                 "尤其客户端 span 与后端 span 的错误码差异。")


def slide_async(prs, n):
    slide = new_slide(prs)
    title(slide, "异步/线程切换：上下文不能丢",
          "线程本地栈只覆盖“同一个线程”；跨线程必须显式传递 SpanContext")
    # 线程 A -> 队列 -> 线程 B
    boxes = [
        (0.9, 2.0, "业务线程 A", "Span A\nset_request_id", BLUE),
        (4.1, 2.0, "任务队列", "排队/延迟", SOFT_GRAY),
        (7.3, 2.0, "工作线程 B", "恢复上下文\n创建子 Span", TEAL),
    ]
    for x, y, head, body, color in boxes:
        rect(slide, x, y, 2.7, 1.1, WHITE, color)
        text(slide, x + 0.15, y + 0.15, 2.4, 0.3, head, size=13, bold=True,
             color=INK)
        text(slide, x + 0.15, y + 0.52, 2.4, 0.5, body, size=9.5, color=MUTED)
    arrow(slide, 3.65, 2.4, 0.4, 0.22, BLUE)
    arrow(slide, 6.85, 2.4, 0.4, 0.22, TEAL)
    code(slide, 0.9, 3.6, 6.1, 1.85,
         "// capture at submit\nSpanContext ctx = current_context();\n"
         "auto task = [ctx] {\n"
         "  // restore in worker\n  auto parent = ctx;\n"
         "  auto span = create_span_with_parent(parent, \"Work\", ...);\n"
         "};", size=9)
    rect(slide, 7.3, 3.6, 5.0, 1.85, SOFT_AMBER)
    text(slide, 7.5, 3.75, 4.6, 0.3, "当前 SDK 边界", size=11, bold=True,
         color=AMBER)
    text(slide, 7.5, 4.1, 4.6, 1.1,
         "线程本地栈不会自动跨线程；\n"
         "线程池/队列/回调包装器与\n"
         "causality.id 尚未实现。",
         size=9.5, color=MUTED, spacing=1.2)
    text(slide, LEFT, 5.8, WIDTH, 0.55,
         "结论：同一线程的 progress 由 Span 栈自动还原；跨线程必须显式捕获并恢复上下文，"
         "否则链路会在回调处断开。",
         size=11, bold=True, color=MUTED)
    footer(slide, n)
    notes(slide, "这是当前最大的边界：线程池/异步回调会断链。"
                 "代码里已经在 SpanContext 层提供了跨线程传递的最小能力，"
                 "但还没有自动化的线程池包装器。")


def slide_case_error(prs, n):
    slide = new_slide(prs)
    title(slide, "真实案例 1：跨语言错误请求",
          "request_id 收口，trace_id 看树，span_id 定位环节")
    code(slide, LEFT, 1.45, 6.2, 3.9,
         "request_id = demo-error-1793575660233\n"
         "trace_id   = 7e22ecdec4d072496a1f92ab77eb7d15\n\n"
         "span tree:\n"
         "demo-python / Python/CallErrorBackend   16.3ms  E_HTTP_500\n"
         "  -> http-backend / HTTP/echo           16.0ms  E_ECHO\n\n"
         "first_error = E_HTTP_500\n"
         "first_path  = Python/CallErrorBackend -> HTTP/echo", size=9)
    rect(slide, 7.05, 1.45, 5.35, 3.9, WHITE, LINE)
    text(slide, 7.25, 1.6, 4.9, 0.3, "还原步骤", size=11, bold=True,
         color=MUTED)
    steps = [
        "1. 按 request_id 找到 trace",
        "2. 按 trace_id 建 Span 树",
        "3. parent 连接 Python → C++",
        "4. 按 begin/end 算耗时",
        "5. 错误链标出两层错误码",
    ]
    for i, value in enumerate(steps):
        text(slide, 7.25, 2.02 + i * 0.5, 4.9, 0.3, value, size=10,
             color=INK)
    text(slide, 7.25, 4.62, 4.9, 0.5,
         "结论：错误发生在后端，但 client 也把它包装成 500；"
         "两条错误都要保留。",
         size=9.5, color=RED, bold=True, spacing=1.2)
    text(slide, LEFT, 5.6, WIDTH, 0.45,
         "数据来源：demo.py --scenario error；截图见 examples/screenshots/13-trace-multidim.png",
         size=9.5, color=MUTED)
    footer(slide, n)
    notes(slide, "用一个真实例子串起来：request_id 收口，trace_id 看树，"
                 "span_id 看出 Python 是父、C++ 是子。")


def slide_case_slow(prs, n):
    slide = new_slide(prs)
    title(slide, "真实案例 2：慢调用与关键路径",
          "span_end 时间差直接给出“慢在哪一跳”")
    rows = [
        ("demo-python / Python/CallSlowBackend", 0, 809, BLUE),
        ("http-backend / HTTP/echo", 1, 808, TEAL),
    ]
    for i, (name, start, duration, color) in enumerate(rows):
        y = 1.8 + i * 1.2
        text(slide, LEFT, y + 0.05, 5.2, 0.3, name, size=10, color=INK)
        rect(slide, 5.8, y, 6.4, 0.4, RGBColor(0x0D, 0x14, 0x20), LINE,
             radius=0.2)
        bx = 5.8 + (start / 820.0) * 6.4
        bw = max(0.2, (duration / 820.0) * 6.4)
        rect(slide, bx, y + 0.05, bw, 0.3, color, radius=0.2)
        text(slide, bx + bw + 0.1, y + 0.02, 1.1, 0.25, "%d ms" % duration,
             size=9, color=color, bold=True)
    text(slide, LEFT, 4.4, WIDTH, 0.35,
         "关键路径：demo-python → http-backend；最慢 Span 是后端 HTTP/echo（约 808ms）。",
         size=11, bold=True, color=base.BLUE_DARK)
    code(slide, LEFT, 4.9, 5.8, 1.35,
         "// backend: x-vmon-sleep-ms: 800\n"
         "// frontend only sees slow; span points to backend", size=9)
    text(slide, 6.8, 4.95, 5.5, 1.2,
         "下一步：\n"
         "· 后端 Span 内再拆函数级 Span\n"
         "· 或打开 Profiling 看火焰图\n"
         "· 关键路径决定优先优化哪一跳",
         size=9.5, color=MUTED, spacing=1.25)
    footer(slide, n)
    notes(slide, "慢调用是时序还原最直接的收益：不需要猜，"
                 "span_end - span_begin 就给出每一跳耗时。")


def slide_pipeline(prs, n):
    slide = new_slide(prs)
    title(slide, "时序数据是怎么到达 UI 的",
          "业务线程只写内存；采集、缓存、分析都在进程外")
    stages = [
        ("SDK 线程", "无锁环\nSpan 栈", BLUE),
        ("Sender", "100ms/64KB\nJSONL", TEAL),
        ("Agent", "UDS 接入\n分段 WAL", AMBER),
        ("Server", "有界 Store\n索引", RED),
        ("UI", "树/瀑布/线程\n错误链", RGBColor(0x6D, 0x5B, 0xE7)),
    ]
    x = 0.8
    for i, (head, body, color) in enumerate(stages):
        rect(slide, x, 1.8, 2.15, 1.35, WHITE, color)
        text(slide, x + 0.15, 1.95, 1.85, 0.3, head, size=12, bold=True,
             color=INK)
        text(slide, x + 0.15, 2.35, 1.85, 0.55, body, size=9, color=MUTED)
        if i < len(stages) - 1:
            arrow(slide, x + 2.2, 2.35, 0.3, 0.2, color)
        x += 2.55
    text(slide, LEFT, 3.5, WIDTH, 0.3, "时间语义", size=11, bold=True,
         color=MUTED)
    items = [
        ("time_ns", "wall clock，跨节点对齐"),
        ("mono_ns", "进程内时序，避免时钟回拨"),
        ("duration", "end_ns - start_ns，由 Server 计算"),
        ("offset", "start_ns - trace.first_ns，用于瀑布图"),
    ]
    for i, (head, body) in enumerate(items):
        col, row = i % 2, i // 2
        x = LEFT + col * 6.05
        y = 3.9 + row * 1.1
        text(slide, x, y, 1.5, 0.28, head, size=10, bold=True, color=BLUE,
             name="Menlo")
        text(slide, x + 1.55, y, 4.2, 0.4, body, size=10, color=MUTED)
    rect(slide, LEFT, 6.35, WIDTH, 0.45, SOFT_BLUE)
    text(slide, LEFT, 6.45, WIDTH, 0.3,
         "可靠性：队列满/WAL 满/Store 满都会丢弃并计数，时序还原接受“有损”而不是阻塞业务。",
         size=10.5, bold=True, color=base.BLUE_DARK, align=PP_ALIGN.CENTER)
    footer(slide, n)
    notes(slide, "这一页把时序还原放回数据流：SDK 产生时间戳，Agent 只搬运，"
                 "Server 计算 duration/offset，UI 画图。")


def slide_boundary(prs, n):
    slide = new_slide(prs)
    title(slide, "能力边界：哪些时序现在能还原",
          "诚实标注“已支持 / 部分 / 未实现”")
    data = [
        ["能力", "状态", "说明"],
        ["跨进程 Span 树", "已支持", "traceparent + create_span_with_parent"],
        ["进程内嵌套 Span", "已支持", "VMON_SPAN RAII + 线程本地栈"],
        ["线程泳道/长空档", "已支持", "Span 携带 tid；>100ms 标记"],
        ["错误传播链/first_error", "已支持", "parent 回溯 + start 排序"],
        ["关键路径/瀑布图", "已支持", "由 Server 计算，UI 渲染"],
        ["异步/线程池上下文", "部分", "可手动传 SpanContext，包装器未实现"],
        ["gRPC streaming 拦截", "接口", "StreamingSpan 有，interceptor 未接"],
        ["eBPF 无侵入弱边", "接口", "只有收集接口，无 BPF 程序"],
        ["仅 Agent（无 SDK）", "不可精确", "只能日志/指标，无法建 Span 树"],
    ]
    base.add_table(slide, LEFT, 1.5, WIDTH, 4.7, data,
                   col_widths=[3.6, 1.5, 6.5], size=10, row_height=0.42)
    text(slide, LEFT, 6.45, WIDTH, 0.45,
         "结论：精确时序必须插桩；L0 只做兜底可见性，不做调用链结论。",
         size=12, bold=True, color=RED, align=PP_ALIGN.CENTER)
    footer(slide, n)
    notes(slide, "不要夸大：跨进程/进程内/线程已经能做，"
                 "异步自动传递和 gRPC/eBPF 还没有真正接入。")


def slide_summary(prs, n):
    slide = new_slide(prs, dark=True)
    text(slide, LEFT, 0.8, 8.0, 0.3, "总结", size=12, bold=True,
         color=RGBColor(0x7D, 0xB0, 0xFF))
    text(slide, LEFT, 1.2, 11.8, 0.8, "时序还原的四个等式", size=34,
         bold=True, color=WHITE)
    items = [
        ("trace", "同一 trace_id = 同一条链路", BLUE),
        ("tree", "parent_span_id = 父子边", TEAL),
        ("time", "duration = end_ns - start_ns", AMBER),
        ("thread", "tid = 线程泳道；异步需显式传上下文", RED),
    ]
    for i, (key, value, color) in enumerate(items):
        y = 2.5 + i * 0.85
        pill(slide, LEFT, y, 1.3, key, color=WHITE, fill=color, size=10)
        text(slide, LEFT + 1.55, y + 0.04, 7.4, 0.32, value, size=15,
             color=WHITE)
    rect(slide, LEFT, 6.0, 11.8, 0.55, RGBColor(0x12, 0x2A, 0x52))
    text(slide, LEFT + 0.25, 6.12, 11.3, 0.3,
         "落地顺序：L1 SDK/传播 → L2 关键函数 Span → 异步包装器 → gRPC/eBPF。",
         size=10.5, bold=True, color=WHITE)
    footer(slide, n)
    notes(slide, "最后用四个等式收尾，方便听众记住。"
                 "同时说明落地顺序：先插桩，再异步，再零改造。")


def main():
    prs = Presentation()
    prs.slide_width = Inches(base.SW)
    prs.slide_height = Inches(base.SH)
    slide_cover(prs)
    slide_problem(prs, 2)
    slide_event(prs, 3)
    slide_cross_process(prs, 4)
    slide_intra_process(prs, 5)
    slide_algorithm(prs, 6)
    slide_cross_timeline(prs, 7)
    slide_thread_lane(prs, 8)
    slide_error_chain(prs, 9)
    slide_async(prs, 10)
    slide_case_error(prs, 11)
    slide_case_slow(prs, 12)
    slide_pipeline(prs, 13)
    slide_boundary(prs, 14)
    slide_summary(prs, 15)
    out = os.path.join(HERE, "trace-timeline.pptx")
    prs.save(out)
    print("已生成：%s" % out)
    print("页数：%d" % len(prs.slides))


if __name__ == "__main__":
    main()
