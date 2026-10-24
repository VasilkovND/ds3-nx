#!/bin/bash
# Generate the recompiled C++ of Dead Space 3 (Title Update 1) from your own executable, and its memory image. Nothing
# here is committed: the output is ds3/out/codegen/.
#
#   ds3/tools/codegen.sh <folder with default.xex(p) and deadspace_game_f.xex(p)>   (tools/stage_sd.py's tu/ folder)
#   ds3/tools/codegen.sh --host                                                         (only build the host rexglue tool)
#
# Dead Space 3's disc has two executables: default.xex, the title, and deadspace_game_f.xex, the game itself, a DLL
# default.xex loads with XexLoadImage (ds3/PORTING.md). Both are recompiled, each with its title update patch.
#
# Steps (each skipped when already done):
#   1. ReXGlue SDK v0.10.0 for the host, built with the Mac's clang (only its `rexglue` tool is used), with two patches:
#      Hell's Gate's (https://github.com/florinp93/hells-gate-recomp, fetched at the tested commit, never copied: it has
#      no license file) for its codegen fixes (lvebx/lvehx/lvewx, vmaddcfp128, vector pack operand order, function
#      pointers in data sections), the same host tool Dante's Inferno's port uses; and ours (host-patches/: dump the
#      loaded image). The build tree is removed afterwards: only the tool and its libraries are kept (~25 MB).
#   2. The manifest: default.xex as the entry point (codegen/names_default.toml) and deadspace_game_f.xex as a
#      [[modules]] entry (codegen/names.toml): hints for branch targets the analysis does not find, and names for the
#      functions the Switch code hooks.
#   3. rexglue codegen. The build stops if the analysis reports a branch to an address that is no function entry: it
#      prints the names.toml entries to add (unresolved_target_<address>; ADD_UNRESOLVED=1 adds them), then run again.
#      The traps left (branches to entries the codegen still does not tie up) become gotos and tail calls
#      (patch_branch_traps.py); any it cannot place stops the build.
#   4. Ours, for the CPU (from gears2-nx and dantes-inferno-nx): patch_stack_access.py and patch_inline_calls.py, and
#      with LOCALREGS=1 (the default; 0 turns it off) patch_local_regs.py (the guest registers in host locals,
#      gow2/CPU_OPTIMIZATION.md in gears2-nx). PLR_CHECK=1 makes its check build.
set -euo pipefail

if [ $# -ne 1 ]; then
  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/ds3/out"
CG="$OUT/codegen"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
LOCALREGS="${LOCALREGS:-1}"
EXE="deadspace_game_f"

HELLS_GATE_COMMIT="3ce3993b0f03c77353f8f10fdcd982160c9a2d18"   # 2026-10-16, v0.8.4-beta
REXGLUE_TAG="v0.10.0"
# The executables this build is made and tested from: disc 1's default.xex + TU1's disc001/default.xexp, disc 1's
# deadspace_game_f.xex + TU1's disc001/deadspace_game_f.xexp, SHA-256 (ds3/PORTING.md, "Files").
TESTED="52dd44d8bf56f80ed726e27d41f4a2736e029f58cfa4de07faa6fd4a3140353b+d364efd4484948b3fbfd7a55733efaa5ec24e14d2dd09b6bc6374f49679d533d+74776c594043eeffd9ee5eea70201ff9e24dd63b490b9239719d22929a2dbb39+bc4337a54a1d8f4427afc6d044b0ed444a9be9548fe7e35e87809ad8bc5e0886"
#   Russia disc 1 (Media ID 4DC6CE0C) + its TU1

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
mkdir -p "$OUT"

# 1. Host ReXGlue
HOST="$OUT/rexglue-host"
BIN="$HOST/out/$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/mac/')-$(uname -m | sed 's/x86_64/amd64/')"
REXGLUE="$BIN/rexglue"
if [ ! -x "$REXGLUE" ]; then
  HG="$OUT/hells-gate-recomp"
  if [ "$(git -C "$HG" rev-parse HEAD 2>/dev/null)" != "$HELLS_GATE_COMMIT" ]; then
    echo "== Hell's Gate $HELLS_GATE_COMMIT (its ReXGlue patch)"
    rm -rf "$HG"
    mkdir -p "$HG"
    git -C "$HG" init -q
    git -C "$HG" remote add origin https://github.com/florinp93/hells-gate-recomp.git
    git -C "$HG" fetch -q --depth 1 origin "$HELLS_GATE_COMMIT"
    git -C "$HG" checkout -q FETCH_HEAD
  fi
  SRC="$OUT/rexglue-host-src"
  if [ ! -d "$SRC/.git" ]; then
    echo "== ReXGlue $REXGLUE_TAG (host)"
    git clone -q --branch "$REXGLUE_TAG" --depth 1 https://github.com/rexglue/rexglue-sdk.git "$SRC"
    git -C "$SRC" submodule update --init --recursive --depth 1 -j 8
  fi
  for p in "$HG/patches/sdk/rexglue-sdk-v0.10.0.patch" "$ROOT"/ds3/host-patches/*.patch; do
    if git -C "$SRC" apply --reverse --check "$p" 2>/dev/null; then continue; fi
    echo "== host patch: $(basename "$p")"
    git -C "$SRC" apply "$p"
  done
  echo "== build rexglue (host, a few minutes)"
  cmake -S "$SRC" -B "$SRC/out/host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_USE_VULKAN=ON > "$OUT/rexglue-host.log"
  cmake --build "$SRC/out/host" --target rexglue -j "$JOBS" >> "$OUT/rexglue-host.log"
  BUILT="$SRC/out/$(basename "$BIN")"
  [ -x "$BUILT/rexglue" ] || { echo "rexglue was not built (see $OUT/rexglue-host.log)"; exit 1; }
  # Keep the tool and the libraries beside it; the sources and the build tree (~1 GB) go.
  rm -rf "$HOST"
  mkdir -p "$(dirname "$BIN")"
  mv "$BUILT" "$BIN"
  rm -rf "$SRC" "$HG"
  if [ "$(uname -s)" = Darwin ]; then
    # Its libraries sit beside it: find them there, so the folder can move.
    for p in $(otool -l "$REXGLUE" | awk '/LC_RPATH/{getline; getline; print $2}'); do
      install_name_tool -rpath "$p" @executable_path "$REXGLUE" 2>/dev/null || true
    done
    codesign -f -s - "$REXGLUE" 2>/dev/null || true
  fi
fi
[ -x "$REXGLUE" ] || { echo "rexglue was not built (see $OUT/rexglue-host.log)"; exit 1; }
if [ "$1" = --host ]; then
  echo "rexglue: $REXGLUE"
  exit 0
fi

GAME=$(cd "$1" && pwd)
for f in default.xex default.xexp $EXE.xex $EXE.xexp; do
  [ -f "$GAME/$f" ] || { echo "$GAME/$f not found (tools/stage_sd.py makes the folder, Title Update 1 included)"; exit 1; }
done
if ! printf '%s\n' "$TESTED" | grep -qx "$(sha "$GAME/default.xex")+$(sha "$GAME/default.xexp")+$(sha "$GAME/$EXE.xex")+$(sha "$GAME/$EXE.xexp")"; then
  echo "warning: the executables / title update are not ones this build was tested with (see ds3/PORTING.md); trying anyway"
fi

# 2. Project folder: the executables (copied: codegen refuses a link out of its game root) and the manifest. Two binaries:
#    default.xex (the title, generated/default) and deadspace_game_f.xex (a DLL it loads, generated/game, a [[modules]]
#    entry: the SDK links it into the NRO and registers it under the name XexLoadImage asks for).
mkdir -p "$CG/game"
for f in default.xex default.xexp $EXE.xex $EXE.xexp; do
  cmp -s "$GAME/$f" "$CG/game/$f" || cp "$GAME/$f" "$CG/game/$f"
done
python3 - "$ROOT/ds3/codegen/names_default.toml" "$ROOT/ds3/codegen/names.toml" "$CG/ds3_manifest.toml" "$EXE" <<'PY'
import re, sys
title, game, out, exe = open(sys.argv[1]).read(), open(sys.argv[2]).read(), sys.argv[3], sys.argv[4]
# names.toml is written for the game module as [entrypoint.functions.X]; under [[modules]] its tables are
# [modules.functions.X] (they belong to the last [[modules]] entry).
game = re.sub(r'^\[entrypoint\.functions\.', '[modules.functions.', game, flags=re.M)
new = ('[project]\nname = "ds3"\nsdk_version = "0.10.0"\ngame_root = "game"\n\n'
       '[entrypoint]\nfile_path = "game/default.xex"\nout_directory_path = "generated/default"\nincludes = []\n\n'
       + title.rstrip() + '\n\n'
       f'[[modules]]\nfile_path = "game/{exe}.xex"\nout_directory_path = "generated/game"\nguest_path = "{exe}.xex"\n'
       'includes = []\n\n' + game)
try:
    if open(out).read() == new:
        sys.exit(0)
except OSError:
    pass
open(out, 'w').write(new)
PY
# A name the SDK already defines as __imp__<name> (its export stubs) would replace the game's function with that stub.
for n in $(sed -n 's/^name = "\(.*\)"$/\1/p' "$ROOT/ds3/codegen/names.toml" "$ROOT/ds3/codegen/names_default.toml"); do
  if grep -rqE "__imp__$n\b" "$ROOT/sdk/src" "$ROOT/sdk/include"; then
    echo "codegen/names*.toml: the SDK already defines __imp__$n; pick another name"; exit 1
  fi
done

# 3. Codegen (re-run only when an input changed) and 4. the passes.
STAMP="$CG/inputs.sha"
INPUTS=$( (cat "$CG"/game/* "$CG/ds3_manifest.toml" "$REXGLUE" \
  "$ROOT"/ds3/tools/patch_branch_traps.py "$ROOT"/ds3/tools/patch_stack_access.py "$ROOT"/ds3/tools/patch_inline_calls.py "$ROOT"/ds3/tools/patch_local_regs.py
  echo "localregs=$LOCALREGS check=${PLR_CHECK:-0}"
  # The generated functions the sources name (hooks, native callers) change what the passes do: those names.
  NAMES=$(sed -n 's/^name = "\(.*\)"$/\1/p' "$CG/ds3_manifest.toml" | paste -sd'|' -)
  cat $(find "$ROOT/ds3/src" "$ROOT/sdk/src" "$ROOT/sdk/include" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
    -o -name '*.cc' -o -name '*.inc' \) -not -path '*/codegen/*' | sort) | grep -oE "\b(sub_[0-9A-F]{8}|${NAMES:-sub_})\b" | sort -u) \
  | shasum -a 256 | cut -d' ' -f1)
if [ "$(cat "$STAMP" 2>/dev/null)" != "$INPUTS" ] || [ ! -f "$CG/generated/default/sources.cmake" ] ||
   [ ! -f "$CG/generated/game/sources.cmake" ]; then
  # Generated and patched in a staging folder, then only the files whose text changed are copied over generated/: the
  # others keep their dates, so after a small change (a name in names.toml) Ninja rebuilds a few units.
  echo "== codegen"
  ST="$CG/staging"
  rm -rf "$ST"
  mkdir -p "$ST"
  cp "$CG/ds3_manifest.toml" "$ST/"
  ln -s ../game "$ST/game"
  (cd "$ST" && REX_DUMP_IMAGE="$CG/image.bin" "$REXGLUE" --force codegen ds3_manifest.toml > "$CG/codegen.log" 2>&1) || {
    tail -20 "$CG/codegen.log"; exit 1; }
  # Branches into no function: each one becomes a trap in the generated code. A target that is no function entry yet
  # is named in names.toml (the game, 0x88xxxxxx) or names_default.toml (the title, 0x82xxxxxx) (ADD_UNRESOLVED=1 does
  # it) and the code generated again; the traps left, to targets that are entries already, are what
  # patch_branch_traps.py turns into gotos and tail calls (below).
  UNRESOLVED=$(grep -oE 'Unresolved (b target|function|conditional branch to) 0x[0-9A-Fa-f]{8}' "$CG/codegen.log" | grep -oE '0x[0-9A-Fa-f]{8}' | sort -u || true)
  : > "$CG/unresolved.toml"
  NEW=0
  for a in $UNRESOLVED; do
    A=$(echo "${a#0x}" | tr '[:lower:]' '[:upper:]')
    case "$A" in 82*) NAMES_FILE="$ROOT/ds3/codegen/names_default.toml" ;; *) NAMES_FILE="$ROOT/ds3/codegen/names.toml" ;; esac
    grep -q "^\[entrypoint.functions.0x$A\]" "$NAMES_FILE" && continue
    NEW=$((NEW + 1))
    printf '# %s\n[entrypoint.functions.0x%s]\nname = "unresolved_target_%s"\n' "$(basename "$NAMES_FILE")" "$A" "$A" >> "$CG/unresolved.toml"
    if [ "${ADD_UNRESOLVED:-0}" = 1 ]; then
      printf '\n[entrypoint.functions.0x%s]\nname = "unresolved_target_%s"\n' "$A" "$A" >> "$NAMES_FILE"
    fi
  done
  if [ "$NEW" != 0 ]; then
    echo "codegen: $NEW branch targets that are no function (see $CG/codegen.log, $CG/unresolved.toml)."
    if [ "${ADD_UNRESOLVED:-0}" = 1 ]; then
      echo "added to ds3/codegen/names*.toml: run again (new ones can appear)"
    else
      echo "add them to ds3/codegen/names.toml / names_default.toml (ADD_UNRESOLVED=1 does it) and run again"
    fi
    exit 1
  fi
  for M in default game; do
    echo "== $M: unresolved-branch traps"
    (cd "$ST" && python3 "$ROOT/ds3/tools/patch_branch_traps.py" generated/$M/*.cpp) > "$CG/patch_branch_traps.$M.log" || {
      cat "$CG/patch_branch_traps.$M.log"; exit 1; }
    head -1 "$CG/patch_branch_traps.$M.log"
    echo "== $M: generated-code passes for the CPU"
    # The guest stack's accesses without volatile (make_nx_pch.py, point 4).
    (cd "$ST" && python3 "$ROOT/ds3/tools/patch_stack_access.py" generated/$M/*.cpp) > "$CG/patch_stack_access.$M.log"
    tail -1 "$CG/patch_stack_access.$M.log"
    # Calls the compiler may inline: same-file functions no hook replaces, and the register save/restore helpers.
    (cd "$ST" && python3 "$ROOT/ds3/tools/patch_inline_calls.py" "$ROOT/ds3/src" "$ROOT/sdk/src" "$ROOT/sdk/include" \
      -- generated/$M/ds3_recomp.*.cpp) > "$CG/patch_inline_calls.$M.log"
    tail -1 "$CG/patch_inline_calls.$M.log" | cut -c1-160
    # The guest registers in host locals inside each function, with ctx exact only where a call or a return needs it.
    if [ "$LOCALREGS" = 1 ]; then
      (cd "$ST" && PLR_MAP="$CG/patch_local_regs.$M.check.map" python3 "$ROOT/ds3/tools/patch_local_regs.py" \
        generated/$M/ds3_inline_helpers.h "$ROOT/ds3/src" "$ROOT/sdk/src" "$ROOT/sdk/include" \
        -- generated/$M/ds3_recomp.*.cpp) > "$CG/patch_local_regs.$M.log" 2>&1 || {
        tail -20 "$CG/patch_local_regs.$M.log"; exit 1; }
      tail -1 "$CG/patch_local_regs.$M.log"
    fi
  done
  for M in default game; do
    python3 - "$ST/generated/$M" "$CG/generated/$M" <<'PY'
import filecmp, os, shutil, sys
new, old = sys.argv[1:]
os.makedirs(old, exist_ok=True)
names = set(os.listdir(new))
changed = 0
for n in sorted(names):
    a, b = os.path.join(new, n), os.path.join(old, n)
    if not os.path.isfile(b) or not filecmp.cmp(a, b, shallow=False):
        shutil.copy2(a, b)
        changed += 1
for n in set(os.listdir(old)) - names:
    os.remove(os.path.join(old, n))
print(f"generated/{os.path.basename(old)}: {changed} of {len(names)} files changed")
PY
  done
  rm -rf "$ST"
  echo "$INPUTS" > "$STAMP"
fi
for M in default game; do
  echo "codegen: $(ls "$CG"/generated/$M/*.cpp | wc -l | tr -d ' ') files in $CG/generated/$M"
done
for f in default.xex default.xexp $EXE.xex $EXE.xexp; do echo "$f: $(sha "$GAME/$f")"; done
