#!/usr/bin/env python
"""Small helper: render PDF pages / crops to PNG and dump words with bboxes."""
import sys, os, json
import pypdfium2 as pdfium
import pypdfium2.raw as raw

PDF = r"D:\Project_src_code\SmartPowerBoxProject\2.原理图\移动电池逆变器电路原理图_20260706.pdf"
OUT = r"D:\Project_src_code\SmartPowerBoxProject\3.code\SmartBattBox\_tools\out"
os.makedirs(OUT, exist_ok=True)


def render(page_index, dpi=150, clip=None, name=None):
    pdf = pdfium.PdfDocument(PDF)
    page = pdf[page_index]
    kwargs = {"scale": dpi / 72.0}
    if clip:
        # clip given as (x0, y0, x1, y1) in top-left origin PDF points
        x0, y0, x1, y1 = clip
        pw, ph = page.get_size()
        page.set_cropbox(x0, ph - y1, x1, ph - y0)
    bitmap = page.render(**kwargs)
    img = bitmap.to_pil()
    fn = os.path.join(OUT, name or f"p{page_index+1}_{dpi}dpi.png")
    img.save(fn)
    print(fn, img.size)


def words(page_index):
    pdf = pdfium.PdfDocument(PDF)
    page = pdf[page_index]
    tp = page.get_textpage()
    n = tp.count_chars()
    chars = []
    for i in range(n):
        ch = tp.get_text_range(i, 1)
        box = tp.get_charbox(i)
        chars.append((ch, box))
    # group into lines roughly by y
    print(json.dumps([[c, [round(v, 1) for v in b]] for c, b in chars], ensure_ascii=False)[:200])


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "render":
        pi = int(sys.argv[2])
        dpi = int(sys.argv[3]) if len(sys.argv) > 3 else 150
        clip = None
        if len(sys.argv) > 7:
            clip = tuple(float(x) for x in sys.argv[4:8])
        name = sys.argv[8] if len(sys.argv) > 8 else None
        render(pi, dpi, clip, name)
    elif cmd == "words":
        words(int(sys.argv[2]))
