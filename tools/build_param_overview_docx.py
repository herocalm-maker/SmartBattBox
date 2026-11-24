# -*- coding: utf-8 -*-
"""把 Doc/配置参数总览_20261122.md 转成 Word (.docx)。

通用 Markdown 子集转换器:
  # / ## / ###   -> 标题
  | 表格 |        -> Word 表格 (首行为表头, 自动跳过 |---| 分隔行)
  ``` 代码块 ```   -> 等宽字体灰底段落
  - / 1. 列表     -> 项目符号 / 编号
  > 引用          -> 缩进斜体
  行内 **粗体** / `code` -> 对应字体强调
"""
import os
import re

from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

import sys

# 用法: python build_param_overview_docx.py [src.md] [out.docx]
# 不给参数时默认转换"配置参数总览"
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Doc", "配置参数总览_20261122.md")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(SRC)[0] + ".docx"

CN_BODY = "宋体"
CN_HEAD = "微软雅黑"
MONO = "Consolas"


# ------------------------------------------------------------------ helpers
def set_run_font(run, name, size=None, bold=None, color=None):
    run.font.name = name
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), name)
    rfonts.set(qn("w:hAnsi"), name)
    rfonts.set(qn("w:eastAsia"), name)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color


def style_font(style, name, size, bold=False, color=None):
    style.font.name = name
    style.font.size = Pt(size)
    style.font.bold = bold
    if color is not None:
        style.font.color.rgb = color
    rpr = style.element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), name)
    rfonts.set(qn("w:hAnsi"), name)
    rfonts.set(qn("w:eastAsia"), name)


def shade(element_pr, color):
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), color)
    element_pr.append(shd)


def cell_bg(cell, color):
    shade(cell._tc.get_or_add_tcPr(), color)


def set_cell_margins(table, top=40, bottom=40, left=80, right=80):
    tbl_pr = table._tbl.tblPr
    mar = OxmlElement("w:tblCellMar")
    for tag, val in (("top", top), ("left", left), ("bottom", bottom), ("right", right)):
        node = OxmlElement(f"w:{tag}")
        node.set(qn("w:w"), str(val))
        node.set(qn("w:type"), "dxa")
        mar.append(node)
    tbl_pr.append(mar)


def repeat_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    node = OxmlElement("w:tblHeader")
    node.set(qn("w:val"), "true")
    tr_pr.append(node)


# ------------------------------------------------------------------ inline
LINK_RE = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")

# Word 里缺 emoji 字形会变成 △, 用等价文字符号代替
EMOJI_MAP = {
    "⚠️": "!",
    "⚠": "!",
    "✅": "√",
    "❌": "×",
    "ℹ️": "i",
    "ℹ": "i",
    "🔴": "!!",
    "🟠": "!",
    "⛔": "×",
}


def normalize(text):
    """去掉 markdown 链接、替换 emoji 为文字符号。"""
    text = LINK_RE.sub(r"\1", text)
    for k, v in EMOJI_MAP.items():
        text = text.replace(k, v)
    return text


TOKEN_RE = re.compile(r"(\*\*.+?\*\*|\*[^*\n]+\*|`[^`]+`)")


def add_inline(paragraph, text, size=10.5, color=None):
    """把 **粗体** 和 `code` 拆成 run, 其余按正文处理。"""
    text = normalize(text)
    for part in TOKEN_RE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            run = paragraph.add_run(part[2:-2].replace("`", ""))
            set_run_font(run, CN_BODY, size, bold=True, color=color)
        elif part.startswith("`") and part.endswith("`"):
            run = paragraph.add_run(part[1:-1])
            set_run_font(run, MONO, size - 0.5, color=RGBColor(0xC0, 0x30, 0x30))
        elif part.startswith("*") and part.endswith("*") and len(part) > 2:
            run = paragraph.add_run(part[1:-1])
            set_run_font(run, CN_BODY, size, color=color)
            run.font.italic = True
        else:
            run = paragraph.add_run(part)
            set_run_font(run, CN_BODY, size, color=color)


# ------------------------------------------------------------------ blocks
def add_heading(doc, text, level):
    sizes = {0: 20, 1: 15, 2: 12.5, 3: 11}
    h = doc.add_heading(level=min(level, 3))
    run = h.add_run(text)
    set_run_font(run, CN_HEAD, sizes.get(level, 11), bold=True,
                 color=RGBColor(0x1F, 0x3B, 0x66))
    h.paragraph_format.space_before = Pt(10 if level <= 1 else 6)
    h.paragraph_format.space_after = Pt(4)
    return h


def add_body(doc, text, size=10.5):
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.line_spacing = 1.15
    add_inline(p, text, size)
    return p


def add_bullet(doc, text, size=10.5):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.space_after = Pt(2)
    add_inline(p, text, size)
    return p


def add_code(doc, lines):
    for line in lines:
        p = doc.add_paragraph()
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.left_indent = Cm(0.5)
        p.paragraph_format.line_spacing = 1.0
        shade(p._p.get_or_add_pPr(), "F2F2F2")
        run = p.add_run(line if line else " ")
        set_run_font(run, MONO, 9)


def add_quote(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.6)
    p.paragraph_format.space_after = Pt(3)
    add_inline(p, text, 10, color=RGBColor(0x55, 0x55, 0x55))


def add_table(doc, rows, font_size=9.0):
    if not rows:
        return
    cols = len(rows[0])
    table = doc.add_table(rows=1, cols=cols)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True

    hdr = table.rows[0]
    for i, text in enumerate(rows[0]):
        cell = hdr.cells[i]
        cell.text = ""
        p = cell.paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        add_inline(p, text, font_size, color=RGBColor(0x1F, 0x3B, 0x66))
        for run in p.runs:
            run.font.bold = True
        cell_bg(cell, "D9E2F3")
    repeat_header(hdr)

    for data in rows[1:]:
        row = table.add_row()
        for i in range(cols):
            cell = row.cells[i]
            cell.text = ""
            p = cell.paragraphs[0]
            p.paragraph_format.space_after = Pt(0)
            add_inline(p, data[i] if i < len(data) else "", font_size)

    set_cell_margins(table)
    sp = doc.add_paragraph()
    sp.paragraph_format.space_before = Pt(0)
    sp.paragraph_format.space_after = Pt(0)
    sp.paragraph_format.line_spacing = Pt(4)


def split_row(line):
    """| a | b | -> ['a','b']"""
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    return [c.strip() for c in s.split("|")]


SEP_RE = re.compile(r"^\|[\s:\-\|]+\|$")


# ------------------------------------------------------------------ main
def main():
    with open(SRC, "r", encoding="utf-8") as fh:
        lines = fh.read().splitlines()

    doc = Document()
    sec = doc.sections[0]
    sec.left_margin = Cm(2.0)
    sec.right_margin = Cm(2.0)
    sec.top_margin = Cm(2.0)
    sec.bottom_margin = Cm(1.8)

    style_font(doc.styles["Normal"], CN_BODY, 10.5)
    for name, size in (("Heading 1", 15), ("Heading 2", 12.5), ("Heading 3", 11)):
        if name in [s.name for s in doc.styles]:
            style_font(doc.styles[name], CN_HEAD, size, bold=True)

    i = 0
    n = len(lines)
    title_done = False
    while i < n:
        raw = lines[i]
        line = raw.rstrip()
        stripped = line.strip()

        # 空行
        if not stripped:
            i += 1
            continue

        # 代码块
        if stripped.startswith("```"):
            i += 1
            buf = []
            while i < n and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            add_code(doc, buf)
            continue

        # 表格
        if stripped.startswith("|") and i + 1 < n and SEP_RE.match(lines[i + 1].strip()):
            rows = [split_row(stripped)]
            i += 2
            while i < n and lines[i].strip().startswith("|"):
                if not SEP_RE.match(lines[i].strip()):
                    rows.append(split_row(lines[i]))
                i += 1
            add_table(doc, rows)
            continue

        # 标题
        m = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if m:
            level = len(m.group(1))
            text = m.group(2)
            if level == 1 and not title_done:
                title_done = True
                h = doc.add_heading(level=0)
                run = h.add_run(text)
                set_run_font(run, CN_HEAD, 22, bold=True,
                             color=RGBColor(0x10, 0x28, 0x50))
                h.alignment = WD_ALIGN_PARAGRAPH.CENTER
            else:
                add_heading(doc, text, level - 1)
            i += 1
            continue

        # 引用
        if stripped.startswith(">"):
            add_quote(doc, stripped.lstrip("> ").strip())
            i += 1
            continue

        # 列表
        if re.match(r"^[-*]\s+", stripped):
            add_bullet(doc, re.sub(r"^[-*]\s+", "", stripped))
            i += 1
            continue
        if re.match(r"^\d+\.\s+", stripped):
            p_num = doc.add_paragraph()
            p_num.paragraph_format.left_indent = Cm(0.75)
            p_num.paragraph_format.first_line_indent = Cm(-0.45)
            p_num.paragraph_format.space_after = Pt(2)
            add_inline(p_num, re.sub(r"^(\d+)\.\s+", r"\1. ", stripped))
            i += 1
            continue

        # 分隔线
        if set(stripped) <= {"-"} and len(stripped) >= 3:
            i += 1
            continue

        add_body(doc, stripped)
        i += 1

    doc.save(OUT)
    print("saved:", OUT)


if __name__ == "__main__":
    main()
