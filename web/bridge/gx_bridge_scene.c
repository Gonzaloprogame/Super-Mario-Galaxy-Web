/* The bridge's test scene, drawn with the game's own GX library (the Revolution SDK in the game module, as Petari
   decompiles it and Nebula builds it): built with the game module's options (web/cmake/SmgGame.cmake). No game code
   or data: a plain triangle on the copy clear colour, the same scene as web/aurora/demo/gx_demo.c drawn through
   Aurora's own GX API, so the two can be compared. The GX calls write the console's FIFO into Nebula's command ring;
   GXDrawDone waits for the host's finish (web/host/aurora_bridge.cpp).

   Two more triangles, each in its own corner and colour, are drawn from memory the stream names by physical address,
   as the game's models are: one from indexed vertex arrays (GXSetArray; big-endian, as the game's data files hold
   them), one from a display list (GXCallDisplayList).

   Two textured squares on the left and right edges, sampled with point filtering: an 8x8 RGB565 texture whose left
   half is one colour and right half another (the format, its byte order and its 4x4-pixel tiles), and an 8x8 C8
   texture whose every texel is index 5 of an RGB565 TLUT loaded into TMEM (every other entry is black). The textures
   are generated here: no game data.

   Last, an EFB copy (GXCopyTex): the 64x64 pixels at (40, 136), whose top half is background and whose bottom half is
   the top of the RGB565 square (magenta on the left, cyan on the right), copied as RGBA8 to a buffer, then drawn as a
   texture on a rectangle at the bottom centre. The copy must keep its place, orientation and colours.

   The frame ends as a game's does: GXCopyDisp copies the whole EFB to an XFB (the frame the VI shows) and clears the
   EFB. A marker drawn after it, at the top centre, must be in the EFB and not in the XFB; the rest of the EFB must
   hold the clear colour. web/tests/bridge_smoke.mjs reads the scene's colours from the XFB.

   EFB peeks, as the game's lens flare reads the EFB: the direct triangle writes depth, a draw-sync token follows the
   scene, and its callback peeks the triangle's colour and depth (GXPeekARGB, GXPeekZ). After the display copy, which
   also clears depth, the scene peeks again outside any callback: the triangle's place (now the clear colour and the
   largest depth) and the marker. The host answers from captures of earlier frames at the same stream positions. */
#include <string.h>
#include <revolution/gx.h>
#include <revolution/gx/GXRegs.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include <revolution/vi.h>

#include <nebula_gx_fifo.h>

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
/* RGB565 colours that expand exactly to 8 bits: (31, 0, 31), (0, 63, 31), (31, 63, 0). */
#define TEX_LEFT_565 0xF81F  /* (255, 0, 255) */
#define TEX_RIGHT_565 0x07FF /* (0, 255, 255) */
#define TLUT_565 0xFFE0      /* (255, 255, 0) */
#define TLUT_INDEX 5
/* The marker drawn after the display copy. */
#define MARK_R 250
#define MARK_G 120
#define MARK_B 0
/* ?clearquad=1's squares */
#define QUAD_A_R 90
#define QUAD_A_G 220
#define QUAD_A_B 230
#define QUAD_B_R 230
#define QUAD_B_G 60
#define QUAD_B_B 90

/* The host's test hook, between frames: returns nonzero to stop (web/bridge/bridge_main.c). */
int smg_bridge_scene_frame_done(void);
/* The host asks for a renderer failure (the test of its report): 0xC8, neither a GX command nor a Nebula marker. */
int smg_bridge_fault_requested(void);
int smg_bridge_arrays_requested(void);
int smg_bridge_clear_quad_requested(void);

static u8 sFifo[256 * 1024] __attribute__((aligned(32)));

/* Indexed arrays: four positions (x, y, z as big-endian f32) and two RGBA8 colours. Entry 0 of each is a decoy that
   the draw does not index (a misread index would put it over the centre triangle, in white); the triangle is entries
   1, 3, 2 in the bottom-right corner, so the array must be read up to its last entry. */
static u32 sPositions[4 * 3] __attribute__((aligned(32)));
/* ?arrays=1: a 96 KB position array (zeros: a degenerate triangle), drawn many times a frame (kArrayDraws) */
enum { kBigPositions = 8192, kArrayDraws = 200 };
static u32 sBigPositions[kBigPositions * 3] __attribute__((aligned(32)));
static u8 sColours[2 * 4] __attribute__((aligned(32))) = {255, 255, 255, 255, IDX_R, IDX_G, IDX_B, 255};

/* A display list: a triangle in the top-right corner (recorded once, called every frame). */
static u8 sDisplayList[128] __attribute__((aligned(32)));
static u8 sDisplayListRecorded[128]; /* the list as recorded: written back before each call */
static u32 sDisplayListSize;

/* Textures and TLUT, in the console's formats (big-endian, tiled), 32-byte aligned as GX requires. */
static u8 sTexture565[8 * 8 * 2] __attribute__((aligned(32)));
static u8 sTextureC8[8 * 8] __attribute__((aligned(32)));
static u8 sTlut[16 * 2] __attribute__((aligned(32)));
static GXTexObj sTexObj565;
static GXTexObj sTexObjC8;
static GXTlutObj sTlutObj;

/* The EFB copy's destination (64x64 RGBA8): the game's memory, never written by the CPU. */
enum { kCopyLeft = 40, kCopyTop = 136, kCopySize = 64 };
static u8 sCopyBuffer[kCopySize * kCopySize * 4] __attribute__((aligned(32)));
static GXTexObj sTexObjCopy;

/* The XFB: the console's 2 bytes per pixel (YUV 4:2:2), allocated as a game does; Aurora keeps the copy on the GPU. */
enum { kXfbWidth = 640, kXfbHeight = 480 };
static u8 sXfb[kXfbWidth * kXfbHeight * 2] __attribute__((aligned(32)));

/* The scene's EFB peeks: in the token's callback, the triangle's colour and depth; after the display copy, the
   triangle's place (colour, depth) and the marker's colour. */
enum { kPeekToken = 0x5A5A };
static u32 sPeeks[5];

static void draw_sync_callback(u16 token) {
    if (token == kPeekToken) {
        GXPeekARGB(320, 272, &sPeeks[0]);
        GXPeekZ(320, 272, &sPeeks[1]);
    }
}

void smg_bridge_scene_peeks(u32* out) {
    for (int i = 0; i < 5; i++) {
        out[i] = sPeeks[i];
    }
}

/* For the host's probe (web/bridge/bridge_main.c). */
const void* smg_bridge_scene_xfb(void) {
    return sXfb;
}

static void store_be16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static u32 big_endian_f32(f32 value) {
    union {
        f32 f;
        u32 u;
    } bits;
    bits.f = value;
    return __builtin_bswap32(bits.u);
}

/* An 8-bit colour index: Petari's GXVert.h has no GXColor1x8 (the game does not call it); the SDK's FIFO write. */
/* A 16-bit position index (this SDK's GXVert.h has only GXPosition1x8): two big-endian bytes in the stream */
static void position_index16(u16 index) {
    __NebulaGXStore16(__NebulaGXReserve(2), index);
}

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

    /* RGB565: 4x4-pixel tiles of 32 bytes, row by row (tile x 0 is the left half of the 8x8 texture). */
    for (int tile = 0; tile < 4; tile++) {
        const u16 colour = (tile & 1) == 0 ? TEX_LEFT_565 : TEX_RIGHT_565;
        for (int texel = 0; texel < 16; texel++) {
            store_be16(sTexture565 + tile * 32 + texel * 2, colour);
        }
    }
    for (int i = 0; i < (int)sizeof(sTextureC8); i++) {
        sTextureC8[i] = TLUT_INDEX;
    }
    for (int i = 0; i < 16; i++) {
        store_be16(sTlut + i * 2, i == TLUT_INDEX ? TLUT_565 : 0x0000);
    }
    DCFlushRange(sTexture565, sizeof(sTexture565));
    DCFlushRange(sTextureC8, sizeof(sTextureC8));
    DCFlushRange(sTlut, sizeof(sTlut));
    GXInitTexObj(&sTexObj565, sTexture565, 8, 8, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&sTexObj565, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXInitTlutObj(&sTlutObj, sTlut, GX_TL_RGB565, 16);
    GXInitTexObjCI(&sTexObjC8, sTextureC8, 8, 8, GX_TF_C8, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0);
    GXInitTexObjLOD(&sTexObjC8, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXInitTexObj(&sTexObjCopy, sCopyBuffer, kCopySize, kCopySize, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&sTexObjCopy, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    OSReport("[bridge] textures at physical 0x%08x (RGB565), 0x%08x (C8), TLUT 0x%08x\n",
             OSCachedToPhysical(sTexture565), OSCachedToPhysical(sTextureC8), OSCachedToPhysical(sTlut));

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
    memcpy(sDisplayListRecorded, sDisplayList, sizeof(sDisplayList));
    OSReport("[bridge] display list: %u bytes\n", sDisplayListSize);
}

static void draw(void) {
    /* Nebula's host markers, which the game writes whatever the host's options (nebula_gx_fifo.h): a bloom phase and
       the EFB clear quad's. Not console commands: Aurora skips them (patch 0014); unskipped, the stream is misread
       ("unsupported primitive type 240", the game's first failure on a phone). */
    __NebulaGXBloomMarker(NEBULA_GX_BLOOM_NORMAL, 0, 0);
    __NebulaGXClearQuadMarker(MAX_Z24);
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

    /* (-0.5, -0.5), (0.5, -0.5), (0, 0.6) at z -0.5 (GX clips camera-space z outside [-1, 0] after this projection),
       writing its depth for the peeks; the other draws leave depth alone. */
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.5f, -0.5f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXPosition3f32(0.f, 0.6f, -0.5f);
    GXColor4u8(TRI_R, TRI_G, TRI_B, 255);
    GXEnd();
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    /* The display list's triangle, in the same vertex format. Right after the call the list's memory is overwritten
       with what Aurora read once from a list the game had reused (bytes ff f5 00 00 00 00 00 00 over and over, as the
       first stage loaded): the list's commands must already be in the stream (web/platform/gx_display_list_inline_wasm.c),
       not read from that memory later by the render thread. */
    memcpy(sDisplayList, sDisplayListRecorded, sizeof(sDisplayList));
    GXCallDisplayList(sDisplayList, sDisplayListSize);
    for (u32 i = 0; i < sizeof(sDisplayList); i += 8) {
        static const u8 kReused[8] = {0xff, 0xf5, 0, 0, 0, 0, 0, 0};
        memcpy(sDisplayList + i, kReused, sizeof(kReused));
    }

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

    /* ?arrays=1: as the game's models do, the vertex cache invalidated between draws of one big array (its last element
       reached): 200 uploads of 96 KB went past Aurora's 8 MiB storage buffer, and it aborted, as a stage did. Each
       upload is reused within the frame (Aurora patch 0015). */
    if (smg_bridge_arrays_requested()) {
        GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
        GXSetArray(GX_VA_POS, sBigPositions, 3 * sizeof(u32));
        for (int i = 0; i < kArrayDraws; ++i) {
            GXInvalidateVtxCache();
            GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
            position_index16(0);
            colour_index8(1);
            position_index16(1);
            colour_index8(1);
            position_index16(kBigPositions - 1);
            colour_index8(1);
            GXEnd();
        }
    }

    /* The textured squares: position and texture coordinate, the texel as is (GX_REPLACE). */
    GXLoadTlut(&sTlutObj, GX_TLUT0);
    GXLoadTexObj(&sTexObj565, GX_TEXMAP0);
    GXLoadTexObj(&sTexObjC8, GX_TEXMAP1);
    GXInvalidateTexAll();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    for (int map = 0; map < 2; map++) {
        /* x from -0.95 to -0.6 (RGB565, left edge) or from 0.6 to 0.95 (C8, right edge); y from -0.3 to 0.3. */
        const f32 x0 = map == 0 ? -0.95f : 0.6f;
        const f32 x1 = x0 + 0.35f;
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, map == 0 ? GX_TEXMAP0 : GX_TEXMAP1, GX_COLOR_NULL);
        GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        GXPosition3f32(x0, 0.3f, -0.5f);
        GXTexCoord2f32(0.f, 0.f);
        GXPosition3f32(x1, 0.3f, -0.5f);
        GXTexCoord2f32(1.f, 0.f);
        GXPosition3f32(x1, -0.3f, -0.5f);
        GXTexCoord2f32(1.f, 1.f);
        GXPosition3f32(x0, -0.3f, -0.5f);
        GXTexCoord2f32(0.f, 1.f);
        GXEnd();
    }

    /* The EFB copy, then its texture on the rectangle x -0.3..0.3, y -0.6..-0.95 (screen 224..416, 384..468). */
    GXSetTexCopySrc(kCopyLeft, kCopyTop, kCopySize, kCopySize);
    GXSetTexCopyDst(kCopySize, kCopySize, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(sCopyBuffer, GX_FALSE);
    GXPixModeSync();
    GXInvalidateTexAll();
    GXLoadTexObj(&sTexObjCopy, GX_TEXMAP2);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP2, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXBegin(GX_QUADS, GX_VTXFMT1, 4);
    GXPosition3f32(-0.3f, -0.6f, -0.5f);
    GXTexCoord2f32(0.f, 0.f);
    GXPosition3f32(0.3f, -0.6f, -0.5f);
    GXTexCoord2f32(1.f, 0.f);
    GXPosition3f32(0.3f, -0.95f, -0.5f);
    GXTexCoord2f32(1.f, 1.f);
    GXPosition3f32(-0.3f, -0.95f, -0.5f);
    GXTexCoord2f32(0.f, 1.f);
    GXEnd();

    /* ?clearquad=1: the game's EFB clear quad (MR::clearZBuffer, MainLoopFramework::clearEfb), after Nebula's marker:
       a quad filling a viewport, colour off, writing its Z texture's depth (the largest) with the compare always.
       Before it, a square in QUAD_A writes depth nearer than the triangle in QUAD_B drawn after it with LEQUAL. Where the
       viewport was (screen 96..224, 52..148) the depth is the largest again and QUAD_B shows; around it QUAD_A stays.
       Aurora has no Z textures: drawn as written, the quad wrote its own near depth and hid QUAD_B (Aurora patch
       0016), as the game's whole 3D world was hidden behind its clear quads. */
    if (smg_bridge_clear_quad_requested()) {
        set_direct_format();
        GXSetNumChans(1);
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        /* QUAD_A: screen 64..256, 20..180, at z -0.3 */
        GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-0.8f, 0.9167f, -0.3f);
        GXColor4u8(QUAD_A_R, QUAD_A_G, QUAD_A_B, 255);
        GXPosition3f32(-0.2f, 0.9167f, -0.3f);
        GXColor4u8(QUAD_A_R, QUAD_A_G, QUAD_A_B, 255);
        GXPosition3f32(-0.2f, 0.25f, -0.3f);
        GXColor4u8(QUAD_A_R, QUAD_A_G, QUAD_A_B, 255);
        GXPosition3f32(-0.8f, 0.25f, -0.3f);
        GXColor4u8(QUAD_A_R, QUAD_A_G, QUAD_A_B, 255);
        GXEnd();
        /* The clear quad, as the game draws it: its viewport, colour off, the Z texture's depth, at z -0.01 (near) */
        GXSetViewport(96.f, 52.f, 128.f, 96.f, 0.f, 1.f);
        GXSetColorUpdate(GX_FALSE);
        GXSetAlphaUpdate(GX_FALSE);
        GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
        GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        __NebulaGXClearQuadMarker(MAX_Z24);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.f, 1.f, -0.01f);
        GXColor4u8(0, 0, 0, 255);
        GXPosition3f32(1.f, 1.f, -0.01f);
        GXColor4u8(0, 0, 0, 255);
        GXPosition3f32(1.f, -1.f, -0.01f);
        GXColor4u8(0, 0, 0, 255);
        GXPosition3f32(-1.f, -1.f, -0.01f);
        GXColor4u8(0, 0, 0, 255);
        GXEnd();
        GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_TRUE);
        GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
        /* QUAD_B: screen 80..240, 36..164, at z -0.5, depth compare LEQUAL */
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-0.75f, 0.85f, -0.5f);
        GXColor4u8(QUAD_B_R, QUAD_B_G, QUAD_B_B, 255);
        GXPosition3f32(-0.25f, 0.85f, -0.5f);
        GXColor4u8(QUAD_B_R, QUAD_B_G, QUAD_B_B, 255);
        GXPosition3f32(-0.25f, 0.3167f, -0.5f);
        GXColor4u8(QUAD_B_R, QUAD_B_G, QUAD_B_B, 255);
        GXPosition3f32(-0.75f, 0.3167f, -0.5f);
        GXColor4u8(QUAD_B_R, QUAD_B_G, QUAD_B_B, 255);
        GXEnd();
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    }

    /* The token whose callback peeks the scene, before the display copy clears it. */
    GXSetDrawSync(kPeekToken);

    /* The frame to the XFB, clearing the EFB: colour, and depth with the depth update on (the clear uses the update
       masks, as on the console). */
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GXSetDispCopySrc(0, 0, kXfbWidth, kXfbHeight);
    GXSetDispCopyDst(kXfbWidth, kXfbHeight);
    GXSetDispCopyYScale(1.f);
    GXCopyDisp(sXfb, GX_TRUE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    /* The marker after the copy: x -0.1..0.1, y 0.7..0.9 (screen 288..352, 24..72), vertex colour. */
    set_direct_format();
    GXSetNumChans(1);
    GXSetNumTexGens(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-0.1f, 0.9f, -0.5f);
    GXColor4u8(MARK_R, MARK_G, MARK_B, 255);
    GXPosition3f32(0.1f, 0.9f, -0.5f);
    GXColor4u8(MARK_R, MARK_G, MARK_B, 255);
    GXPosition3f32(0.1f, 0.7f, -0.5f);
    GXColor4u8(MARK_R, MARK_G, MARK_B, 255);
    GXPosition3f32(-0.1f, 0.7f, -0.5f);
    GXColor4u8(MARK_R, MARK_G, MARK_B, 255);
    GXEnd();

    /* Peeks outside a callback: the cleared EFB where the triangle was, and the marker. */
    GXPeekARGB(320, 272, &sPeeks[2]);
    GXPeekZ(320, 272, &sPeeks[3]);
    GXPeekARGB(320, 48, &sPeeks[4]);
}

/* The "game main" the native OS core boots (nebula_os_boot), on the game's boot thread. */
void smg_bridge_scene_main(void) {
    VIInit();
    GXInit(sFifo, sizeof(sFifo));
    GXSetDrawSyncCallback(draw_sync_callback);
    prepare_memory();
    for (int frame = 0;; frame++) {
        draw();
        if (frame == 3 && smg_bridge_fault_requested()) {
            GX_WRITE_U8(0xC8); /* Aurora reads it as a draw of primitive 0xC8, which does not exist */
        }
        GXDrawDone();
        /* The XFB to show from the next retrace, as a game's frame ends */
        VISetNextFrameBuffer(sXfb);
        VISetBlack(FALSE);
        VIFlush();
        if (smg_bridge_scene_frame_done()) {
            break;
        }
    }
    OSReport("[bridge] scene stopped\n");
    for (;;) {
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
}
