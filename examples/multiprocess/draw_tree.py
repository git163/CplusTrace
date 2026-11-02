#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 /api/traces/{id} 的 Span 树渲染成 SVG，并打印 ASCII 树。

只依赖标准库；输入是 trace_detail_json 的 JSON 对象。
"""

import html


def _ms(ns):
    return "%.1f ms" % (float(ns or 0) / 1e6)


def walk(nodes, depth=0, parent=None, rows=None):
    rows = rows if rows is not None else []
    for node in nodes or []:
        rows.append({"node": node, "depth": depth, "parent": parent})
        walk(node.get("children") or [], depth + 1, len(rows) - 1, rows)
    return rows


def print_tree(detail):
    def visit(node, prefix, is_last, is_root):
        connector = "" if is_root else ("└─ " if is_last else "├─ ")
        status = node.get("error_code") or node.get("status") or "ok"
        print("%s%s%s / %s  %s  tid=%s  %s" % (
            prefix, connector, node.get("service", "?"),
            node.get("name", "?"), _ms(node.get("duration_ns")),
            node.get("tid", 0), status))
        children = node.get("children") or []
        next_prefix = prefix if is_root else prefix + (
            "   " if is_last else "│  ")
        for index, child in enumerate(children):
            visit(child, next_prefix, index == len(children) - 1, False)

    roots = detail.get("roots") or []
    for index, root in enumerate(roots):
        visit(root, "", index == len(roots) - 1, True)


def _color(service, error):
    if error:
        return "#ef4444", "#fdecec"
    palette = [
        ("#2563eb", "#eaf2ff"),
        ("#0f9d8f", "#e6f7f4"),
        ("#d97706", "#fff4e0"),
        ("#6d5be7", "#efecfd"),
        ("#0e7490", "#e0f2fe"),
    ]
    index = sum(ord(c) for c in str(service)) % len(palette)
    return palette[index]


def render_svg(detail, path):
    rows = walk(detail.get("roots") or [])
    box_w, box_h, gap_y = 470, 54, 22
    left, top = 40, 92
    x_step = 76
    row_h = box_h + gap_y
    width = 1180
    height = top + len(rows) * row_h + 40

    positions = []
    for i, row in enumerate(rows):
        x = left + row["depth"] * x_step
        y = top + i * row_h
        positions.append((x, y))

    def esc(value):
        return html.escape(str(value or ""))

    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">' % (width, height, width, height),
        '<rect width="100%" height="100%" fill="#f7f9fc"/>',
        '<text x="40" y="38" font-family="PingFang SC, Microsoft YaHei, sans-serif" '
        'font-size="20" font-weight="600" fill="#14213d">Span 树 · %s</text>' % esc(
            detail.get("request_id") or detail.get("trace_id")),
        '<text x="40" y="62" font-family="Menlo, monospace" font-size="11" '
        'fill="#5b6b82">trace_id=%s  span=%s  services=%s</text>' % (
            esc(detail.get("trace_id")), esc(
                (detail.get("summary") or {}).get("span_count")),
            esc((detail.get("summary") or {}).get("service_count"))),
    ]

    # 先画连线，再画盒子，避免线压在文字上。
    for i, row in enumerate(rows):
        parent = row["parent"]
        if parent is None:
            continue
        px, py = positions[parent]
        x, y = positions[i]
        p_mid = px + box_w / 2
        c_mid = x + box_w / 2
        parts.append(
            '<path d="M%.1f %.1f V%.1f H%.1f V%.1f" fill="none" '
            'stroke="#b8c4d6" stroke-width="1.4"/>' % (
                p_mid, py + box_h, py + box_h + gap_y / 2, c_mid, y))

    for i, row in enumerate(rows):
        node = row["node"]
        x, y = positions[i]
        error = node.get("error_code") or (
            node.get("status") == "error" and "error")
        stroke, fill = _color(node.get("service"), error)
        parts.append(
            '<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="6" '
            'fill="%s" stroke="%s" stroke-width="1.2"/>' % (
                x, y, box_w, box_h, fill, stroke))
        parts.append(
            '<text x="%.1f" y="%.1f" font-family="PingFang SC, Microsoft YaHei, '
            'sans-serif" font-size="12" font-weight="600" fill="#14213d">%s / %s</text>' % (
                x + 12, y + 20, esc(node.get("service")), esc(node.get("name"))))
        parts.append(
            '<text x="%.1f" y="%.1f" font-family="Menlo, monospace" '
            'font-size="10" fill="#5b6b82">%s  tid=%s  %s</text>' % (
                x + 12, y + 40, _ms(node.get("duration_ns")),
                esc(node.get("tid", 0)),
                esc(node.get("error_code") or node.get("status") or "ok")))
        if node.get("span_id"):
            parts.append(
                '<text x="%.1f" y="%.1f" font-family="Menlo, monospace" '
                'font-size="8" fill="#8a9bb4">%s</text>' % (
                    x + box_w - 78, y + 20, esc(node["span_id"][:10])))

    parts.append("</svg>")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    return path


if __name__ == "__main__":
    import json
    import sys

    if len(sys.argv) < 3:
        print("用法: draw_tree.py trace.json out.svg")
        raise SystemExit(2)
    detail = json.load(open(sys.argv[1], encoding="utf-8"))
    print_tree(detail)
    render_svg(detail, sys.argv[2])
    print("SVG:", sys.argv[2])
