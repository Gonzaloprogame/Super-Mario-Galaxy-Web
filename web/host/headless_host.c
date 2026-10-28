/* A headless host for the game module on WebAssembly: Nebula's NebulaHostServices (nebula_game_api.h) with no
   renderer, no audio and no input. It loads the user's game.pak, runs the game, consumes the GX command stream
   without drawing it (counting it), drives the 60 Hz retrace and reports what the game did. It is the first runtime
   check of the web build (docs/COMPILACION_WASM.md), in Node.js and in a browser worker.

   Built with SMG_HOST_AURORA (smg_web, web/engine) it draws: the GX stream, the PE markers, the EFB peeks and the
   VI's frame go to Aurora on the browser's WebGPU (web/host/aurora_bridge.cpp), which posts the XFB the VI shows to
   the page (web/shell/present.js).

   Written in C on purpose: the game module brings the game's own C++ library (MSL), whose std:: templates would merge
   with libc++'s in one wasm binary (Nebula keeps them in separate native modules for that reason).

   Usage (Node.js, built with NODERAWFS):  node smg_headless.js <game.pak> [seconds] */
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nebula_game_api.h>

#include "host_services.h"
#if defined(SMG_HOST_AURORA)
#include "aurora_bridge.h"
#endif

/* ---- game.pak: the decrypted game partition at its own offsets (crates/nebula-import) ---- */

typedef struct {
    FILE* file;
    uint64_t size;
    char id[7];
    uint8_t* fst;
    size_t fst_size;
    uint8_t* dol;
    size_t dol_size;
} Pak;

static Pak sPak;

static uint32_t load_be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Every end of the process: with Aurora, its render thread is stopped first (smg_bridge_stop). */
static void host_exit(int code) {
#if defined(SMG_HOST_AURORA)
    smg_bridge_stop();
#endif
    exit(code);
}

static void die(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fputs("[host] error: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    host_exit(2);
}

static int read_at(uint64_t offset, void* dst, size_t size) {
    if (offset > sPak.size || sPak.size - offset < size) {
        return 0;
    }
    if (fseeko(sPak.file, (off_t)offset, SEEK_SET) != 0) {
        return 0;
    }
    return fread(dst, 1, size, sPak.file) == size;
}

static uint8_t* read_block(uint64_t offset, size_t size, const char* what) {
    uint8_t* block = (uint8_t*)malloc(size);
    if (block == NULL || !read_at(offset, block, size)) {
        die("%s cannot be read from the game partition", what);
    }
    return block;
}

/* The checks Nebula's host makes on game.pak (native/host/host_main.cpp, open_pak): the release, then boot.bin's
   offsets of main.dol and the file table, in 4-byte units on Wii discs. */
static void open_pak(const char* path) {
    uint8_t boot[0x440];
    sPak.file = fopen(path, "rb");
    if (sPak.file == NULL) {
        die("%s cannot be opened", path);
    }
    fseeko(sPak.file, 0, SEEK_END);
    sPak.size = (uint64_t)ftello(sPak.file);
    if (!read_at(0, boot, sizeof(boot))) {
        die("%s is too short for a game partition", path);
    }
    memcpy(sPak.id, boot, 6);
    sPak.id[6] = 0;
    if (strcmp(sPak.id, "RMGE01") != 0 && strcmp(sPak.id, "RMGP01") != 0 && strcmp(sPak.id, "RMGJ01") != 0) {
        die("%s holds %.6s, not Super Mario Galaxy for North America, Europe or Japan", path, sPak.id);
    }
    const uint64_t dol = (uint64_t)load_be32(boot + 0x420) << 2;
    const uint64_t fst = (uint64_t)load_be32(boot + 0x424) << 2;
    const uint64_t fst_size = (uint64_t)load_be32(boot + 0x428) << 2;
    if (dol + 0x100 > sPak.size || fst + fst_size > sPak.size) {
        die("%s: boot.bin points past the partition", path);
    }
    sPak.fst = read_block(fst, (size_t)fst_size, "the file table");
    sPak.fst_size = (size_t)fst_size;
    uint8_t header[0x100];
    if (!read_at(dol, header, sizeof(header))) {
        die("main.dol's header cannot be read");
    }
    uint64_t end = 0x100;
    for (int s = 0; s < 18; s++) {
        const uint64_t size = load_be32(header + 0x90 + s * 4);
        const uint64_t section_end = (uint64_t)load_be32(header + s * 4) + size;
        if (size != 0 && section_end > end) {
            end = section_end;
        }
    }
    sPak.dol = read_block(dol, (size_t)end, "main.dol");
    sPak.dol_size = (size_t)end;
    fprintf(stderr, "[host] %s: %s, %" PRIu64 " bytes, main.dol %zu bytes, file table %zu bytes\n", path, sPak.id,
            sPak.size, sPak.dol_size, sPak.fst_size);
}

/* ---- what the game did ---- */

static _Atomic uint64_t sGxBytes, sGxSpans, sDiscReads, sDiscBytes, sRetraces, sFrames, sFsRequests;
static _Atomic uint32_t sPeMarkers;
static char sStage[64];

/* ---- services ---- */

static void svc_log(void* user, const char* text) {
    (void)user;
    fputs(text, stderr);
}

static void report(void);

static void svc_fatal(void* user, const char* text) {
    (void)user;
    fprintf(stderr, "[host] the game stopped: %s\n", text);
    report();
    host_exit(3);
}

#if defined(SMG_HOST_AURORA)
/* The GX command stream, its PE markers and the EFB peeks: drawn by Aurora, which releases the spans and answers the
   markers as its render thread reaches them. */
static void svc_gx_submit(void* user, const uint8_t* bytes, size_t size) {
    (void)user;
    atomic_fetch_add(&sGxBytes, size);
    atomic_fetch_add(&sGxSpans, 1);
    smg_bridge_submit(bytes, size);
}

static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    atomic_fetch_add(&sPeMarkers, 1);
    smg_bridge_pe_marker(finish, token);
}

static uint32_t svc_gx_peek_efb(void* user, uint16_t x, uint16_t y, uint32_t type, uint16_t token) {
    (void)user;
    return smg_bridge_peek_efb(x, y, type, token);
}
#else
/* The GX command stream: counted and released; drawing it is the renderer's work (docs/ARQUITECTURA_WEB.md). */
static void svc_gx_submit(void* user, const uint8_t* bytes, size_t size) {
    (void)user;
    (void)bytes;
    atomic_fetch_add(&sGxBytes, size);
    atomic_fetch_add(&sGxSpans, 1);
    nebula_game_gx_release(size);
}

/* With nothing drawn, every draw-sync token and draw done completes as soon as the stream reaches it. */
static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    atomic_fetch_add(&sPeMarkers, 1);
    nebula_game_gx_event(finish, token);
}
#endif

static const uint8_t* svc_disc_fst(void* user, size_t* size) {
    (void)user;
    *size = sPak.fst_size;
    return sPak.fst;
}

static void svc_disc_read(void* user, uint64_t offset, uint32_t size, void* dst, uint32_t slot) {
    (void)user;
    atomic_fetch_add(&sDiscReads, 1);
    if (!read_at(offset, dst, size)) {
        nebula_game_disc_complete(slot, -1);
        return;
    }
    atomic_fetch_add(&sDiscBytes, size);
    nebula_game_disc_complete(slot, (int32_t)size);
}

static const uint8_t* svc_disc_dol(void* user, size_t* size) {
    (void)user;
    *size = sPak.dol_size;
    return sPak.dol;
}

static const char* svc_disc_id(void* user) {
    (void)user;
    return sPak.id;
}

static void svc_vi_frame(void* user, uint32_t xfb, int black, uint32_t retrace) {
    (void)user;
    (void)retrace;
    atomic_fetch_add(&sFrames, 1);
#if defined(SMG_HOST_AURORA)
    smg_bridge_vi_frame(xfb, black);
#else
    (void)xfb;
    (void)black;
#endif
}

/* No NAND yet: every request fails as a missing file system would (persistence is a later step). */
static void svc_fs_request(void* user, const NebulaFsRequest* request, uint32_t slot) {
    (void)user;
    (void)request;
    atomic_fetch_add(&sFsRequests, 1);
    nebula_game_fs_complete(slot, -1);
}

static void svc_power(void* user, uint32_t what) {
    (void)user;
    fprintf(stderr, "[host] power request %u\n", what);
    host_exit(0);
}

static void svc_game_stage(void* user, const char* name) {
    (void)user;
    snprintf(sStage, sizeof(sStage), "%s", name != NULL ? name : "");
    fprintf(stderr, "[host] stage: %s\n", sStage);
}

/* ---- clocks ---- */

static double sSeconds = 10.0;

static void report(void) {
    fprintf(stderr,
            "[host] report: retraces %" PRIu64 ", frames %" PRIu64 ", GX %" PRIu64 " bytes in %" PRIu64
            " spans, PE markers %u, disc reads %" PRIu64 " (%" PRIu64 " bytes), NAND requests %" PRIu64
            ", stage '%s'\n",
            atomic_load(&sRetraces), atomic_load(&sFrames), atomic_load(&sGxBytes), atomic_load(&sGxSpans),
            atomic_load(&sPeMarkers), atomic_load(&sDiscReads), atomic_load(&sDiscBytes), atomic_load(&sFsRequests),
            sStage);
#if defined(SMG_HOST_AURORA)
    fprintf(stderr, "[host] Aurora: %u frames drawn, %u shown on the page\n", smg_bridge_frames(),
            smg_bridge_presented());
#endif
}

/* The 60 Hz retrace (Nebula paces the game at 60, not the console's 59.94) and the end of the run (none when the
   run's length is 0). */
static void* clock_main(void* arg) {
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const uint64_t total = sSeconds > 0.0 ? (uint64_t)(sSeconds * 60.0) : UINT64_MAX;
    for (uint64_t n = 1; n <= total; n++) {
        next.tv_nsec += 16666667;
        if (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        atomic_fetch_add(&sRetraces, 1);
        nebula_game_vi_retrace();
        if (n % 600 == 0) {
            report();
        }
    }
    report();
    host_exit(0);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game.pak> [seconds, 0: no end]\n", argv[0]);
        return 1;
    }
    if (argc > 2) {
        sSeconds = atof(argv[2]);
    }
    open_pak(argv[1]);

    static NebulaHostServices services;
    smg_host_default_services(&services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.gx_submit = svc_gx_submit;
    services.gx_pe_marker = svc_gx_pe_marker;
    services.disc_fst = svc_disc_fst;
    services.disc_read = svc_disc_read;
    services.disc_dol = svc_disc_dol;
    services.disc_id = svc_disc_id;
    services.vi_frame = svc_vi_frame;
#if defined(SMG_HOST_AURORA)
    services.gx_peek_efb = svc_gx_peek_efb;
#endif
    services.fs_request = svc_fs_request;
    services.power = svc_power;
    services.game_stage = svc_game_stage;
    if (nebula_game_attach(&services) == 0) {
        die("the game module refused the host's services (API %u)", NEBULA_GAME_API_VERSION);
    }
#if defined(SMG_HOST_AURORA)
    if (!smg_bridge_start(1)) {
        die("Aurora did not start on this browser's WebGPU");
    }
    fprintf(stderr, "[host] Aurora is drawing on WebGPU\n");
#endif
    pthread_t clock;
    if (pthread_create(&clock, NULL, clock_main, NULL) != 0) {
        die("the retrace clock cannot start");
    }
    if (sSeconds > 0.0) {
        fprintf(stderr, "[host] running the game for %.1f s\n", sSeconds);
    } else {
        fprintf(stderr, "[host] running the game\n");
    }
    nebula_game_run(); /* never returns */
    return 0;
}
