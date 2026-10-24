// ds3-nx - CPU hooks on the game's own functions (built into the game module: cmake/ds3_switch.cmake says why).
//
// SetThreadPriority (the XDK's Win32 one, 0x88BA72D0; Dead Space 2's EA::Thread::SetThreadPriority wrapper is inlined in
// Dead Space 3, which calls this directly): EA's job workers raise their priority to 127 before each job and drop it to
// -128 after it. In the SDK kernel each call is a handle lookup, a priority change and a release, every one under the
// object table's lock (the one every WaitForSingleObject and ReleaseSemaphore also takes), and with
// ignore_thread_priorities the host thread's priority does not change anyway. Then it only reports success. (Run 6
// stack samples: 8-10 % of each job worker's samples were in it.)
//
// Direct3D's GPU wait step (0x88A20950): BlockOnFence and the other Direct3D waits call it in a loop until the GPU (here
// the ring thread) has passed a fence. On the Xbox 360 its db16cyc delays cost one hardware thread of six; here they
// compile to nothing and the loop takes a whole core from the threads it waits for (the ring thread, the audio mixers,
// the job workers). Run 7's stack samples: 6 % of the main thread in calm play, 40-60 % while the ring thread was slow.
// While it answers "still waiting" the thread yields the first ds3_espera_gpu_cesiones times and then sleeps
// ds3_espera_gpu_us: a Horizon sleep often lasts far longer than asked, so short waits (most) do not pay for one.
// gears2-nx hooks the same function (gow2_perf_hooks.cpp).

#include "ds3_pch.h"

#include <chrono>

#include <rex/cvar.h>
#include <rex/thread.h>

REXCVAR_DECLARE(bool, ignore_thread_priorities);

REX_EXTERN(__imp__ds3_SetThreadPriority);
REX_EXTERN(ds3_SetThreadPriority) {
  if (REXCVAR_GET(ignore_thread_priorities)) {
    ctx.r3.u64 = 1;  // TRUE
    return;
  }
  __imp__ds3_SetThreadPriority(ctx, base);
}

REXCVAR_DEFINE_INT32(ds3_espera_gpu_us, 100, "DS3",
                     "Microsegundos que duerme la espera activa de Direct3D a la GPU (0x88A20950) tras las cesiones; "
                     "0 = girar como en la Xbox 360")
    .range(0, 2000);
REXCVAR_DEFINE_INT32(ds3_espera_gpu_cesiones, 32, "DS3",
                     "Veces que la espera de Direct3D a la GPU solo cede el nucleo antes de dormir")
    .range(0, 100000);

REX_EXTERN(__imp__ds3_D3D_GpuWaitStep);
REX_EXTERN(ds3_D3D_GpuWaitStep) {
  thread_local uint32_t seguidas = 0;  // "still waiting" answers in a row on this thread
  __imp__ds3_D3D_GpuWaitStep(ctx, base);
  if (ctx.r3.u32 != 1) {  // the GPU got there
    seguidas = 0;
    return;
  }
  const int32_t us = REXCVAR_GET(ds3_espera_gpu_us);
  if (us <= 0) return;
  if (++seguidas <= uint32_t(REXCVAR_GET(ds3_espera_gpu_cesiones))) {
    rex::thread::MaybeYield();
  } else {
    rex::thread::Sleep(std::chrono::microseconds(us));
  }
}
