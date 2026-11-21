#!/usr/bin/env python
"""Dump words with bounding boxes (top-left origin) for a page."""
import sys
import pypdfium2 as pdfium

PDF = r"D:\Project_src_code\SmartPowerBoxProject\2.原理图\移动电池逆变器电路原理图_20260706.pdf"

page_index = int(sys.argv[1])
needle = sys.argv[2] if len(sys.argv) > 2 else None
pdf = pdfium.PdfDocument(PDF)
page = pdf[page_index]
ph = page.get_size()[1]
tp = page.get_textpage()
n = tp.count_chars()
chars = []
for i in range(n):
    ch = tp.get_text_range(i, 1)
    l, b, r, t = tp.get_charbox(i)
    if ch.strip():
        chars.append([ch, l, b, r, t])
# merge into words: same line (y overlap) and gap < 1.2 * char height
words = []
cur = None
for ch, l, b, r, t in chars:
    if cur and abs(((b + t) / 2) - cur["y"]) < 0.5 * (t - b) and l - cur["r"] < 3.0:
        cur["text"] += ch
        cur["r"] = r
        cur["b"] = min(cur["b"], b)
        cur["t"] = max(cur["t"], t)
    else:
        if cur:
            words.append(cur)
        cur = {"text": ch, "l": l, "r": r, "b": b, "t": t, "y": (b + t) / 2}
if cur:
    words.append(cur)
for w in words:
    txt = w["text"]
    if needle and needle.lower() not in txt.lower():
        continue
    print(f'{txt!r:40s} x={w["l"]:7.1f}..{w["r"]:7.1f}  ytop={ph-w["t"]:7.1f}..{ph-w["b"]:7.1f}')
