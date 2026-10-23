#!/bin/bash
# Build ds3-nx.nro with devkitPro's toolchain on a Mac (Apple Silicon or Intel); on Linux the same steps with
# your distribution's packages instead of Homebrew (not tested).
#
# Once:
#   1. devkitPro: install the pkg from https://github.com/devkitPro/pacman/releases (or
#      https://devkitpro.org/wiki/Getting_Started), then:  sudo dkp-pacman -S switch-dev deko3d
#   2. Homebrew tools:  brew install cmake ninja python git
#   3. The Vulkan SDK (https://vulkan.lunarg.com/, for dxc and spirv-val: the shaders)
#
# Then, from the repository root, with the tu/ folder tools/stage_sd.py made (deadspace_game_f.xex and Title Update 1's
# deadspace_game_f.xexp):
#   ds3/tools/build_mac.sh <tu folder>
# It generates the code (tools/codegen.sh), the shaders (tools/shaders.sh, once the console has dumped them into
# shaders_dump/ beside the NRO and you copied that folder to ds3/out/shaders_dump/) and the NRO:
#   ds3/out/sw/ds3.nro                    -> sdmc:/switch/ds3-nx/ds3-nx.nro
#   ds3/out/shaders/ds3_shaders.dslib     -> beside it
#   ds3/out/shaders/shadercache_dksh.bin  -> beside it
# SD=<folder> also copies those three there when they changed (for example SD=for_sd/switch/ds3-nx).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ $# -eq 1 ] || { sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
GAME="$1"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$PATH"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"

[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] || { echo "devkitA64 not found in $DEVKITPRO (step 1)"; exit 1; }
[ -f "$DEVKITPRO/libnx/lib/libdeko3d.a" ] || { echo "deko3d not installed: sudo dkp-pacman -S deko3d"; exit 1; }

cd "$ROOT"
if [ ! -f sdk/CMakeLists.txt ]; then
  echo "== sdk submodule"
  git submodule update --init sdk
fi
if [ ! -f sdk/thirdparty/uam/uam_api.h ]; then
  echo "== Switch third-party sources"
  (cd sdk && python3 tools/switch_setup.py)
fi

# Fixes to the SDK that the game needs and that are not upstream yet (ds3/sdk-patches/), in order. Several touch the
# same files, so a patch already applied cannot always be recognised by reverse-applying it alone once a later one is
# on top: what was applied is recorded in ds3/out/sdk-patches.applied (name and hash). A patch not recorded is
# applied, or recorded if it is already there (reverse-applies).
APPLIED="$ROOT/ds3/out/sdk-patches.applied"
mkdir -p "$ROOT/ds3/out"
touch "$APPLIED"
for p in "$ROOT"/ds3/sdk-patches/*.patch; do
  [ -f "$p" ] || continue
  entry="$(basename "$p") $(shasum -a 256 "$p" | cut -d' ' -f1)"
  grep -qxF "$entry" "$APPLIED" && continue
  if git -C sdk apply --check "$p" 2>/dev/null; then
    echo "== sdk patch: $(basename "$p")"
    git -C sdk apply "$p"
  elif git -C sdk apply --reverse --check "$p" 2>/dev/null; then
    echo "== sdk patch already applied: $(basename "$p")"
  else
    echo "sdk patch $(basename "$p") does not apply to sdk/ (changed by hand, or an older version of it applied?)"
    exit 1
  fi
  echo "$entry" >> "$APPLIED"
done

# sdk/tools/switch_setup.py copied the SDK's Switch overlay (sdk/switch/overlay/: FFmpeg's configuration) into
# sdk/thirdparty/ before the patches above; a patch that changes the overlay (0009, the VP6 decoder) is copied again.
(cd sdk/switch/overlay && find . -type f) | while read -r f; do
  cmp -s "sdk/switch/overlay/$f" "sdk/thirdparty/$f" || { cp "sdk/switch/overlay/$f" "sdk/thirdparty/$f"; echo "== overlay: $f"; }
done

"$ROOT/ds3/tools/codegen.sh" "$GAME"
if [ -d ds3/out/shaders_dump ] && { [ ! -f ds3/out/shaders/shadercache_dksh.bin ] || [ "${SHADERS:-0}" = 1 ]; }; then
  "$ROOT/ds3/tools/shaders.sh"
fi

# Configure only the first time (or with RECONFIGURE=1): afterwards Ninja re-runs CMake by itself when a CMake file
# changes.
if [ ! -f ds3/out/sw/build.ninja ] || [ "${RECONFIGURE:-0}" = 1 ]; then
  echo "== configure"
  cmake -S ds3 -B ds3/out/sw -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/switch/cmake/switch-devkitA64.cmake" \
    -DREXSDK_DIR="$ROOT/sdk" -DDS3_CODEGEN_DIR="$ROOT/ds3/out/codegen" -DREXGLUE_SWITCH_DEKO3D=ON
fi

# The mirror of the generated tree (tools/prepare_tree.py) is made at configure time: refreshed here as well, for files
# the codegen passes add (ds3_inline_helpers.h) and changes to the Switch PCH (tools/make_nx_pch.py).
python3 ds3/tools/prepare_tree.py ds3/out/codegen/generated/default ds3/out/sw/generated_nx | tail -1

echo "== build ($JOBS jobs)"
cmake --build ds3/out/sw -j "$JOBS"
ls -la ds3/out/sw/ds3.nro
ls -la ds3/out/shaders/ds3_shaders.dslib ds3/out/shaders/shadercache_dksh.bin 2>/dev/null ||
  echo "no shader library yet: the first start on the console dumps the game's shaders (INSTALL.md)"
# Every NRO's ELF is kept, named by the NRO's SHA-256 (first 16 digits): the console's rex_perfil.log and rex_crash.log
# give image offsets, and only the ELF of the very NRO that ran turns them into names.
mkdir -p ds3/out/elf
cp ds3/out/sw/ds3 "ds3/out/elf/$(shasum -a 256 ds3/out/sw/ds3.nro | cut -c1-16).elf"
if [ -n "${SD:-}" ]; then
  mkdir -p "$SD"
  for f in sw/ds3.nro:ds3-nx.nro shaders/ds3_shaders.dslib:ds3_shaders.dslib \
           shaders/shadercache_dksh.bin:shadercache_dksh.bin; do
    [ -f "ds3/out/${f%%:*}" ] || continue
    cmp -s "ds3/out/${f%%:*}" "$SD/${f##*:}" || { cp "ds3/out/${f%%:*}" "$SD/${f##*:}"; echo "== copied ${f##*:} to $SD"; }
  done
fi
