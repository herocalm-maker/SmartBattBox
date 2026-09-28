"""Batch fix single-line if statements in drv_*.c files.

Pattern: '    if (...) statement;'  ->  expand to multi-line with braces.
"""
import re, os, glob

# Match: leading whitespace + 'if (...)' + space + non-brace content + ';'
# Does NOT match lines that already have '{' on them
pat = re.compile(r'^(\s+)if\s*\((.+)\)\s+([^{}\n]+);\s*$')

def fix_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    out = []
    changed = 0
    for line in lines:
        m = pat.match(line.rstrip('\n').rstrip('\r'))
        if m:
            indent = m.group(1)
            cond = m.group(2)
            body = m.group(3).strip()
            out.append(indent + 'if (' + cond + ') {\n')
            out.append(indent + '    ' + body + ';\n')
            out.append(indent + '}\n')
            changed += 1
        else:
            out.append(line)
    if changed > 0:
        with open(path, 'w', encoding='utf-8') as f:
            f.writelines(out)
        print(f'{os.path.basename(path)}: {changed} lines fixed')
    else:
        print(f'{os.path.basename(path)}: no changes')

os.chdir(r'D:\Project_src_code\SmartPowerBoxProject\3.code\SmartBattBox')
files = glob.glob('BSP/Driver/Src/drv_*.c')
for f in sorted(files):
    fix_file(f)
