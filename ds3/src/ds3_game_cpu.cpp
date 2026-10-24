// ds3-nx - CPU hooks on the game's own functions (built into the game module: cmake/ds3_switch.cmake says why).
//
// SetThreadPriority (the XDK's Win32 one, 0x88BA72D0; Dead Space 2's EA::Thread::SetThreadPriority wrapper is inlined in
// Dead Space 3, which calls this directly): EA's job workers raise their priority to 127 before each job and drop it to
// -128 after it. In the SDK kernel each call is a handle lookup, a priority change and a release, every one under the
// object table's lock (the one every WaitForSingleObject and ReleaseSemaphore also takes), and with
// ignore_thread_priorities the host thread's priority does not change anyway. Then it only reports success. (Run 6
// stack samples: 8-10 % of each job worker's samples were in it.)

#include "ds3_pch.h"

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, ignore_thread_priorities);

REX_EXTERN(__imp__ds3_SetThreadPriority);
REX_EXTERN(ds3_SetThreadPriority) {
  if (REXCVAR_GET(ignore_thread_priorities)) {
    ctx.r3.u64 = 1;  // TRUE
    return;
  }
  __imp__ds3_SetThreadPriority(ctx, base);
}
