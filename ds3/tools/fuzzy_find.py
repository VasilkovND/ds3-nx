"""Find the function of the generated tree most like a named function of a reference tree (mnemonic sequences, difflib
ratio), searching the address range [lo, hi).
Usage: fuzzy_find.py <named reference tree> <ds3/out/codegen/generated/default> <lo hex> <hi hex> name..."""
import os, re, sys, difflib
sys.path.insert(0, os.path.dirname(__file__))
FUNC = re.compile(r'^DEFINE_REX_FUNC\((\w+)\) \{', re.M)
INSN = re.compile(r'^\t// ([a-z][\w.+-]*)', re.M)
def bodies(root, want=None, lo=0, hi=1<<32):
    out = {}
    for dp, _, fs in os.walk(root):
      for f in fs:
        if not f.endswith('.cpp'): continue
        t = open(os.path.join(dp, f), errors='replace').read()
        ms = list(FUNC.finditer(t))
        for i, m in enumerate(ms):
          n = m.group(1)
          if want is not None and n not in want: continue
          if want is None:
              a = re.search(r'([0-9A-F]{8})$', n)
              if not a or not (lo <= int(a.group(1), 16) < hi): continue
          out[n] = INSN.findall(t[m.end(): ms[i+1].start() if i+1 < len(ms) else len(t)])
    return out
ds2_root, di_root, lo, hi, *names = sys.argv[1:]
ds2 = bodies(ds2_root, set(names))
di = bodies(di_root, None, int(lo, 16), int(hi, 16))
for n in names:
    if n not in ds2: print(n, 'not in ds2'); continue
    a = ds2[n]
    best = sorted(((difflib.SequenceMatcher(None, a, b, autojunk=False).ratio(), k, len(b)) for k, b in di.items()
                   if 0.5 < len(b) / max(1, len(a)) < 2), reverse=True)[:3]
    print(n, len(a), [(round(r, 3), k, l) for r, k, l in best])
