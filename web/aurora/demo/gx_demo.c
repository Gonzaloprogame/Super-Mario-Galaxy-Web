/* A GX scene through Aurora on the browser's WebGPU: the first check of the web renderer (docs/BACKEND_WEBGPU.md).

   The scene is plain on purpose, so a test can check it pixel by pixel: the copy clear colour, then one triangle in
   the middle of the 640x480 EFB, drawn with vertex colours through one TEV stage (GX_PASSCLR), all three vertices the
   same colour. It goes through the parts of Aurora the game's stream needs: the command processor, the vertex and
   uniform buffers, the TEV shader generator (WGSL), the pipeline cache, the EFB and the copy to the canvas.

   With ?headless=1 the EFB is rendered offscreen (AURORA_SURFACE_HEADLESS) and checked from inside: three EFB peeks
   (corner, triangle, background) through Aurora's readback API, the path the game's own GXPeekARGB/GXPeekZ take (the
   lens flare). Headless Chromium on a software adapter loses any WebGPU device that presents to a canvas
   (docs/BACKEND_WEBGPU.md), so the tests use this mode; a browser with a GPU shows the canvas.

   window.gxDemo (read by web/tests/aurora_smoke.mjs): {frames, headless, peeks: {corner, centre, outside}, error}. */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>

/* The colours the test expects. */
#define CLEAR_R 24
#define CLEAR_G 32
#define CLEAR_B 96
#define TRI_R 240
#define TRI_G 176
#define TRI_B 16

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
    static const char* const names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
    (void)len;
    fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s: %s] %s\n", names[level], module, message);
    if (level == LOG_FATAL) {
        EM_ASM({ globalThis.gxDemo = Object.assign(globalThis.gxDemo || {}, {error : UTF8ToString($0)}); }, message);
        abort();
    }
}

/* ?tev=reg: the triangle's colour from TEV register C0 instead of the rasterised vertex colour, so a test can tell the
   vertex colour path from the TEV and uniform paths. */
static int sTevRegister;

static void draw(void) {
    GXSetCopyClear((GXColor){CLEAR_R, CLEAR_G, CLEAR_B, 255}, GX_MAX_Z24);
    GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
    GXSetScissor(0, 0, 640, 480);

    /* Orthographic projection over [-1, 1] in x and y; identity position matrix. */
    const f32 projection[4][4] = {
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, -1.f, -1.f},
        {0.f, 0.f, 0.f, 1.f},
    };
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    const f32 identity[3][4] = {{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}};
    GXLoadPosMtxImm(identity, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    if (sTevRegister) {
        GXSetTevColor(GX_TEVREG0, (GXColor){TRI_R, TRI_G, TRI_B, 255});
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    } else {
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);

    /* A triangle around the centre: (-0.5, -0.5), (0.5, -0.5), (0, 0.6), at z -0.5. GX's camera looks down -z: this
       projection maps z to -z - 1, and GX clips outside [-1, 0], so the triangle must sit at a negative z. */
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.f, 0.6f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXEnd();
}

/* EFB points (640x480, origin top left): a corner, the triangle's centroid (0, -0.133 in clip space), background. */
static const AuroraReadbackPeekRequest kPeeks[] = {
    {0, AURORA_READBACK_PEEK_ARGB, 16, 16},
    {1, AURORA_READBACK_PEEK_ARGB, 320, 272},
    {2, AURORA_READBACK_PEEK_ARGB, 96, 408},
};

/* The results of the latest completed capture, then a new capture of this frame's EFB. */
static void peek_efb(AuroraReadbackStream stream) {
    u32 argb[3] = {0, 0, 0};
    AuroraReadbackDest dests[3] = {
        {0, &argb[0], sizeof(u32), GX_FALSE},
        {1, &argb[1], sizeof(u32), GX_FALSE},
        {2, &argb[2], sizeof(u32), GX_FALSE},
    };
    if (AuroraReadbackGet(stream, dests, 3) && dests[0].found && dests[1].found && dests[2].found) {
        /* The object literals are parenthesised: EM_ASM is a macro, and only parentheses protect their commas. */
        EM_ASM({ globalThis.gxDemo.peeks = ({corner : $0 >>> 0, centre : $1 >>> 0, outside : $2 >>> 0}); }, argb[0],
               argb[1], argb[2]);
    }
    AuroraReadbackCapturePeeks(stream, kPeeks, 3);
}

int main(int argc, char* argv[]) {
    const int headless = EM_ASM_INT({ return new URLSearchParams(location.search).get('headless') === '1' ? 1 : 0; });
    sTevRegister = EM_ASM_INT({ return new URLSearchParams(location.search).get('tev') === 'reg' ? 1 : 0; });
    const AuroraConfig config = {
        .appName = "gx_demo",
        .logCallback = &log_callback,
        /* Software WebGPU (SwiftShader, Chromium's fallback adapter) is accepted: the tests run without a GPU. */
        .allowCpuAdapter = true,
        /* One thread: WebGPU objects belong to the thread that made them (patches/0004). The FIFO is processed when
           the frame ends. */
        .gxProcessingMode = GX_PROCESSING_DRAIN,
        .surface = {.type = headless ? AURORA_SURFACE_HEADLESS : AURORA_SURFACE_SDL_MANAGED,
                    .width = headless ? 640 : 0,
                    .height = headless ? 480 : 0},
    };
    aurora_initialize(argc, argv, &config);
    /* As every game does at boot: GXInit sets GX's default state, including the register shadows the GXSet* calls
       update and write (each shadow carries its BP register's address; without GXInit, TEV state goes to BP 0). */
    static u8 fifo[256 * 1024] __attribute__((aligned(32)));
    GXInit(fifo, sizeof(fifo));
    EM_ASM({ globalThis.gxDemo = ({frames : 0, headless : !!$0}); }, headless);
    const AuroraReadbackStream stream = AuroraReadbackCreateStream();

    unsigned frames = 0;
    for (;;) {
        const AuroraEvent* event = aurora_update();
        int exiting = 0;
        while (event != NULL && event->type != AURORA_NONE) {
            exiting |= event->type == AURORA_EXIT;
            ++event;
        }
        if (exiting) {
            break;
        }
        if (aurora_begin_frame()) {
            draw();
            peek_efb(stream);
            aurora_end_frame();
            ++frames;
            EM_ASM({ globalThis.gxDemo.frames = $0; }, frames);
        }
        /* Back to the browser between frames: the canvas presents what was rendered when the task ends. */
        emscripten_sleep(16);
    }
    aurora_shutdown();
    return 0;
}
