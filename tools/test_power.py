"""Execute actual power controller, relay driver and AC classifier in Unicorn.
Requires ARM GCC and Python unicorn. Outputs must stay outside the IDE project.
"""
import argparse
import glob
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def toolchain(explicit):
    if explicit:
        return Path(explicit)
    p = shutil.which('arm-none-eabi-gcc')
    if p:
        return Path(p).parent
    hits = glob.glob('C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe')
    if not hits:
        raise SystemExit('Pass --toolchain <directory containing arm-none-eabi-gcc>')
    return Path(sorted(hits)[-1]).parent

def run(cmd):
    subprocess.run([str(v) for v in cmd], check=True)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--target', choices=['test'], default='test')
    p.add_argument('--out', required=True, help='Build directory outside project source tree')
    p.add_argument('--toolchain')
    a = p.parse_args()
    tc = toolchain(a.toolchain)
    exe = '.exe' if os.name == 'nt' else ''
    gcc, objcopy, nm, size = [tc / ('arm-none-eabi-' + s + exe) for s in ['gcc', 'objcopy', 'nm', 'size']]
    out = Path(a.out).resolve()
    if ROOT == out or ROOT in out.parents:
        raise SystemExit('--out must be outside project to avoid IDE auto-discovery of tests')
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-mthumb', '-ffunction-sections', '-fdata-sections', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-Os']
    includes = [ROOT] + [ROOT / p for p in ['Core/Inc', 'App/Inc', 'Module/Inc', 'BSP/Inc', 'BSP/Driver/Inc', 'Comm/Inc', 'Drivers/STM32F4xx_HAL_Driver/Inc', 'Drivers/CMSIS/Device/ST/STM32F4xx/Include', 'Drivers/CMSIS/Include', 'Middlewares/Third_Party/FreeRTOS/Source/include', 'Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2', 'Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F']]
    flags += ['-DUSE_HAL_DRIVER', '-DSTM32F407xx']
    sources = []
    test_src = out / 'test_power.c'
    test_src.write_text((ROOT / 'tests/power/test_power.c.txt').read_text(encoding='utf-8'), encoding='utf-8')
    if a.target == 'test':
        flags += ['-mcpu=cortex-m4', '-mfloat-abi=soft', '-fno-builtin']
        sources += [test_src]
        link = ['-nostartfiles', '--specs=nosys.specs', '-Wl,--start-group', '-lc', '-lgcc', '-Wl,--end-group', '-T' + str(ROOT / 'tests/power/test.ld'), '-lgcc']
    inc = ['-I' + str(i) for i in includes]
    objects = []
    for i, src in enumerate(sources):
        obj = out / (str(i) + '_' + src.stem + '.o')
        vendor_flags = []
        run([gcc, *flags, *vendor_flags, *inc, '-c', src, '-o', obj])
        objects.append(obj)
    elf = out / 'power.elf'
    run([gcc, *flags, *objects, '-Wl,--gc-sections', '-Wl,-Map=' + str(out / 'power.map'), *link, '-o', elf])
    run([size, elf])
    run([objcopy, '-O', 'ihex', elf, out / 'power.hex'])
    if a.target == 'test':
        # Load ELF PT_LOAD segments directly, including initialized RAM (.data).
        import unicorn
        from unicorn.arm_const import UC_ARM_REG_SP
        uc = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
        uc.mem_map(0x08000000, 1024 * 1024)
        uc.mem_map(0x20000000, 128 * 1024)
        blob = elf.read_bytes()
        phoff = struct.unpack_from('<I', blob, 28)[0]
        phentsize, phnum = struct.unpack_from('<HH', blob, 42)
        for i in range(phnum):
            typ, off, va, pa, filesz, memsz, perm, align = struct.unpack_from('<8I', blob, phoff + i * phentsize)
            if typ == 1 and filesz:
                uc.mem_write(va, blob[off:off + filesz])
        symbols = {}
        for line in subprocess.check_output([str(nm), str(elf)], text=True).splitlines():
            cols = line.split()
            if len(cols) == 3:
                symbols[cols[2]] = int(cols[0], 16)
        uc.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
        uc.emu_start(symbols['test_main'] | 1, symbols['test_done'], timeout=30000000, count=100000000)
        checks, failures, first = [struct.unpack('<I', uc.mem_read(symbols[k], 4))[0]
            for k in ['test_checks', 'test_failures', 'test_first_failure']]
        from unicorn.arm_const import UC_ARM_REG_PC
        if uc.reg_read(UC_ARM_REG_PC) != symbols['test_done']:
            raise SystemExit('Test execution timed out or exhausted instruction budget')
        report = f'ARM emulation: {checks} checks, {failures} failures; first failed source line={first}'
        print(report)
        (out / 'result.txt').write_text(report + '\n', encoding='utf-8')
        if failures:
            raise SystemExit(1)
    else:
        print('Built F407 example:', elf)

if __name__ == '__main__':
    main()
