// ds3-nx - the scene resolution: the game's own 1152x720 or 864x540 (three quarters, the same 1.6 shape, so the game
// stretches it to the screen exactly as it does 1152x720). 864x540 is for stock clocks: the GPU does about 56 % of
// the pixel work. The game sets its scene size once, when it sets up the display (ds3_resolution.cpp, the hook on
// 0x8895BAE0), so a change takes effect at the next start.
//
// The choice is in ds3_resolucion.txt beside the NRO ("720" or "540"), written by the options menu (ds3_menu.cpp,
// ZL + ZR + Minus). Without it: 720 when the CPU runs at 1.5 GHz or more (an overclocked console), else 540.
// Header-only: the hook is built into the game module and the menu into the title, which share no variables.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>

#include <rex/filesystem.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace ds3::resolucion {

// Where the game keeps its scene size (big-endian u16), written by 0x8895BAE0.
constexpr uint32_t kAnchoEscena = 0x890B1378;
constexpr uint32_t kAltoEscena = 0x890B137C;

inline std::filesystem::path Fichero() { return rex::filesystem::GetExecutableFolder() / "ds3_resolucion.txt"; }

// The saved choice (720 or 540), 0 if there is none.
inline uint32_t Guardada() {
  FILE* f = std::fopen(Fichero().string().c_str(), "r");
  if (!f) return 0;
  unsigned valor = 0;
  const bool leido = std::fscanf(f, "%u", &valor) == 1;
  std::fclose(f);
  return leido && (valor == 720 || valor == 540) ? valor : 0;
}

inline bool Guardar(uint32_t valor) {
  FILE* f = std::fopen(Fichero().string().c_str(), "w");
  if (!f) return false;
  const bool escrito = std::fprintf(f, "%u\n", unsigned(valor)) > 0;
  return std::fclose(f) == 0 && escrito;
}

// The CPU clock in MHz (0 if it cannot be read).
inline uint32_t CpuMhz() {
#if defined(__SWITCH__)
  uint32_t hz = 0;
  if (R_SUCCEEDED(clkrstInitialize())) {
    ClkrstSession sesion;
    if (R_SUCCEEDED(clkrstOpenSession(&sesion, PcvModuleId_CpuBus, 3))) {
      clkrstGetClockRate(&sesion, &hz);
      clkrstCloseSession(&sesion);
    }
    clkrstExit();
  }
  return hz / 1000000;
#else
  return 0;
#endif
}

inline uint32_t PorDefecto() {
  const uint32_t mhz = CpuMhz();
  return mhz == 0 || mhz >= 1500 ? 720 : 540;
}

inline uint32_t Elegida() {
  const uint32_t guardada = Guardada();
  return guardada ? guardada : PorDefecto();
}

}  // namespace ds3::resolucion
