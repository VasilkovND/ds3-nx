"""Name the functions of a generated tree by matching their PPC bodies against a named recompiled tree (Dead Space 2's
src_tu2, which has names from its prototype PDB). From gears2-nx; used here for Dead Space 3.

Each function becomes the sequence of its PPC instructions (from the codegen's comments) with branch targets and
absolute-address halves (lis/addi/ori/lwz with large offsets) masked. Bodies with a unique exact match in both trees
inherit the reference name (Dead Space 2 -> Dead Space 3: 5,460). fuzzy_find.py ranks the closest bodies for the names
that do not match exactly.
Usage: match_funcs.py <named reference tree> <ds3/out/codegen/generated/default> <out.tsv>
"""
import os, re, sys, hashlib
from collections import defaultdict

FUNC = re.compile(r'^DEFINE_REX_FUNC\((\w+)\) \{', re.M)
INSN = re.compile(r'^\t// ([a-z][\w.+-]*)(?: (.*))?$', re.M)
BRANCH = re.compile(r'^b[a-z]*$')

def norm(m, ops):
    ops = ops or ''
    if BRANCH.match(m) and not m.endswith(('lr', 'ctr', 'lrl', 'ctrl')):
        ops = re.sub(r'0x[0-9A-Fa-f]+|\b[0-9A-F]{8}\b|\bloc_\w+|\bsub_\w+|\w+__\w+', 'T', ops)
    if m in ('lis', 'addis'):
        ops = re.sub(r',-?\d+$', ',H', ops)
    # Large displacements / immediates are usually address halves.
    ops = re.sub(r'-?\d{4,}', 'N', ops)
    return m + ' ' + ops

def scan(root):
    funcs = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith('.cpp'):
                continue
            t = open(os.path.join(dp, f), encoding='utf-8', errors='replace').read()
            ms = list(FUNC.finditer(t))
            for i, m in enumerate(ms):
                body = t[m.end(): ms[i + 1].start() if i + 1 < len(ms) else len(t)]
                ins = [norm(a, b) for a, b in INSN.findall(body)]
                if len(ins) >= 8:
                    funcs[m.group(1)] = (hashlib.sha1('\n'.join(ins).encode()).hexdigest(), len(ins))
    return funcs

ds2, di = scan(sys.argv[1]), scan(sys.argv[2])
by = lambda d: (lambda g: [g[h].append(n) for n, (h, _) in d.items()] and g)(defaultdict(list))
g2, g3 = by(ds2), by(di)
out = []
for h, names in g2.items():
    if len(names) == 1 and len(g3.get(h, [])) == 1 and not names[0].startswith('sub_'):
        out.append((g3[h][0], names[0], ds2[names[0]][1]))
out.sort()
with open(sys.argv[3], 'w') as f:
    for a, n, k in out:
        f.write(f'{a}\t{n}\t{k}\n')
print(f'reference {len(ds2)} funcs, target {len(di)} funcs, {len(out)} named matches')
