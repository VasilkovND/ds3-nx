#!/bin/bash
# The deko3d shader library and DKSH cache, from your own copy: the shader containers the game created on the console
# (src/ds3_shader_dump.cpp writes them to shaders_dump/ beside the NRO; copy that folder to ds3/out/shaders_dump/),
# translated ahead of time (shaders/build_library.sh) and compiled for the console's GPU, so the game never compiles a
# shader while it runs. Nothing here is committed.
#
#   ds3/tools/shaders.sh        -> ds3/out/shaders/ds3_shaders.dslib and shadercache_dksh.bin (copy beside the NRO)
#
# Needs the Vulkan SDK's dxc and spirv-val (VULKAN_BIN, see shaders/README.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/ds3/out"
DUMP="$OUT/shaders_dump"
[ -d "$DUMP" ] || { echo "$DUMP not found: copy shaders_dump/ from beside the NRO on the SD card"; exit 1; }
# The variants the console compiled in the background in earlier sessions (tools/dksh_variants.py) go in the cache too.
VARIANTS="$ROOT/ds3/dksh_variants.txt"
[ -f "$VARIANTS" ] || : > "$VARIANTS"
DKSH=1 VARIANTES="$VARIANTS" "$ROOT/shaders/build_library.sh" "$DUMP" "$OUT/shaders"
ls -la "$OUT/shaders/ds3_shaders.dslib" "$OUT/shaders/shadercache_dksh.bin"
