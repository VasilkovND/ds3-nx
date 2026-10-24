# Porting notes: Dead Space 3

Dead Space 3 (Xbox 360, 2013, Visceral Games / EA, Title Update 1) on the Nintendo Switch, as a homebrew NRO. This file
records the plan, what is known before the first run, and (as they come) every problem met and what fixed it. The
reference for everything it does not repeat is ds2-nx's `deadspace2/PORTING.md` (Dead Space 2: same studio, engine
family and EA libraries) and dantes-inferno-nx's `dante/PORTING.md` (the from-scratch codegen this port copies).

## Status

| Step | State |
| --- | --- |
| Repository: SDK submodule (rexglue-sdk-nx `f961303`), SDK patches (dantes-inferno-nx 0001-0012 + ds2-nx's guest clock), build scripts, ring reader and renderer glue, disc swap | done |
| Host `rexglue` (ReXGlue v0.10.0 + Hell's Gate's codegen fixes + `host-patches/0001-dump-image`) | builds (`tools/codegen.sh --host`, ~4 min, 22 MB kept) |
| Disc 1 + TU1 read (`tools/stage_sd.py`, also straight from a `.7z`) | done, Russia edition (Media ID 4DC6CE0C) |
| Codegen from `deadspace_game_f.xex` + TU1 | done: 54,556 functions, 200 units, 210 MB of C++, 0 traps left |
| Names from Dead Space 2 (`tools/match_funcs.py`) | 5,460 exact matches; Create*Shader and the mixer named |
| NRO links | done: `ds3.nro` 63 MB (882 build steps, ~15 min on 10 M1 Pro cores), shader dump and NEON mixer hooks in |
| On the console (`/switch/ds3-nx/`: NRO, `tu/`, `disc1/`, over FTP) | first run pending |
| Boots to the first frame | - |
| Disc 2 (`stage_sd.py --disc2`), the swap at chapter 5 | needs the user's Disc 2 |
| Shaders dumped on the console, library and DKSH cache | - |
| CPU passes: guest registers in host locals (`LOCALREGS=1`) | after the game runs as it is |

## What is known about the game

| | |
| --- | --- |
| Title ID | `4541099D` |
| Discs | two; disc 1 holds chapters 0-4, disc 2 chapters 5-10 (`disc1_info.txt`) |
| Executables on the disc | `default.xex` (725 KB, a launcher) and `deadspace_game_f.xex` (17 MB, the game) |
| Title Update 1 | one package per disc edition (Media IDs 0992A619, 0CC756B7, 4DC6CE0C, 7139B889), each with `disc001/` and `disc002/` patches for both executables |
| Image (TU1, 0.0.1.2) | `0x88000000`-`0x89200000`, code `0x88250000` + 12.1 MB (not `0x82000000` like Dead Space 2) |
| Videos | VP6 (`VideoDecoder_Vp6`, `attract.vp6`), software-decoded like Dead Space 2's and Dante's Inferno's |
| Shaders | not in the executable as raw containers (to check on the first run: dumped at creation, as in Dead Space 2) |

**Only the game is recompiled.** `default.xex` is a launcher (Kinect speech data `nuisp*`, avatar assets, the
`$SystemUpdate` folder sit beside it on the disc); the runtime loads `game:\deadspace_game_f.xex` directly
(`src/ds3_app.h`). If the game expects launch data from the launcher (`XamLoaderGetLaunchData`), the first log says so
and the launcher's work moves into the app.

### Files (SHA-256)

| File | Edition | SHA-256 |
| --- | --- | --- |
| Disc 1 `deadspace_game_f.xex` (v0.0.0.2) | Russia, 4DC6CE0C | `74776c594043eeffd9ee5eea70201ff9e24dd63b490b9239719d22929a2dbb39` |
| Disc 1 `default.xex` | Russia, 4DC6CE0C | `52dd44d8bf56f80ed726e27d41f4a2736e029f58cfa4de07faa6fd4a3140353b` |
| TU1 `disc001/deadspace_game_f.xexp` (`TU_12K22CT_0000008000000.0000000000082`) | 4DC6CE0C | `bc4337a54a1d8f4427afc6d044b0ed444a9be9548fe7e35e87809ad8bc5e0886` |
| TU1 `disc001/default.xexp` | 4DC6CE0C | `d364efd4484948b3fbfd7a55733efaa5ec24e14d2dd09b6bc6374f49679d533d` |

`tools/codegen.sh` (`TESTED`) and `tools/stage_sd.py` (`TESTED`) check these. Other editions are tried with a warning;
whether their TU1 images generate the same code as the Russian one (Dead Space 2's World and Russian TU2 did, byte for
byte) is not known.

## 1. Code

- **No recompiled tree exists for Dead Space 3** (no PC port as of October 2026), so the codegen runs here on the
  player's executable, as for Dante's Inferno and UFC 3. Dead Space 2's checked-in tree (`src_tu2`) cannot be reused:
  the generated C++ is tied to one executable's addresses and functions.
- **Host tool**: ReXGlue v0.10.0 with Hell's Gate's SDK patch (fetched, not copied: no license file), the same
  `rexglue` dantes-inferno-nx uses. Its data-section function-pointer scan finds 9,613 functions DS3 only reaches
  through pointers.
- **Unresolved branches.** The first codegen reported 535 branches into no function (167 addresses). Declared as
  function entries in `codegen/names.toml` (203 after three rounds, `ADD_UNRESOLVED=1`), all are resolved except 46:
  conditional branches back to the *first instruction of their own function* (a loop whose head is the entry). ReXGlue
  classifies those as calls to a function and, finding no call target, writes a trap, although the label `loc_<entry>`
  is right there. `tools/patch_branch_traps.py` turns each into `goto loc_<entry>;` (and would turn a branch to another
  function's entry into a tail call). 0 traps remain.
- **Gotos into another function.** The first build stopped on `label 'loc_88AD50A8' used but not defined`:
  `sub_88AD4FAC` branches to 0x88AD50A8, inside its own address range, where another function (`sub_88AD50A8`)
  starts, so the codegen wrote a `goto` but emitted the label only in that other function. 20 such gotos; the same
  pass makes each a tail call to the function there (17), and the 3 to 0x88760F6C, where no function started, were
  fixed by naming it in `names.toml`.
- **Names**: every function is `sub_XXXXXXXX`; the ones the Switch code hooks get names in `codegen/names.toml`, found
  by matching bodies against Dead Space 2's PDB-named tree (`tools/match_funcs.py`: 5,460 exact matches: rw::audio,
  EARS, EA::Thread, XDK D3D, ...). `tools/fuzzy_find.py` ranks near matches for the rest (EA::Thread::SetThreadPriority
  and rw::audio::core::Compressor1::Process only reach ~0.6: not named yet).
- **Hooks are compiled only when named** (`CMakeLists.txt`, `ds3_add_hook`): today the shader dump
  (`ds3_D3DDevice_Create{Vertex,Pixel}Shader`, 0x88A2D910 / 0x88A2D728) and the NEON mixer
  (`rw::audio::core::MixWithGain` / `CopyWithGain`, 0x886BE8E8 / 0x886B56C8).
- The Switch PCH is dantes-inferno-nx's `make_nx_pch.py` (GCC `DEFINE_REX_FUNC`, per-thread `mtmsrd`, stack accesses
  without volatile).
- **CPU passes** (gears2-nx `gow2/CPU_OPTIMIZATION.md`, dantes-inferno-nx `dante/CPU_OPTIMIZATION.md`):
  `patch_stack_access.py` and `patch_inline_calls.py` run always (229,096 stack accesses; 1,169 same-file calls and
  35,517 helper calls inlined). `patch_local_regs.py` (guest registers in host locals) waits for `LOCALREGS=1` until
  the game runs as it is: it must know every guest setjmp/longjmp site first (Dante's Inferno's save system had one;
  Dead Space 2 had four: Lua's protected call, libpng, JPEG errors).

## 2. Discs

Extracted folders, as in Dead Space 2 (the Switch cannot map a whole ISO): `disc1/`, `disc2/`, and `tu/` with disc 1's
executables and TU1's patches (`game:`). bigfile0.viv 3.6 GB and bigfile1.viv 3.8 GB on disc 1: FAT32 works.
`src/ds3_disc_swap.cpp` is ds2-nx's, with two changes: a disc is identified by `deadspace_game_f.xex`'s execution info,
and the `XamSwapDisc` import thunk is found in the executable's import table (xam ordinal 0xA28) instead of a fixed
address. To check with disc 2: that disc 2's `deadspace_game_f.xex` + its `disc002` patch load to the same image as
disc 1's (then one NRO serves both, as in Dead Space 2); if not, the game must keep running disc 1's code after the swap.

`stage_sd.py` reads a disc from the ISO or from a `.7z` holding it, through `7zz e -so` (one pass per directory level,
then one pass for all files in disc order): the 8.7 GB ISO never has to be unpacked on the computer.

## 3. Plan for the first runs

1. Boot: the log should reach the game's first frame. Expected trouble from earlier ports: `2001-0103` (memory
   mappings, sdk-patches/0001 handles it), unregistered indirect-call targets (sdk-patches/0007 logs and returns),
   missing launch data from the launcher, Kinect/NUI calls.
2. Shaders: the first start without a library writes every container to `shaders_dump/`; copy it back,
   `tools/shaders.sh`, ship `ds3_shaders.dslib` + `shadercache_dksh.bin`.
3. Renderer: Dead Space 3 is a later Visceral engine than Dead Space 2 (deferred lighting, more post-processing); the
   ring reader and renderer are game-neutral, so start with ds2-nx's notes on clears, resolves and shadows.
4. CPU: profile with the stack sampler before optimizing; then `LOCALREGS=1` with its check build, VP6 through FFmpeg
   (dantes-inferno-nx's `dante_video.cpp` and sdk-patches/0009), the audio compressor bypass and EA thread priority
   hooks once found.

## Log

- 2026-10-23: repository created from dantes-inferno-nx 1.1.0 (tools, SDK patches, glue) and ds2-nx 1.1.0 (disc swap,
  STFS reader, guest clock patch), with gears2-nx's `match_funcs.py` / `fuzzy_find.py`. Disc 1 (Russia) and TU1 read
  from the user's files; codegen with 0 traps left; names from Dead Space 2. Second codegen issue (gotos into an
  overlapping function) fixed in `patch_branch_traps.py`; first NRO links. Copied to the console with disc 1.
- 2026-10-24, run 1 on the console: the game thread ended at once ("Execution complete"), then a shutdown crash in
  the SDK (`MnkInputDriver::DetachFromWindow`, std::system_error). Cause: `deadspace_game_f.xex` is a DLL (module
  flags 0x9; its entry `xstart` is a DllMain that returns at once without DLL_PROCESS_ATTACH); `default.xex`
  (0x82000000, 254 functions) is the title that loads it with XexLoadImage. Now both are generated
  (`[[modules]]` in the manifest, `generated/default` + `generated/game`; `names_default.toml` for the title, 17
  branch targets), the game is a guest module linked into the NRO (rexglue-sdk-nx's `rexglue_switch_link_module`,
  from fc3-nx), the hooks on game functions build inside that module, and `XamSwapDisc` goes through a handler the
  SDK gains in `sdk-patches/0014`. Built (NRO 65 MB, 890 steps) and copied to the console; run 2 pending.
- 2026-10-24, run 3: default.xex ran and loaded the DLL, but its code was not wired: the loader's path
  (`device/ds3titleupdate/deadspace_game_f.xex`, game: remapped to the title update folder) did not match the
  manifest's `guest_path`. `sdk-patches/0015` falls back to the file name and logs whether a module was wired.
- Run 4: the game runs. "Recompiled module 'deadspace_game_f.xex' wired", 54,884 functions, the GPU ring starts, audio
  starts, 1,179 shader containers dumped (all at startup, 2008 format `0x102A1100/01`), a few frames of clears and
  resolves (1152x720 targets), then no more GPU work after `Call to invalid or unregistered function at 0x88981840`:
  an 8-instruction virtual function in a gap between two generated functions. `tools/find_code_gaps.py` finds 70 such
  gaps (code between functions, ending in blr/bctr/b; none is pointed to from the loaded image, so their vtables are
  filled at run time) and they are named `gap_*` in `names.toml` (it needs the DLL's image: run `rexglue codegen` with
  `REX_DUMP_IMAGE` on a manifest whose entry point is deadspace_game_f.xex). Splitting at them added 39 branch targets.
- Shaders: 1,179 translated after one fix: the translator passes `NAME_InvTamano` to `getWeights2D` too (103 shadow
  filter shaders), and `shader_common.h` gets that overload. 10,083 DKSH variants precompiled. Black screen in run 4
  was this (no library: every draw skipped, "VS not identified") and the stop above.
- Run 5 (overclocked): menus right at ~30 fps; in game some textures missing (ground), the player half transparent,
  fights down to 10-14 fps (CPU 288 % of 300 %, GPU 21-28 ms a frame). The log: 3 % of draws rejected by the renderer
  for formats it lacked (render target k_16_16_16_16, copies to k_8: `sdk-patches/0016`, the first from gears2-nx's
  0010), 8 % skipped for three VS the library did not have (created in place with XGRegisterVertexShader /
  XGRegisterPixelShader, not Create*Shader: now hooked too, the container rebuilt from the object and its microcode),
  4 more indirect calls to functions not generated (named in `names.toml`). Debug shortcuts (ZL + ZR) on by default
  (`ds3_atajos_depuracion`) for the stack sampler.
- Run 6 (stack samples in fights; analysis by a read-only side session): the dips are CPU-bound, and about a fifth of
  the main thread and of RenderWare Audio's two mixer threads was waiting on the SDK's guest clock mutex (every guest
  `mftb` takes it; RW audio reads the timebase around each plug-in). `sdk-patches/0017` makes `UpdateGuestClock`
  lock-free (atomics, each caller claims its share of the host time; per-thread monotonic; 0013's sleep clamp kept).
  Also: the final compressor's bypass (`sub_886C6ED8`, hot in the samples; its limiter twin never runs), the Win32
  `SetThreadPriority` (`sub_88BA72D0`) answered without the object table when priorities are ignored (8-10 % of each job
  worker), 103 DKSH variants the console compiled added to `dksh_variants.txt` (the compile thread reached 60 % of a
  core in fights). The game's `setjmp`/`longjmp` (0x88C99620 / 0x88C991F0) are named in the manifest now (a
  correctness fix: a longjmp used to return to its caller), and `patch_local_regs.py` gets ds2-nx's handling of the
  codegen's setjmp (those 4 functions left as they are but analysed): offline it rewrites 54,665 functions, 4 left.
- The unknown-VS draws (still ~7 %, IM_LOAD_IMMEDIATE: Direct3D's own shaders, not from the hooked creation calls)
  now log their state (primitive, mask, formats, c0) once each; the frame dump and draw trace come back with
  ds2-nx's `ds3_gpu.cpp` under `DS3_DEPURACION` (ON while the port is brought up).
- Run 7 (much faster; rendering wrong: washed-out, "half transparent" surfaces, armour like chrome, missing detail such
  as mouths): 10 frame dumps with full draw traces. The frame: G-buffer (normals 0C666000, depth 09DF8000), light
  volumes into the deferred lighting buffer 0BE50000 (k_16_16_16_16, looked right in the dump), then the material pass
  (406 draws) reads it back unsigned and multiplies it by 8 (`ps_0f5c3c863fdb728f`: `r6 = r8 * 8`). The renderer
  handed the shader the float the target holds; the hardware returns the target's fixed-point bits (value x 1024,
  -32..32) as a normalized integer, about value / 64, times 2^exp_adjust of the fetch constant. `sdk-patches/0018`
  (gears2-nx's 0035, 0036 and 0046, extended): 16_16 and 16_16_16_16 targets read back as such are converted the way the
  hardware returns them, with the fetch's exp_adjust (which gears2-nx's velocity buffer read showed is set to undo the
  scale there), and resolves apply copy_dest_exp_bias. The draw trace now prints each sampler's exp_adjust, and the
  first conversion logs it, to confirm on the next run.
- Also from run 7: Direct3D clears with two rectangles in one draw (6 vertices, the 640-pitch half-size targets) were
  skipped as "VS not identified" and left the previous frames in those targets; any multiple of 3 vertices is a
  clear now. The other unidentified VS is a single point with color writes off (a fence-like draw): harmless.
- Run 10 (normal build): the scene went dark (the player a black silhouette). The log settles it: the material pass
  reads the lighting buffer with exp_adjust 0, unsigned, and the buffer's resolve carries copy_dest_exp_bias -5
  ("resolve con exp bias -5 desde EDRAM formato 5"): the light pass writes up to ~32, the resolve scales by 1/32, the
  material shader by 8. Run 7 lacked the resolve scale (32x too bright: washed out, chrome armour, "transparent"
  surfaces); run 10 had it (gears2-nx 0036) plus the fixed-point read conversion on top (2048x too dark). The
  conversion is off again (`nfsmw_dk_16_16_fijo` false, as in gears2-nx; the four-channel case removed): reads give
  the value the resolve wrote, which the hardware also returns for this data path.
- Frame dumps restart at volcado_00 every session and overwrite the earlier ones file by file: after downloading,
  move them away on the SD (`volcados_previos/`) so the next session's dumps are not mixed with old files.
- LOCALREGS check build (runs 8-9) faulted at 0x5E5304A3: a real bug in `patch_local_regs.py`, found by a side
  session. Its control flow had no edge from a `switch` to its case labels, so a case ending in a tail call +
  `return;` left every later case looking clean; in the Havok MOPP ray query (`sub_88D57180`) the other cases
  tail-called the function's own epilogue piece with r1, lr and the non-volatiles unwritten, and the epilogue popped a
  frame from the caller's r1. Fixed (edges from each switch line to its case/default labels; 43 functions change, no
  REF/MOD/LIVEOUT change). The same gap exists in the ds2-nx, dantes-inferno-nx and gears2-nx copies of the pass. Next:
  a new check build. (Also: the call-chain walk of sdk-patches/0019 can fault on itself past the stack's top.)
- Run 12: the raw texture dump (`ds3/tools/decode_texture_dump.py`, `--montage`) shows every texture intact in guest
  memory (faces, teeth, eyes, armour, rock), all linear DXT1/DXT5 with mips; the draws using them draw. The defects the
  user pointed at (a see-through player with the health bar on his back showing through, holes in the ground,
  untextured distant mountains, the menu's projector missing) are a depth mismatch: DS3 draws each mesh twice, the
  G-buffer pass (writes depth, LEQUAL) and the material pass (LEQUAL, no depth write) with another vertex shader; on the
  Xbox 360 both give the same depth bit for bit, here two separately compiled shaders differ in the last bit and the
  material pass fails on part of the pixels. `sdk-patches/0021` (gears2-nx's 0029) pulls such passes 4 depth units
  towards passing, only for passes that do not write depth (the G-buffer pass writes it with the same function, and
  pulling both cancelled out).

