# ds3-nx

Dead Space 3 (Xbox 360, Title Update 1) on the Nintendo Switch, natively. It's not an emulator: the game's PowerPC code
is statically recompiled to C++ with [ReXGlue](https://github.com/rexglue/rexglue-sdk), built for the Switch as a
homebrew NRO, and frames are drawn by a native deko3d renderer from the game's translated shaders. It follows
[ds2-nx](https://github.com/VasilkovND/ds2-nx), the Dead Space 2 port (same studio, engine family and EA libraries),
and dantes-inferno-nx, whose from-scratch codegen it uses.

> [!NOTE]
> You need your own copy of the game (both discs and Title Update 1). This repository contains no game data or game
> code: the code, the shaders and the NRO are all made on your computer from your copy.

Status: **1.0.1, stable and playable.** At stock clocks the game runs at 540p and 30 fps; with a mild overclock
it runs at 720p, also at 30 fps. Tested on a console in handheld mode. Details and every problem met:
[ds3/PORTING.md](ds3/PORTING.md).

## Controls and options

| Combination | What it does |
| --- | --- |
| **ZL + ZR + Minus** | Opens the options menu over the game: **Up / Down** choose a row (resolution, vibration), **Left / Right** change vibration strength, **A** saves, **B** closes. While it is open the game does not see the controller |

**Resolution.** Two modes, chosen in the menu and saved in `ds3_resolucion.txt` beside the NRO; the game sets its
scene size when it starts, so a new choice applies **at the next start**:

| Mode | For | Scene |
| --- | --- | --- |
| **720p** | overclocked consoles (default when the CPU runs at 1500 MHz or more) | 1152x720, the console original's own size |
| **540p** | stock clocks (default otherwise) | 864x540, upscaled to the screen |

In handheld mode the port asks Horizon for its handheld performance profile with the GPU at 460.8 MHz (one that
commercial games use, not an overclock; CPU and memory clocks are not raised). Docked, or on an overclocked console (CPU
above 1020 MHz at start), it asks for nothing. `ds3_gpu_portatil_mhz = 0` in `ds3.toml` leaves the stock 307.2 MHz.

**`ds3.toml`** beside the NRO sets the port's options (`name = value` lines). For bug reports:
`ds3_atajos_depuracion = true` turns on the debug shortcuts (ZL + ZR held with the D-pad: capture, frame dump and draw
trace, draw-skipping modes; Up toggles the FPS counter, the only one on by default), and an empty `telemetria.flag`
beside the NRO turns on the automatic captures, frame dumps, frame traces and the CPU stack profile (they cost frames).

## Layout

| Folder | Contents |
| --- | --- |
| `ds3/` | The game project: CMake, Switch glue (`src/`: app, disc swap, GPU ring reader and renderer glue, shader dump and library, audio and CPU hooks), build scripts (`tools/`), names and hints for the codegen (`codegen/`), our SDK patches (`sdk-patches/`) and host-tool patches (`host-patches/`) |
| `shaders/` | Shader toolchain: Xenos microcode -> HLSL -> SPIR-V -> shader library and DKSH cache ([shaders/README.md](shaders/README.md)) |
| `sdk/` | **Submodule** -> [`rexglue-sdk-nx`](https://github.com/nx-mod/rexglue-sdk-nx) (`switch` branch): the ReXGlue SDK + Horizon (Switch) layer + deko3d renderer. Our changes to it live as patches in `ds3/sdk-patches/`, applied by the build script |
| `tools/switch/cmake/` | `switch-devkitA64.cmake`, the CMake toolchain file for devkitA64 |

## Build

devkitA64, libnx and deko3d from [devkitPro](https://devkitpro.org) (`sudo dkp-pacman -S switch-dev deko3d`), CMake,
Ninja, Python 3, clang, 7-Zip (`brew install sevenzip`, to read a `.7z` disc image) and the
[Vulkan SDK](https://vulkan.lunarg.com) (`dxc`, `spirv-val`). On a Mac:

```sh
python3 ds3/tools/stage_sd.py --disc1 "Dead Space 3 (Disc 1).iso" --disc2 "Dead Space 3 (Disc 2).iso" \
    --tu <Title Update 1 package, or a folder/.zip with it> -o sd/ds3-nx
ds3/tools/build_mac.sh sd/ds3-nx/tu
```

`build_mac.sh` runs, in order:

1. `ds3/tools/codegen.sh`: builds ReXGlue v0.10.0's `rexglue` tool for the Mac (with the codegen fixes of
   [Hell's Gate](https://github.com/florinp93/hells-gate-recomp)'s SDK patch, fetched at a pinned commit), generates the
   C++ from your `deadspace_game_f.xex` + `deadspace_game_f.xexp` (~54,500 functions) with `ds3/codegen/names.toml`,
   turns the branches the codegen left as traps into gotos and calls, and runs the CPU passes (the guest registers in
   host locals, `LOCALREGS=1` by default; `LOCALREGS=0` builds without it).
2. `ds3/tools/shaders.sh`, once the console has dumped the game's shaders: `ds3_shaders.dslib` and
   `shadercache_dksh.bin`.
3. The NRO: `ds3/out/sw/ds3.nro`.

## SD card

```
sdmc:/switch/ds3-nx/
    ds3-nx.nro               ds3/out/sw/ds3.nro
    ds3_shaders.dslib        ds3/out/shaders/ (once made)
    shadercache_dksh.bin     ds3/out/shaders/ (once made)
    tu/                      Disc 1's executables + Title Update 1's patches, by stage_sd.py
    disc1/                   the extracted Disc 1
    disc2/                   the extracted Disc 2 (the game asks for it at chapter 5)
```

Start it in title takeover (hold R while starting any game, then the Homebrew Menu): applet mode does not have the
memory. The program creates `cache/`, `logs/`, `shaders_dump/` and its saves.

## Legal

Unofficial fan port with no affiliation to Electronic Arts or Visceral Games. "Dead Space" is a trademark of Electronic
Arts. You provide your own legally obtained copy of the game. No prebuilt files are distributed: the NRO contains code
recompiled from the game and the shader library the game's own shaders, so both are made on your own computer from your
copy. Running homebrew requires custom firmware, which violates Nintendo's terms and can get a console banned.

**Official source.** The only official source of this project is this repository,
[github.com/VasilkovND/ds3-nx](https://github.com/VasilkovND/ds3-nx): its source code and its releases. I do not
publish ready-made NRO or NSP files, and never anything with game data. Builds, NSP/XCI packages or archives offered on
other sites (ROM sites, file hosts, forums, chats) are not made, endorsed or checked by me, may be modified, and are
not covered by this project; distributing the game's files that way infringes its rights holders' copyright and is
solely the responsibility of whoever does it.

Source code is under the GPL-3.0 license (see [LICENSE](LICENSE)). The SDK changes are under the SDK's BSD-3-Clause
license, and the shader translator changes under MIT.

## Credits

- **[bifftannen1](https://github.com/bifftannen1/Dead-Space-2-Recomp)**: Dead-Space-2-Recomp, whose PDB-named code
  gave this port its function names (matched by body) and whose disc swap it adapts.
- **[ds2-nx](https://github.com/VasilkovND/ds2-nx), [dantes-inferno-nx](https://github.com/VasilkovND/dantes-inferno-nx)
  and [gears2-nx](https://github.com/VasilkovND/gears2-nx)**: the earlier ports whose code, CPU passes and SDK patches
  (partial submits, real occlusion queries) this one reuses.
- **[florinp93](https://github.com/florinp93/hells-gate-recomp)**: Hell's Gate, the PC port of Dante's Inferno, whose
  ReXGlue codegen fixes are fetched at build time.
- **[nx-mod](https://github.com/nx-mod/rexglue-sdk-nx)**: rexglue-sdk-nx, the Switch fork of the SDK, and its Halo 3
  port, where the PM4 ring reader and the universal deko3d renderer started.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)**: NFSMW-NX: the Switch layer, deko3d renderer and shader tools.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)**: the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)**: XenosRecomp.
- **[centollOS](https://github.com/centollOS/SwitchWakerHD)**: the uam shader compiler patches.
- **[devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx)**: devkitA64, libnx
  and deko3d. **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**: the custom firmware.
- The other libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
