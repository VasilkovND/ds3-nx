#!/usr/bin/env python3
"""Find code the codegen left out: gaps between the functions it generated that hold instructions.

A function reached only through a pointer the analysis did not see (a vtable entry, a callback table built at run time)
is not generated, and a call to it at run time logs "Call to invalid or unregistered function" and returns without
running it: Dead Space 3 stopped drawing after the first such call (0x88981840, an 8-instruction virtual function
between two functions the codegen did find). This lists every such gap, so it can be named in codegen/names.toml.

A function's extent is its start plus 4 bytes per instruction the generated code comments (`\t// mnemonic ...`). A gap
is the space from one function's end to the next one's start; zero words (padding) at its start are skipped. A gap is
taken as code when its first word is no padding and its last word before the next function ends a function (blr, bctr,
or an unconditional b), and, with --pointers, only when the image holds a big-endian pointer to its start.

Usage: find_code_gaps.py <generated/game> <image.bin of the module> <image base hex> [--pointers] > gaps.toml
"""
import re
import struct
import sys
from pathlib import Path

FUNC = re.compile(r'^DEFINE_REX_FUNC\((\w+)\) \{', re.M)
INSN = re.compile(r'^\t// [a-z]', re.M)


def ends_function(word):
    if word in (0x4E800020, 0x4E800420):  # blr, bctr
        return True
    return (word >> 26) == 18 and (word & 3) == 0  # b (no link, relative)


def main():
    gen, image_path, base = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3], 16)
    want_pointers = '--pointers' in sys.argv
    image = image_path.read_bytes()
    addr_of = {m.group(2): int(m.group(1), 16)
               for m in re.finditer(r'\{ 0x([0-9A-F]{8}), (\w+) \}', (gen / 'ds3_init.cpp').read_text())}
    extent = {}
    for f in gen.glob('ds3_recomp.*.cpp'):
        t = f.read_text(errors='replace')
        ms = list(FUNC.finditer(t))
        for i, m in enumerate(ms):
            body = t[m.end(): ms[i + 1].start() if i + 1 < len(ms) else len(t)]
            a = addr_of.get(m.group(1))
            if a is not None:
                extent[a] = a + 4 * len(INSN.findall(body))
    starts = sorted(extent)
    word = lambda a: struct.unpack_from('>I', image, a - base)[0]
    pointers = None
    if want_pointers:
        pointers = set(struct.unpack_from(f'>{len(image) // 4}I', image[:len(image) // 4 * 4]))
    found = []
    for a, b in zip(starts, starts[1:]):
        g = max(extent[a], a)
        while g < b and word(g) == 0:
            g += 4
        if g >= b or b - g < 8:
            continue
        if not ends_function(word(b - 4)):
            continue
        if pointers is not None and g not in pointers:
            continue
        found.append(g)
    for g in found:
        print(f'[entrypoint.functions.0x{g:08X}]\nname = "gap_{g:08X}"\n')
    print(f'# {len(found)} gaps of code between {len(starts)} functions', file=sys.stderr)


if __name__ == '__main__':
    main()
