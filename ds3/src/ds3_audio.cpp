// ds3-nx - lighter audio for the Switch's CPU.
//
// Dead Space 3 mixes with the same RenderWare Audio (rwaudiocore) as Dead Space 2, where the mixer thread was the
// heaviest after the game's own. ds3/codegen/names.toml names the functions hooked here (MixWithGain and CopyWithGain
// are byte for byte Dead Space 2's); CMake compiles this file only when their names are there.
//
// The final compressor (Dead Space 2: rw::audio::core::Compressor1::Process, a third of its mixer thread) has its own
// bypass: with its threshold at 0 dB or its ratio at 1 it leaves the mixer's buffer as it is (it works in place),
// clears its state and returns 1. ds3_audio_sin_compresor takes that path always. In Dead Space 3 (a newer
// rwaudiocore) it is the 98-instruction plug-in Process at 0x886C6ED8 that Mixer::Execute calls and that run 6's stack
// samples show (its twin at 0x886CB0B8, which drives the same CompressorLimiter1 loop as a limiter, never runs). What is
// lost is the dynamics compression of the final mix: loud moments are not squeezed (the output is still clamped).

#include "ds3_pch.h"

#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

// ---- Native versions of the mixer's leaf loops. Translated, each 4-float VMX vector goes through byte shuffles and
// stores in the context; MixWithGain alone runs 100-170 thousand times a second in a fight (about 14 % of a core at
// stock clocks). The same arithmetic on NEON: big-endian floats swapped on load and store, multiply then add (not
// fused), as vmaddfp.

// ---- The mixer threads' priority. RenderWare Audio's two mixer threads run at Horizon 0x3B, the band of the game's
// main thread and its job workers; when those keep the cores busy (always at stock clocks) a mixer gets its time
// slice too late and the frame goes out as silence (run 17: up to 474 frames in 10 s in fights; run 19, at 1020 MHz,
// the sound "lagged unbearably"). On the Xbox 360 a mixer runs time-critical. The threads that call the mixer's leaf
// loops below are the mixers: each raises itself once to ds3_prioridad_mezclador, above the game's threads and below
// the ring thread (0x2C) and the XMA decoder (0x2B). They block on their tick event between frames, so they cannot
// keep a core from the others.

REXCVAR_DEFINE_INT32(ds3_prioridad_mezclador, 0x2E, "DS3",
                     "Prioridad de Horizon de los hilos de mezcla de audio del juego (0x2C-0x3A); 0 = la de los "
                     "demas hilos del juego (0x3B)")
    .range(0, 0x3B);

#if defined(__SWITCH__)
extern "C" bool RexSwitchSetCurrentThreadPriorityOk(int priority);
#endif

namespace {
void ElevarMezclador() {
  thread_local bool hecho = false;
  if (hecho) return;
  hecho = true;
#if defined(__SWITCH__)
  const int32_t prioridad = REXCVAR_GET(ds3_prioridad_mezclador);
  if (prioridad >= 0x2C && prioridad < 0x3B) {
    const bool ok = RexSwitchSetCurrentThreadPriorityOk(prioridad);
    REXLOG_INFO("[audio] hilo de mezcla a la prioridad 0x{:X}: {}", prioridad, ok ? "hecho" : "FALLO");
  }
#endif
}
}  // namespace

REXCVAR_DEFINE_BOOL(ds3_audio_sin_compresor, true, "DS3",
                    "Saltarse el compresor final del audio del juego (su propio camino de bypass): parte del hilo de "
                    "mezcla. false = el sonido original, mas CPU");

REX_EXTERN(__imp__ds3_rw_audio_Compressor1_Process);
REX_EXTERN(ds3_rw_audio_Compressor1_Process) {
  if (REXCVAR_GET(ds3_audio_sin_compresor)) {
    static bool avisado = false;
    if (!avisado) {
      avisado = true;
      REXLOG_INFO("[audio] compresor final del juego desactivado (ds3_audio_sin_compresor)");
    }
    ctx.r3.u64 = 1;  // what the bypass returns
    return;
  }
  __imp__ds3_rw_audio_Compressor1_Process(ctx, base);
}

REXCVAR_DEFINE_BOOL(ds3_audio_nativo, true, "DS3",
                    "MixWithGain y CopyWithGain del mezclador en NEON nativo en vez de traducidos");

namespace {

#if defined(__aarch64__)
inline float32x4_t CargarBe(const uint8_t* p) { return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(p))); }
inline void GuardarBe(uint8_t* p, float32x4_t v) { vst1q_u8(p, vrev32q_u8(vreinterpretq_u8_f32(v))); }
#endif

inline float CargarFloatBe(const uint8_t* p) {
  uint32_t u;
  std::memcpy(&u, p, 4);
  u = __builtin_bswap32(u);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

inline void GuardarFloatBe(uint8_t* p, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  u = __builtin_bswap32(u);
  std::memcpy(p, &u, 4);
}

// dst[i] = dst[i] + src[i] * gain (mezclar) or src[i] * gain.
void ConGanancia(uint8_t* dst, const uint8_t* src, float gain, uint32_t n, bool mezclar) {
  uint32_t i = 0;
#if defined(__aarch64__)
  const float32x4_t g = vdupq_n_f32(gain);
  for (; i + 8 <= n; i += 8) {
    const float32x4_t a = vmulq_f32(CargarBe(src + i * 4), g);
    const float32x4_t b = vmulq_f32(CargarBe(src + i * 4 + 16), g);
    if (mezclar) {
      GuardarBe(dst + i * 4, vaddq_f32(CargarBe(dst + i * 4), a));
      GuardarBe(dst + i * 4 + 16, vaddq_f32(CargarBe(dst + i * 4 + 16), b));
    } else {
      GuardarBe(dst + i * 4, a);
      GuardarBe(dst + i * 4 + 16, b);
    }
  }
#endif
  for (; i < n; ++i) {
    const float v = CargarFloatBe(src + i * 4) * gain;
    GuardarFloatBe(dst + i * 4, mezclar ? CargarFloatBe(dst + i * 4) + v : v);
  }
}

}  // namespace

// void MixWithGain(float* dst, float* src, float gain, unsigned count)
REX_EXTERN(__imp__rw__audio__core__MixWithGain);
REX_EXTERN(rw__audio__core__MixWithGain) {
  ElevarMezclador();
  if (REXCVAR_GET(ds3_audio_nativo)) {
    ConGanancia(REX_RAW_ADDR(ctx.r3.u32), REX_RAW_ADDR(ctx.r4.u32), float(ctx.f1.f64), ctx.r6.u32, true);
  } else {
    __imp__rw__audio__core__MixWithGain(ctx, base);
  }
}

// void CopyWithGain(float* dst, float* src, float gain, unsigned count)
REX_EXTERN(__imp__rw__audio__core__CopyWithGain);
REX_EXTERN(rw__audio__core__CopyWithGain) {
  ElevarMezclador();
  if (REXCVAR_GET(ds3_audio_nativo)) {
    ConGanancia(REX_RAW_ADDR(ctx.r3.u32), REX_RAW_ADDR(ctx.r4.u32), float(ctx.f1.f64), ctx.r6.u32, false);
    return;
  }
  __imp__rw__audio__core__CopyWithGain(ctx, base);
}
