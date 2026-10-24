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

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "ds3_cpu.h"
#include "ds3_disc_swap.h"
#include "ds3_gpu.h"

#if defined(__SWITCH__)
// The SDK's debug shortcuts (ZL + ZR held: Down capture, Left frame dump and draw trace, Right draw-skipping modes,
// R CPU stack sampler, L GPU lap; Up the FPS counter): the SDK has them off (a release build); on while the port is
// being brought up.
REXCVAR_DECLARE(bool, switch_atajos_depuracion);
REXCVAR_DEFINE_BOOL(ds3_atajos_depuracion, true, "DS3",
                    "Atajos de depuracion con ZL+ZR (captura, volcado de fotograma, modos de omision, muestreo de "
                    "pilas con R). false = solo el contador de FPS");
#endif

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
    try {
      ds3::disc_swap::RegisterImport(*runtime());
    } catch (const std::exception& error) {
      Fail(std::string("Dead Space 3: ") + error.what());
    }
  }

 private:
  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }
};
