/* The monotonic clock of a wasm binary that holds the game module, strictly increasing (linked with
   --wrap=clock_gettime, web/cmake/SmgGame.cmake).

   Nebula's time base (OSGetTime, OSGetTick: native/sdk/os/os_native.c) reads CLOCK_MONOTONIC. On the console the
   time base register counts at 60.75 MHz, and natively the clock has nanoseconds: two reads always differ. In the
   browser CLOCK_MONOTONIC is performance.now(), whose resolution is a few microseconds, so two reads close together
   can be equal. The game divides by such differences, or adds them up in a loop: JUTVideo::preRetraceProc keeps the
   time between two retraces (sVideoInterval), and MainLoopFramework::calcCombinationRatio adds it up until it passes
   two frames (`while (var1 < 2 * mLastFrameTime) var1 += videoInterval`). Two retraces delivered back to back (Nebula
   delivers the ones that came while the game was busy at its next safe point) read the same time: the interval is 0
   and that loop never ends. It hung the game at full speed (docs/PROGRESO_AUTONOMO.md, block 12).

   Here a read that does not pass the last one returns the last one plus 20 ns, a little more than one of the
   console's ticks (16.5 ns): the clock goes on as the console's does, ahead of the real one by at most 20 ns per
   such read, which the real clock overtakes at its next step. Other clocks pass through unchanged. */
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

int __real_clock_gettime(clockid_t clock, struct timespec* ts);

static _Atomic int64_t sLastMonotonicNs;

int __wrap_clock_gettime(clockid_t clock, struct timespec* ts) {
    const int result = __real_clock_gettime(clock, ts);
    if (result != 0 || clock != CLOCK_MONOTONIC) {
        return result;
    }
    int64_t now = (int64_t)ts->tv_sec * 1000000000 + ts->tv_nsec;
    int64_t last = atomic_load_explicit(&sLastMonotonicNs, memory_order_relaxed);
    for (;;) {
        const int64_t next = now > last ? now : last + 20;
        if (atomic_compare_exchange_weak_explicit(&sLastMonotonicNs, &last, next, memory_order_relaxed,
                                                  memory_order_relaxed)) {
            now = next;
            break;
        }
    }
    ts->tv_sec = (time_t)(now / 1000000000);
    ts->tv_nsec = (long)(now % 1000000000);
    return 0;
}
