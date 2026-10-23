#!/usr/bin/env python3
"""Turn the codegen's unresolved-branch traps into the branches they are.

ReXGlue v0.10.0 writes a trap (REX_FATAL("Unresolved branch/call from 0xA to 0xB")) for a branch whose target it could
not tie to a call target, even when the target is known:

  * a conditional branch back to the entry of the function it is in (a loop whose head is the function's first
    instruction): the label loc_B is right there, but the branch is classified as a call to a function;
  * a branch (b, bc) to the entry of another function that names.toml declares: a tail call the discover phase did not
    record.

Within the function that holds loc_B the trap becomes `goto loc_B;` (exact). Otherwise, when a function starts at B,
it becomes a tail call: `B(ctx, base); return;` (as the codegen writes the tail calls it resolved). `bl` (a call that
comes back) becomes the call alone. A trap whose target is neither is left as it is and listed: the script exits 1.

The codegen also writes `goto loc_B;` for a branch to B inside the address range of the function it is in when B is
the entry of another function overlapping it (the label is then in that function, not here: GCC stops on "label used
but not defined"). Those become the same tail call, or are listed if no function starts at B (name it in names.toml).

Usage: patch_branch_traps.py generated/default/*.cpp     (idempotent)
"""
import re
import sys

TRAP = re.compile(r'^(\t+)(?:if \((.+?)\) )?REX_FATAL\("Unresolved (branch|call) from 0x([0-9A-F]{8}) to 0x([0-9A-F]{8})"\);$',
                  re.M)
INSN = re.compile(r'^\t// ([a-z.+-]+) ', re.M)


def declare(text, names):
    """The file's own declarations header only declares what the codegen called: add the functions the pass calls."""
    names = sorted(n for n in names if f'DECLARE_REX_FUNC({n});' not in text)
    if not names:
        return text
    m = list(re.finditer(r'^#include [^\n]*\n', text[:4096], re.M))
    at = m[-1].end() if m else 0
    return text[:at] + ''.join(f'DECLARE_REX_FUNC({n});\n' for n in names) + text[at:]


def main(paths):
    texts = {p: open(p, encoding='utf-8').read() for p in paths}
    # Every function the generated code defines, by address: its registration table ({ 0xADDRESS, name }, in
    # ds3_init.cpp), which also has the functions names.toml names.
    starts = {}
    for t in texts.values():
        for m in re.finditer(r'\b0x([0-9A-F]{8}), (\w+)\b', t):
            starts.setdefault(m.group(1), m.group(2))

    fixed = {'goto': 0, 'tail call': 0, 'call': 0}
    left = []
    for path, t in texts.items():
        if 'REX_FATAL("Unresolved' not in t:
            continue
        # Function bodies: from one definition to the next.
        heads = [m.start() for m in re.finditer(r'^\S[^\n]*\) \{$', t, re.M)] + [len(t)]
        out, last, called = [], 0, set()
        for m in TRAP.finditer(t):
            indent, cond, kind, src, dst = m.groups()
            k = max(i for i, h in enumerate(heads) if h <= m.start()) if heads[0] <= m.start() else -1
            lo, hi = (heads[k], heads[k + 1]) if k >= 0 else (0, len(t))
            body = t[lo:hi]
            insns = INSN.findall(t[max(lo, m.start() - 400):m.start()])
            mnemonic = insns[-1] if insns else ''
            if f'\nloc_{dst}:' in body:
                new, what = f'goto loc_{dst};', 'goto'
            elif dst in starts:
                if mnemonic.startswith('bl') and not mnemonic.startswith(('blt', 'ble')) and kind == 'call':
                    new, what = f'{starts[dst]}(ctx, base);', 'call'
                else:
                    new, what = f'{{ {starts[dst]}(ctx, base); return; }}', 'tail call'
            else:
                left.append(f'{path}: 0x{src} -> 0x{dst}')
                continue
            fixed[what] += 1
            if what != 'goto':
                called.add(starts[dst])
            out.append(t[last:m.start()])
            out.append(f'{indent}if ({cond}) {new}' if cond else f'{indent}{new}')
            last = m.end()
        out.append(t[last:])
        new_text = declare(''.join(out), called)
        if new_text != t:
            open(path, 'w', encoding='utf-8').write(new_text)
    # Gotos to a label the function does not have.
    fixed['foreign goto'] = 0
    for path in paths:
        t = open(path, encoding='utf-8').read()
        heads = [m.start() for m in re.finditer(r'^\S[^\n]*\) \{$', t, re.M)] + [len(t)]
        out, changed, called = [t[:heads[0]]], False, set()
        for lo, hi in zip(heads, heads[1:]):
            body = t[lo:hi]
            labels = set(re.findall(r'^loc_([0-9A-F]{8}):', body, re.M))

            def fix(m):
                nonlocal changed
                dst = m.group(1)
                if dst in labels:
                    return m.group(0)
                if dst not in starts:
                    left.append(f'{path}: goto 0x{dst} (no label here, no function there)')
                    return m.group(0)
                changed = True
                called.add(starts[dst])
                fixed['foreign goto'] += 1
                return f'{{ {starts[dst]}(ctx, base); return; }}'
            out.append(re.sub(r'goto loc_([0-9A-F]{8});', fix, body))
        if changed:
            open(path, 'w', encoding='utf-8').write(declare(''.join(out), called))
    print(f"patch_branch_traps: {fixed['goto']} gotos, {fixed['tail call']} tail calls, {fixed['call']} calls; "
          f"{fixed['foreign goto']} gotos into another function made tail calls; {len(left)} left")
    for line in left:
        print('  left:', line)
    return 1 if left else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
