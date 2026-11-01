#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 PPTX 近似渲染成 PNG，便于在没有 PowerPoint/LibreOffice 中文字体的环境里预览。

用法：
    .venv/bin/python docs/presentation/render_preview.py \
        docs/presentation/vmon-intro.pptx -o /tmp/vmon-preview

说明：
- 只支持本项目用到的形状：矩形/圆角矩形/椭圆/箭头/文本框/表格；
- 字体使用系统已安装的 Noto Sans SC / PingFang SC / Menlo；
- 这是“够用”的预览，不是像素级还原，最终以 PowerPoint/WPS 打开为准。
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont
from pptx import Presentation
from pptx.enum.dml import MSO_FILL
from pptx.enum.shapes import MSO_SHAPE, MSO_SHAPE_TYPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN

EMU_PER_INCH = 914400.0
DPI = 150

FONT_CANDIDATES = [
    "/Users/tshua/Library/Fonts/NotoSansSC-Regular.ttf",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]
BOLD_CANDIDATES = [
    "/Users/tshua/Library/Fonts/NotoSansSC-Bold.ttf",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]
MONO_CANDIDATES = [
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
]


def first_existing(paths):
    for path in paths:
        if os.path.exists(path):
            return path
    return None


class FontCache:
    def __init__(self, dpi):
        self.dpi = dpi
        self.cache = {}
        self.regular = first_existing(FONT_CANDIDATES)
        self.bold = first_existing(BOLD_CANDIDATES)
        self.mono = first_existing(MONO_CANDIDATES)

    def get(self, size_pt, bold=False, mono=False):
        key = (round(size_pt, 1), bold, mono)
        if key in self.cache:
            return self.cache[key]
        size_px = max(6, int(round(size_pt * self.dpi / 72.0)))
        path = self.regular
        if mono and self.mono:
            path = self.mono
        elif bold and self.bold:
            path = self.bold
        font = ImageFont.truetype(path, size_px)
        self.cache[key] = font
        return font


def px(value):
    return int(round(float(value) / EMU_PER_INCH * DPI))


def rgb(color):
    try:
        value = color.rgb
        return (value[0], value[1], value[2])
    except Exception:
        return None


def shape_fill(shape):
    try:
        if shape.fill.type == MSO_FILL.SOLID:
            return rgb(shape.fill.fore_color)
    except Exception:
        pass
    return None


def shape_line(shape):
    try:
        if shape.line.fill.type == MSO_FILL.SOLID:
            color = rgb(shape.line.color)
            width = int(shape.line.width.pt * 2) if shape.line.width else 2
            return color, max(1, width)
    except Exception:
        pass
    return None, 0


def wrap_text(text, font, max_width):
    lines = []
    for raw in text.split("\n"):
        if not raw:
            lines.append("")
            continue
        current = ""
        for ch in raw:
            candidate = current + ch
            if font.getlength(candidate) <= max_width or not current:
                current = candidate
            else:
                lines.append(current)
                current = ch
        lines.append(current)
    return lines


def draw_text_frame(draw, shape, fonts):
    tf = shape.text_frame
    x = px(shape.left) + px(tf.margin_left)
    y = px(shape.top) + px(tf.margin_top)
    width = max(1, px(shape.width) - px(tf.margin_left) - px(tf.margin_right))
    height = max(1, px(shape.height) - px(tf.margin_top) - px(tf.margin_bottom))
    paragraphs = list(tf.paragraphs)
    if not any(p.runs for p in paragraphs):
        return
    rendered = []
    total_h = 0
    for para in paragraphs:
        runs = list(para.runs)
        if not runs:
            rendered.append(([], para, 0))
            total_h += int(12 * 1.2)
            continue
        run = runs[0]
        size = run.font.size.pt if run.font.size else 18
        bold = bool(run.font.bold)
        color = rgb(run.font.color) or (20, 33, 61)
        mono = (run.font.name or "").lower() in ("menlo", "consolas", "courier new")
        font = fonts.get(size, bold=bold, mono=mono)
        spacing = para.line_spacing
        if isinstance(spacing, float) and spacing > 0:
            line_h = int(size * fonts.dpi / 72.0 * 1.2 * spacing)
        elif spacing is not None and hasattr(spacing, "pt"):
            line_h = int(spacing.pt * fonts.dpi / 72.0)
        else:
            line_h = int(size * fonts.dpi / 72.0 * 1.25)
        text = "".join(r.text for r in runs)
        lines = wrap_text(text, font, width)
        rendered.append((lines, para, line_h))
        total_h += line_h * len(lines)
    anchor = tf.vertical_anchor
    offset = 0
    if anchor == MSO_ANCHOR.MIDDLE:
        offset = max(0, (height - total_h) // 2)
    elif anchor == MSO_ANCHOR.BOTTOM:
        offset = max(0, height - total_h)
    cursor = y + offset
    for lines, para, line_h in rendered:
        run = para.runs[0] if para.runs else None
        if run is None:
            cursor += line_h
            continue
        size = run.font.size.pt if run.font.size else 18
        bold = bool(run.font.bold)
        color = rgb(run.font.color) or (20, 33, 61)
        mono = (run.font.name or "").lower() in ("menlo", "consolas", "courier new")
        font = fonts.get(size, bold=bold, mono=mono)
        align = para.alignment or PP_ALIGN.LEFT
        for line in lines:
            text_w = font.getlength(line)
            tx = x
            if align == PP_ALIGN.CENTER:
                tx = x + (width - text_w) / 2
            elif align == PP_ALIGN.RIGHT:
                tx = x + width - text_w
            draw.text((tx, cursor), line, font=font, fill=color)
            cursor += line_h


def draw_shape(draw, shape, fonts):
    x, y = px(shape.left), px(shape.top)
    w, h = px(shape.width), px(shape.height)
    fill = shape_fill(shape)
    line, line_w = shape_line(shape)
    kind = None
    try:
        kind = shape.auto_shape_type
    except Exception:
        pass
    if shape.shape_type == MSO_SHAPE_TYPE.AUTO_SHAPE:
        if kind == MSO_SHAPE.OVAL:
            draw.ellipse([x, y, x + w, y + h], fill=fill,
                         outline=line, width=line_w)
        elif kind in (MSO_SHAPE.RIGHT_ARROW, MSO_SHAPE.DOWN_ARROW):
            if fill:
                if kind == MSO_SHAPE.RIGHT_ARROW:
                    head = min(w * 0.45, h * 1.2)
                    points = [(x, y + h * 0.25), (x + w - head, y + h * 0.25),
                              (x + w - head, y), (x + w, y + h / 2),
                              (x + w - head, y + h), (x + w - head, y + h * 0.75),
                              (x, y + h * 0.75)]
                else:
                    head = min(h * 0.45, w * 1.2)
                    points = [(x + w * 0.25, y), (x + w * 0.25, y + h - head),
                              (x, y + h - head), (x + w / 2, y + h),
                              (x + w, y + h - head), (x + w * 0.75, y + h - head),
                              (x + w * 0.75, y)]
                draw.polygon(points, fill=fill)
        elif kind == MSO_SHAPE.RECTANGLE:
            draw.rectangle([x, y, x + w, y + h], fill=fill, outline=line,
                           width=line_w)
        else:
            radius = int(min(w, h) * 0.12)
            draw.rounded_rectangle([x, y, x + w, y + h], radius=radius,
                                   fill=fill, outline=line, width=line_w)
    elif shape.shape_type == MSO_SHAPE_TYPE.TABLE:
        draw_table(draw, shape, fonts, x, y, w, h)
    if shape.has_text_frame:
        draw_text_frame(draw, shape, fonts)


def draw_table(draw, shape, fonts, x, y, w, h):
    table = shape.table
    rows = len(table.rows)
    cols = len(table.columns)
    widths = [px(col.width) for col in table.columns]
    scale = w / max(1, sum(widths))
    widths = [int(v * scale) for v in widths]
    heights = [px(row.height) for row in table.rows]
    scale_h = h / max(1, sum(heights))
    heights = [int(v * scale_h) for v in heights]
    cy = y
    for r in range(rows):
        cx = x
        for c in range(cols):
            cell = table.cell(r, c)
            fill = None
            try:
                if cell.fill.type == MSO_FILL.SOLID:
                    fill = rgb(cell.fill.fore_color)
            except Exception:
                pass
            draw.rectangle([cx, cy, cx + widths[c], cy + heights[r]],
                           fill=fill, outline=(216, 224, 236), width=1)
            tf = cell.text_frame
            text = tf.text
            if text:
                run = tf.paragraphs[0].runs[0] if tf.paragraphs[0].runs else None
                size = run.font.size.pt if run and run.font.size else 11
                bold = bool(run.font.bold) if run else False
                color = (rgb(run.font.color) if run else None) or (20, 33, 61)
                font = fonts.get(size, bold=bold)
                tw = font.getlength(text)
                tx = cx + (widths[c] - tw) / 2
                ty = cy + (heights[r] - size * fonts.dpi / 72.0) / 2
                draw.text((tx, ty), text, font=font, fill=color)
            cx += widths[c]
        cy += heights[r]


def render(pptx_path, out_dir, dpi=150):
    global DPI
    DPI = dpi
    prs = Presentation(pptx_path)
    fonts = FontCache(dpi)
    os.makedirs(out_dir, exist_ok=True)
    width = int(prs.slide_width / EMU_PER_INCH * dpi)
    height = int(prs.slide_height / EMU_PER_INCH * dpi)
    files = []
    for index, slide in enumerate(prs.slides, start=1):
        background = (255, 255, 255)
        try:
            if slide.background.fill.type == MSO_FILL.SOLID:
                background = rgb(slide.background.fill.fore_color) or background
        except Exception:
            pass
        image = Image.new("RGB", (width, height), background)
        draw = ImageDraw.Draw(image)
        for shape in slide.shapes:
            draw_shape(draw, shape, fonts)
        path = os.path.join(out_dir, "slide-%02d.png" % index)
        image.save(path)
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description="PPTX 近似预览渲染")
    parser.add_argument("pptx")
    parser.add_argument("-o", "--out", default="/tmp/vmon-preview")
    parser.add_argument("--dpi", type=int, default=150)
    args = parser.parse_args()
    files = render(args.pptx, args.out, args.dpi)
    print("已生成 %d 张预览：%s" % (len(files), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
