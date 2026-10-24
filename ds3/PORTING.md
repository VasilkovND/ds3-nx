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
- Runs 13-15 against the user's Xbox 360 screenshots of the same scenes: on the console the blizzard washes everything
  out (distant rocks and the sky barely visible, white snow on the armour); here distant rocks and the sky were black
  and characters partly see-through, while the G-buffer held the whole scene. The fog is the game's own (VS constants,
  linear 0.0896 per unit in the prologue). The cause is the depth mismatch above, which grows with distance: a few ULPs
  of the vertex position are more than a fixed 4-unit bias far away, so the material pass of distant geometry failed
  everywhere. Fix at the source: vertex shaders are translated with `precise` registers and position
  (`shaders/ds_hlsl.cpp`: SPIR-V NoContraction, GLSL precise for uam), so the G-buffer and material vertex shaders,
  whose position code is the same, give the same depth again.

- Run 16 (8 traces with raw textures; the precise vertex shaders did not change the picture). The earlier depth
  mismatch theory was wrong; the traces show four real defects, each with its evidence:
  1. **The light buffer's clear wiped the scene's depth and stencil.** Direct3D clears the k_16_16_16_16 light buffer
     (c0 2A3, mask FFFF, depthcontrol 0) with c1-c3 left on base 000; `EmulateClear` took c1-c3 as Direct3D's way of
     clearing depth through color (Dead Space 2) and cleared z 000 ("clear shape: msaa 2, mode 4: c0=2A3
     z=000->FFFFFF3F"), right before the light volumes and the material pass. The lights' stencil marks failed (the
     menu's machines unlit, black), and the material pass tested against a cleared depth, so whatever came later in
     the frame was drawn over what is in front of it (the see-through player, his health bar showing through, the
     ground and rocks). The clear's PS (n198) writes oC0 only, and the Xenos leaves targets the PS does not export
     alone: c1-c3 are now cleared only when the PS writes them. (Found by a side session from run 15's menu trace.)
  2. **Stacked shadow cascades.** The sun shadow pass (PS n155, half size, writes the shadow mask the materials read
     as g_attenShadowBuf) samples three 960x960 depth resolves laid one after another (0ACBC000, 0B040000, 0B3C4000,
     0x384000 apart) as one 960x2880 texture (960x1920 with two). The renderer gave it the first resolve alone,
     stretched over the three: the mask had the player fully in shadow and everything distant shadowed (the dark
     player and the black distant rocks). `sdk-patches/0022`: such a fetch gets an image of its size with each
     resolve copied to its rows.
  3. **The half-size depth read at the wrong scale.** It is resolved 640 wide (the EDRAM pitch) and read as 576x360;
     color resolves read smaller get a copy of their corner, depth ones did not. The ambient occlusion and the depth
     of field came out squeezed to 576/640 of the screen (the AO dump ends at x 518 of 576; outlines of the player
     beside him in the frame). 0022 crops depth too.
  4. **Nested loops in the shader translator.** Both levels used the one aL, so the outer loop ran once: the lamp
     visibility shader (PS n645, 7x7 depth taps into the 64x8 visibility target) took 7 taps, and glows showed through
     what covers them. The translator keeps the outer counter now (two shaders change: n645 and n356).
  A scan of every sampler against the resolves of the same frame finds no other size mismatch (the color ones are the
  corner reads, already handled).
- Run 16, fights (side-session analysis, `ds3-fps-fight.md` in the shared notes): 26-28 fps with the three cores at
  257-293 % and the GPU 28-33 ms a frame at 614 MHz; the worst windows are the ones where the console compiled shader
  variants in the background (80 of them, not in the cache). Done: those 80 go into `dksh_variants.txt` (10,186
  DKSH now), and `sdk-patches/0023` (gears2-nx's partial submits: the recorded work goes to the GPU every 256 draws,
  `nfsmw_dk_envio_parcial`, 0 = off; descriptor-only pre-lists without the full barrier) so the GPU stops idling
  2-10 ms a frame while the ring thread records. Left for later: the geometry cache trusting stable ranges (risk of
  a stale pose on CPU-skinned meshes for a few frames), sleeping in Direct3D's GPU wait (needs a codegen run),
  LOCALREGS.
- Run 17 (build 21): the user reports every texture right now. Left: (a) after the prologue the screen stayed black
  with the sound going, then flickered; (b) at the start the blizzard does not hide the sky (black behind the
  snow); (c) small bright artifacts on the shoulders and along shadows; (d) occasional hitches with audio stutter.
  - (a) The level did load: the traces after the prologue are complete frames (600 G-buffer draws, lights,
    materials, post). The log shows the moment: the first achievement unlocks at 16:03:03 and in the same
    millisecond the presenter switches to "UI thread, on request, 1 UI drawer". That drawer is the SDK's achievement
    toast (an ImGui dialog created with the first event and kept for the session); with any UI drawer the presenter
    paints from the UI thread on request (the SDK's own comment in rex_app.cpp: frames halved), and here the screen
    went black, then flickered. `Ds3App::CreateAchievementNotificationDialog` returns none: achievements still unlock
    and are saved, without the toast.
  - (b) Direct3D's clear in Dead Space 3 takes its color from its own vertices: the clear VS (27 words, first
    30052003, found at 0x8801CF58 in the module) fetches 7 floats per vertex from fetch constant 0.0, xyz and the
    color, and PS n198 outputs that color. `EmulateClear` took PS c0 (Dead Space 2's way), which here holds game
    constants (1/1152, 1/720...): the material target was cleared to black, the color the blizzard's sky shows
    through. The color is now read from the clear's vertices (logged for the first eight).
  - (c) The shadow mask's edge pass (PS n633) and its depth-aware upsample (PS n476) are if/else shaders: a jump to
    the else part when !p0 and, at the end of the then part, a jump over it. Their ranges cross, and the translator's
    nested `if`s closed the first at the second's target: the else part sat inside the then part and never ran. The
    upsample's edge pixels kept their texture coordinates as their color (the bright dashes along every depth edge
    in the full-size mask). Crossing jumps now make the translator use its `pc` form; only n476 and n633 change.
  - (d) Hitches with audio stutter (side-session analysis, `ds3-run17-stutter.md` in the shared notes): in fights the
    three cores reach 270-287 %, and the audio queue never held its configured cushion: `PumpTick` asked for an
    extra frame only below 2 queued, so it sat at 2-6 frames instead of 10 and the game's mixer threads had to
    answer within ~5-15 ms; under load up to 474 frames per 10 s went out as silence. `sdk-patches/0024` keeps the
    queue at `audio_switch_tramas_en_cola`; 0025 takes the event lock out of the mixers' per-tick Reset (9.8 % of
    their samples waited there) and wakes waiters after the unlock; 0026 logs frames over 45 ms. The 24 shader
    variants compiled live in run 17 are in the cache now (10,210).
- Run 18 (build 23) froze after 6 minutes: an RenderWare Audio thread read freed memory (0x963617D4, in the command
  interpreter sub_88981368, which calls 0x886C2E40 / 0x886C0178) and the fault handler's call-chain walk then
  faulted on itself past the stack top, over and over, so the game hung instead of crashing; a 27 s stall earlier in
  the run too. The only changes of build 23 that touch the audio threads were 0024 (a deeper queue: the mixer ran
  further ahead) and 0025 (event wake-ups): both are out again; the stutter comes back to build 21's level. 0027:
  the walk stops at the first page that is not readable.
- Stock clocks (build 24): the scene resolution is now a choice. 0x8895BAE0 writes the scene size the renderer reads
  (u16 0x890B1378 / 0x890B137C: 1152x720 in HD, 1024x768 in SD, so the engine renders other sizes); the hook
  (`src/ds3_resolution.cpp`) puts 864x540 there when 540p is chosen (the same 1.6 shape, stretched as 1152x720 is).
  The options menu (`src/ds3_menu.cpp`, ZL + ZR + Minus) saves the choice in ds3_resolucion.txt; it applies at the
  next start. Without one: 720p from a 1.5 GHz CPU up, else 540p. The menu is painted by the presenter
  (`sdk-patches/0028`, no ImGui, which moves presentation to the UI thread) and the game sees the controller idle
  while it is open. Also: Direct3D's GPU wait step (0x88A20950) yields, then sleeps 100 us, instead of spinning a
  core (gears2-nx).
- Run 19 (build 24, stock clocks in handheld: CPU 1020, GPU 307.2, memory 1331 MHz; 540p chosen by default): 30 fps
  at first, then 21-25 and 15-19 in fights; the sound "lagged unbearably". GPU per frame 38-44 ms at 864x540
  (scene 22-30, copies 7-8, small passes 2-5): the GPU is the first limit in handheld; in fights it also idled 10-22
  ms a frame waiting for the CPU. The sound: the two RenderWare Audio mixer threads share 0x3B with the main thread
  and the job workers, and at 1020 MHz they get their slice too late (run 17's stacks). Build 25: each thread that
  calls the mixer's leaf loops (src/ds3_audio.cpp) raises itself once to `ds3_prioridad_mezclador` (0x2E, between
  the game's threads and the ring/XMA threads; they block on their tick event between frames). LOCALREGS on
  (54,665 functions with registers in locals, 4 left; the NRO 66 -> 55 MB), with its check build
  (`PLR_CHECK=1`, ds3-nx-check.nro) to run first. sdk-patches/0029: the geometry store's XXH3 inlined (gears2-nx),
  its "trust stable ranges" off by default (`nfsmw_dk_geometria_confianza` 1: the game skins on the CPU).
- Run 20 (build 25, stock handheld, 540p, LOCALREGS): stable, 26-29 fps calm and 20-22 in fights with the three cores
  at 304-312 %; the sound still lost up to a third of its frames in fights (1,359 of ~4,100 requests in 10 s without
  data). Stacks: the two raised mixers wait 70 % of the time (not short of CPU); two more RenderWare mixer threads
  (the same loop, 0x886A8910, through another DSP path, never calling MixWithGain) and the two XAudio threads the
  Audio Worker's callback waits on (0x88BD2DE8, 0x88BD2E48) stayed at 0x3B with the game. Build 26 raises all six at
  their loops (src/ds3_audio.cpp). The Audio Worker's callback spent 97 % in WaitMultiple's 1 ms sleeps:
  sdk-patches/0030 wakes a sleeping WaitMultiple on any signal (the 1 ms slice stays as the fallback; the objects'
  own waits and signals unchanged).
- Run 21 (build 26): the sound is right (0 requests without data in almost every window), but 15-20 fps in fights.
  The audio side went from ~60 % to ~210 % of a core: 0030's global wake-up woke every WaitMultiple waiter on every
  event signal in the game, and the Audio Worker and the XAudio threads (now at 0x2E) spent their time locking,
  waking and sleeping again (Audio Worker 2.4 -> 21.7 %, XAudio 7-19 % each, XMA decoder 12 -> 42 %). 0030 is out
  (build 27); the six audio threads stay raised, which is what fixed the sound.
- Run 22 (build 27, stock handheld, 540p): the sound is right (no request without data), 26-30 fps, 20-22 in the
  heaviest fights. GPU 31-37 ms a frame at 307.2 MHz: the GPU is the limit most of the time. Build 28 asks Horizon
  for the handheld profile 0x92220008 (GPU 460.8 MHz, CPU 1020 and memory 1331.2 unchanged: a profile commercial
  games use, not an overclock) through the SDK's switch_apm.cpp (`ds3_gpu_portatil_mhz`, 460; 0 = untouched).
  Left on the CPU in fights: the audio (XMA decoder ~52 % of a core, FFmpeg already NEON; the two busiest mixers ~50 %
  each), the main thread ~77 %, the ring thread ~52 %.
- Run 23 (build 28): GPU 460.8 MHz with the memory at 1331.2, 30 fps nearly everywhere at 540p, no audio gaps. Two
  artifacts dumped in the prologue: far terrain and rocks flat black instead of hidden by the blizzard, and fire
  glows through rock. The glows draw without depth; the game sizes them from its lamp visibility pass (PS n645, 64x8,
  read back to the CPU every frame: that works), and PS n645 sampled the depth around the wrong row:
  `r1.zw = ...; r0.y = r1.w; ps = r0.y - r0.w;`. A Xenos ALU instruction co-issues a vector and a scalar operation
  that both read their sources before either writes; the translator wrote the vector line first, and the scalar
  read the new value. Now the register is copied before the vector line when the scalar reads the register the
  vector writes (552 of 1,179 shaders have such an instruction, some harmless; among them the terrain and rock
  materials n489, n1086, n959, the composite n245, the sun shadow mask n155 and the blizzard sheets' VS n521).
