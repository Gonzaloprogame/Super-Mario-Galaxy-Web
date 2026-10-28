/* A headless host for the game module on WebAssembly: Nebula's NebulaHostServices (nebula_game_api.h) with no
   renderer, no audio and no input. It loads the user's game.pak, runs the game, consumes the GX command stream
   without drawing it (counting it), drives the 60 Hz retrace and reports what the game did. It is the first runtime
   check of the web build (docs/COMPILACION_WASM.md), in Node.js today and in a browser worker later.

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

static void die(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fputs("[host] error: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    exit(2);
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

static void svc_fatal(void* user, const char* text) {
    (void)user;
    fprintf(stderr, "[host] the game stopped: %s\n", text);
    exit(3);
}

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

static uint32_t svc_gx_peek_efb(void* user, uint16_t x, uint16_t y, uint32_t type, uint16_t token) {
    (void)user;
    (void)x;
    (void)y;
    (void)type;
    (void)token;
    return 0;
}

static void svc_gx_scene_marker(void* user, uint32_t marker, const float* view) {
    (void)user;
    (void)marker;
    (void)view;
}

static void svc_gx_stripe_particle(void* user, size_t span_offset, uint32_t particle, uint32_t age_lifetime,
                                   uint8_t strip) {
    (void)user;
    (void)span_offset;
    (void)particle;
    (void)age_lifetime;
    (void)strip;
}

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
    (void)xfb;
    (void)black;
    (void)retrace;
    atomic_fetch_add(&sFrames, 1);
}

/* No NAND yet: every request fails as a missing file system would (persistence is a later step). */
static void svc_fs_request(void* user, const NebulaFsRequest* request, uint32_t slot) {
    (void)user;
    (void)request;
    atomic_fetch_add(&sFsRequests, 1);
    nebula_game_fs_complete(slot, -1);
}

static uint32_t svc_sc_get(void* user, uint32_t item) {
    (void)user;
    (void)item;
    return 0;
}

static void svc_wpad_control(void* user, uint32_t chan, uint32_t what, uint32_t value) {
    (void)user;
    (void)chan;
    (void)what;
    (void)value;
}

static void svc_wpad_speaker_data(void* user, uint32_t chan, const uint8_t* data, uint32_t size) {
    (void)user;
    (void)chan;
    (void)data;
    (void)size;
}

static int svc_audio_dsp(void* user, const uint32_t* words, uint32_t count) {
    (void)user;
    (void)words;
    (void)count;
    return 0;
}

static void svc_ai_dma(void* user, uint32_t start, uint32_t size) {
    (void)user;
    (void)start;
    (void)size;
}

static void svc_ai_control(void* user, uint32_t what, uint32_t value) {
    (void)user;
    (void)what;
    (void)value;
}

static int svc_hbm(void* user, uint32_t what, uint32_t value) {
    (void)user;
    (void)what;
    (void)value;
    return 0;
}

static int32_t svc_thp_video_decode(void* user, const void* frame, uint32_t size, void* y, void* u, void* v) {
    (void)user;
    (void)frame;
    (void)size;
    (void)y;
    (void)u;
    (void)v;
    return -1;
}

static void svc_gpu_coherent(void* user, uint32_t physical, uint32_t size) {
    (void)user;
    (void)physical;
    (void)size;
}

static void svc_game_context(void* user, uint32_t what, int32_t value) {
    (void)user;
    (void)what;
    (void)value;
}

static void svc_power(void* user, uint32_t what) {
    (void)user;
    fprintf(stderr, "[host] power request %u\n", what);
    exit(0);
}

static int svc_test_scene_frame(void* user, NebulaTestAction* action) {
    (void)user;
    (void)action;
    return 0;
}

static uint32_t svc_display_extent(void* user) {
    (void)user;
    return 0; /* the console's 4:3 */
}

static void svc_scene_surround(void* user, float intensity) {
    (void)user;
    (void)intensity;
}

static uint32_t svc_option(void* user, uint32_t what) {
    (void)user;
    (void)what;
    return 0;
}

static void svc_game_light(void* user, uint32_t kind, uint32_t id, const float* values) {
    (void)user;
    (void)kind;
    (void)id;
    (void)values;
}

static void svc_game_stage(void* user, const char* name) {
    (void)user;
    snprintf(sStage, sizeof(sStage), "%s", name != NULL ? name : "");
    fprintf(stderr, "[host] stage: %s\n", sStage);
}

static void svc_object_name(void* user, uint32_t id, const char* name) {
    (void)user;
    (void)id;
    (void)name;
}

static float svc_light_reach(void* user, const char* model, float radius) {
    (void)user;
    (void)model;
    return radius;
}

static int svc_traced_sun(void* user, float* direction) {
    (void)user;
    (void)direction;
    return 0;
}

static void svc_movie_frame(void* user, const char* name, int32_t frame, float frame_rate, uint32_t y, uint32_t u,
                            uint32_t v, uint32_t width, uint32_t height) {
    (void)user;
    (void)name;
    (void)frame;
    (void)frame_rate;
    (void)y;
    (void)u;
    (void)v;
    (void)width;
    (void)height;
}

static int svc_code_patch(void* user, uint32_t id, uint32_t* value) {
    (void)user;
    (void)id;
    (void)value;
    return 0;
}

static void svc_presence(void* user, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars) {
    (void)user;
    (void)galaxy;
    (void)star;
    (void)power_stars;
}

static int svc_achievement(void* user, const char* id, uint16_t* name, uint32_t capacity) {
    (void)user;
    (void)id;
    (void)name;
    (void)capacity;
    return 0;
}

static int32_t svc_achievement_date(void* user, const char* id) {
    (void)user;
    (void)id;
    return 0;
}

static void svc_stat_add(void* user, const char* name, uint64_t delta) {
    (void)user;
    (void)name;
    (void)delta;
}

static uint64_t svc_stat_get(void* user, const char* name) {
    (void)user;
    (void)name;
    return 0;
}

static void svc_settings_view(void* user, NebulaSettingsView* view) {
    (void)user;
    memset(view, 0, sizeof(*view));
}

static void svc_settings_input(void* user, uint32_t action, int32_t arg) {
    (void)user;
    (void)action;
    (void)arg;
}

static void svc_sharp_2d(void* user, uint32_t on) {
    (void)user;
    (void)on;
}

static void svc_button_icons(void* user, NebulaButtonIcons* icons) {
    (void)user;
    memset(icons, 0, sizeof(*icons));
}

static void svc_wpad_sample(void* user, uint32_t chan) {
    (void)user;
    (void)chan;
}

static uint32_t svc_art_export(void* user, const NebulaArtExport* art) {
    (void)user;
    (void)art;
    return 0;
}

static uint32_t svc_layer_2d_scale(void* user) {
    (void)user;
    return 0;
}

static int32_t svc_pointer_wheel(void* user) {
    (void)user;
    return 0;
}

static int svc_galaxy_select_stage(void* user, uint32_t index, char* id, uint32_t capacity, uint32_t* stars) {
    (void)user;
    (void)index;
    (void)id;
    (void)capacity;
    (void)stars;
    return 0;
}

static int svc_galaxy_select_names_wanted(void* user) {
    (void)user;
    return 0;
}

static void svc_galaxy_select_name(void* user, uint32_t index, uint32_t star, const uint16_t* name) {
    (void)user;
    (void)index;
    (void)star;
    (void)name;
}

static void svc_curved_surface(void* user, uint32_t array_address, const void* array, uint32_t array_bytes,
                               const float* positions, uint32_t position_count, const float* normals,
                               uint32_t normal_count, const uint16_t* corners, uint32_t triangle_count) {
    (void)user;
    (void)array_address;
    (void)array;
    (void)array_bytes;
    (void)positions;
    (void)position_count;
    (void)normals;
    (void)normal_count;
    (void)corners;
    (void)triangle_count;
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
}

/* The 60 Hz retrace (Nebula paces the game at 60, not the console's 59.94) and the end of the run. */
static void* clock_main(void* arg) {
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const uint64_t total = (uint64_t)(sSeconds * 60.0);
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
    exit(0);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game.pak> [seconds]\n", argv[0]);
        return 1;
    }
    if (argc > 2) {
        sSeconds = atof(argv[2]);
    }
    open_pak(argv[1]);

    static const NebulaHostServices services = {
        .version = NEBULA_GAME_API_VERSION,
        .size = sizeof(NebulaHostServices),
        .user = NULL,
        .log = svc_log,
        .fatal = svc_fatal,
        .gx_submit = svc_gx_submit,
        .gx_pe_marker = svc_gx_pe_marker,
        .gx_peek_efb = svc_gx_peek_efb,
        .gx_scene_marker = svc_gx_scene_marker,
        .gx_stripe_particle = svc_gx_stripe_particle,
        .disc_fst = svc_disc_fst,
        .disc_read = svc_disc_read,
        .disc_dol = svc_disc_dol,
        .disc_id = svc_disc_id,
        .vi_frame = svc_vi_frame,
        .fs_request = svc_fs_request,
        .sc_get = svc_sc_get,
        .wpad_control = svc_wpad_control,
        .wpad_speaker_data = svc_wpad_speaker_data,
        .audio_dsp = svc_audio_dsp,
        .ai_dma = svc_ai_dma,
        .ai_control = svc_ai_control,
        .hbm = svc_hbm,
        .thp_video_decode = svc_thp_video_decode,
        .gpu_coherent = svc_gpu_coherent,
        .game_context = svc_game_context,
        .power = svc_power,
        .test_scene_frame = svc_test_scene_frame,
        .display_extent = svc_display_extent,
        .scene_surround = svc_scene_surround,
        .option = svc_option,
        .game_light = svc_game_light,
        .game_stage = svc_game_stage,
        .object_name = svc_object_name,
        .light_reach = svc_light_reach,
        .traced_sun = svc_traced_sun,
        .movie_frame = svc_movie_frame,
        .code_patch = svc_code_patch,
        .presence = svc_presence,
        .achievement = svc_achievement,
        .achievement_date = svc_achievement_date,
        .stat_add = svc_stat_add,
        .stat_get = svc_stat_get,
        .settings_view = svc_settings_view,
        .settings_input = svc_settings_input,
        .sharp_2d = svc_sharp_2d,
        .button_icons = svc_button_icons,
        .wpad_sample = svc_wpad_sample,
        .art_export = svc_art_export,
        .layer_2d_scale = svc_layer_2d_scale,
        .pointer_wheel = svc_pointer_wheel,
        .galaxy_select_stage = svc_galaxy_select_stage,
        .galaxy_select_names_wanted = svc_galaxy_select_names_wanted,
        .galaxy_select_name = svc_galaxy_select_name,
        .curved_surface = svc_curved_surface,
    };
    if (nebula_game_attach(&services) == 0) {
        die("the game module refused the host's services (API %u)", NEBULA_GAME_API_VERSION);
    }
    pthread_t clock;
    if (pthread_create(&clock, NULL, clock_main, NULL) != 0) {
        die("the retrace clock cannot start");
    }
    fprintf(stderr, "[host] running the game for %.1f s\n", sSeconds);
    nebula_game_run(); /* never returns */
    return 0;
}
