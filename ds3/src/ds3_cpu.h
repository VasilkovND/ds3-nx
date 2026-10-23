// ds3-nx - the recompiled code's "interrupts off" regions (ds3_cpu.cpp).

#pragma once

namespace ds3::cpu {

// Before the game's first thread runs: reads ds3_msr_cerrojo_global.
void Configurar();

// The build (version, commit) and the console (firmware, Atmosphere, title takeover or applet, handheld or docked) at
// the top of the log: what a player's log needs to say before anything else.
void InformarSistema();

}  // namespace ds3::cpu
