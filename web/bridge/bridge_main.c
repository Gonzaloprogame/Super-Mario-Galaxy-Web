/* A host for the bridge test (web/tests/bridge_smoke.mjs): the game module's native OS core runs the test scene
   (web/bridge/gx_bridge_scene.c) instead of the game, and its GX stream goes to Aurora (web/host/aurora_bridge.cpp).
   Reports the EFB colours read back to the page once they are in (or after a frame limit), then stops the scene.
   It does not exit: exit() runs the C++ static destructors on the page's thread while Aurora's render thread still
   runs, and one of them waits on it forever (the page's thread, paused in Chrome's debugger: exitOnMainThread ->
   exitRuntime -> __funcs_on_exit). Runs in the browser with PROXY_TO_PTHREAD, on a page served with COOP/COEP. */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>

#include <nebula_game_api.h>

#include "aurora_bridge.h"
#include "host_services.h"

void nebula_os_boot(void (*gameMain)(void)); /* native/sdk/os/os_native.c */
void smg_bridge_scene_main(void);           /* web/bridge/gx_bridge_scene.c */

/* EFB points (640x480): a corner, the direct triangle's centroid, background, the indexed triangle's centroid (bottom
   right), the display list triangle's (top right), the RGB565 square's left and right halves (left edge) and the C8
   square's centre (right edge), and the EFB copy's rectangle (bottom centre): top left, bottom left, bottom right. */
enum { kPeekCount = 11 };
static const uint16_t kPeeks[kPeekCount][2] = {{16, 16},  {320, 272}, {96, 408},  {568, 444}, {568, 36}, {44, 240},
                                               {100, 240}, {568, 240}, {272, 405}, {272, 447}, {368, 447}};
enum { kFrameLimit = 600 };

static void svc_gx_submit(void* user, const uint8_t* bytes, size_t size) {
    (void)user;
    smg_bridge_submit(bytes, size);
}

static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    smg_bridge_pe_marker(finish, token);
}

/* Called by the scene after each GXDrawDone, on the game's thread. */
int smg_bridge_scene_frame_done(void) {
    uint32_t argb[kPeekCount];
    const uint32_t frames = smg_bridge_frames();
    if (smg_bridge_get_peeks(argb, kPeekCount)) {
        printf("[bridge] frames %u peeks corner=0x%08x centre=0x%08x outside=0x%08x indexed=0x%08x displayList=0x%08x "
               "texLeft=0x%08x texRight=0x%08x texC8=0x%08x copyTop=0x%08x copyLeft=0x%08x copyRight=0x%08x\n",
               frames, argb[0], argb[1], argb[2], argb[3], argb[4], argb[5], argb[6], argb[7], argb[8], argb[9],
               argb[10]);
        MAIN_THREAD_ASYNC_EM_ASM(
            {
                globalThis.bridge.result = ({
                    frames : $0,
                    corner : $1 >>> 0,
                    centre : $2 >>> 0,
                    outside : $3 >>> 0,
                    indexed : $4 >>> 0,
                    displayList : $5 >>> 0,
                    texLeft : $6 >>> 0,
                    texRight : $7 >>> 0,
                    texC8 : $8 >>> 0,
                    copyTop : $9 >>> 0,
                    copyLeft : $10 >>> 0,
                    copyRight : $11 >>> 0
                });
            },
            frames, argb[0], argb[1], argb[2], argb[3], argb[4], argb[5], argb[6], argb[7], argb[8], argb[9],
            argb[10]);
        return 1;
    }
    if (frames >= kFrameLimit) {
        printf("[bridge] frames %u, no EFB readback\n", frames);
        MAIN_THREAD_ASYNC_EM_ASM({ globalThis.bridge.result = ({frames : $0, error : 'no EFB readback'}); }, frames);
        return 1;
    }
    return 0;
}

int main(void) {
    static NebulaHostServices services;
    smg_host_default_services(&services);
    services.gx_submit = svc_gx_submit;
    services.gx_pe_marker = svc_gx_pe_marker;
    if (nebula_game_attach(&services) == 0) {
        fprintf(stderr, "[bridge] the game module refused the host's services\n");
        return 2;
    }
    if (!smg_bridge_start(1)) {
        fprintf(stderr, "[bridge] Aurora did not start\n");
        return 2;
    }
    smg_bridge_set_peeks(kPeeks, kPeekCount);
    printf("[bridge] Aurora ready; booting the game module's OS core with the test scene\n");
    nebula_os_boot(smg_bridge_scene_main); /* never returns */
    return 0;
}
