/* The web host's default answers (web/host/host_services.c) that change what the game does, against those of Nebula's
   host built without a renderer (third_party/nebula/native/host/host_main.cpp). The first real run on a phone drew
   the HOME Menu over every frame: NEBULA_HBM_VIEW answered 0 (open) instead of -1.
     emcc -I<nebula>/native/include web/tests/host_services_test.c web/host/host_services.c (run_os_tests.sh) */
#include <stdio.h>

#include "host_services.h"

/* The game module's side, which the default answers call: not reached here */
void nebula_game_disc_complete(uint32_t slot, int32_t result) { (void)slot; (void)result; }
void nebula_game_fs_complete(uint32_t slot, int32_t result) { (void)slot; (void)result; }
void nebula_game_gx_event(int finish, uint16_t token) { (void)finish; (void)token; }
void nebula_game_gx_release(size_t size) { (void)size; }

static int failures;

static void check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

int main(void) {
    NebulaHostServices s;
    smg_host_default_services(&s);
    void* u = s.user;
    check(s.hbm(u, NEBULA_HBM_VIEW, 1) == -1, "HOME Menu: not shown (NEBULA_HBM_VIEW -1)");
    check(s.hbm(u, NEBULA_HBM_POLL, 0) == 0, "HOME Menu: closes at once (NEBULA_HBM_POLL 0)");
    check(s.sc_get(u, NEBULA_SC_LANGUAGE) == 1, "system language: English");
    check(s.sc_get(u, NEBULA_SC_ASPECT_RATIO) == 0, "aspect ratio: 4:3 (the page's 640x480)");
    check(s.sc_get(u, NEBULA_SC_SENSOR_BAR_POSITION) == 1 && s.sc_get(u, NEBULA_SC_DPD_SENSITIVITY) == 3,
          "sensor bar above, sensitivity 3");
    check(s.option(u, NEBULA_OPTION_TEXT_SPEED) == 1, "text speed 1");
    check(s.option(u, NEBULA_OPTION_LOD_SCALE) == 1, "LOD scale 1 (the console's resolution)");
    check(s.option(u, NEBULA_OPTION_DRAW_IDENTITIES) == 0 && s.option(u, NEBULA_OPTION_BLOOM) == 0,
          "no draw identities, the console's bloom");
    const uint32_t release = 3u << 16;
    const uint32_t other = 1u << 16;
    check(s.audio_dsp(u, &release, 0) == 1 && s.audio_dsp(u, &other, 0) == 0 && s.audio_dsp(u, &release, 1) == 0,
          "DSP: the release of a halted subframe completes it");
    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
