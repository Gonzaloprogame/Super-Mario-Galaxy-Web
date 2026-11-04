/* The game's clock (OSGetTick, Nebula's native/sdk/os/os_native.c over CLOCK_MONOTONIC) in wasm: two reads in a row
   must differ, as on the console, where the time base counts at 60.75 MHz. With the browser's clock alone (a few
   microseconds of resolution) they could be equal, and the game's frame timing loop never ended on a 0 interval
   (MainLoopFramework::calcCombinationRatio; web/platform/monotonic_clock_wasm.c). Built by web/tests/run_os_tests.sh
   with and without the wrapper: the plain build only reports (it shows the problem), the wrapped one must pass. */
#include <revolution/os.h>
#include <stdio.h>
#include <stdlib.h>

#include "nebula_sdk.h"

/* No host: the OS core runs on its own, as in native/sdk/os/tests/os_scheduler_tests.c */
const struct NebulaHostServices* nebula_host(void) {
    return NULL;
}

/* On the game's thread, after Nebula's boot has started its clock (os_native.c, time_init) */
static void game_main(void) {
    enum { kReads = 1000000 };
    unsigned equal = 0, backwards = 0;
    OSTick previous = OSGetTick();
    for (int i = 0; i < kReads; i++) {
        const OSTick now = OSGetTick();
        const int32_t step = (int32_t)(now - previous);
        if (step == 0) {
            equal++;
        } else if (step < 0) {
            backwards++;
        }
        previous = now;
    }
    printf("%d reads of OSGetTick in a row: %u equal to the one before, %u going back\n", kReads, equal, backwards);
#if defined(SMG_TEST_WRAPPED_CLOCK)
    if (equal != 0 || backwards != 0) {
        printf("FAIL: two reads of the game's clock in a row must differ (the console's time base never repeats)\n");
        exit(1);
    }
    printf("PASS: every read of the game's clock passes the one before\n");
#endif
    exit(0);
}

int main(void) {
    nebula_os_boot(game_main);
}
