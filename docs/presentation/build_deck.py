#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 vmon 项目介绍 PPT。

用法：
    python3 -m venv .venv
    .venv/bin/pip install python-pptx
    .venv/bin/python docs/presentation/build_deck.py

输出：docs/presentation/vmon-intro.pptx
脚本只依赖 python-pptx，所有图形均为可编辑形状，不依赖外部图片。
"""

import math
import os
import sys

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

# ---------------------------------------------------------------- 主题

# macOS 上优先用 PingFang SC；Windows/WPS 会回退到微软雅黑等中文字体。
# 可用 PPT_FONT=Heiti SC 覆盖，便于在没有 PingFang 的环境里验证渲染。
FONT = os.environ.get("PPT_FONT", "PingFang SC")
NAVY = RGBColor(0x0B, 0x1B, 0x33)
INK = RGBColor(0x14, 0x21, 0x3D)
MUTED = RGBColor(0x5B, 0x6B, 0x82)
BLUE = RGBColor(0x25, 0x63, 0xEB)
BLUE_DARK = RGBColor(0x1D, 0x4E, 0xD8)
TEAL = RGBColor(0x0F, 0x9D, 0x8F)
AMBER = RGBColor(0xD9, 0x77, 0x06)
RED = RGBColor(0xDC, 0x26, 0x26)
LINE = RGBColor(0xD8, 0xE0, 0xEC)
BG = RGBColor(0xF7, 0xF9, 0xFC)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
SOFT_BLUE = RGBColor(0xEA, 0xF2, 0xFF)
SOFT_TEAL = RGBColor(0xE6, 0xF7, 0xF4)
SOFT_AMBER = RGBColor(0xFF, 0xF4, 0xE0)
SOFT_RED = RGBColor(0xFD, 0xEC, 0xEC)
SOFT_GRAY = RGBColor(0xEE, 0xF2, 0xF7)

SW, SH = 13.333, 7.5
LEFT = 0.75
RIGHT = 12.58
WIDTH = RIGHT - LEFT


def set_font(run, size=18, bold=False, color=INK, name=FONT):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = color
    run.font.name = name
    rpr = run._r.get_or_add_rPr()
    ea = rpr.find(qn("a:ea"))
    if ea is None:
        ea = rpr.makeelement(qn("a:ea"), {})
        rpr.append(ea)
    ea.set("typeface", name)


def add_text(slide, x, y, w, h, text, size=18, bold=False, color=INK,
             align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, spacing=1.12,
             name=FONT):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.auto_size = None
    tf.margin_left = tf.margin_right = Inches(0)
    tf.margin_top = tf.margin_bottom = Inches(0)
    tf.vertical_anchor = anchor
    lines = text.split("\n")
    for index, line in enumerate(lines):
        p = tf.paragraphs[0] if index == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        run = p.add_run()
        run.text = line
        set_font(run, size=size, bold=bold, color=color, name=name)
    return box


def add_rect(slide, x, y, w, h, fill, line=None, shape=MSO_SHAPE.RECTANGLE,
             radius=None, line_width=1.0, dash=None):
    rect = slide.shapes.add_shape(
        shape, Inches(x), Inches(y), Inches(w), Inches(h))
    rect.shadow.inherit = False
    if fill is None:
        rect.fill.background()
    else:
        rect.fill.solid()
        rect.fill.fore_color.rgb = fill
    if line is None:
        rect.line.fill.background()
    else:
        rect.line.color.rgb = line
        rect.line.width = Pt(line_width)
        if dash is not None:
            rect.line.dash_style = dash
    if radius is not None:
        try:
            rect.adjustments[0] = radius
        except Exception:
            pass
    return rect


def add_card(slide, x, y, w, h, title, body, accent=BLUE, fill=WHITE,
             title_size=17, body_size=13, badge=None):
    add_rect(slide, x, y, w, h, fill, LINE, MSO_SHAPE.ROUNDED_RECTANGLE, 0.06)
    add_rect(slide, x, y + 0.16, 0.07, h - 0.32, accent,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.5)
    add_text(slide, x + 0.26, y + 0.18, w - 0.44, 0.42, title,
             size=title_size, bold=True, color=INK)
    if badge:
        add_text(slide, x + w - 0.95, y + 0.18, 0.8, 0.3, badge,
                 size=11, bold=True, color=accent, align=PP_ALIGN.RIGHT)
    add_text(slide, x + 0.26, y + 0.66, w - 0.44, h - 0.8, body,
             size=body_size, color=MUTED, spacing=1.18)


def add_title(slide, title, subtitle=None, dark=False):
    color = WHITE if dark else INK
    sub_color = RGBColor(0xB9, 0xC6, 0xD8) if dark else MUTED
    add_text(slide, LEFT, 0.42, WIDTH, 0.55, title, size=29, bold=True,
             color=color)
    if subtitle:
        add_text(slide, LEFT, 0.98, WIDTH, 0.38, subtitle, size=14,
                 color=sub_color)


def add_footer(slide, number, text="vmon · 局域网 C++ 维测中心", dark=False):
    color = RGBColor(0x8A, 0x9B, 0xB4) if dark else RGBColor(0x9A, 0xA7, 0xB8)
    add_text(slide, LEFT, 7.06, 8.0, 0.25, text, size=10, color=color)
    add_text(slide, RIGHT - 0.6, 7.06, 0.6, 0.25, str(number), size=10,
             color=color, align=PP_ALIGN.RIGHT)


def add_bg(slide, color):
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = color


def add_arrow(slide, x, y, w, h, color=BLUE, direction="right"):
    shape = MSO_SHAPE.RIGHT_ARROW if direction == "right" else MSO_SHAPE.DOWN_ARROW
    add_rect(slide, x, y, w, h, color, shape=shape)


def add_pill(slide, x, y, w, text, color=BLUE, fill=SOFT_BLUE, size=11):
    add_rect(slide, x, y, w, 0.32, fill, shape=MSO_SHAPE.ROUNDED_RECTANGLE,
             radius=0.5)
    add_text(slide, x, y + 0.045, w, 0.24, text, size=size, bold=True,
             color=color, align=PP_ALIGN.CENTER)


def add_bullets(slide, x, y, w, h, items, size=14, color=INK, gap=0.1):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.auto_size = None
    tf.margin_left = tf.margin_right = Inches(0)
    tf.margin_top = tf.margin_bottom = Inches(0)
    for index, item in enumerate(items):
        p = tf.paragraphs[0] if index == 0 else tf.add_paragraph()
        p.space_after = Pt(gap * 72)
        p.line_spacing = 1.15
        run = p.add_run()
        run.text = "•  " + item
        set_font(run, size=size, color=color)
    return box


def add_code(slide, x, y, w, h, code, size=11):
    add_rect(slide, x, y, w, h, RGBColor(0x0F, 0x1B, 0x2D),
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.04)
    box = slide.shapes.add_textbox(Inches(x + 0.18), Inches(y + 0.12),
                                   Inches(w - 0.36), Inches(h - 0.2))
    tf = box.text_frame
    tf.word_wrap = False
    tf.auto_size = None
    tf.margin_left = tf.margin_right = Inches(0)
    tf.margin_top = tf.margin_bottom = Inches(0)
    for index, line in enumerate(code.split("\n")):
        p = tf.paragraphs[0] if index == 0 else tf.add_paragraph()
        p.line_spacing = 1.1
        run = p.add_run()
        run.text = line
        set_font(run, size=size, color=RGBColor(0xD7, 0xE3, 0xF4),
                 name="Menlo")


def add_table(slide, x, y, w, h, data, col_widths=None, header=True,
              size=11, row_height=0.38):
    rows, cols = len(data), len(data[0])
    table = slide.shapes.add_table(rows, cols, Inches(x), Inches(y),
                                   Inches(w), Inches(h)).table
    if col_widths:
        total = sum(col_widths)
        for index, cw in enumerate(col_widths):
            table.columns[index].width = Emu(int(Inches(w) * cw / total))
    for r in range(rows):
        table.rows[r].height = Inches(row_height)
        for c in range(cols):
            cell = table.cell(r, c)
            cell.margin_left = Inches(0.08)
            cell.margin_right = Inches(0.08)
            cell.margin_top = Inches(0.03)
            cell.margin_bottom = Inches(0.03)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            text = str(data[r][c])
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            p.alignment = PP_ALIGN.CENTER if c > 0 else PP_ALIGN.LEFT
            run = p.add_run()
            run.text = text
            if header and r == 0:
                set_font(run, size=size, bold=True, color=WHITE)
                cell.fill.solid()
                cell.fill.fore_color.rgb = NAVY
            else:
                set_font(run, size=size, color=INK,
                         bold=(c == 0))
                cell.fill.solid()
                cell.fill.fore_color.rgb = WHITE if r % 2 else SOFT_GRAY
    return table


# ---------------------------------------------------------------- 幻灯片


def slide_cover(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, NAVY)
    # 装饰：右侧渐层圆角块与线条
    add_rect(slide, 9.25, 0.0, 4.08, 7.5, RGBColor(0x12, 0x2A, 0x52),
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_rect(slide, 9.9, 0.9, 3.4, 3.4, RGBColor(0x16, 0x35, 0x66),
             shape=MSO_SHAPE.OVAL)
    for i, (yy, ww, color) in enumerate([
            (2.0, 3.3, BLUE), (2.5, 2.5, TEAL), (3.0, 1.8, AMBER)]):
        add_rect(slide, 9.85, yy, ww, 0.10, color,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.5)

    add_text(slide, 0.9, 1.15, 7.5, 0.35, "CplusTrace  /  vmon", size=14,
             bold=True, color=RGBColor(0x7D, 0xB0, 0xFF))
    add_text(slide, 0.86, 1.62, 8.4, 1.5,
             "局域网 C++ 系统\n维测中心", size=44, bold=True, color=WHITE,
             spacing=1.08)
    add_text(slide, 0.9, 3.35, 7.8, 0.9,
             "把「翻多机多进程日志」变成「看一条请求」",
             size=20, color=RGBColor(0xC8, 0xD6, 0xE8))
    for i, text in enumerate(["离线私有部署", "10–50 节点", "50–200 进程"]):
        add_pill(slide, 0.9 + i * 2.05, 4.55, 1.85, text, color=WHITE,
                 fill=RGBColor(0x1B, 0x35, 0x5F), size=11)
    add_text(slide, 0.9, 6.55, 8.0, 0.3,
             "项目介绍 · 2026-11", size=12, color=RGBColor(0x7E, 0x90, 0xAA))
    add_footer(slide, 1, dark=True)
    slide.notes_slide.notes_text_frame.text = (
        "开场先讲结论：这套系统不是又一个监控大屏，而是把一次请求在多进程、"
        "多节点里的链路、日志、指标串起来，让排障从“多人多小时”变成“单人分钟级”。")


def slide_summary(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "一句话：输入一个请求号，拿到完整排障证据",
              "不是更多日志，而是更短的定位路径")
    add_rect(slide, LEFT, 1.55, 6.15, 4.55, NAVY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.05)
    add_text(slide, LEFT + 0.45, 1.95, 5.3, 0.4, "排障入口", size=13,
             bold=True, color=RGBColor(0x7D, 0xB0, 0xFF))
    add_text(slide, LEFT + 0.45, 2.35, 5.35, 1.3,
             "request_id\ntrace_id / task_id / 错误码",
             size=23, bold=True, color=WHITE, spacing=1.25)
    add_text(slide, LEFT + 0.45, 3.72, 5.3, 1.9,
             "自动还原跨进程调用链\n关联每个环节的关键日志\n"
             "给出首个错误与可疑根因\n导出可复核的证据包",
             size=16, color=RGBColor(0xD3, 0xDF, 0xEE), spacing=1.5)
    add_text(slide, LEFT + 0.45, 5.55, 5.3, 0.3,
             "目标：分钟级定位，而不是多人多小时", size=13, bold=True,
             color=RGBColor(0x7D, 0xB0, 0xFF))

    cards = [
        ("看得见", "节点 / 进程 / 指标\n主机负载、内存、存活状态", BLUE),
        ("串得起", "跨进程调用链\nW3C traceparent 贯穿", TEAL),
        ("定得准", "日志降噪 + 首个错误\n模板聚类、关键路径", AMBER),
        ("留得下", "事件闭环 + 证据包\n脱敏、校验和、可带走", RED),
    ]
    for i, (title, body, color) in enumerate(cards):
        col, row = i % 2, i // 2
        add_card(slide, 7.2 + col * 2.72, 1.55 + row * 2.3, 2.55, 2.05,
                 title, body, color, title_size=18, body_size=12)
    add_text(slide, 7.2, 6.15, 5.3, 0.3, "四件事：看得见、串得起、定得准、留得下",
             size=13, bold=True, color=MUTED)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这四张卡片就是整套系统的价值主张，后面每一块能力都会对应到这里。")


def slide_pain(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "为什么现在排障这么慢？",
              "问题不在“没有日志”，而在“日志之间没有关系”")
    items = [
        ("进程多、链路长", "10–50 个节点、50–200 个进程\n一次请求可能跨 5 跳以上", BLUE),
        ("日志严重过载", "监控、控制、业务、错误日志\n混在一起，信息量过大", AMBER),
        ("缺少请求上下文", "出错时只打一行错误日志\n不知道之前发生了什么", RED),
        ("人工对齐时间线", "多人、多机、多文件\n按时间戳手工拼图", TEAL),
    ]
    for i, (title, body, color) in enumerate(items):
        col, row = i % 2, i // 2
        add_card(slide, LEFT + col * 6.05, 1.7 + row * 2.1, 5.75, 1.85,
                 title, body, color, title_size=19, body_size=14)
    add_rect(slide, LEFT, 6.05, WIDTH, 0.62, SOFT_RED,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_text(slide, LEFT + 0.3, 6.2, WIDTH - 0.6, 0.35,
             "结果：定位依赖个人经验，同类问题难以复用，故障复盘缺少证据",
             size=14, bold=True, color=RED)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "先让听众共鸣：不是没有工具，而是工具之间不关联，排障仍然靠人。")


def slide_goal(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "目标不是“大而全”，而是最短排障路径",
              "把发现异常 → 定位 → 复盘，压缩成一条固定路线")
    stats = [
        ("分钟级", "从发现到定位", "按请求查，不再全量翻日志", BLUE),
        ("≥ 50%", "日志量下降", "错误全留，低价值日志降噪", TEAL),
        ("0 行", "存量程序起步改造", "L0 部署 Agent 即可获得基础能力", AMBER),
    ]
    for i, (big, label, body, color) in enumerate(stats):
        x = LEFT + i * 4.1
        add_rect(slide, x, 1.6, 3.85, 2.25, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
        add_text(slide, x + 0.3, 1.82, 3.2, 0.7, big, size=34, bold=True,
                 color=color)
        add_text(slide, x + 0.3, 2.5, 3.2, 0.3, label, size=13, bold=True,
                 color=INK)
        add_text(slide, x + 0.3, 2.85, 3.25, 0.7, body, size=12, color=MUTED)
    add_text(slide, LEFT, 4.2, 6.0, 0.35, "固定路线", size=14, bold=True,
             color=MUTED)
    steps = ["发现异常", "看链路/指标", "定位服务与节点", "关联日志", "事件闭环", "证据包"]
    x = LEFT
    for i, step in enumerate(steps):
        w = 1.82 if i < 5 else 1.65
        fill = NAVY if i in (0, 5) else SOFT_BLUE
        color = WHITE if i in (0, 5) else BLUE_DARK
        add_rect(slide, x, 4.62, w, 0.62, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.18)
        add_text(slide, x + 0.05, 4.78, w - 0.1, 0.3, step, size=11,
                 bold=True, color=color, align=PP_ALIGN.CENTER)
        x += w + 0.14
    add_rect(slide, LEFT, 5.62, WIDTH, 1.0, SOFT_GRAY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
    add_text(slide, LEFT + 0.3, 5.78, WIDTH - 0.6, 0.3, "明确不做", size=12,
             bold=True, color=MUTED)
    add_text(slide, LEFT + 1.55, 5.76, WIDTH - 1.9, 0.7,
             "公网 SaaS、多租户、RUM/移动端、K8s 资产、云成本、SIEM；"
             "也不做会明显影响生产的常驻高开销采样。",
             size=13, color=INK)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "强调取舍：我们只服务局域网 C++ 后端这一套系统，换取更短的路径和更低的部署门槛。")


def slide_architecture(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "总体架构：四层，一条数据流",
              "业务进程埋点 → 节点采集 → 中心分析 → 工作台使用")
    bands = [
        ("业务进程", "探针", BLUE, SOFT_BLUE,
         ["结构化日志", "Span / traceparent", "心跳", "崩溃兜底"]),
        ("vmon-agent", "采集员", TEAL, SOFT_TEAL,
         ["UDS 接入", "分段 WAL", "进程 / 日志采集", "命令执行"]),
        ("vmon-server", "大脑", AMBER, SOFT_AMBER,
         ["鉴权接入", "有界存储", "链路还原", "聚类 / 规则", "诊断 / 证据包"]),
        ("使用方", "工作台", RGBColor(0x6D, 0x5B, 0xE7), RGBColor(0xEF, 0xEC, 0xFD),
         ["Web UI", "CLI vmonctl", "MCP（内部版）"]),
    ]
    y = 1.38
    band_h = 1.05
    gap = 0.25
    for index, (name, role, color, fill, comps) in enumerate(bands):
        add_rect(slide, LEFT, y, WIDTH, band_h, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.06)
        add_text(slide, LEFT + 0.22, y + 0.18, 1.75, 0.35, name, size=15,
                 bold=True, color=INK)
        add_text(slide, LEFT + 0.22, y + 0.56, 1.75, 0.3, role, size=11,
                 color=color, bold=True)
        cx = LEFT + 2.05
        avail = WIDTH - 2.25
        cw = (avail - (len(comps) - 1) * 0.12) / len(comps)
        for comp in comps:
            add_rect(slide, cx, y + 0.22, cw, 0.6, WHITE, LINE,
                     MSO_SHAPE.ROUNDED_RECTANGLE, 0.12)
            add_text(slide, cx + 0.04, y + 0.38, cw - 0.08, 0.3, comp,
                     size=12, color=INK, align=PP_ALIGN.CENTER)
            cx += cw + 0.12
        if index < len(bands) - 1:
            add_arrow(slide, 6.55, y + band_h + 0.03, 0.22, gap - 0.08, color)
            labels = ["UDS JSONL", "HTTP NDJSON", "HTTP 只读 API"]
            add_text(slide, 6.85, y + band_h + 0.05, 2.2, 0.22,
                     labels[index], size=10, color=MUTED)
        y += band_h + gap
    add_rect(slide, LEFT, 6.48, WIDTH, 0.44, NAVY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_text(slide, LEFT + 0.25, 6.57, WIDTH - 0.5, 0.3,
             "纯内网 · 可离线 · 无外部存储依赖    ｜    "
             "Agent 本地 WAL 断网续传，Server 内存有界存储",
             size=12, bold=True, color=WHITE, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "用“探针、采集员、大脑、工作台”四个比喻讲清四层。强调数据只往一个方向流，"
        "断网时 Agent 先存本地，恢复后补传。")


def slide_modules(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "模块设计：一套事件模型，五个可替换部件",
              "模块之间只通过事件、UDS 与 HTTP API 交互，便于独立升级")
    panels = [
        ("SDK / 客户端", "进程内探针", BLUE, SOFT_BLUE, [
            ("core/", "线程本地环、Span 栈"),
            ("trace/", "Span、traceparent"),
            ("transport/", "发送线程、批量"),
            ("control/", "动态日志级别"),
            ("crash/", "崩溃兜底"),
            ("clients/", "Python / Web 客户端"),
            ("adapters/", "gRPC / Socket 传播"),
        ]),
        ("Agent", "节点采集员", TEAL, SOFT_TEAL, [
            ("ingest", "UDS 接入"),
            ("wal", "分段本地缓存"),
            ("uploader", "批量上传"),
            ("process_scan", "进程 / 主机扫描"),
            ("log_tail", "文件日志采集"),
            ("command_runner", "白名单只读命令"),
            ("crash_collect", "崩溃报告收集"),
        ]),
        ("Server", "中心大脑", AMBER, SOFT_AMBER, [
            ("api", "HTTP / 鉴权 / MCP"),
            ("store", "有界内存存储"),
            ("catalog", "服务-实例-节点"),
            ("trace_builder", "Span 树 / 关键路径"),
            ("log_cluster", "模板 / 指纹"),
            ("rules + slo", "规则 / SLO"),
            ("diagnostics", "诊断 / 证据包"),
            ("profiler/crash", "剖析 / 崩溃"),
        ]),
    ]
    for i, (title, role, color, fill, rows) in enumerate(panels):
        x = 0.6 + i * 3.98
        w = 3.78
        add_rect(slide, x, 1.38, w, 4.55, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
        add_rect(slide, x, 1.38, w, 0.56, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
        add_text(slide, x + 0.2, 1.5, w - 0.4, 0.3, title, size=13,
                 bold=True, color=INK)
        add_text(slide, x + w - 1.5, 1.52, 1.3, 0.25, role, size=9,
                 bold=True, color=color, align=PP_ALIGN.RIGHT)
        for j, (name, desc) in enumerate(rows):
            y = 2.1 + j * 0.5
            add_text(slide, x + 0.2, y, 1.35, 0.24, name, size=9,
                     bold=True, color=color, name="Menlo")
            add_text(slide, x + 1.55, y, w - 1.75, 0.3, desc, size=9,
                     color=MUTED)
    add_text(slide, 4.12, 3.4, 0.55, 0.25, "UDS", size=8, color=MUTED,
             align=PP_ALIGN.CENTER)
    add_text(slide, 8.1, 3.4, 0.55, 0.25, "HTTP", size=8, color=MUTED,
             align=PP_ALIGN.CENTER)
    add_rect(slide, 0.6, 6.05, 11.78, 0.55, SOFT_GRAY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.1)
    add_text(slide, 0.8, 6.17, 11.4, 0.3,
             "common/ 事件模型与 SHA-256    ·    symbolizer/ 离线符号化    ·    "
             "tools/ CLI、MCP、crash-helper    ·    web/ 静态 UI",
             size=10, bold=True, color=MUTED, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页讲代码模块，不讲运行时流程。三列分别是探针、采集员、中心，"
        "底部是公共能力；模块之间只有事件、UDS、HTTP 三种接口。"
        "详细类图、时序图与框架原理图见 docs/module-design.md。")


def slide_scheduling(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "系统框架调度：线程、定时器与背压",
              "所有耗时操作都在业务线程之外；满了就降级，不阻塞业务")
    lanes = [
        ("业务进程", 1.35, 1.25, SOFT_BLUE, BLUE, [
            ("业务线程", "写 ring，无锁"),
            ("sender 线程", "100ms / 64KB 批量"),
            ("UDS", "JSONL 事件"),
            ("控制 socket", "级别 / 栈 / 命令"),
        ]),
        ("Agent", 2.95, 1.35, SOFT_TEAL, TEAL, [
            ("ingest", "收 UDS"),
            ("WAL", "分段落盘"),
            ("uploader", "200ms 上传"),
            ("scan", "2s 进程/主机"),
            ("log tail", "500ms 轮询"),
            ("command", "100ms 轮询"),
        ]),
        ("Server", 4.7, 1.35, SOFT_AMBER, AMBER, [
            ("HTTP 线程池", "4 线程"),
            ("store", "有界内存"),
            ("分析", "链路 / 聚类"),
            ("规则 / SLO", "事件评估"),
            ("查询 API", "只读"),
            ("证据包", "脱敏导出"),
        ]),
    ]
    for name, y, h, fill, color, boxes in lanes:
        add_rect(slide, 0.6, y, 12.35, h, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.06)
        add_text(slide, 0.8, y + 0.18, 1.65, 0.3, name, size=13, bold=True,
                 color=INK)
        count = len(boxes)
        avail = 10.0
        bw = (avail - (count - 1) * 0.12) / count
        bx = 2.5
        for title, body in boxes:
            add_rect(slide, bx, y + 0.18, bw, h - 0.36, WHITE, LINE,
                     MSO_SHAPE.ROUNDED_RECTANGLE, 0.1)
            add_text(slide, bx + 0.05, y + 0.3, bw - 0.1, 0.24, title,
                     size=9.5, bold=True, color=INK, align=PP_ALIGN.CENTER)
            add_text(slide, bx + 0.05, y + 0.58, bw - 0.1, 0.45, body,
                     size=7.8, color=MUTED, align=PP_ALIGN.CENTER,
                     spacing=1.05)
            bx += bw + 0.12
    # 定时器与协议标注
    add_text(slide, 2.6, 2.72, 9.8, 0.2,
             "定时器：scan 2s · log tail 500ms · command 100ms · "
             "upload 200ms · SDK flush 100ms",
             size=8.5, color=MUTED)
    add_text(slide, 2.6, 4.45, 9.8, 0.2,
             "协议：UDS JSONL（SDK→Agent）  ·  HTTP NDJSON（Agent→Server）  ·  "
             "HTTP 只读（Web/CLI/MCP→Server）",
             size=8.5, color=MUTED)
    add_rect(slide, 0.6, 6.25, 12.35, 0.55, NAVY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_text(slide, 0.8, 6.37, 11.95, 0.3,
             "背压：队列满 → 丢弃并计数  ·  WAL 满 → 优先保错误  ·  "
             "Store 满 → 淘汰最旧  ·  中心停机 → 恢复后补传",
             size=9.5, bold=True, color=WHITE, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页回答“它到底怎么跑”：业务线程只写内存环，后面全是独立线程和定时器。"
        "任何一级满了都走背压，不反向阻塞业务。")


def slide_model(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "框架原理 ①：统一事件模型，是跨信号关联的基础",
              "日志、指标、Span、崩溃走同一个结构，只是 type 不同")
    add_code(slide, LEFT, 1.55, 6.55, 4.2,
             "{\n"
             "  \"type\": \"span_end\",\n"
             "  \"time_ns\": 1793540000123000000,\n"
             "  \"node\": \"node-07\",\n"
             "  \"service\": \"order\",\n"
             "  \"version\": \"v2.3.1\",\n"
             "  \"instance\": \"order-3\",\n"
             "  \"trace_id\": \"4bf92f3577b34da6...\",\n"
             "  \"span_id\": \"00f067aa0ba902b7\",\n"
             "  \"parent\": \"a3ce929d0e0e4736\",\n"
             "  \"request_id\": \"REQ-2026-001\",\n"
             "  \"task_id\": \"TASK-88\",\n"
             "  \"pid\": 4242,\n"
             "  \"attrs\": {\n"
             "    \"duration_ns\": \"86000000\",\n"
             "    \"error_code\": \"E_DB\"\n"
             "  }\n"
             "}", size=10)
    add_text(slide, LEFT, 5.95, 6.55, 0.6,
             "同一个结构带来三个好处：\n"
             "① 一条上传通道    ② 一套鉴权/限流    ③ 一份索引逻辑",
             size=11.5, color=MUTED, spacing=1.3)

    keys = [
        ("trace_id + span_id + parent", "谁调谁", "把一次请求经过的所有进程\n还原成父子树", BLUE),
        ("request_id / task_id", "业务是哪一单", "跨系统补齐未采样段落\n按业务号复盘", TEAL),
        ("node + pid + start_time", "哪个进程、哪次启动", "避免 PID 复用\n区分重启前后", AMBER),
        ("build_id + version", "对应哪次构建", "崩溃、符号、发布变更\n都靠它关联", RED),
    ]
    for i, (field, title, body, color) in enumerate(keys):
        y = 1.55 + i * 1.18
        add_rect(slide, 7.65, y, 4.93, 1.0, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.08)
        add_rect(slide, 7.65, y + 0.14, 0.07, 0.72, color)
        add_text(slide, 7.87, y + 0.10, 4.5, 0.22, field, size=10,
                 bold=True, color=color, name="Menlo")
        add_text(slide, 7.87, y + 0.34, 4.5, 0.26, title, size=12, bold=True,
                 color=INK)
        add_text(slide, 7.87, y + 0.62, 4.5, 0.34, body, size=9, color=MUTED,
                 spacing=1.1)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "先讲“数据长什么样”。所有信号统一成事件，服务端只维护一套管道。"
        "四组关联键分别回答：谁调谁、哪一单、哪个进程、哪次构建。")


def slide_principle(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "框架原理 ②：一次请求的完整路径",
              "四个关联键 + 一条可靠的数据通道")
    steps = [
        ("01", "创建 Span", "业务进程自动带上\ntrace_id / span_id / request_id"),
        ("02", "跨进程传播", "gRPC / HTTP header 携带\ntraceparent，子进程续接"),
        ("03", "日志带上下文", "同线程日志自动注入\n当前 Span，不再靠猜"),
        ("04", "本地缓冲", "Agent 写 WAL，批量上传\n断网不丢、恢复补传"),
        ("05", "中心还原", "Span 树、关键路径\n首个错误、关联日志"),
        ("06", "按请求查看", "Web 输入 request_id\n得到同一条时间线"),
    ]
    for i, (num, title, body) in enumerate(steps):
        y = 1.42 + i * 0.86
        add_rect(slide, LEFT, y, 0.52, 0.52, NAVY,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.25)
        add_text(slide, LEFT, y + 0.13, 0.52, 0.26, num, size=11, bold=True,
                 color=WHITE, align=PP_ALIGN.CENTER)
        add_text(slide, LEFT + 0.7, y + 0.02, 1.7, 0.3, title, size=14,
                 bold=True, color=INK)
        add_text(slide, LEFT + 2.35, y + 0.02, 4.0, 0.55, body, size=11,
                 color=MUTED, spacing=1.1)

    # 右侧：迷你 trace 树 + 瀑布图
    px, py, pw, ph = 7.35, 1.4, 5.2, 3.55
    add_rect(slide, px, py, pw, ph, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
    add_text(slide, px + 0.25, py + 0.16, 3.0, 0.3, "同一 trace 的时间线",
             size=12, bold=True, color=MUTED)
    rows = [
        ("Gateway / Handle", 0.0, 3.9, BLUE, 0),
        ("Order / Create", 0.35, 2.6, TEAL, 1),
        ("Inventory / Check", 0.9, 1.5, AMBER, 2),
        ("Cache / Get", 1.15, 0.8, RED, 3),
    ]
    bar_x = px + 2.35
    bar_w = 2.45
    for i, (name, offset, length, color, depth) in enumerate(rows):
        y = py + 0.62 + i * 0.62
        add_text(slide, px + 0.25 + depth * 0.18, y + 0.06, 2.0, 0.28,
                 name, size=10, color=INK)
        add_rect(slide, bar_x + offset * 0.55, y + 0.08,
                 max(0.25, length * 0.55), 0.26, color,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.35)
    add_text(slide, bar_x, py + 3.05, bar_w, 0.25, "相对时间 →", size=9,
             color=MUTED, align=PP_ALIGN.RIGHT)
    add_text(slide, px + 0.25, py + 3.02, 2.0, 0.25,
             "错误 Span 标红", size=9, color=RED, bold=True)
    add_rect(slide, px, 5.18, pw, 0.8, SOFT_BLUE,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
    add_text(slide, px + 0.25, 5.32, pw - 0.5, 0.55,
             "trace_id 串链路 · request_id 串业务\nWAL 保可靠 · 有界存储保可控",
             size=12, bold=True, color=BLUE_DARK, spacing=1.2)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页是原理核心：Span 负责“谁调谁”，request_id 负责“业务上是哪一单”，"
        "日志继承当前 Span 上下文，Agent WAL 保证网络抖动不丢数据。")


def slide_internals(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "框架原理 ③：三层各自做了什么",
              "SDK 负责“轻”，Agent 负责“稳”，Server 负责“算”")
    columns = [
        ("SDK：进程内探针", "轻", BLUE, SOFT_BLUE, [
            "每线程一个无锁队列，写日志/打点不抢锁",
            "Span 用 RAII：进函数自动开始，出函数自动结束",
            "上下文放线程本地，日志自动带 trace/request",
            "队列满就丢弃并计数，绝不阻塞业务线程",
        ]),
        ("Agent：节点采集员", "稳", TEAL, SOFT_TEAL, [
            "接收业务进程 UDS 上的 JSONL 事件",
            "先写本地分段 WAL 小账本，再批量上传",
            "扫描进程与日志文件，执行白名单只读命令",
            "中心停机或断网时继续缓存，恢复后补传",
        ]),
        ("Server：中心大脑", "算", AMBER, SOFT_AMBER, [
            "接入、鉴权、采样、去重，统一入口",
            "把 begin/end 事件拼成 Span 树",
            "计算关键路径、首个错误、日志模板指纹",
            "内存有界：超过上限淘汰最旧数据",
        ]),
    ]
    for i, (title, tag, color, fill, items) in enumerate(columns):
        x = LEFT + i * 4.1
        add_rect(slide, x, 1.5, 3.85, 4.9, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
        add_rect(slide, x, 1.5, 3.85, 0.62, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
        add_text(slide, x + 0.22, 1.63, 2.6, 0.35, title, size=14, bold=True,
                 color=INK)
        add_pill(slide, x + 3.05, 1.63, 0.6, tag, color=WHITE, fill=color,
                 size=10)
        for j, item in enumerate(items):
            y = 2.3 + j * 0.8
            add_rect(slide, x + 0.25, y + 0.12, 0.1, 0.1, color,
                     shape=MSO_SHAPE.OVAL)
            add_text(slide, x + 0.48, y, 3.15, 0.68, item, size=10.5,
                     color=MUTED, spacing=1.2)
        footers = ["CPU < 1% · 常驻 < 15 MB",
                   "WAL 有上限 · 恢复后补传",
                   "查询有超时 · 存储有上限"]
        add_rect(slide, x + 0.22, 5.78, 3.4, 0.34, fill,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.2)
        add_text(slide, x + 0.22, 5.84, 3.4, 0.24, footers[i], size=9.5,
                 bold=True, color=color, align=PP_ALIGN.CENTER)
    add_text(slide, LEFT, 6.55, WIDTH, 0.3,
             "一句话：业务只负责“打点”，采集与计算都在进程外完成，"
             "所以维测组件可以独立升级、限流和关闭。",
             size=11.5, color=MUTED, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "用三个字概括三层：轻、稳、算。强调所有重活都不在业务进程里做，"
        "这是“不影响生产”的结构性保证。")


def slide_trace_assembly(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "框架原理 ④：两条事件，怎样还原一棵调用链",
              "begin 建节点、end 补耗时、parent 组树、关键路径定慢点")
    add_rect(slide, LEFT, 1.5, 5.5, 3.3, RGBColor(0x0F, 0x1B, 0x2D),
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.04)
    add_text(slide, LEFT + 0.2, 1.62, 3.0, 0.25, "到达中心的事件（可乱序）",
             size=10.5, bold=True, color=RGBColor(0x7D, 0xB0, 0xFF))
    events = [
        ("span_begin", "T1 / S1  Gateway/Handle"),
        ("span_begin", "T1 / S2  parent=S1  Order/Create"),
        ("span_end", "T1 / S2  86ms  error=E_DB"),
        ("span_end", "T1 / S1  120ms"),
    ]
    for i, (kind, body) in enumerate(events):
        y = 2.05 + i * 0.62
        color = RGBColor(0x7D, 0xD3, 0xFC) if kind == "span_begin" \
            else RGBColor(0xF8, 0x7A, 0x7A)
        add_text(slide, LEFT + 0.2, y, 0.95, 0.25, kind, size=9, bold=True,
                 color=color, name="Menlo")
        add_text(slide, LEFT + 1.15, y, 4.1, 0.25, body, size=9.5,
                 color=RGBColor(0xC9, 0xD6, 0xE8), name="Menlo")
    add_text(slide, LEFT + 0.2, 4.45, 5.1, 0.25,
             "乱序也能还原：树在 end 到达后补齐，查询时最终一致。",
             size=9, color=RGBColor(0x8A, 0x9B, 0xB4))

    # 右侧：树
    add_rect(slide, 6.75, 1.5, 5.85, 3.3, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
    add_text(slide, 6.97, 1.62, 3.0, 0.25, "还原后的 Span 树",
             size=10.5, bold=True, color=MUTED)
    add_rect(slide, 7.0, 2.15, 2.3, 0.7, SOFT_BLUE, BLUE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.08)
    add_text(slide, 7.1, 2.25, 2.1, 0.25, "Gateway / Handle", size=10,
             bold=True, color=INK)
    add_text(slide, 7.1, 2.52, 2.1, 0.22, "120 ms", size=9, color=MUTED)
    add_rect(slide, 8.15, 3.2, 0.02, 0.35, LINE)
    add_rect(slide, 7.0, 3.55, 2.3, 0.7, SOFT_RED, RED,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.08)
    add_text(slide, 7.1, 3.65, 2.1, 0.25, "Order / Create  ×", size=10,
             bold=True, color=RED)
    add_text(slide, 7.1, 3.92, 2.1, 0.22, "86 ms · E_DB", size=9,
             color=RED)
    add_text(slide, 9.85, 2.3, 2.5, 0.3, "① 按 trace_id 分桶", size=9.5,
             color=INK)
    add_text(slide, 9.85, 2.65, 2.5, 0.3, "② begin 建节点 / end 补耗时", size=9.5,
             color=INK)
    add_text(slide, 9.85, 3.0, 2.5, 0.3, "③ parent 组树，孤儿挂根", size=9.5,
             color=INK)
    add_text(slide, 9.85, 3.35, 2.5, 0.4, "④ 关键路径 = 根到最慢叶子", size=9.5,
             color=INK)
    add_text(slide, 9.85, 3.85, 2.5, 0.4, "日志按 trace_id + 时间窗\n挂到对应 Span",
             size=9, color=MUTED, spacing=1.15)

    steps = [
        ("分桶", "同 trace_id 的事件放一起"),
        ("建节点", "begin 到达即创建，end 到达补状态"),
        ("组树", "按 parent 指针连接，缺失时挂根并标注"),
        ("出结论", "首个错误、最慢 Span、关键路径、关联日志"),
    ]
    for i, (title, body) in enumerate(steps):
        x = LEFT + i * 3.03
        add_rect(slide, x, 5.2, 2.85, 1.15, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.08)
        add_text(slide, x + 0.16, 5.32, 2.55, 0.28, title, size=11, bold=True,
                 color=BLUE)
        add_text(slide, x + 0.16, 5.64, 2.55, 0.6, body, size=9.5, color=MUTED,
                 spacing=1.15)
    add_text(slide, LEFT, 6.55, WIDTH, 0.3,
             "关键路径的价值：它把“链路很长”压缩成“最慢的那一条”。",
             size=11.5, color=MUTED, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页解释服务端算法，但不用讲代码：分桶、建节点、组树、出结论。"
        "强调事件可以乱序到达，查询时最终一致。")


def slide_reliability(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "框架原理 ⑤：出问题时，系统怎么退化",
              "维测组件可以降级，业务不能受影响")
    data = [
        ["风险 / 故障", "机制", "结果"],
        ["Agent 离线或中心停机", "本地分段 WAL + 恢复后补传", "关键数据不丢"],
        ["业务写入过快", "无锁队列 + 背压 + 丢弃计数", "业务线程不阻塞"],
        ["日志风暴", "INFO 采样、指纹去重、噪声模板", "存储可控"],
        ["跨节点时钟偏差", "monotonic + wall-clock 双时间戳", "时间线可校正"],
        ["进程崩溃", "async-signal-safe，只写预打开 fd", "不二次崩溃"],
        ["中心内存增长", "有界 Store，超限淘汰最旧", "内存可控"],
    ]
    add_table(slide, LEFT, 1.5, WIDTH, 3.85, data,
              col_widths=[3.1, 4.4, 2.5], size=10.5, row_height=0.52)
    add_rect(slide, LEFT, 5.65, WIDTH, 0.95, SOFT_BLUE,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
    add_text(slide, LEFT + 0.3, 5.8, WIDTH - 0.6, 0.65,
             "三条硬约束：不阻塞业务线程、不把异常抛给业务、不在信号处理里做重活。\n"
             "任何一项能力关闭或降级，原有业务功能与行为保持不变。",
             size=12, bold=True, color=BLUE_DARK, spacing=1.3)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页回答“万一它自己出问题怎么办”。每种风险都有对应退化路径，"
        "核心原则是 fail-open：宁可少收集，不影响业务。")


def slide_capabilities(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "六块能力，覆盖「看得见 → 定得准 → 留得下」",
              "每一块都能单独使用，也能串成一条排障路线")
    items = [
        ("调用链与瀑布图", "跨进程父子 Span\n关键路径、首个错误",
         "从“哪个服务出错”到“哪个函数慢”", BLUE),
        ("日志治理与聚类", "错误全留、INFO 采样\n模板聚合、指纹下钻",
         "日志量下降，错误证据不丢", TEAL),
        ("指标与服务拓扑", "主机 / 进程指标\n强边与弱边分离",
         "谁调用谁，影响面一眼可见", AMBER),
        ("崩溃与性能剖析", "SIGSEGV 捕获、符号化\n折叠栈火焰图",
         "崩溃可读，热点可定位", RED),
        ("SLO 与事件中心", "可用性 / 延迟 SLI\nopen → ack → resolved",
         "目标量化，问题闭环", RGBColor(0x6D, 0x5B, 0xE7)),
        ("诊断与证据包", "一键诊断、巡检\n脱敏导出、SHA-256",
         "现场复盘有共同事实", RGBColor(0x0E, 0x74, 0x90)),
    ]
    for i, (title, body, outcome, color) in enumerate(items):
        col, row = i % 3, i // 3
        x = LEFT + col * 4.05
        y = 1.55 + row * 2.3
        add_card(slide, x, y, 3.8, 2.0, title, body, color,
                 title_size=16, body_size=12)
        add_text(slide, x + 0.26, y + 1.42, 3.35, 0.3, outcome, size=9.5,
                 bold=True, color=color)
    add_text(slide, LEFT, 6.5, WIDTH, 0.3,
             "共同前提：默认不影响生产；所有能力可灰度、可关闭。",
             size=12, color=MUTED, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "不用逐项念，指出这是能力地图，后面挑调用链、日志、事件闭环三块展开。")


def slide_trace(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "调用链：跨进程层级 + 进程内耗时，一屏看清",
              "从“哪个服务出错”下钻到“哪个函数慢”")
    # 左侧树
    add_rect(slide, LEFT, 1.45, 4.55, 4.9, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
    add_text(slide, LEFT + 0.22, 1.6, 3.0, 0.3, "Span 树", size=13, bold=True,
             color=MUTED)
    tree = [
        ("Gateway / Handle", 0, BLUE, "120 ms"),
        ("Order / Create", 1, TEAL, "86 ms"),
        ("Inventory / Check", 2, AMBER, "42 ms"),
        ("Cache / Get  ×", 3, RED, "12 ms"),
        ("DB / Query", 2, TEAL, "18 ms"),
        ("Payment / Charge", 1, BLUE, "22 ms"),
    ]
    for i, (name, depth, color, dur) in enumerate(tree):
        y = 2.02 + i * 0.68
        if i:
            add_rect(slide, LEFT + 0.45 + depth * 0.28, y - 0.14, 0.02, 0.14,
                     LINE)
        add_rect(slide, LEFT + 0.38 + depth * 0.28, y + 0.02, 0.12, 0.12,
                 color, shape=MSO_SHAPE.OVAL)
        add_text(slide, LEFT + 0.62 + depth * 0.28, y - 0.05, 2.55, 0.28,
                 name, size=11, bold=(i == 3), color=INK)
        add_text(slide, LEFT + 3.55, y - 0.05, 0.8, 0.28, dur, size=10,
                 color=MUTED, align=PP_ALIGN.RIGHT)
    # 右侧瀑布
    add_rect(slide, 5.55, 1.45, 7.0, 4.9, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
    add_text(slide, 5.78, 1.6, 3.0, 0.3, "瀑布图（相对 trace 起点）",
             size=13, bold=True, color=MUTED)
    bars = [
        ("Gateway", 0.0, 4.9, BLUE),
        ("Order", 0.5, 3.6, TEAL),
        ("Inventory", 1.1, 1.9, AMBER),
        ("Cache", 1.5, 0.55, RED),
        ("DB", 1.25, 0.9, TEAL),
        ("Payment", 0.7, 1.0, BLUE),
    ]
    bx, bw = 7.6, 4.4
    for i, (name, offset, length, color) in enumerate(bars):
        y = 2.1 + i * 0.62
        add_text(slide, 5.78, y, 1.65, 0.26, name, size=10, color=INK)
        add_rect(slide, bx + offset * 0.55, y + 0.04, length * 0.55, 0.24,
                 color, shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.35)
    add_text(slide, bx, 5.95, bw, 0.25, "时间 →", size=9, color=MUTED,
             align=PP_ALIGN.RIGHT)
    # 四个标注
    notes = [
        ("① 跨服务父子关系", "同一 trace_id 串起两个进程", BLUE),
        ("② 关键路径", "最慢的一条链路一眼可见", TEAL),
        ("③ 首个错误", "红色 Span + 错误码", RED),
        ("④ 未结束 Span", "标注「进行中」，不显示负数", AMBER),
    ]
    for i, (title, body, color) in enumerate(notes):
        x = LEFT + (i % 2) * 6.05
        y = 6.5 - (1 - i // 2) * 0.0  # 底部两行
        # 使用底部横排四张小卡
    for i, (title, body, color) in enumerate(notes):
        x = LEFT + i * 3.03
        add_rect(slide, x, 6.42, 2.85, 0.48, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.12)
        add_text(slide, x + 0.12, 6.47, 2.6, 0.2, title, size=9.5, bold=True,
                 color=color)
        add_text(slide, x + 0.12, 6.66, 2.65, 0.2, body, size=8.5, color=MUTED)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "对照左右两张图：树回答层级，瀑布回答时间。错误 Span 标红，"
        "首个错误和关键路径帮助快速收敛。")


def slide_logs(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "日志治理：默认少收，但错误一条不少",
              "先分流，再降噪，最后才存储")
    # 左：原始日志
    add_rect(slide, LEFT, 1.5, 6.0, 4.7, RGBColor(0x0F, 0x1B, 0x2D),
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.04)
    add_text(slide, LEFT + 0.25, 1.65, 3.0, 0.3, "治理前：一屏几百行",
             size=12, bold=True, color=RGBColor(0xF8, 0x7A, 0x7A))
    raw = [
        ("info", "monitor tick cpu=12% mem=31%"),
        ("info", "monitor tick cpu=12% mem=31%"),
        ("debug", "recv packet len=1024 seq=88"),
        ("info", "health check ok"),
        ("info", "order 12345 created"),
        ("warn", "retry downstream 1/3"),
        ("error", "db timeout request_id=R1"),
        ("info", "monitor tick cpu=13% mem=32%"),
        ("debug", "recv packet len=512 seq=89"),
        ("error", "db timeout request_id=R2"),
    ]
    for i, (level, msg) in enumerate(raw):
        y = 2.08 + i * 0.38
        color = { "error": RGBColor(0xF8, 0x7A, 0x7A),
                  "warn": RGBColor(0xF6, 0xC3, 0x6B),
                  "debug": RGBColor(0x7E, 0x90, 0xAA),
                  "info": RGBColor(0xA8, 0xC5, 0xE8) }[level]
        add_text(slide, LEFT + 0.25, y, 0.7, 0.25, level, size=9,
                 bold=(level == "error"), color=color)
        add_text(slide, LEFT + 0.95, y, 4.7, 0.25, msg, size=9,
                 color=RGBColor(0xC9, 0xD6, 0xE8), name="Menlo")
    add_text(slide, LEFT + 3.9, 5.72, 1.9, 0.3, "信息过载 →", size=12,
             bold=True, color=RGBColor(0xF8, 0x7A, 0x7A),
             align=PP_ALIGN.RIGHT)
    # 右：策略
    add_text(slide, 7.1, 1.5, 5.3, 0.35, "治理后：保留能定位问题的部分",
             size=13, bold=True, color=INK)
    policies = [
        ("错误 100% 保留", "error / 异常 / 失败返回\n带前序事件与栈指纹"),
        ("INFO 按比例采样", "默认少量保留\n错误与慢请求不受影响"),
        ("同指纹去重", "60 秒窗口内同模板计数\n不重复占存储"),
        ("模板聚类 + 噪声", "归一化成模板\n已知无害日志只计数"),
    ]
    for i, (title, body) in enumerate(policies):
        y = 1.95 + i * 1.05
        add_rect(slide, 7.1, y, 5.3, 0.88, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.08)
        add_rect(slide, 7.1, y + 0.12, 0.07, 0.64, [TEAL, BLUE, AMBER, RED][i])
        add_text(slide, 7.32, y + 0.12, 2.1, 0.28, title, size=12, bold=True,
                 color=INK)
        add_text(slide, 9.35, y + 0.12, 2.85, 0.62, body, size=9.5, color=MUTED)
    add_rect(slide, 7.1, 6.1, 5.3, 0.5, SOFT_TEAL,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_text(slide, 7.1, 6.22, 5.3, 0.3, "目标：日志量下降 ≥ 50%，错误证据不丢",
             size=12, bold=True, color=TEAL, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "强调不是“少收集”，而是“错误优先”。错误日志 100% 保留，"
        "低价值的监控/心跳日志转指标或采样。")


def slide_event(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "从告警到证据包：一条闭环",
              "问题不只被“发现”，还要能“确认、解决、复盘”")
    stages = [
        ("规则命中", "心跳缺失 / 错误突增\n慢 trace / 指标阈值", BLUE),
        ("事件中心", "统一级别与服务\nopen → ack → resolved", TEAL),
        ("一键诊断", "按 pid / 请求号\n只读采集与检查", AMBER),
        ("证据包", "脱敏 + SHA-256\n可带走的自包含目录", RED),
    ]
    x = LEFT
    for i, (title, body, color) in enumerate(stages):
        add_rect(slide, x, 1.55, 2.65, 1.65, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.06)
        add_text(slide, x + 0.2, 1.72, 2.25, 0.3, title, size=14, bold=True,
                 color=color)
        add_text(slide, x + 0.2, 2.12, 2.25, 0.9, body, size=10.5, color=MUTED)
        if i < len(stages) - 1:
            add_arrow(slide, x + 2.68, 2.24, 0.24, 0.24, color)
        x += 3.02
    add_text(slide, LEFT, 3.58, 5.0, 0.3, "证据包内容", size=13, bold=True,
             color=INK)
    files = [
        "manifest.json — 版本、窗口、服务、脱敏规则、文件校验和",
        "timeline.json — 告警 / 崩溃 / 重启 / 发布变更时间线",
        "request.json — trace 树、跨节点边、错误码",
        "logs.jsonl — 模板、计数、代表样本",
        "metrics.json / profiles/ / dumps/ / index.jsonl / hypotheses.md",
    ]
    add_rect(slide, LEFT, 3.95, 7.55, 2.55, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
    for i, text in enumerate(files):
        y = 4.12 + i * 0.45
        add_rect(slide, LEFT + 0.25, y + 0.09, 0.12, 0.12, BLUE,
                 shape=MSO_SHAPE.OVAL)
        add_text(slide, LEFT + 0.5, y, 6.8, 0.35, text, size=10.5, color=INK)
    add_rect(slide, 8.65, 3.58, 3.9, 2.92, NAVY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.06)
    add_text(slide, 8.9, 3.8, 3.4, 0.3, "为什么“可带走”重要", size=13,
             bold=True, color=RGBColor(0x7D, 0xB0, 0xFF))
    add_text(slide, 8.9, 4.2, 3.4, 1.9,
             "现场数据不出内网；\n开发者拿到同一份事实；\n"
             "内部 AI 可基于证据复核，\n而不是凭猜测给结论。",
             size=12, color=RGBColor(0xD3, 0xDF, 0xEE), spacing=1.35)
    add_text(slide, LEFT, 6.65, WIDTH, 0.3,
             "边界：证据包与 MCP 属于内部能力，对外发行版关闭。",
             size=11, color=MUTED)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页讲闭环：告警只是开始，事件状态要能推进，诊断要能执行，"
        "最后证据包让复盘和 AI 分析有共同事实基础。")


def slide_innovation(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "六个设计选择，让它适合内网 C++ 现场",
              "不是功能堆砌，而是围绕“敢在生产上用”做取舍")
    items = [
        ("01", "错误优先的日志治理",
         "错误全留、低价值降噪；\n先保证证据，再谈成本。"),
        ("02", "强边 / 弱边分离",
         "显式传播进 Span 树；\n推断边只进拓扑，不误导根因。"),
        ("03", "证据包可带走",
         "脱敏 + 校验和 + 自包含；\n现场数据不出内网。"),
        ("04", "L0–L3 分级接入",
         "存量程序 0 行代码起步；\n按价值逐步加深。"),
        ("05", "对生产零影响",
         "fail-open、热路径无锁、\n崩溃处理 async-signal-safe。"),
        ("06", "全链路可验证",
         "132 个测试、\n17 类端到端场景。"),
    ]
    for i, (num, title, body) in enumerate(items):
        x = LEFT + (i % 3) * 4.05
        y = 1.6 + (i // 3) * 2.15
        w = 3.8
        add_rect(slide, x, y, w, 1.9, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.06)
        add_text(slide, x + 0.22, y + 0.16, 0.6, 0.3, num, size=13,
                 bold=True, color=BLUE)
        add_text(slide, x + 0.72, y + 0.16, w - 0.9, 0.35, title, size=14,
                 bold=True, color=INK)
        add_text(slide, x + 0.22, y + 0.72, w - 0.44, 0.9, body, size=11,
                 color=MUTED, spacing=1.25)
    add_rect(slide, LEFT + 4.05, 5.95, 3.8, 0.62, SOFT_BLUE,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.1)
    add_text(slide, LEFT + 4.2, 6.08, 3.5, 0.4,
             "共性：任何一项出问题，都不能影响原有业务功能。",
             size=10, bold=True, color=BLUE_DARK, spacing=1.2)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "创新点不要吹成“行业首创”，而是“针对离线 C++ 现场的设计选择”。"
        "重点讲错误优先、强弱边分离、证据包和零影响。")


def slide_scenarios(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "四个高频场景，直接照着做",
              "每个场景都有 demo 命令：先看输入，再看观察面，最后拿产出")
    items = [
        ("跨进程超时", "输入：request_id / trace_id",
         "看到：哪一跳耗时最长、对端状态",
         "产出：超时发生在服务 / 节点 / 方法", BLUE),
        ("发布后错误突增", "输入：服务名 + 时间窗",
         "看到：错误模板、首个错误、拓扑边",
         "产出：是否由本次变更引入", AMBER),
        ("进程崩溃", "输入：崩溃列表 / 服务",
         "看到：信号、栈帧、build_id、最近日志",
         "产出：崩溃指纹与可疑版本", RED),
        ("性能退化 / 内存增长", "输入：服务 + 指标 / 剖析",
         "看到：火焰图、RSS 趋势、慢 Span",
         "产出：热点函数与泄漏候选", TEAL),
    ]
    for i, (title, inp, see, out, color) in enumerate(items):
        col, row = i % 2, i // 2
        x = LEFT + col * 6.05
        y = 1.55 + row * 2.4
        add_rect(slide, x, y, 5.75, 2.1, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.06)
        add_rect(slide, x, y + 0.16, 0.07, 1.78, color)
        add_text(slide, x + 0.25, y + 0.15, 5.2, 0.3, title, size=15,
                 bold=True, color=INK)
        add_text(slide, x + 0.25, y + 0.6, 5.2, 0.3, inp, size=10,
                 color=MUTED)
        add_text(slide, x + 0.25, y + 0.95, 5.2, 0.3, see, size=10,
                 color=INK)
        add_text(slide, x + 0.25, y + 1.45, 5.2, 0.4, out, size=11,
                 bold=True, color=color)
    add_text(slide, LEFT, 6.5, WIDTH, 0.3,
             "./examples/scenarios/demo.py --scenario rpc|slow|error|logstorm|slo|crash --keep",
             size=10.5, bold=True, color=BLUE_DARK, align=PP_ALIGN.CENTER,
             name="Menlo")
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "用四个真实场景让听众代入：不是讲功能列表，而是讲“遇到这事怎么用”。")


def slide_quick_diagnosis(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "快速诊断三步法：收口 → 定位 → 证据",
              "把“先翻所有日志”改成“先锁定一条请求”")
    steps = [
        ("①", "收口", "request_id / trace_id / task_id\n或服务 + 时间窗",
         "不用先登录每台机器"),
        ("②", "定位", "首个错误、最慢 Span、关键路径\n拓扑边确认影响面",
         "先看结论，再看细节"),
        ("③", "证据", "关联日志、模板指纹\n诊断结论、脱敏证据包",
         "复盘有共同事实"),
    ]
    for i, (num, title, body, note) in enumerate(steps):
        y = 1.5 + i * 1.55
        add_rect(slide, LEFT, y, 0.62, 0.62, BLUE,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.25)
        add_text(slide, LEFT, y + 0.16, 0.62, 0.3, num, size=16, bold=True,
                 color=WHITE, align=PP_ALIGN.CENTER)
        add_text(slide, LEFT + 0.8, y - 0.02, 1.8, 0.35, title, size=18,
                 bold=True, color=INK)
        add_text(slide, LEFT + 0.8, y + 0.38, 4.6, 0.65, body, size=11.5,
                 color=MUTED, spacing=1.2)
        add_text(slide, LEFT + 0.8, y + 0.98, 4.6, 0.25, note, size=9.5,
                 bold=True, color=BLUE)

    add_rect(slide, 6.55, 1.5, 5.85, 4.65, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
    add_text(slide, 6.8, 1.65, 5.3, 0.3, "同一个故障，两条路径", size=12,
             bold=True, color=MUTED)
    add_text(slide, 6.8, 2.05, 2.55, 0.3, "传统", size=12, bold=True,
             color=RED)
    add_text(slide, 9.5, 2.05, 2.6, 0.3, "vmon", size=12, bold=True,
             color=TEAL)
    old_path = ["登录多台机器", "grep 多个日志文件", "人工对齐时间戳",
                "拼出跨进程链路", "凭经验猜根因"]
    new_path = ["输入 request_id", "看首个错误 / 最慢 Span",
                "看关键路径与拓扑", "下钻关联日志", "导出证据包"]
    for i, text in enumerate(old_path):
        y = 2.45 + i * 0.52
        add_text(slide, 6.8, y, 2.5, 0.28, text, size=10, color=MUTED)
        add_text(slide, 6.8, y + 0.23, 2.5, 0.2, "~", size=9, color=LINE)
    for i, text in enumerate(new_path):
        y = 2.45 + i * 0.52
        add_text(slide, 9.5, y, 2.6, 0.28, text, size=10, color=INK)
    add_rect(slide, 6.8, 5.15, 5.3, 0.75, SOFT_TEAL,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.1)
    add_text(slide, 6.95, 5.27, 5.0, 0.55,
             "30–120 分钟、依赖个人经验\n→ 1–5 分钟、证据可复核",
             size=10.5, bold=True, color=TEAL, spacing=1.2)
    add_text(slide, LEFT, 6.35, WIDTH, 0.55,
             "优势来源：错误优先的日志治理 · 强/弱边分离 · 一条请求串起链路与指标 · "
             "证据包可带走",
             size=11, bold=True, color=BLUE_DARK, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "这一页是给不了解系统的人看的：同一件事，传统路径和 vmon 路径的步骤与耗时对比。"
        "强调不是功能多，而是路径短、证据可复核。")


def slide_clients(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "客户端接入：Python / Web 也是链路起点",
              "已支持：传播优先，后端仍是业务真相源")
    # 左侧拓扑
    add_rect(slide, 0.6, 1.5, 12.35, 3.45, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.04)
    boxes = [
        (1.0, 1.7, 2.2, 0.7, "浏览器", SOFT_BLUE, BLUE),
        (3.85, 1.7, 2.2, 0.7, "BFF / 网关", SOFT_GRAY, MUTED),
        (3.85, 3.15, 2.2, 0.7, "Python 服务", SOFT_TEAL, TEAL),
        (6.65, 1.8, 2.35, 1.95, "C++ 后端（SDK）", SOFT_AMBER, AMBER),
        (9.5, 2.35, 2.6, 0.7, "vmon-server", RGBColor(0xEF, 0xEC, 0xFD),
         RGBColor(0x6D, 0x5B, 0xE7)),
    ]
    for x, y, w, h, text, fill, color in boxes:
        add_rect(slide, x, y, w, h, fill, color,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.1)
        add_text(slide, x, y + 0.18, w, 0.3, text, size=10.5, bold=True,
                 color=INK, align=PP_ALIGN.CENTER)
    add_arrow(slide, 3.25, 1.95, 0.4, 0.2, BLUE)
    add_arrow(slide, 6.1, 1.95, 0.4, 0.2, BLUE)
    add_arrow(slide, 6.1, 3.4, 0.4, 0.2, TEAL)
    add_arrow(slide, 9.05, 2.6, 0.4, 0.2, BLUE)
    add_text(slide, 2.0, 2.5, 3.5, 0.25, "traceparent / x-request-id", size=8.5,
             color=MUTED)
    add_text(slide, 4.9, 2.9, 2.4, 0.25, "同一条 trace", size=8.5, color=MUTED)
    add_text(slide, 7.0, 3.9, 2.5, 0.25, "Agent → Server（SDK 上报）", size=8.5,
             color=MUTED)
    add_text(slide, 7.0, 2.4, 2.3, 0.4, "续接父上下文\n创建 server span", size=8.5,
             color=MUTED, align=PP_ALIGN.CENTER, spacing=1.1)
    add_text(slide, 9.3, 3.2, 3.5, 0.25,
             "Python 事件可直报 /v1/ingest", size=8.5, color=MUTED)
    add_text(slide, 1.05, 4.5, 11.5, 0.3,
             "浏览器不直连中心写接口：由 BFF 统一鉴权、注入上下文、采样上报。",
             size=9.5, color=MUTED)

    # 右侧代码与要点
    add_text(slide, 0.6, 5.15, 5.0, 0.3, "Python：一个上下文管理器", size=11,
             bold=True, color=INK)
    add_code(slide, 0.6, 5.45, 5.9, 1.35,
             "with client.span(\"GET /orders\", request_id=\"REQ-1\"):\n"
             "    client.request(\"GET\", backend + \"/api/orders\")")
    add_text(slide, 6.85, 5.15, 6.1, 0.3, "C++ 后端：从头恢复父上下文", size=11,
             bold=True, color=INK)
    add_code(slide, 6.85, 5.45, 6.1, 1.35,
             "vmon::SpanContext parent;\n"
             "if (vmon::context_from_traceparent(h, &parent))\n"
             "  auto span = vmon::create_span_with_parent(\n"
             "      parent, \"Handle\", __FILE__, __LINE__);")
    add_pill(slide, 10.7, 1.12, 2.1, "已支持 · E2E 覆盖", color=TEAL,
             fill=SOFT_TEAL, size=10)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "上游不是盲区：Python/浏览器生成 trace 起点，后端用同一个 traceparent 续接。"
        "浏览器只上报给 BFF，不直接持有中心写 token。")


def slide_adoption(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "接入很轻：从 0 行代码到关键路径",
              "改公共库，而不是逐个改 200 个程序")
    levels = [
        ("L0", "零改造", "部署 Agent、保留 debuginfo", "指标 / 日志 / 崩溃 / 粗粒度拓扑", TEAL),
        ("L1", "轻改造", "CMake 一行 + 初始化 + 日志宏", "精确跨进程 trace、请求级日志", BLUE),
        ("L2", "标准改造", "关键函数 VMON_SPAN", "进程内时序、关键路径", AMBER),
    ]
    for i, (level, name, work, gain, color) in enumerate(levels):
        x = LEFT + i * 4.05
        add_rect(slide, x, 1.55, 3.8, 2.6, WHITE, LINE,
                 MSO_SHAPE.ROUNDED_RECTANGLE, 0.06)
        add_pill(slide, x + 0.22, 1.72, 0.75, level, color=WHITE,
                 fill=color, size=12)
        add_text(slide, x + 1.1, 1.72, 2.4, 0.3, name, size=14, bold=True,
                 color=INK)
        add_text(slide, x + 0.22, 2.2, 3.35, 0.3, "改什么", size=10, bold=True,
                 color=MUTED)
        add_text(slide, x + 0.22, 2.48, 3.35, 0.5, work, size=10.5, color=INK)
        add_text(slide, x + 0.22, 3.08, 3.35, 0.3, "得到什么", size=10,
                 bold=True, color=MUTED)
        add_text(slide, x + 0.22, 3.36, 3.35, 0.55, gain, size=10.5, color=INK)
    add_text(slide, LEFT, 4.42, 5.4, 0.3, "10 分钟跑起来", size=13,
             bold=True, color=INK)
    add_code(slide, LEFT, 4.8, 6.2, 1.75,
             "$ ./scripts/vmon.sh up\n"
             "Web UI: http://127.0.0.1:18428/\n"
             "Token:  dev-token\n\n"
             "$ ./scripts/vmon.sh status\n"
             "$ ./scripts/vmon.sh down")
    add_text(slide, 7.35, 4.42, 5.2, 0.3, "业务侧接入（L1）", size=13,
             bold=True, color=INK)
    add_code(slide, 7.35, 4.8, 5.2, 1.75,
             "// CMake\nvmon_attach(your_target)\n\n"
             "// C++\nvmon::init(cfg);\nVMON_SPAN(\"Handle\");\n"
             "VMON_INFO(\"request_id=%s\", id);")
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "降低接入焦虑：先用脚本 10 分钟跑起来；存量程序 L0 起步，"
        "公共库做一次 L1 就能覆盖大部分进程。")


def slide_compare(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "和成熟平台比：不拼大而全，拼最短路径",
              "定性比较，重点看私有离线、证据闭环与部署成本")
    data = [
        ["能力 / 产品", "私有离线", "跨进程 Trace", "日志降噪", "崩溃符号化", "证据包", "部署复杂度"],
        ["观测云", "●", "●", "●", "●", "●", "中"],
        ["Datadog", "○", "●", "●", "●", "◐", "低（SaaS）"],
        ["Dynatrace", "○", "●", "●", "●", "◐", "低（SaaS）"],
        ["Grafana LGTM", "●", "◐", "◐", "○", "○", "高"],
        ["SigNoz", "●", "●", "◐", "○", "○", "中"],
        ["vmon（本项目）", "●", "●", "●", "●", "●", "低"],
    ]
    add_table(slide, LEFT, 1.55, WIDTH, 3.9, data,
              col_widths=[2.2, 1.4, 1.65, 1.4, 1.55, 1.3, 1.7], size=10.5,
              row_height=0.48)
    add_text(slide, LEFT, 5.55, WIDTH, 0.3,
             "● 原生支持    ◐ 部分支持 / 需组合    ○ 不支持或需自建",
             size=10, color=MUTED)
    add_rect(slide, LEFT, 5.95, WIDTH, 0.85, SOFT_BLUE,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.08)
    add_text(slide, LEFT + 0.3, 6.08, WIDTH - 0.6, 0.6,
             "差异不在“采集器数量”，而在：离线私有部署、错误优先的日志治理、"
             "可复核证据包，以及不改业务代码就能起步。",
             size=12.5, bold=True, color=BLUE_DARK)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "比较表是定性判断，不要贬低竞品。说明大平台能力强，但本项目在"
        "离线、C++、证据闭环和部署成本上更贴合目标场景。")


def slide_roadmap(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, BG)
    add_title(slide, "现在能做什么，接下来做什么",
              "每一步都可单独验收，不一次性铺开")
    add_rect(slide, LEFT, 1.55, 5.95, 4.75, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
    add_text(slide, LEFT + 0.3, 1.72, 4.0, 0.35, "已具备（本机验收）", size=14,
             bold=True, color=TEAL)
    done = [
        "SDK / Agent / Server / Web / CLI / MCP 全链路",
        "跨进程 + 进程内 Span、瀑布图、关键路径",
        "日志采样 / 去重 / 聚类 / 动态级别",
        "主机与进程指标、服务拓扑",
        "崩溃捕获、离线符号化、指纹聚合",
        "SLO、事件闭环、规则表单",
        "一键诊断 / 巡检、脱敏证据包",
        "Python / Web 客户端 trace 接入",
        "132 个测试 + 17 类端到端场景",
    ]
    add_bullets(slide, LEFT + 0.3, 2.18, 5.35, 3.3, done, size=11.5,
                gap=0.14)
    add_text(slide, LEFT + 0.3, 5.62, 5.3, 0.3,
             "验收：ctest 全绿 + Web UI 14 个视图回归", size=10,
             bold=True, color=TEAL)
    add_rect(slide, 6.95, 1.55, 5.6, 4.75, WHITE, LINE,
             MSO_SHAPE.ROUNDED_RECTANGLE, 0.05)
    add_text(slide, 7.25, 1.72, 4.0, 0.35, "下一步（按需推进）", size=14,
             bold=True, color=AMBER)
    todo = [
        "通知渠道：Webhook / 邮件 / syslog",
        "gRPC streaming / 异步拦截与 eBPF 弱边",
        "常驻 CPU / 内存 / 锁剖析",
        "BFF 聚合前端事件 / OTLP 接入",
        "长期存储（VictoriaMetrics / Parquet）",
        "Linux aarch64 / gcc 7.3 实机验证",
        "72 小时长稳与混沌测试",
    ]
    add_bullets(slide, 7.25, 2.18, 5.0, 3.3, todo, size=11.5, gap=0.16)
    add_text(slide, 7.25, 5.62, 5.0, 0.3,
             "节奏：按现场需求推进，每项独立开关、可灰度", size=10,
             bold=True, color=AMBER)
    add_rect(slide, LEFT, 6.45, WIDTH, 0.45, NAVY,
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.12)
    add_text(slide, LEFT, 6.55, WIDTH, 0.3,
             "原则：先框架后能力；新增能力默认不影响生产，可灰度、可关闭。",
             size=11.5, bold=True, color=WHITE, align=PP_ALIGN.CENTER)
    add_footer(slide, n)
    slide.notes_slide.notes_text_frame.text = (
        "诚实说明边界：已具备的可以直接演示；下一步按现场需求推进，"
        "不承诺一次性做完所有平台能力。")


def slide_end(prs, n):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_bg(slide, NAVY)
    add_text(slide, LEFT, 0.9, 8.0, 0.35, "总结", size=13, bold=True,
             color=RGBColor(0x7D, 0xB0, 0xFF))
    add_text(slide, LEFT, 1.35, 11.5, 0.9, "记住三件事", size=40, bold=True,
             color=WHITE)
    items = [
        ("1", "一条请求，串起链路、日志、指标",
         "输入 request_id / trace_id，得到同一时间线"),
        ("2", "错误优先 + 证据包，排障可复核",
         "错误全留、模板聚类、脱敏导出、SHA-256"),
        ("3", "离线内网，对生产零影响",
         "fail-open、本地 WAL、有界存储、可灰度"),
    ]
    for i, (num, title, body) in enumerate(items):
        y = 2.55 + i * 1.22
        add_rect(slide, LEFT, y, 0.5, 0.5, BLUE,
                 shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.25)
        add_text(slide, LEFT, y + 0.11, 0.5, 0.28, num, size=13, bold=True,
                 color=WHITE, align=PP_ALIGN.CENTER)
        add_text(slide, LEFT + 0.75, y, 7.5, 0.35, title, size=18, bold=True,
                 color=WHITE)
        add_text(slide, LEFT + 0.75, y + 0.45, 7.5, 0.3, body, size=12,
                 color=RGBColor(0xB9, 0xC6, 0xD8))
    add_rect(slide, 9.0, 2.4, 3.55, 3.6, RGBColor(0x12, 0x2A, 0x52),
             shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.06)
    add_text(slide, 9.25, 2.62, 3.05, 0.3, "现在就可以试", size=13,
             bold=True, color=RGBColor(0x7D, 0xB0, 0xFF))
    add_text(slide, 9.25, 3.05, 3.05, 0.3, "./scripts/vmon.sh up",
             size=12, bold=True, color=WHITE, name="Menlo")
    add_text(slide, 9.25, 3.45, 3.05, 0.3, "http://127.0.0.1:18428/",
             size=11, color=RGBColor(0xD3, 0xDF, 0xEE), name="Menlo")
    add_text(slide, 9.25, 4.0, 3.05, 1.7,
             "使用手册：docs/user-guide.md\n"
             "快速诊断：docs/quick-diagnosis.md\n"
             "场景手册：docs/scenario-cookbook.md",
             size=10, color=RGBColor(0xB9, 0xC6, 0xD8), spacing=1.5)
    add_footer(slide, n, dark=True)
    slide.notes_slide.notes_text_frame.text = (
        "收尾只留三句话，方便听众记住。最后给出启动命令和文档位置。")


# ---------------------------------------------------------------- 验收


def check_deck(path):
    """粗检：越界、文字过长、空页。"""
    prs = Presentation(path)
    problems = []
    slide_w = prs.slide_width / 914400.0
    slide_h = prs.slide_height / 914400.0
    for index, slide in enumerate(prs.slides, start=1):
        if not slide.shapes:
            problems.append("第 %d 页没有内容" % index)
        for shape in slide.shapes:
            left = shape.left / 914400.0
            top = shape.top / 914400.0
            width = shape.width / 914400.0
            height = shape.height / 914400.0
            if left < -0.6 or top < -0.6 or left + width > slide_w + 0.6 \
                    or top + height > slide_h + 0.6:
                problems.append(
                    "第 %d 页形状越界: %s (%.2f, %.2f, %.2f, %.2f)" % (
                        index, shape.shape_type, left, top, width, height))
            if not shape.has_text_frame:
                continue
            text = shape.text_frame.text.strip()
            if not text:
                continue
            # 估算：中文按 1em，英文按 0.55em；行高 1.35 倍。
            font_pt = 16
            for para in shape.text_frame.paragraphs:
                for run in para.runs:
                    if run.font.size:
                        font_pt = run.font.size.pt
                        break
                if font_pt:
                    break
            usable_pt = max(1.0, width * 72.0 - 4)
            per_line = max(1.0, usable_pt / max(8.0, font_pt))
            lines = 0
            for line in text.split("\n"):
                # 中文/全角按 1 个字符，ASCII 按 0.55
                units = sum(0.55 if ord(ch) < 128 else 1.0 for ch in line)
                lines += max(1, math.ceil(units / per_line))
            needed_in = lines * font_pt * 1.35 / 72.0
            if height > 0 and needed_in > height * 1.75 + 0.12:
                problems.append(
                    "第 %d 页可能溢出: '%s...' 需要 %.2fin，只有 %.2fin" % (
                        index, text[:24].replace("\n", " "), needed_in, height))
    print("页数：%d" % len(prs.slides))
    if problems:
        print("检查发现 %d 个疑似问题：" % len(problems))
        for item in problems:
            print("  - " + item)
    else:
        print("结构检查通过：无越界、无明显文字溢出。")
    return problems


def main():
    prs = Presentation()
    prs.slide_width = Inches(SW)
    prs.slide_height = Inches(SH)

    slide_cover(prs)
    slide_summary(prs, 2)
    slide_pain(prs, 3)
    slide_goal(prs, 4)
    slide_architecture(prs, 5)
    slide_modules(prs, 6)
    slide_scheduling(prs, 7)
    slide_model(prs, 8)
    slide_principle(prs, 9)
    slide_internals(prs, 10)
    slide_trace_assembly(prs, 11)
    slide_reliability(prs, 12)
    slide_capabilities(prs, 13)
    slide_trace(prs, 14)
    slide_logs(prs, 15)
    slide_event(prs, 16)
    slide_innovation(prs, 17)
    slide_scenarios(prs, 18)
    slide_quick_diagnosis(prs, 19)
    slide_clients(prs, 20)
    slide_adoption(prs, 21)
    slide_compare(prs, 22)
    slide_roadmap(prs, 23)
    slide_end(prs, 24)

    out_dir = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(out_dir, "vmon-intro.pptx")
    prs.save(out)
    print("已生成：%s" % out)
    return check_deck(out)


if __name__ == "__main__":
    sys.exit(0 if not main() else 1)
