/* The game's EFB copies to textures, noted for the diagnostic view (linked with --wrap=GXSetTexCopyDst and
   --wrap=GXCopyTex, web/cmake/SmgGame.cmake): the page with ?debug=1 shows, each frame, the largest copy as Aurora
   made it (web/host/aurora_bridge.cpp). The copies themselves are unchanged. */
#include <revolution/gx.h>

/* The renderer's side (web/host/aurora_bridge.cpp); nothing without one */
__attribute__((weak)) void smg_host_copy_noted(u32 physical, u16 width, u16 height, u32 format) {
    (void)physical;
    (void)width;
    (void)height;
    (void)format;
}

static u16 sWidth, sHeight;
static u32 sFormat;

void __real_GXSetTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap);
void __real_GXCopyTex(void* dest, GXBool clear);

void __wrap_GXSetTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap) {
    __real_GXSetTexCopyDst(width, height, format, mipmap);
    sWidth = width;
    sHeight = height;
    sFormat = (u32)format;
}

void __wrap_GXCopyTex(void* dest, GXBool clear) {
    __real_GXCopyTex(dest, clear);
    smg_host_copy_noted(GX_PHY_ADDR(dest), sWidth, sHeight, sFormat);
}
