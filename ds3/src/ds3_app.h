// ds3-nx - Dead Space 3 (Xbox 360, Title Update 1) on the Nintendo Switch.
//
// Where the game is, next to the NRO (sdmc:/switch/ds3-nx/ on the console):
//   tu/       disc 1's default.xex and deadspace_game_f.xex, with Title Update 1's default.xexp and
//             deadspace_game_f.xexp (game:, and game_data_root)
//   disc1/    the extracted Disc 1 (deadspace_game_f.xex, bigfile0.viv, bigfile1.viv, disc1_info.txt, ...)
//   disc2/    the extracted Disc 2 (optional until chapter 5)
// tools/stage_sd.py makes this layout from your ISOs and title update.
//
// default.xex is the title; the game itself is deadspace_game_f.xex, a DLL it loads with XexLoadImage (both recompiled:
// the game is a guest module linked into the NRO). The code is TU1's: it only matches the images the title update
// patches, so tu/ needs both .xexp files (the runtime applies each when it loads its executable).

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>
#include <rex/ui/overlay/achievement_notification.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "ds3_cpu.h"
#include "ds3_disc_swap.h"
#include "ds3_gpu.h"
#include "ds3_menu.h"
#include "ds3_resolution.h"

#if defined(__SWITCH__)
// The SDK's debug shortcuts (ZL + ZR held: Down capture, Left frame dump and draw trace, Right draw-skipping modes,
// R CPU stack sampler, L GPU lap; Up the FPS counter): off since 1.0.0 except the FPS counter; ds3.toml turns them on
// for bug reports.
REXCVAR_DECLARE(bool, switch_atajos_depuracion);
REXCVAR_DEFINE_BOOL(ds3_atajos_depuracion, false, "DS3",
                    "Atajos de depuracion con ZL+ZR (captura, volcado de fotograma, modos de omision, muestreo de "
                    "pilas con R). false = solo el contador de FPS");
#endif

#if defined(__SWITCH__)
#include <rex/ui/switch_apm.h>
#endif
// Handheld GPU clock through Horizon's own performance profiles (the ones commercial games pick; no overclock): at
// stock the handheld GPU runs at 307.2 MHz, and run 22 needed 31-37 ms of GPU per frame at 540p. 460 asks for
// 0x92220008 (GPU 460.8 MHz, CPU 1020 and memory 1331.2 as before; the SDK's switch_apm.cpp, from the NFS port, tries
// the pairs that keep the memory clock first and puts it back if it rises). Docked, or with sys-clk setting the
// clocks, nothing changes.
REXCVAR_DEFINE_INT32(ds3_gpu_portatil_mhz, 460, "DS3",
                     "GPU en modo portatil por los perfiles de rendimiento de Horizon: 460 o 384 MHz; 0 = no tocar "
                     "(307,2 MHz de serie)")
    .range(0, 460);

REXCVAR_DEFINE_STRING(ds3_disc1, "", "DS3", "Carpeta del disco 1 extraido. Vacio: disc1/ junto al ejecutable.");
REXCVAR_DEFINE_STRING(ds3_disc2, "", "DS3", "Carpeta del disco 2 extraido. Vacio: disc2/ junto al ejecutable.");

class Ds3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ds3App>(new Ds3App(ctx, "ds3", PPCImageConfig));
  }

 protected:
  // No Xenos emulation: ds3_gpu.cpp reads the game's command ring and draws with the deko3d renderer.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) config.graphics = ds3::gpu::CreateGraphicsSystem();
  }

  // game_data_root is the folder with the executable (ConstructRuntime checks it): tu/, where the title update is.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (folder.empty()) return;
    paths.game_data_root = folder / "tu";
  }

  void OnPostInitLogging() override {
    // The SDK sends the controllers zero vibration unless input_vibracion is on; on, the game's own vibration setting
    // decides (ds2-nx 1.2.2). ds3.toml can still turn it off.
    DefaultFlag("input_vibracion", "true");
#if defined(__SWITCH__)
    REXCVAR_SET(switch_atajos_depuracion, REXCVAR_GET(ds3_atajos_depuracion));
#endif
    ds3::cpu::InformarSistema();
  }

  // After Runtime::Setup (the VFS exists), before LoadXexImage.
  void OnLoadXexImage(std::string& xex_image) override {
    const auto folder = rex::filesystem::GetExecutableFolder();
    auto pick = [&](const std::string& flag, const char* name) {
      return flag.empty() ? folder / name : std::filesystem::path(flag);
    };
    std::error_code ec;
    for (const char* name : {"default.xex", "default.xexp", "deadspace_game_f.xex", "deadspace_game_f.xexp"}) {
      if (!std::filesystem::is_regular_file(game_data_root() / name, ec)) {
        Fail(fmt::format("Dead Space 3: {} is missing from {}. Make the folder with tools/stage_sd.py (INSTALL.md).",
                         name, game_data_root().string()));
      }
    }
    try {
      ds3::disc_swap::Prepare(*runtime(), game_data_root(), pick(REXCVAR_GET(ds3_disc1), "disc1"),
                              pick(REXCVAR_GET(ds3_disc2), "disc2"));
    } catch (const std::exception& error) {
      Fail(std::string("Dead Space 3: ") + error.what());
    }
    REXLOG_INFO("ds3: title update at {}; loading {}", game_data_root().string(), xex_image);
  }

  void OnPreLaunchModule() override {
    ds3::cpu::Configurar();
    ds3::menu::Instalar(*runtime());
#if defined(__SWITCH__)
    // Not on an overclocked console: there the profile became the clocks that stayed (run 27: sys-clk had the CPU at
    // 1785 and the GPU at 614.4 at start, and from the first seconds on everything ran at the profile's 1020 / 460.8 /
    // 1331.2, the fights CPU-bound at 23 fps; in runs 25 and 26 sys-clk had put its clocks back).
    const uint32_t cpu_mhz = ds3::resolucion::CpuMhz();
    if (const int32_t mhz = REXCVAR_GET(ds3_gpu_portatil_mhz); mhz > 0 && cpu_mhz <= 1020) {
      RexSwitchApmPedirGpuMhz(mhz);
      RexSwitchApmAplicar();
    } else if (mhz > 0) {
      REXLOG_INFO("ds3: CPU at {} MHz (overclocked): the handheld GPU profile is not requested", cpu_mhz);
    }
#endif
    try {
      ds3::disc_swap::RegisterImport(*runtime());
    } catch (const std::exception& error) {
      Fail(std::string("Dead Space 3: ") + error.what());
    }
  }

  // No achievement toast. Its ImGui dialog, created with the first achievement (the end of the prologue), is a UI
  // drawer for the rest of the session, and with one the presenter paints from the UI thread on request: from that
  // moment (run 17) the screen stayed black with the sound going, then flickered. The achievement is still unlocked
  // and saved.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    REXLOG_INFO("ds3: achievement toast off (it would move presentation to the UI thread)");
    return nullptr;
  }

 private:
  // A flag's value unless ds3.toml (or the command line) already set it.
  static void DefaultFlag(const char* name, const char* value) {
    if (!rex::cvar::GetFlagInfo(name) || rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) return;
    rex::cvar::SetFlagByName(name, value);
  }

  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }
};
