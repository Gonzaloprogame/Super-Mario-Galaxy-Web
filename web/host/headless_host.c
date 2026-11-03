/* A headless host for the game module on WebAssembly: Nebula's NebulaHostServices (nebula_game_api.h) with no
   renderer, no sound and no input yet (a Wii Remote at rest, the audio interface's clock and a NAND in memory, as
   Nebula's host without a renderer: web/host/console_services.c). It loads the user's game.pak, runs the game, consumes the GX command stream
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

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

#include <nebula_game_api.h>

#include "console_services.h"
#include "host_services.h"
#include "rvz_reader.h"
#if defined(SMG_HOST_AURORA)
#include "audio_output.h"
#include "aurora_bridge.h"
#include "thp_video.h"
#endif

/* ---- game.pak: the decrypted game partition at its own offsets (crates/nebula-import) ----
   Or the user's RVZ disc image (Dolphin), whose game partition is read in place (web/host/rvz_reader.c): the same
   bytes, without writing game.pak. */

typedef struct {
    FILE* file;
    RvzReader* rvz; /* NULL: the file is game.pak */
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

/* Every end of the process: with Aurora, its render thread is stopped first (smg_bridge_stop). The page is told the
   code on the presenter's channel (web/shell/present.js) before the exit, and ends the worker itself: Emscripten's
   exit does not always reach the page (onExit). In the real game's first stop, after Aurora had drawn, it did not: the
   worker went on running (the retrace clock reporting), as when exit halts without ending the runtime because something
   keeps it alive. emscripten_force_exit clears that. */
static void host_exit(int code) {
#if defined(SMG_HOST_AURORA)
    smg_bridge_stop();
#endif
#if defined(__EMSCRIPTEN__)
    fprintf(stderr, "[host] exit %d\n", code);
    EM_ASM(
        {
            if (typeof BroadcastChannel !== 'undefined') {
                const channel = new BroadcastChannel('smg-present');
                channel.postMessage({type : 'exit', code : $0});
                channel.close();
            }
        },
        code);
    emscripten_force_exit(code);
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
    if (sPak.rvz != NULL) {
        if (!rvz_read(sPak.rvz, offset, dst, size)) {
            fprintf(stderr, "[host] the RVZ image cannot be read: %s\n", rvz_last_error(sPak.rvz));
            return 0;
        }
        return 1;
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
    /* What the file is, from its first bytes */
    uint8_t magic[0x424] = {0};
    fseeko(sPak.file, 0, SEEK_SET);
    const size_t got = fread(magic, 1, sizeof(magic), sPak.file);
    if (got >= 4 && (memcmp(magic, "RVZ\x01", 4) == 0 || memcmp(magic, "WIA\x01", 4) == 0)) {
        char error[256];
        sPak.rvz = rvz_open(sPak.file, error, sizeof(error));
        if (sPak.rvz == NULL) {
            die("%s: %s", path, error);
        }
        sPak.size = rvz_partition_size(sPak.rvz);
        fprintf(stderr, "[host] %s: an RVZ image of %s; its game partition is read in place (%" PRIu64 " bytes)\n",
                path, rvz_disc_id(sPak.rvz), sPak.size);
    } else if (got >= 4 && memcmp(magic, "WBFS", 4) == 0) {
        die("%s is a WBFS image, whose game partition is encrypted: convert it to RVZ in Dolphin (or make game.pak "
            "with Nebula's importer, docs/ARCHIVOS_DEL_JUEGO.md)", path);
    } else if (got == sizeof(magic) && load_be32(magic + 0x18) == 0x5D1C9EA3u && load_be32(magic + 0x420) == 0) {
        /* A disc's own header, which (unlike game.pak's boot.bin) does not point to a main.dol: a disc image */
        die("%s is a disc image (ISO), whose game partition is encrypted: convert it to RVZ in Dolphin (or make "
            "game.pak with Nebula's importer, docs/ARCHIVOS_DEL_JUEGO.md)", path);
    }
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

static _Atomic uint64_t sGxBytes, sGxSpans, sDiscReads, sDiscBytes, sRetraces, sFrames;
static _Atomic uint32_t sPeMarkers;
static char sStage[64];

static uint64_t now_micros(void);

/* ---- the watch log: the host's reports in a ring of shared memory the page reads by itself ----
   The engine's stderr reaches the page through the game's thread: Emscripten proxies a pthread's write to the worker's
   main thread, where the game runs, and waits for it. If the game's thread hangs, the clock thread itself stops at its
   next report, and the stall report never comes (the log goes silent just when it matters). So when the page reads
   this ring (sWatchReader: the engine runs in the page's worker), the reports go only here, written without waiting
   for anyone; the page reads it every second (web/shell/app.js) and adds its lines to the log. `beat` is the game's
   thread's heartbeat: counted at each host service it calls (SERVICE_ENTER, each scene frame); when it stops for 8 s
   while the ring goes on, the page says the game's thread does not answer and marks the lines "[vigía]". Without the
   page (the Node builds) the reports go to stderr. `written` counts every byte ever written; the text is at
   written % SMG_WATCH_SIZE. */
#define SMG_WATCH_SIZE 32768u
static struct {
    _Alignas(8) _Atomic uint32_t written;
    uint32_t size;
    _Atomic uint32_t beat;
    uint32_t reserved;
    char text[SMG_WATCH_SIZE];
} sWatch = {.size = SMG_WATCH_SIZE};
static pthread_mutex_t sWatchLock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int sWatchReader;

static void host_log(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    if (!atomic_load(&sWatchReader)) {
        fputs(line, stderr);
        return;
    }
    pthread_mutex_lock(&sWatchLock);
    const uint32_t at = atomic_load(&sWatch.written);
    for (int i = 0; i < n; i++) {
        sWatch.text[(at + (uint32_t)i) % SMG_WATCH_SIZE] = line[i];
    }
    atomic_store(&sWatch.written, at + (uint32_t)n);
    pthread_mutex_unlock(&sWatchLock);
}

/* Where the game's thread is in the host: set around the services that can wait (the renderer's queue and answers,
   the disc, the audio mix, the movie decoder), for the stall report. */
static _Atomic(const char*) sInService;
static _Atomic uint64_t sInServiceSince;
#define SERVICE_ENTER(name)                              \
    do {                                                 \
        atomic_fetch_add(&sWatch.beat, 1);               \
        atomic_store(&sInServiceSince, now_micros());    \
        atomic_store(&sInService, (const char*)(name));  \
    } while (0)
#define SERVICE_LEAVE() atomic_store(&sInService, (const char*)NULL)

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
    SERVICE_ENTER("gx_submit (the renderer's queue)");
    smg_bridge_submit(bytes, size);
    SERVICE_LEAVE();
}

static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    atomic_fetch_add(&sPeMarkers, 1);
    SERVICE_ENTER("gx_pe_marker (the renderer's queue)");
    smg_bridge_pe_marker(finish, token);
    SERVICE_LEAVE();
}

static uint32_t svc_gx_peek_efb(void* user, uint16_t x, uint16_t y, uint32_t type, uint16_t token) {
    (void)user;
    SERVICE_ENTER("gx_peek_efb (waiting for the renderer's answer)");
    const uint32_t value = smg_bridge_peek_efb(x, y, type, token);
    SERVICE_LEAVE();
    return value;
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

/* The disc reads' time, for the report: the total, and the read in progress (when it started, its size) for the stall
   report */
static _Atomic uint64_t sDiscMicros, sDiscBusySinceMicros, sDiscBusySize;

static uint64_t now_micros(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void svc_disc_read(void* user, uint64_t offset, uint32_t size, void* dst, uint32_t slot) {
    (void)user;
    atomic_fetch_add(&sDiscReads, 1);
    const uint64_t start = now_micros();
    atomic_store(&sDiscBusySize, size);
    atomic_store(&sDiscBusySinceMicros, start);
    SERVICE_ENTER("disc_read");
    const int ok = read_at(offset, dst, size);
    SERVICE_LEAVE();
    atomic_store(&sDiscBusySinceMicros, 0);
    atomic_fetch_add(&sDiscMicros, now_micros() - start);
    if (!ok) {
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

/* Movies (THP). With the renderer, Nebula's decoder writes the frame's planes (web/host/thp_video.cpp); the first
   frame and any refused one are said in the log. Without it (the Node builds) there is nothing to show them on: the
   frame is refused as a failed decode, and the game's player does not show it. */
static _Atomic uint64_t sThpDecodes;

static int32_t svc_thp_video_decode(void* user, const void* frame, uint32_t size, void* y, void* u, void* v) {
    (void)user;
    const uint64_t n = atomic_fetch_add(&sThpDecodes, 1);
#if defined(SMG_HOST_AURORA)
    SERVICE_ENTER("thp_video_decode");
    const int32_t result = smg_thp_video_decode(frame, size, y, u, v);
    SERVICE_LEAVE();
    uint16_t width = 0, height = 0;
    if (n == 0 || result != 0) {
        smg_thp_frame_size(frame, size, &width, &height);
        fprintf(stderr, "[host] movie frame %" PRIu64 " (THP, %ux%u, %u bytes): %s (decoder error %d)\n", n + 1,
                width, height, size, result == 0 ? "decoded" : "REFUSED", (int)result);
    }
    return result;
#else
    (void)frame;
    (void)y;
    (void)u;
    (void)v;
    if (n == 0) {
        fprintf(stderr, "[host] the game plays a movie (THP, frame of %u bytes): this build has no renderer, so no decoder\n",
                size);
    }
    return -1;
#endif
}

static void svc_power(void* user, uint32_t what) {
    (void)user;
    fprintf(stderr, "[host] power request %u\n", what);
    host_exit(0);
}

/* ---- the galaxy tour (the page's ?tour=, docs/MOTOR_WEB.md): each stop in turn, through the game's own stage change
   (MR::requestChangeStageInGameMoving, NEBULA_TEST_CHANGE_STAGE), every `frames` scene frames. The way Nebula's host
   visits galaxies (GALAXY_TEST_GALAXY_TOUR): to find, galaxy by galaxy, what the web engine cannot draw or run, without
   playing to each one. The list comes from the page, not from this repository. ---- */
#define SMG_TOUR_MAX 160
static struct {
    char stage[64];
    int32_t scenario;
} sTour[SMG_TOUR_MAX];
static size_t sTourStops, sTourNext;
static uint64_t sTourFrames = 1200, sTourSince, sSceneFrames, sTourStartMicros;
static int sTourDone;

/* "Stage:scenario,Stage:scenario,..." (scenario 1 when left out) */
static void tour_parse(const char* spec, const char* frames) {
    const char* p = spec;
    while (*p != '\0' && sTourStops < SMG_TOUR_MAX) {
        const char* end = strchr(p, ',');
        const size_t length = end != NULL ? (size_t)(end - p) : strlen(p);
        char entry[80];
        snprintf(entry, sizeof(entry), "%.*s", (int)(length < sizeof(entry) - 1 ? length : sizeof(entry) - 1), p);
        char* colon = strchr(entry, ':');
        int scenario = 1;
        if (colon != NULL) {
            *colon = '\0';
            scenario = atoi(colon + 1);
        }
        if (entry[0] != '\0') {
            snprintf(sTour[sTourStops].stage, sizeof(sTour[sTourStops].stage), "%s", entry);
            sTour[sTourStops].scenario = scenario > 0 ? scenario : 1;
            sTourStops++;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    if (frames != NULL && atoi(frames) >= 60) {
        sTourFrames = (uint64_t)atoi(frames);
    }
    host_log("[host] tour: %zu stops, %" PRIu64 " scene frames apart\n", sTourStops, sTourFrames);
}

static int svc_test_scene_frame(void* user, NebulaTestAction* action) {
    (void)user;
    sSceneFrames++;
    atomic_fetch_add(&sWatch.beat, 1);
    if (sTourStops == 0 || sTourDone) {
        return 0;
    }
    if (sTourStartMicros == 0) {
        sTourStartMicros = now_micros();
    }
    /* The first stop 2 s into the first game scene, the others `frames` apart */
    if (++sTourSince < (sTourNext == 0 ? 120u : sTourFrames)) {
        return 0;
    }
    sTourSince = 0;
    const double elapsed = (double)(now_micros() - sTourStartMicros) / 1e6;
    if (sTourNext == sTourStops) {
        sTourDone = 1;
        host_log("[host] tour: done, %zu stops in %.0f s\n", sTourStops, elapsed);
        return 0;
    }
    action->kind = NEBULA_TEST_CHANGE_STAGE;
    action->scenario = sTour[sTourNext].scenario;
    snprintf(action->stage, sizeof(action->stage), "%s", sTour[sTourNext].stage);
    host_log("[host] tour: stop %zu of %zu, %s scenario %d (scene frame %" PRIu64 ", %.0f s)\n", sTourNext + 1,
             sTourStops, action->stage, (int)action->scenario, sSceneFrames, elapsed);
    sTourNext++;
    return 1;
}

/* The game names its stage often (each scene step): logged when it changes */
#if defined(SMG_HOST_AURORA)
/* Aurora's pipelines (patch 0023): the time creating them, all of it on the render thread in the browser build (a
   frame waits for each), and how many wait in its queue (their draws are skipped until they are made). */
void aurora_pipeline_build_times(uint64_t* totalNs, uint32_t* count, uint64_t* maxNs, uint32_t* queued);

/* "N created in X ms (the slowest Y ms)" since the previous call with the same `last` */
static void pipeline_times_since(uint64_t last[2], char* out, size_t size) {
    uint64_t total = 0, slowest = 0;
    uint32_t count = 0, queued = 0;
    aurora_pipeline_build_times(&total, &count, &slowest, &queued);
    snprintf(out, size, "%" PRIu64 " created in %.0f ms; %u in the queue; slowest so far %.1f ms",
             (uint64_t)count - last[1], (double)(total - last[0]) / 1e6, queued, (double)slowest / 1e6);
    last[0] = total;
    last[1] = count;
}
#endif

/* The game names its stage before each 3D scene (each frame while it plays). A change follows the load: the time from
   the old stage's last scene to the new one's first, and of it the disc's and the pipelines' share, said in the log
   (what the rest is: the game's own work, decompressing and building the scene, and waiting for the renderer). */
static void svc_game_stage(void* user, const char* name) {
    (void)user;
    static uint64_t lastSceneMicros, discMicrosAtLast, discReadsAtLast;
#if defined(SMG_HOST_AURORA)
    static uint64_t pipelinesAtLast[2], pipelineCountAtLast;
#endif
    char stage[sizeof(sStage)];
    snprintf(stage, sizeof(stage), "%s", name != NULL ? name : "");
    const uint64_t now = now_micros();
    if (strcmp(stage, sStage) != 0) {
        if (sStage[0] != '\0' && lastSceneMicros != 0) {
            char pipelines[160] = "";
#if defined(SMG_HOST_AURORA)
            uint64_t total = 0, slowest = 0;
            uint32_t count = 0, queued = 0;
            aurora_pipeline_build_times(&total, &count, &slowest, &queued);
            snprintf(pipelines, sizeof(pipelines), "; pipelines %" PRIu64 " created in %.0f ms",
                     (uint64_t)count - pipelineCountAtLast, (double)(total - pipelinesAtLast[0]) / 1e6);
#endif
            host_log("[host] load '%s' -> '%s': %.1f s from the last scene to the first; disc %" PRIu64
                     " reads in %.0f ms%s\n",
                     sStage, stage, (double)(now - lastSceneMicros) / 1e6, atomic_load(&sDiscReads) - discReadsAtLast,
                     (double)(atomic_load(&sDiscMicros) - discMicrosAtLast) / 1e3, pipelines);
        }
        memcpy(sStage, stage, sizeof(sStage));
        fprintf(stderr, "[host] stage: %s\n", sStage);
    }
    lastSceneMicros = now;
    discMicrosAtLast = atomic_load(&sDiscMicros);
    discReadsAtLast = atomic_load(&sDiscReads);
#if defined(SMG_HOST_AURORA)
    {
        uint64_t total = 0, slowest = 0;
        uint32_t count = 0, queued = 0;
        aurora_pipeline_build_times(&total, &count, &slowest, &queued);
        pipelinesAtLast[0] = total;
        pipelineCountAtLast = count;
    }
#endif
}

/* ---- the NAND between sessions ----
   The page keeps the NAND in IndexedDB (web/shell/app.js): it hands the last copy to the engine as a file (argument 4,
   written by web/shell/boot_worker.post.js), and gets a new one on the presenter's channel after each change. */
static void restore_nand(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    uint8_t* blob = NULL;
    long size = 0;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0 &&
        (blob = malloc((size_t)size)) != NULL && fread(blob, 1, (size_t)size, file) == (size_t)size) {
        const int count = smg_console_nand_import(blob, (size_t)size);
        if (count < 0) {
            fprintf(stderr, "[host] the NAND kept by the page is unreadable: starting without it\n");
        } else {
            fprintf(stderr, "[host] NAND restored from the page: %d folders and files\n", count);
        }
    }
    free(blob);
    fclose(file);
}

static void nand_changed(const uint8_t* blob, size_t size) {
#if defined(__EMSCRIPTEN__)
    EM_ASM(
        {
            if (typeof BroadcastChannel !== 'undefined') {
                const channel = new BroadcastChannel('smg-present');
                channel.postMessage({type : 'nand', id : UTF8ToString($2), data : HEAPU8.slice($0, $0 + $1)});
                channel.close();
            }
        },
        blob, size, sPak.id);
#else
    (void)blob;
    (void)size;
#endif
}

/* ---- the console's system settings ---- */

/* The system language (SCGetLanguage: 0 Japanese, 1 English, 2 German, 3 French, 4 Spanish, 5 Italian, 6 Dutch), as
   the page asks for it. The game reads its text from the folder its release's table (Language.cpp) gives that
   language; a folder the disc does not hold stops it (FileRipper: "File isn't exist."). So the host keeps the language
   asked for only if the disc's file table has its folder; otherwise English, then Japanese, said in the log. */
static uint32_t sLanguage = 1;
static uint32_t (*sDefaultScGet)(void* user, uint32_t item);

/* The folder of each system language, per release (the rows of Language.cpp's table for RMGJ01, RMGE01, RMGP01) */
static const char* language_folder(char region, int language) {
    static const char* const japan[] = {"JpJapanese", "UsEnglish", "EuGerman", "EuFrench",
                                        "EuSpanish",  "EuItalian", "EuDutch"};
    static const char* const america[] = {"JpJapanese", "UsEnglish", "EuGerman", "UsFrench",
                                          "UsSpanish",  "EuItalian", "EuDutch"};
    static const char* const europe[] = {"JpJapanese", "EuEnglish", "EuGerman", "EuFrench",
                                         "EuSpanish",  "EuItalian", "EuDutch"};
    if (language < 0 || language > 6) {
        return NULL;
    }
    return (region == 'J' ? japan : region == 'E' ? america : europe)[language];
}

/* Whether the disc's file table has a directory named `name` */
static int disc_has_directory(const char* name) {
    if (sPak.fst_size < 12) {
        return 0;
    }
    const uint32_t entries = load_be32(sPak.fst + 8);
    if (entries == 0 || (uint64_t)entries * 12 > sPak.fst_size) {
        return 0;
    }
    const char* strings = (const char*)sPak.fst + (size_t)entries * 12;
    const size_t stringsSize = sPak.fst_size - (size_t)entries * 12;
    const size_t length = strlen(name);
    for (uint32_t i = 1; i < entries; i++) {
        const uint8_t* entry = sPak.fst + (size_t)i * 12;
        const uint32_t nameOffset = load_be32(entry) & 0xFFFFFF;
        if (entry[0] == 1 && nameOffset < stringsSize && stringsSize - nameOffset > length &&
            memcmp(strings + nameOffset, name, length + 1) == 0) {
            return 1;
        }
    }
    return 0;
}

static void choose_language(int asked) {
    static const char* const names[] = {"Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch"};
    const char region = sPak.id[3];
    const int candidates[] = {asked, 1, 0};
    for (int i = 0; i < 3; i++) {
        const char* folder = language_folder(region, candidates[i]);
        if (folder != NULL && disc_has_directory(folder)) {
            if (i > 0 && asked >= 0) {
                fprintf(stderr, "[host] language %d is not on this disc (no %s folder)\n", asked,
                        language_folder(region, asked) != NULL ? language_folder(region, asked) : "such");
            }
            sLanguage = (uint32_t)candidates[i];
            fprintf(stderr, "[host] system language: %s (the disc's %s folder)\n", names[sLanguage], folder);
            return;
        }
    }
    /* No known folder (a synthetic test partition): English, and the game says what it misses */
    sLanguage = asked >= 0 && asked <= 6 ? (uint32_t)asked : 1;
    fprintf(stderr, "[host] system language: %s (no language folder found in the disc's file table)\n",
            names[sLanguage]);
}

static uint32_t svc_sc_get(void* user, uint32_t item) {
    if (item == NEBULA_SC_LANGUAGE) {
        return sLanguage;
    }
    return sDefaultScGet(user, item);
}

/* ---- clocks ---- */

static double sSeconds = 10.0;

static void report(void) {
    SmgConsoleCounters console;
    smg_console_counters(&console);
    host_log(
            "[host] report: retraces %" PRIu64 ", frames %" PRIu64 ", GX %" PRIu64 " bytes in %" PRIu64
            " spans, PE markers %u, disc reads %" PRIu64 " (%" PRIu64 " bytes, %" PRIu64 " ms), NAND requests %" PRIu64 " (%" PRIu64
            " failed), Wii Remote reports %" PRIu64 ", audio blocks %" PRIu64 ", stage '%s'\n",
            atomic_load(&sRetraces), atomic_load(&sFrames), atomic_load(&sGxBytes), atomic_load(&sGxSpans),
            atomic_load(&sPeMarkers), atomic_load(&sDiscReads), atomic_load(&sDiscBytes), atomic_load(&sDiscMicros) / 1000,
            console.nand_requests,
            console.nand_failures, console.wpad_reports, console.ai_blocks, sStage);
#if defined(SMG_HOST_AURORA)
    char peaks[256];
    smg_bridge_buffer_peaks(peaks, sizeof(peaks));
    host_log( "[host] Aurora: %u frames drawn, %u shown on the page; frame buffers' peaks: %s\n",
            smg_bridge_frames(), smg_bridge_presented(), peaks);
    char stats[256];
    smg_bridge_debug_stats(stats, sizeof(stats));
    host_log( "[host] Aurora pipelines: %s\n", stats);
    char times[320];
    smg_bridge_phase_times(times, sizeof(times));
    host_log( "[host] render thread: %s\n", times);
    char audio[320];
    smg_audio_stats(audio, sizeof(audio));
    host_log( "[host] audio: %s\n", audio);
    static uint64_t sinceReport[2];
    char pipelines[160];
    pipeline_times_since(sinceReport, pipelines, sizeof(pipelines));
    host_log("[host] pipelines in the last 10 s: %s\n", pipelines);
    uint64_t movieDecoded = 0, movieRefused = 0, movieNs = 0;
    smg_thp_stats(&movieDecoded, &movieRefused, &movieNs);
    if (movieDecoded + movieRefused != 0) {
        host_log( "[host] movies: %" PRIu64 " frames decoded, %" PRIu64 " refused, %.2f ms a frame\n",
                movieDecoded, movieRefused, (double)movieNs / 1e6 / (double)(movieDecoded + movieRefused));
    }
#endif
#if defined(__EMSCRIPTEN__)
    host_log( "[host] wasm memory %zu MB\n", emscripten_get_heap_size() >> 20);
#endif
}

/* The game's own activity, from services it calls while it works: its audio thread's DSP commands, the caches it
   flushes for the GPU as it loads resources, the objects it names as it builds a scene. Counted around the default
   answers (host_services.c), for the stall report: a game loading slowly calls them, a game stuck does not. */
static NebulaHostServices sDefaults;
static _Atomic uint64_t sDspCalls, sCoherentCalls, sObjectNames;

static int svc_audio_dsp(void* user, const uint32_t* words, uint32_t count) {
    atomic_fetch_add(&sDspCalls, 1);
#if defined(SMG_HOST_AURORA)
    (void)user;
    SERVICE_ENTER("audio_dsp (the JAudio mix)");
    const int done = smg_audio_dsp(words, count); /* the game's JAudio DSP program, mixed (web/host/audio_output.cpp) */
    SERVICE_LEAVE();
    return done;
#else
    return sDefaults.audio_dsp(user, words, count);
#endif
}

static void svc_gpu_coherent(void* user, uint32_t physical, uint32_t size) {
    atomic_fetch_add(&sCoherentCalls, 1);
#if defined(SMG_HOST_AURORA)
    (void)user;
    SERVICE_ENTER("gpu_coherent (the renderer's queue)");
    smg_bridge_coherent(physical, size);
    SERVICE_LEAVE();
#else
    sDefaults.gpu_coherent(user, physical, size);
#endif
}

static void svc_object_name(void* user, uint32_t id, const char* name) {
    atomic_fetch_add(&sObjectNames, 1);
    sDefaults.object_name(user, id, name);
}

/* When the game's stream stops while the retrace goes on (5 s without GX): what each side is doing, in the log, and
   again every 30 s while it lasts. The game's reads of its remotes say whether its main loop still runs. */
/* What the game's thread does while it stalls, apart from host services (it calls none):
   - its threads' switches (web/platform/context_switch_wasm.c): none while one runs on, or while it sleeps for work;
   - a probe: an empty task the clock thread hands the game's thread. Waiting (Nebula's scheduler sleeping for want of
     work, or a wait that yields) it runs it; running its own code without yielding it never does. */
#if defined(__EMSCRIPTEN__)
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
uint64_t nebula_web_switches(void);
uintptr_t nebula_web_running_thread(uintptr_t* arg);
static _Atomic uint32_t sProbeAnswers;
static void probe_answer(void* arg) {
    (void)arg;
    atomic_fetch_add(&sProbeAnswers, 1);
}
#endif

static void check_stall(uint64_t retrace) {
    static uint64_t lastGx, sinceRetrace, lastSamples, reported, lastDisc, lastDsp, lastCoherent, lastNames;
#if defined(__EMSCRIPTEN__)
    static uint64_t lastSwitches;
    static uint32_t probeSent, probeAnswersBefore; /* probeSent: the retrace it went out, 0 when none */
#endif
    const uint64_t gx = atomic_load(&sGxBytes);
    SmgConsoleCounters console;
    smg_console_counters(&console);
    if (gx != lastGx || gx == 0) {
        if (reported != 0) {
            host_log( "[host] the game's stream resumed after %.0f s\n", (retrace - sinceRetrace) / 60.0);
        }
        lastGx = gx;
        sinceRetrace = retrace;
        lastSamples = console.wpad_samples;
        lastDisc = atomic_load(&sDiscReads);
        lastDsp = atomic_load(&sDspCalls);
        lastCoherent = atomic_load(&sCoherentCalls);
        lastNames = atomic_load(&sObjectNames);
#if defined(__EMSCRIPTEN__)
        lastSwitches = nebula_web_switches();
        probeSent = 0;
#endif
        reported = 0;
        return;
    }
    const uint64_t stalled = retrace - sinceRetrace;
#if defined(__EMSCRIPTEN__)
    /* The probe goes out 4 s into the stall, a second before the first report reads it */
    if (stalled >= 240 && probeSent == 0) {
        probeAnswersBefore = atomic_load(&sProbeAnswers);
        probeSent = (uint32_t)retrace;
        emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), probe_answer,
                               NULL);
    }
#endif
    if (stalled < 300 || (reported != 0 && stalled - reported < 1800)) {
        return;
    }
    reported = stalled;
    char bridge[512] = "no renderer";
#if defined(SMG_HOST_AURORA)
    smg_bridge_describe(bridge, sizeof(bridge));
#endif
    const uint64_t disc = atomic_load(&sDiscReads) - lastDisc, dsp = atomic_load(&sDspCalls) - lastDsp;
    const uint64_t coherent = atomic_load(&sCoherentCalls) - lastCoherent, names = atomic_load(&sObjectNames) - lastNames;
    char discNow[96] = "no disc read in progress";
    const uint64_t busySince = atomic_load(&sDiscBusySinceMicros);
    if (busySince != 0) {
        snprintf(discNow, sizeof(discNow), "a disc read of %" PRIu64 " bytes in progress for %" PRIu64 " ms",
                 atomic_load(&sDiscBusySize), (now_micros() - busySince) / 1000);
    }
    char inService[512] = "the game's thread is not inside a host service";
    const char* service = atomic_load(&sInService);
    if (service != NULL) {
        snprintf(inService, sizeof(inService), "the game's thread has been inside the host's %s for %" PRIu64 " ms",
                 service, (now_micros() - atomic_load(&sInServiceSince)) / 1000);
    }
#if defined(__EMSCRIPTEN__)
    {
        uintptr_t arg = 0;
        const uintptr_t entry = nebula_web_running_thread(&arg);
        const int answered = probeSent != 0 && atomic_load(&sProbeAnswers) != probeAnswersBefore;
        const size_t used = strlen(inService);
        snprintf(inService + used, sizeof(inService) - used,
                 "; its threads switched %" PRIu64 " times meanwhile; the running one entered at function table index "
                 "%u (arg 0x%x); a probe sent %.1f s ago %s",
                 nebula_web_switches() - lastSwitches, (unsigned)entry, (unsigned)arg,
                 probeSent != 0 ? (double)(retrace - probeSent) / 60.0 : 0.0,
                 answered ? "was run: the game's thread waits (its scheduler sleeping for work, or a wait that yields)"
                          : "was not run: the game's thread runs its own code without yielding (a loop)");
    }
#endif
    host_log(
            "[host] stall: no GX from the game for %.0f s (VI frames %" PRIu64 ", stage '%s'); meanwhile the game "
            "read its remotes %" PRIu64 " times (its main loop %s), read the disc %" PRIu64 " times, sent its audio "
            "DSP %" PRIu64 " commands, flushed %" PRIu64 " ranges for the GPU and named %" PRIu64
            " objects (the game %s); %s; movie frames asked to decode %" PRIu64 "; NAND requests %" PRIu64
            "; audio blocks %" PRIu64 "; %s; %s\n",
            stalled / 60.0, atomic_load(&sFrames), sStage, console.wpad_samples - lastSamples,
            console.wpad_samples != lastSamples ? "runs" : "is stopped", disc, dsp, coherent, names,
            disc + dsp + coherent + names != 0 ? "works: loading, or slow" : "does nothing the host sees: stuck?", discNow,
            atomic_load(&sThpDecodes), console.nand_requests, console.ai_blocks, inService, bridge);
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
        if (n % 60 == 0) {
            check_stall(n);
        }
    }
    report();
    host_exit(0);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game.pak or .rvz> [seconds, 0: no end] [system language, 0-6] [NAND kept]\n",
                argv[0]);
        return 1;
    }
    if (argc > 2) {
        sSeconds = atof(argv[2]);
    }
    open_pak(argv[1]);
    choose_language(argc > 3 ? atoi(argv[3]) : -1);

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
    /* The console's devices, as Nebula's host without a renderer has them (web/host/console_services.c) */
    services.fs_request = smg_console_fs_request;
    services.wpad_control = smg_console_wpad_control;
    services.wpad_sample = smg_console_wpad_sample;
    services.ai_dma = smg_console_ai_dma;
    services.ai_control = smg_console_ai_control;
    services.power = svc_power;
    services.game_stage = svc_game_stage;
    services.thp_video_decode = svc_thp_video_decode;
    services.test_scene_frame = svc_test_scene_frame;
    sDefaults = services;
    services.audio_dsp = svc_audio_dsp;
    services.gpu_coherent = svc_gpu_coherent;
    services.object_name = svc_object_name;
    sDefaultScGet = services.sc_get;
    services.sc_get = svc_sc_get;
    smg_console_start(sPak.id);
    if (argc > 4 && argv[4][0] != '\0') {
        restore_nand(argv[4]);
    }
    if (argc > 6 && argv[6][0] != '\0') {
        tour_parse(argv[6], argc > 7 ? argv[7] : NULL);
    }
    smg_console_on_nand_change(nand_changed);
#if defined(SMG_HOST_AURORA)
    if (argc > 5 && strcmp(argv[5], "debug") == 0) {
        smg_bridge_set_debug_copies(1);
        fprintf(stderr, "[host] diagnostic view: the game's largest RGBA8 copy each frame\n");
    }
#endif
#if defined(__EMSCRIPTEN__)
    /* The page's controls (web/shell/controls.js) write their words straight into the engine's shared memory */
    EM_ASM(
        {
            if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
                postMessage({type : 'input', buffer : HEAPU8.buffer, address : $0, words : $1});
            }
        },
        smg_console_input(), SMG_INPUT_WORDS);
    /* The watch log (above): from here on the host's reports reach the page through it */
    atomic_store(&sWatchReader, EM_ASM_INT(
                                    {
                                        if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
                                            postMessage({type : 'watch', buffer : HEAPU8.buffer, address : $0});
                                            return 1;
                                        }
                                        return 0;
                                    },
                                    &sWatch));
#if defined(SMG_HOST_AURORA)
    /* The game's sound: the ring of samples the page's AudioWorklet plays (web/host/audio_output.h) */
    EM_ASM(
        {
            if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
                postMessage({type : 'audio', buffer : HEAPU8.buffer, address : $0});
            }
        },
        smg_audio_ring());
    /* The renderer's diagnostic mask (Aurora patch 0017), which the page sets: smgRenderDebug in web/shell/app.js */
    EM_ASM(
        {
            if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
                postMessage({type : 'renderdebug', buffer : HEAPU8.buffer, address : $0});
            }
        },
        smg_bridge_render_debug());
#endif
#endif
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
    if (argc > 5 && strncmp(argv[5], "hangtest", 8) == 0) {
        const int yielding = strcmp(argv[5], "hangtest-yield") == 0;
        /* The page's ?hangtest=1 (web/tests/browser_smoke.mjs): as if the game had drawn and then hung inside a host
           service, its thread spins without yielding while the clock thread goes on. The reports and the stall report
           (which names the service) reach the page only through the watch log. */
        fprintf(stderr, "[host] hang test: the game's thread stops answering now\n");
        atomic_store(&sGxBytes, 1);
        SERVICE_ENTER("hang test");
        for (;;) {
            /* hangtest-yield: a wait that yields (as Nebula's scheduler sleeping for work), which runs the probe */
            if (yielding) {
                sched_yield();
            } else {
                (void)atomic_load(&sRetraces);
            }
        }
    }
    if (sSeconds > 0.0) {
        fprintf(stderr, "[host] running the game for %.1f s\n", sSeconds);
    } else {
        fprintf(stderr, "[host] running the game\n");
    }
    nebula_game_run(); /* never returns */
    return 0;
}
