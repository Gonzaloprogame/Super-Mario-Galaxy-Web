/* The bridge's test scene, drawn with the game's own GX library (the Revolution SDK in the game module, as Petari
   decompiles it and Nebula builds it): built with the game module's options (web/cmake/SmgGame.cmake). No game code
   or data: a plain triangle on the copy clear colour, the same scene as web/aurora/demo/gx_demo.c drawn through
   Aurora's own GX API, so the two can be compared. The GX calls write the console's FIFO into Nebula's command ring;
   GXDrawDone waits for the host's finish (web/host/aurora_bridge.cpp).

   Two more triangles, each in its own corner and colour, are drawn from memory the stream names by physical address,
   as the game's models are: one from indexed vertex arrays (GXSetArray; big-endian, as the game's data files hold
   them), one from a display list (GXCallDisplayList). */
#include <revolution/gx.h>
#include <revolution/gx/GXRegs.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

/* The EFB's largest 24-bit depth (the SDK's GX_MAX_Z24, which Petari's headers do not define). */
#define MAX_Z24 0x00FFFFFF

/* The colours web/tests/bridge_smoke.mjs expects. */
#define CLEAR_R 24
#define CLEAR_G 32
#define CLEAR_B 96
#define TRI_R 240
#define TRI_G 176
#define TRI_B 16
#define IDX_R 32
#define IDX_G 200
#define IDX_B 64
#define DL_R 200
#define DL_G 48
#define DL_B 160

/* The host's test hook, between frames: returns nonzero to stop (web/bridge/bridge_main.c). */
int smg_bridge_scene_frame_done(void);

static u8 sFifo[256 * 1024] __attribute__((aligned(32)));

/* Indexed arrays: four positions (x, y, z as big-endian f32) and two RGBA8 colours. Entry 0 of each is a decoy that
   the draw does not index (a misread index would put it over the centre triangle, in white); the triangle is entries
   1, 3, 2 in the bottom-right corner, so the array must be read up to its last entry. */
static u32 sPositions[4 * 3] __attribute__((aligned(32)));
static u8 sColours[2 * 4] __attribute__((aligned(32))) = {255, 255, 255, 255, IDX_R, IDX_G, IDX_B, 255};

/* A display list: a triangle in the top-right corner (recorded once, called every frame). */
static u8 sDisplayList[128] __attribute__((aligned(32)));
static u32 sDisplayListSize;

static u32 big_endian_f32(f32 value) {
    union {
        f32 f;
        u32 u;
    } bits;
    bits.f = value;
    return __builtin_bswap32(bits.u);
}

/* An 8-bit colour index: Petari's GXVert.h has no GXColor1x8 (the game does not call it); the SDK's FIFO write. */
static void colour_index8(u8 index) {
    GX_WRITE_U8(index);
}

static void set_direct_format(void) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

static void prepare_memory(void) {
    static const f32 positions[4][3] = {
        {0.f, 0.f, -0.5f}, {0.6f, -0.95f, -0.5f}, {0.775f, -0.65f, -0.5f}, {0.95f, -0.95f, -0.5f}};
    for (int i = 0; i < 4; i++) {
        for (int c = 0; c < 3; c++) {
            sPositions[i * 3 + c] = big_endian_f32(positions[i][c]);
        }
    }
    DCFlushRange(sPositions, sizeof(sPositions));
    DCFlushRange(sColours, sizeof(sColours));

    set_direct_format();
    GXBeginDisplayList(sDisplayList, sizeof(sDisplayList));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(0.6f, 0.95f, -0.5f);
    GXColor4u8(DL_R, DL_G, DL_B, 255);
    GXPosition3f32(0.95f, 0.95f, -0.5f);
    GXColor4u8(DL_R, DL_G, DL_B, 255);
    GXPosition3f32(0.775f, 0.65f, -0.5f);
    GXColor4u8(DL_R, DL_G, DL_B, 255);
    GXEnd();
    sDisplayListSize = GXEndDisplayList();
    OSReport("[bridge] display list: %u bytes\n", sDisplayListSize);
}

static void draw(void) {
    GXSetCopyClear((GXColor){CLEAR_R, CLEAR_G, CLEAR_B, 255}, MAX_Z24);
    GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
    GXSetScissor(0, 0, 640, 480);

    /* Orthographic projection over [-1, 1]; identity position matrix (Mtx44 and Mtx as the SDK takes them). */
    Mtx44 projection = {
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, -1.f, -1.f},
        {0.f, 0.f, 0.f, 1.f},
    };
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    Mtx identity = {{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}};
    GXLoadPosMtxImm(identity, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    set_direct_format();

    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);

    /* (-0.5, -0.5), (0.5, -0.5), (0, 0.6) at z -0.5 (GX clips camera-space z outside [-1, 0] after this projection). */
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.f, 0.6f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXEnd();

    /* The display list's triangle, in the same vertex format. */
    GXCallDisplayList(sDisplayList, sDisplayListSize);

    /* The indexed triangle: 8-bit indices into the arrays. */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
    GXSetArray(GX_VA_POS, sPositions, 3 * sizeof(u32));
    GXSetArray(GX_VA_CLR0, sColours, 4);
    GXInvalidateVtxCache();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(1);
    colour_index8(1);
    GXPosition1x8(3);
    colour_index8(1);
    GXPosition1x8(2);
    colour_index8(1);
    GXEnd();
}

/* The "game main" the native OS core boots (nebula_os_boot), on the game's boot thread. */
void smg_bridge_scene_main(void) {
    GXInit(sFifo, sizeof(sFifo));
    prepare_memory();
    for (;;) {
        draw();
        GXDrawDone();
        if (smg_bridge_scene_frame_done()) {
            break;
        }
    }
    OSReport("[bridge] scene stopped\n");
    for (;;) {
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
}
