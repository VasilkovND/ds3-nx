// ds3-nx - the scene resolution chosen in the options menu (ds3_resolution.h), applied where the game sets up its
// display: 0x8895BAE0 writes the scene size every part of the renderer reads (1152x720 in HD; 1024x768 and a 640x480
// back buffer in SD, so the engine already renders other sizes) and the back buffer's (1280x720, kept). After it runs,
// 864x540 replaces 1152x720 when that is the choice. Built into the game module (cmake/ds3_switch.cmake).

#include "ds3_pch.h"

#include <rex/logging.h>
#include <rex/memory.h>

#include "ds3_resolution.h"

REX_EXTERN(__imp__ds3_ConfigureDisplay);
REX_EXTERN(ds3_ConfigureDisplay) {
  __imp__ds3_ConfigureDisplay(ctx, base);
  namespace r = ds3::resolucion;
  const uint16_t ancho = rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena));
  const uint16_t alto = rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena));
  const uint32_t guardada = r::Guardada();
  const uint32_t elegida = guardada ? guardada : r::PorDefecto();
  if (elegida == 540 && ancho == 1152 && alto == 720) {
    rex::memory::store_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena), 864);
    rex::memory::store_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena), 540);
  }
  REXLOG_INFO("ds3: escena {}x{} (el juego puso {}x{}; elegida {}p, {}; CPU {} MHz)",
              rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAnchoEscena)),
              rex::memory::load_and_swap<uint16_t>(REX_RAW_ADDR(r::kAltoEscena)), ancho, alto, elegida,
              guardada ? "guardada" : "por defecto", r::CpuMhz());
}
