# -*- coding: utf-8 -*-
"""生成《SmartBattBox 固件烧录说明》Word 文档（客户发放版）。"""

from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
import os

OUT = os.path.join("Doc", "SmartBattBox固件烧录说明_V1.0.docx")

CN_BODY = "宋体"
CN_HEAD = "微软雅黑"
MONO = "Consolas"


# ---------------------------------------------------------------- helpers
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


def para_bg(paragraph, color):
    shade(paragraph._p.get_or_add_pPr(), color)


def set_cell_margins(table, top=50, bottom=50, left=85, right=85):
    tbl_pr = table._tbl.tblPr
    mar = OxmlElement("w:tblCellMar")
    for tag, val in (("top", top), ("left", left), ("bottom", bottom), ("right", right)):
        el = OxmlElement("w:" + tag)
        el.set(qn("w:w"), str(val))
        el.set(qn("w:type"), "dxa")
        mar.append(el)
    tbl_pr.append(mar)


def repeat_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    el = OxmlElement("w:tblHeader")
    el.set(qn("w:val"), "true")
    tr_pr.append(el)


def cant_split(row):
    tr_pr = row._tr.get_or_add_trPr()
    el = OxmlElement("w:cantSplit")
    el.set(qn("w:val"), "true")
    tr_pr.append(el)


def add_table(doc, headers, rows, widths, font_size=9.0, header_bg="D9E2F3"):
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    set_cell_margins(table)

    hdr = table.rows[0]
    hdr.height = Cm(0.68)
    repeat_header(hdr)
    cant_split(hdr)
    for i, text in enumerate(headers):
        cell = hdr.cells[i]
        cell.width = Cm(widths[i])
        cell_bg(cell, header_bg)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(1)
        p.paragraph_format.space_after = Pt(1)
        run = p.add_run(text)
        set_run_font(run, CN_HEAD, font_size, bold=True)

    for row in rows:
        r = table.add_row()
        cant_split(r)
        cells = r.cells
        for i, text in enumerate(row):
            cells[i].width = Cm(widths[i])
            p = cells[i].paragraphs[0]
            p.paragraph_format.space_before = Pt(1)
            p.paragraph_format.space_after = Pt(1)
            run = p.add_run(str(text))
            set_run_font(run, CN_BODY, font_size)
    return table


def add_code(doc, lines, size=8.5):
    for idx, line in enumerate(lines):
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.space_before = Pt(3 if idx == 0 else 0)
        pf.space_after = Pt(3 if idx == len(lines) - 1 else 0)
        pf.left_indent = Cm(0.6)
        pf.right_indent = Cm(0.4)
        pf.line_spacing = 1.1
        pf.keep_together = True
        pf.keep_with_next = (idx < len(lines) - 1)   # 整块不跨页
        para_bg(p, "F2F2F2")
        run = p.add_run(line if line else " ")
        set_run_font(run, MONO, size)


def body(doc, text, space_after=3, keep_next=False):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(space_after)
    p.paragraph_format.line_spacing = 1.36
    p.paragraph_format.keep_with_next = keep_next
    run = p.add_run(text)
    set_run_font(run, CN_BODY, 10.5)
    return p


def bullet(doc, text):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.line_spacing = 1.32
    run = p.add_run(text)
    set_run_font(run, CN_BODY, 10.5)
    return p


def num_item(doc, n, text):
    """手动编号，避免 Word 列表跨章节连续编号。"""
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.space_before = Pt(0)
    pf.space_after = Pt(2)
    pf.line_spacing = 1.32
    pf.left_indent = Cm(0.85)
    pf.first_line_indent = Cm(-0.85)
    r1 = p.add_run("%d. " % n)
    set_run_font(r1, CN_BODY, 10.5, bold=True)
    r2 = p.add_run(text)
    set_run_font(r2, CN_BODY, 10.5)
    return p


def heading(doc, text, level=1):
    h = doc.add_heading(level=level)
    h.paragraph_format.space_before = Pt(12 if level == 1 else 9)
    h.paragraph_format.space_after = Pt(5)
    h.paragraph_format.keep_with_next = True
    run = h.add_run(text)
    set_run_font(run, CN_HEAD, 14 if level == 1 else 11.5, bold=True,
                 color=RGBColor(0x1F, 0x3B, 0x63))
    return h


def sub_head(doc, text, space_before=6):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(space_before)
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.keep_with_next = True
    run = p.add_run(text)
    set_run_font(run, CN_HEAD, 10.5, bold=True)
    return p


def add_footer(doc):
    section = doc.sections[0]
    p = section.footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run("SmartBattBox 固件烧录说明 V1.0    第 ")
    set_run_font(run, CN_BODY, 8.5, color=RGBColor(0x80, 0x80, 0x80))
    r = p.add_run()
    set_run_font(r, CN_BODY, 8.5, color=RGBColor(0x80, 0x80, 0x80))
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = "PAGE"
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    r._r.append(fld_begin)
    r._r.append(instr)
    r._r.append(fld_end)
    r2 = p.add_run(" 页")
    set_run_font(r2, CN_BODY, 8.5, color=RGBColor(0x80, 0x80, 0x80))


# ---------------------------------------------------------------- document
doc = Document()

sec = doc.sections[0]
sec.page_width = Cm(21.0)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.0)
sec.bottom_margin = Cm(1.8)
sec.left_margin = Cm(2.4)
sec.right_margin = Cm(2.4)

normal = doc.styles["Normal"]
style_font(normal, CN_BODY, 10.5)
normal.paragraph_format.line_spacing = 1.36
normal.paragraph_format.space_after = Pt(3)

style_font(doc.styles["Heading 1"], CN_HEAD, 14, bold=True)
style_font(doc.styles["Heading 2"], CN_HEAD, 11.5, bold=True)

add_footer(doc)

# ---- 标题
p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_before = Pt(18)
p.paragraph_format.space_after = Pt(2)
run = p.add_run("SmartBattBox 固件烧录说明")
set_run_font(run, CN_HEAD, 20, bold=True, color=RGBColor(0x1F, 0x3B, 0x63))

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(10)
run = p.add_run("STM32CubeProgrammer + ST-LINK")
set_run_font(run, CN_HEAD, 12, color=RGBColor(0x59, 0x59, 0x59))

meta = [
    ("适用产品", "SmartBattBox 移动储能逆变电源控制板"),
    ("主控芯片", "STM32F407VGT6（Cortex-M4F，1MB Flash）"),
    ("适用固件", "SmartBattBox.bin"),
    ("烧录工具", "STM32CubeProgrammer（本说明以 v2.23.0 为基准）"),
    ("文档版本", "V1.0　　更新日期：2026-11-16"),
]
add_table(doc, ("项目", "内容"), meta, (3.4, 12.8), header_bg="DEEAF6")

# ---- 目录
p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(14)
p.paragraph_format.space_after = Pt(4)
run = p.add_run("目　录")
set_run_font(run, CN_HEAD, 12, bold=True, color=RGBColor(0x1F, 0x3B, 0x63))

toc = [
    "一、准备清单",
    "二、软件安装",
    "三、接线（SWD 四线）",
    "四、烧录步骤（图形界面）",
    "五、烧录成功的确认方法",
    "六、重要注意事项",
    "七、产线批量烧录（命令行）",
    "八、常见问题排查",
    "九、版本记录",
]
for item in toc:
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.space_before = Pt(0)
    pf.space_after = Pt(1)
    pf.left_indent = Cm(0.8)
    run = p.add_run(item)
    set_run_font(run, CN_BODY, 10.5)

# ---- 一、准备清单（另起一页，第 1 页作为封面+目录）
doc.add_page_break()
heading(doc, "一、准备清单")
add_table(
    doc,
    ("类别", "项目", "说明"),
    [
        ("硬件", "ST-LINK 下载器", "V2 或 V3 均可，需支持 SWD"),
        ("硬件", "4 芯 SWD 排线", "SWDIO / SWCLK / GND / 3V3"),
        ("硬件", "目标板", "控制板 + 电池（12V 系统）"),
        ("硬件", "电脑", "Windows 10 / 11 64 位"),
        ("软件", "STM32CubeProgrammer", "官网免费下载，安装保留默认选项（含 ST-LINK 驱动）"),
        ("文件", "SmartBattBox.bin", "随本说明一同发放的固件文件"),
    ],
    (2.0, 4.6, 9.6),
)
body(doc, "固件文件请勿改名，也不要使用压缩包内的其它文件（如 .elf）。")

# ---- 二、软件安装
heading(doc, "二、软件安装")
for i, s in enumerate([
    "访问 ST 官网下载 STM32CubeProgrammer：https://www.st.com/en/development-tools/stm32cubeprog.html",
    "安装时保持默认勾选，程序会自动安装 ST-LINK 驱动。",
    "首次插入 ST-LINK 后打开「设备管理器」，应能看到 “STMicroelectronics STLink dongle”。",
    "若显示黄色感叹号，请重新安装驱动或更换 USB 口（建议使用主板后置 USB 口）。",
], 1):
    num_item(doc, i, s)

# ---- 三、接线
heading(doc, "三、接线（SWD 四线）")
body(doc, "控制板上标有 SWD 的调试排针，按下表对应连接：")
add_table(
    doc,
    ("ST-LINK 信号（丝印）", "控制板信号", "MCU 引脚", "是否必须"),
    [
        ("SWDIO", "SWDIO", "PA13", "必须"),
        ("SWCLK", "SWCLK", "PA14", "必须"),
        ("GND", "GND", "—", "必须"),
        ("3V3 / VAPP", "3V3U", "—", "建议（电平参考）"),
        ("NRST / RST", "NRST", "—", "可选（连接异常时使用）"),
    ],
    (4.0, 3.6, 3.2, 5.4),
)
sub_head(doc, "接线要点")
for s in [
    "不同型号 ST-LINK 的排针编号不同，一律按丝印名称对应，不要按引脚序号连接。",
    "目标板由电池独立供电时，ST-LINK 的 3V3 只作电平参考，不要用它给整机供电。",
    "排线尽量短（不超过 20 cm），过长会导致连接不稳定。",
]:
    bullet(doc, s)

# ---- 四、烧录步骤
heading(doc, "四、烧录步骤（图形界面）")
num_item(doc, 1, "给目标板上电（接入电池），再把 ST-LINK 插入电脑 USB 口。")
num_item(doc, 2, "打开 STM32CubeProgrammer。")
num_item(doc, 3, "在右上角连接栏按下方设置，然后点击 Connect（连接）：")
add_table(
    doc,
    ("连接项", "设置值"),
    [
        ("接口类型", "ST-LINK"),
        ("Port（端口）", "SWD"),
        ("Reset mode（复位方式）", "Software reset（默认）"),
        ("Frequency（频率）", "默认 4 MHz"),
    ],
    (6.0, 10.2),
)
body(doc, "连接成功后，右上角会显示芯片型号 STM32F407VG。", space_after=5)
num_item(doc, 4, "切换到左侧「Erasing & Programming」（擦除与编程）页面。")
num_item(doc, 5, "按下表填写烧录参数：")
add_table(
    doc,
    ("界面项", "填写内容"),
    [
        ("File path（文件路径）", "选择 SmartBattBox.bin"),
        ("Start address（起始地址）", "0x08000000"),
        ("File size（文件大小）", "自动识别，约 71 332 字节"),
        ("Verify programming（校验）", "勾选"),
        ("Skip flash erase before programming", "勾选（跳过擦除，速度更快）"),
        ("Full chip erase（全片擦除）", "不要勾选"),
    ],
    (7.2, 9.0),
)
num_item(doc, 6, "点击 Start Programming（开始编程）。")
num_item(doc, 7, "等待进度到 100%，日志出现 Programmed and verified successfully。")
num_item(doc, 8, "点击 Disconnect（断开），给目标板断电再重新上电。")

# ---- 五、确认方法
heading(doc, "五、烧录成功的确认方法")
sub_head(doc, "方法一：观察屏幕", space_before=2)
body(doc, "上电后约 1~2 秒，屏幕电量区域先空白，随后显示真实电量数字与电量条，说明固件已正常运行。", space_after=5)
sub_head(doc, "方法二：查看串口日志（推荐）", space_before=2)
body(doc, "用 USB 转 TTL 连接调试串口（UART1，115200 8N1），上电应打印：", space_after=2, keep_next=True)
add_code(doc, [
    "=== SmartBattBox boot ===",
    "clk: 168000000 Hz",
    "tim4: ok",
    "boardio: all drivers init",
    "bsp init done, starting RTOS...",
    "=== System ready, tasks running ===",
])
body(doc, "能看到以上内容，说明固件烧录正确、系统已启动。")

# ---- 六、注意事项
heading(doc, "六、重要注意事项")
notes = [
    ("起始地址必须是 0x08000000。", "填错（例如漏掉 0x08）会导致烧录“成功”但板子不启动。"),
    ("文件类型必须选 .bin。", "不要选 .elf、.hex 或压缩包。"),
    ("不要勾选「Full chip erase」。", "0x080E0000 起 128 KB 为参数区（电池学习参数、标定值），全片擦除会清除。"),
    ("烧录过程中不要断电、不要拔 USB。", "否则可能损坏固件或使芯片进入异常状态。"),
    ("若芯片开启了读保护（RDP）。", "需先在 Option bytes 页解除保护，该操作会触发全片擦除。"),
]
for i, (head, tail) in enumerate(notes, 1):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.space_before = Pt(0)
    pf.space_after = Pt(2)
    pf.line_spacing = 1.32
    pf.left_indent = Cm(0.75)
    pf.first_line_indent = Cm(-0.75)
    r1 = p.add_run("%d）%s" % (i, head))
    set_run_font(r1, CN_BODY, 10.5, bold=True)
    r2 = p.add_run(tail)
    set_run_font(r2, CN_BODY, 10.5)

# ---- 七、命令行
heading(doc, "七、产线批量烧录（命令行）")
body(doc, "安装 CubeProgrammer 后自带命令行工具，默认安装目录：", space_after=2, keep_next=True)
add_code(doc, [r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"])
body(doc, "该目录下的 STM32_Programmer_CLI.exe 即为命令行工具。单次烧录命令：", space_after=2)
add_code(doc, [
    'STM32_Programmer_CLI.exe -c port=SWD -w "SmartBattBox.bin" 0x08000000 -v -rst',
])
add_table(
    doc,
    ("参数", "含义"),
    [
        ("-c port=SWD", "使用 SWD 接口连接"),
        ("-w <文件> 0x08000000", "把固件写入 0x08000000"),
        ("-v", "烧录后校验"),
        ("-rst", "烧录完成后复位运行"),
    ],
    (5.4, 10.8),
)
body(doc, "批量烧录时可在批处理脚本中检查返回码（0 表示成功）。")

# ---- 八、常见问题
heading(doc, "八、常见问题排查")
add_table(
    doc,
    ("现象", "可能原因", "处理方法"),
    [
        ("Connect 提示 No STM32 target found", "接线错误、目标板未上电、SWD 被占用",
         "检查 SWDIO / SWCLK / GND；确认板子已通电；断开后重新上电"),
        ("连接栏找不到 ST-LINK", "驱动未安装、USB 线只能充电",
         "重装 CubeProgrammer；更换数据线与 USB 口"),
        ("连接成功但读不到芯片型号", "SWD 频率过高、排线过长",
         "把 Frequency 降到 1 MHz；缩短排线"),
        ("烧录完成但板子不启动", "起始地址填错、文件不是本产品固件",
         "确认地址 0x08000000、文件为 SmartBattBox.bin"),
        ("校验失败 Verification failed", "供电不稳、目标板在烧录中被复位",
         "加大供电、缩短排线、降低频率后重烧"),
        ("提示 Device is protected", "芯片开启了读保护",
         "在 Option bytes 页把 RDP 设为 Level 0，执行后再重新烧录"),
        ("烧录成功但屏幕无显示", "屏幕接线或供电异常（非固件问题）",
         "检查屏幕 UART 接线与 12V_LCD 供电"),
    ],
    (4.8, 5.2, 6.2),
    font_size=8.5,
)

# ---- 九、版本记录
heading(doc, "九、版本记录")
add_table(
    doc,
    ("固件文件", "大小", "构建时间", "说明"),
    [
        ("SmartBattBox.bin", "71 332 字节（69.7 KB）", "2026-11-15 23:10", "当前发布版本"),
    ],
    (4.2, 4.4, 4.0, 3.6),
)
body(doc, "固件占用的 Flash 范围：", space_after=2)
add_table(
    doc,
    ("区域", "地址范围", "大小", "说明"),
    [
        ("应用程序", "0x08000000 – 0x080DFFFF", "896 KB", "固件写入区域"),
        ("参数区", "0x080E0000 – 0x080FFFFF", "128 KB", "掉电保存参数，烧录时不要擦除"),
    ],
    (3.2, 5.6, 2.6, 4.8),
)

# ---- 结尾
p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(12)
p.paragraph_format.space_after = Pt(2)
run = p.add_run("如有疑问，请联系技术支持，并提供：烧录日志截图、串口上电日志、板卡版本信息。")
set_run_font(run, CN_BODY, 9.5, color=RGBColor(0x59, 0x59, 0x59))

doc.save(OUT)
print("saved:", OUT)
