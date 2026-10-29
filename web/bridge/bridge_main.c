/* A host for the bridge test (web/tests/bridge_smoke.mjs): the game module's native OS core runs the test scene
   (web/bridge/gx_bridge_scene.c) instead of the game, and its GX stream goes to Aurora (web/host/aurora_bridge.cpp).
   Reports the colours read back from the XFB and the EFB to the page once they are in (or after a frame limit), then stops the scene.
   It does not exit unless asked to (?exit=1): an exit stops Aurora's render thread first (smg_bridge_exit), since with
   it running one of the C++ static destructors that exit() runs waits on it forever (the page's thread, paused in
   Chrome's debugger: exitOnMainThread -> exitRuntime -> __funcs_on_exit). Runs in the browser with PROXY_TO_PTHREAD,
   on a page served with COOP/COEP. */
#include <emscripten.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <nebula_game_api.h>

#include "aurora_bridge.h"
#include "host_services.h"

void nebula_os_boot(void (*gameMain)(void)); /* native/sdk/os/os_native.c */
void smg_bridge_scene_main(void);           /* web/bridge/gx_bridge_scene.c */
const void* smg_bridge_scene_xfb(void);     /* web/bridge/gx_bridge_scene.c */
void smg_bridge_scene_peeks(uint32_t* out); /* web/bridge/gx_bridge_scene.c */

/* XFB points (640x480, the display copy of the frame): a corner, the direct triangle's centroid, background, the
   indexed triangle's centroid (bottom right), the display list triangle's (top right), the RGB565 square's left and
   right halves (left edge), the C8 square's centre (right edge), and the EFB copy's rectangle (bottom centre): top
   left, bottom left, bottom right; and where the marker drawn after the display copy is (it must not be in the XFB). */
enum { kXfbPointCount = 12 };
static const uint16_t kXfbPoints[kXfbPointCount][2] = {{16, 16},  {320, 272}, {96, 408},  {568, 444},
                                                       {568, 36}, {44, 240},  {100, 240}, {568, 240},
                                                       {272, 405}, {272, 447}, {368, 447}, {320, 48}};
static const char* const kXfbNames[kXfbPointCount] = {"corner",   "centre",   "outside",  "indexed",
                                                      "displayList", "texLeft", "texRight", "texC8",
                                                      "copyTop",  "copyLeft", "copyRight", "marker"};
/* EFB points at the frame's end, after the display copy cleared it: a corner, where the direct triangle was, and the
   marker drawn after the copy (top centre). */
enum { kEfbPointCount = 3 };
static const uint16_t kEfbPoints[kEfbPointCount][2] = {{16, 16}, {320, 272}, {320, 48}};
static const char* const kEfbNames[kEfbPointCount] = {"corner", "centre", "marker"};
enum { kFrameLimit = 600 };

static void svc_gx_submit(void* user, const uint8_t* bytes, size_t size) {
    (void)user;
    smg_bridge_submit(bytes, size);
}

static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    smg_bridge_pe_marker(finish, token);
}

static uint32_t svc_gx_peek_efb(void* user, uint16_t x, uint16_t y, uint32_t type, uint16_t token) {
    (void)user;
    return smg_bridge_peek_efb(x, y, type, token);
}

static void svc_vi_frame(void* user, uint32_t xfb, int black, uint32_t retrace) {
    (void)user;
    (void)retrace;
    smg_bridge_vi_frame(xfb, black);
}

/* The display clock: a 60 Hz retrace, as the engine's host drives it (web/host/headless_host.c). */
static void* retrace_main(void* arg) {
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        next.tv_nsec += 16666667;
        if (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        nebula_game_vi_retrace();
    }
    return NULL;
}

/* The scene's own peeks (GXPeekARGB/GXPeekZ): in the token's callback, then after the display copy. */
enum { kGamePeekCount = 5 };
static const char* const kGamePeekNames[kGamePeekCount] = {"tokenColour", "tokenDepth", "afterCopyColour",
                                                           "afterCopyDepth", "afterCopyMarker"};

static char sReport[2048];

static int append_points(int at, const char* label, const char* const* names, const uint32_t* argb, int count) {
    at += snprintf(sReport + at, sizeof(sReport) - at, "%s\"%s\":{", at > 1 ? "," : "", label);
    for (int i = 0; i < count; i++) {
        at += snprintf(sReport + at, sizeof(sReport) - at, "%s\"%s\":%u", i ? "," : "", names[i], argb[i]);
    }
    return at + snprintf(sReport + at, sizeof(sReport) - at, "}");
}

/* The test of a renderer failure (bridge_smoke.mjs, ?fault=1): the scene writes a byte that is not a GX command. */
int smg_bridge_fault_requested(void) {
    static int requested = -1;
    if (requested < 0) {
        requested = MAIN_THREAD_EM_ASM_INT({ return new URLSearchParams(location.search).get('fault') === '1' ? 1 : 0; });
    }
    return requested;
}

/* The test of the process's end after Aurora has drawn (bridge_smoke.mjs, ?exit=1): once reported, the scene exits
   with code 4 as a host does (smg_bridge_exit), and the page must hear it. */
static int exit_requested(void) {
    static int requested = -1;
    if (requested < 0) {
        requested = MAIN_THREAD_EM_ASM_INT({ return new URLSearchParams(location.search).get('exit') === '1' ? 1 : 0; });
    }
    return requested;
}

/* Called by the scene after each GXDrawDone, on the game's thread. */
int smg_bridge_scene_frame_done(void) {
    static int reported;
    if (reported) {
        if (exit_requested()) {
            printf("[bridge] exiting with code 4\n");
            smg_bridge_exit(4);
        }
        return 0; /* keeps drawing, as a game does: the page goes on painting the XFB the VI shows */
    }
    uint32_t xfb[kXfbPointCount];
    uint32_t efb[kEfbPointCount];
    uint32_t game[kGamePeekCount];
    uint32_t captured = 0;
    const uint32_t frames = smg_bridge_frames();
    /* The game's peeks of this frame, all answered from captures (not the 0 of a point never captured) */
    const int gamePeeks = smg_bridge_game_peeks(&captured);
    const int gameAnswered = gamePeeks == kGamePeekCount && captured == (uint32_t)gamePeeks;
    if (gameAnswered && smg_bridge_get_xfb(kXfbPoints, kXfbPointCount, xfb) &&
        smg_bridge_get_peeks(efb, kEfbPointCount)) {
        smg_bridge_scene_peeks(game);
        int at = snprintf(sReport, sizeof(sReport), "{\"frames\":%u", frames);
        at = append_points(at, "xfb", kXfbNames, xfb, kXfbPointCount);
        at = append_points(at, "efb", kEfbNames, efb, kEfbPointCount);
        at = append_points(at, "game", kGamePeekNames, game, kGamePeekCount);
        at += snprintf(sReport + at, sizeof(sReport) - at, ",\"presented\":%u,\"viXfb\":%u", smg_bridge_presented(),
                       smg_bridge_vi_xfb());
        snprintf(sReport + at, sizeof(sReport) - at, "}");
        printf("[bridge] %s\n", sReport);
        /* sReport is not written again (reported) */
        MAIN_THREAD_ASYNC_EM_ASM({ globalThis.bridge.result = JSON.parse(UTF8ToString($0)); }, sReport);
        reported = 1;
        return 0;
    }
    if (frames >= kFrameLimit) {
        printf("[bridge] frames %u, no EFB or XFB readback, or the game's peeks not captured (%d, %u captured)\n",
               frames, gamePeeks, captured);
        MAIN_THREAD_ASYNC_EM_ASM(
            { globalThis.bridge.result = ({frames : $0, error : 'no EFB or XFB readback, or game peeks not captured'}); },
            frames);
        return 1;
    }
    return 0;
}

int main(void) {
    static NebulaHostServices services;
    smg_host_default_services(&services);
    services.gx_submit = svc_gx_submit;
    services.gx_pe_marker = svc_gx_pe_marker;
    services.gx_peek_efb = svc_gx_peek_efb;
    services.vi_frame = svc_vi_frame;
    if (nebula_game_attach(&services) == 0) {
        fprintf(stderr, "[bridge] the game module refused the host's services\n");
        return 2;
    }
    if (!smg_bridge_start(1)) {
        fprintf(stderr, "[bridge] Aurora did not start\n");
        return 2;
    }
    smg_bridge_set_peeks(kEfbPoints, kEfbPointCount);
    smg_bridge_set_xfb_probe(smg_bridge_scene_xfb(), 640, 480);
    pthread_t retrace;
    if (pthread_create(&retrace, NULL, retrace_main, NULL) != 0) {
        fprintf(stderr, "[bridge] the retrace clock cannot start\n");
        return 2;
    }
    printf("[bridge] Aurora ready; booting the game module's OS core with the test scene\n");
    nebula_os_boot(smg_bridge_scene_main); /* never returns */
    return 0;
}
