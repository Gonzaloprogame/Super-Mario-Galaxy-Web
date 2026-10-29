/* Neutral answers for every host service (host_services.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_services.h"

static void svc_log(void* user, const char* text) {
    (void)user;
    fputs(text, stderr);
}

static void svc_fatal(void* user, const char* text) {
    (void)user;
    fprintf(stderr, "[host] the game stopped: %s\n", text);
    exit(3);
}

static void svc_gx_submit(void* user, const uint8_t* bytes, size_t size) {
    (void)user;
    (void)bytes;
    nebula_game_gx_release(size);
}

static void svc_gx_pe_marker(void* user, int finish, uint16_t token) {
    (void)user;
    nebula_game_gx_event(finish, token);
}

static const uint8_t* svc_disc_fst(void* user, size_t* size) {
    (void)user;
    *size = 0;
    return NULL;
}

static void svc_disc_read(void* user, uint64_t offset, uint32_t size, void* dst, uint32_t slot) {
    (void)user;
    (void)offset;
    (void)size;
    (void)dst;
    nebula_game_disc_complete(slot, -1);
}

static const uint8_t* svc_disc_dol(void* user, size_t* size) {
    (void)user;
    *size = 0;
    return NULL;
}

static const char* svc_disc_id(void* user) {
    (void)user;
    return "RMGE01";
}

static void svc_vi_frame(void* user, uint32_t xfb, int black, uint32_t retrace) {
    (void)user;
    (void)xfb;
    (void)black;
    (void)retrace;
}

static void svc_fs_request(void* user, const NebulaFsRequest* request, uint32_t slot) {
    (void)user;
    (void)request;
    nebula_game_fs_complete(slot, -1);
}

static void svc_power(void* user, uint32_t what) {
    (void)user;
    fprintf(stderr, "[host] power request %u\n", what);
    exit(0);
}

static void svc_game_stage(void* user, const char* name) {
    (void)user;
    (void)name;
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

/* The console's system settings: Nebula's host's answers (native/host/host_main.cpp), but for the aspect ratio, 4:3
   here: the page shows the XFB (640x480) as it is, without the 16:9 television's horizontal stretch. A host sets the
   language it was asked for (headless_host.c); English here, a language every release's table has. */
static uint32_t svc_sc_get(void* user, uint32_t item) {
    (void)user;
    switch (item) {
    case NEBULA_SC_LANGUAGE:
        return 1; /* English */
    case NEBULA_SC_SOUND_MODE:
        return 1; /* stereo */
    case NEBULA_SC_ASPECT_RATIO:
        return 0; /* 4:3 */
    case NEBULA_SC_PROGRESSIVE_MODE:
        return 0;
    case NEBULA_SC_EURGB60_MODE:
        return 1;
    case NEBULA_SC_SENSOR_BAR_POSITION:
        return 1; /* above the screen */
    case NEBULA_SC_DPD_SENSITIVITY:
        return 3;
    case NEBULA_SC_SPEAKER_VOLUME:
        return 0x58;
    case NEBULA_SC_SCREEN_SAVER_MODE:
        return 1;
    case NEBULA_SC_MOTOR_MODE:
        return 1;
    default:
        return 0;
    }
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

void smg_host_default_services(NebulaHostServices* s) {
    memset(s, 0, sizeof(*s));
    s->version = NEBULA_GAME_API_VERSION;
    s->size = sizeof(NebulaHostServices);
    s->achievement = svc_achievement;
    s->achievement_date = svc_achievement_date;
    s->ai_control = svc_ai_control;
    s->ai_dma = svc_ai_dma;
    s->art_export = svc_art_export;
    s->audio_dsp = svc_audio_dsp;
    s->button_icons = svc_button_icons;
    s->code_patch = svc_code_patch;
    s->curved_surface = svc_curved_surface;
    s->disc_dol = svc_disc_dol;
    s->disc_fst = svc_disc_fst;
    s->disc_id = svc_disc_id;
    s->disc_read = svc_disc_read;
    s->display_extent = svc_display_extent;
    s->fatal = svc_fatal;
    s->fs_request = svc_fs_request;
    s->galaxy_select_name = svc_galaxy_select_name;
    s->galaxy_select_names_wanted = svc_galaxy_select_names_wanted;
    s->galaxy_select_stage = svc_galaxy_select_stage;
    s->game_context = svc_game_context;
    s->game_light = svc_game_light;
    s->game_stage = svc_game_stage;
    s->gpu_coherent = svc_gpu_coherent;
    s->gx_pe_marker = svc_gx_pe_marker;
    s->gx_peek_efb = svc_gx_peek_efb;
    s->gx_scene_marker = svc_gx_scene_marker;
    s->gx_stripe_particle = svc_gx_stripe_particle;
    s->gx_submit = svc_gx_submit;
    s->hbm = svc_hbm;
    s->layer_2d_scale = svc_layer_2d_scale;
    s->light_reach = svc_light_reach;
    s->log = svc_log;
    s->movie_frame = svc_movie_frame;
    s->object_name = svc_object_name;
    s->option = svc_option;
    s->pointer_wheel = svc_pointer_wheel;
    s->power = svc_power;
    s->presence = svc_presence;
    s->sc_get = svc_sc_get;
    s->scene_surround = svc_scene_surround;
    s->settings_input = svc_settings_input;
    s->settings_view = svc_settings_view;
    s->sharp_2d = svc_sharp_2d;
    s->stat_add = svc_stat_add;
    s->stat_get = svc_stat_get;
    s->test_scene_frame = svc_test_scene_frame;
    s->thp_video_decode = svc_thp_video_decode;
    s->traced_sun = svc_traced_sun;
    s->vi_frame = svc_vi_frame;
    s->wpad_control = svc_wpad_control;
    s->wpad_sample = svc_wpad_sample;
    s->wpad_speaker_data = svc_wpad_speaker_data;
}
