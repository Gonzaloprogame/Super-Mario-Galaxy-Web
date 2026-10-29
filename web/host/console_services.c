/* The console's devices without a renderer (console_services.h), after Nebula's host without a renderer
   (third_party/nebula/native/host/host_main.cpp): its stand-in Wii Remote at rest, its audio interface clock, its NAND
   operations (here on folders and files kept in memory instead of the host's disk). */
#include "console_services.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Atomic uint64_t sWpadReports, sWpadSamples, sAiBlocks, sNandRequests, sNandFailures;

void smg_console_counters(SmgConsoleCounters* out) {
    out->wpad_reports = atomic_load(&sWpadReports);
    out->wpad_samples = atomic_load(&sWpadSamples);
    out->ai_blocks = atomic_load(&sAiBlocks);
    out->nand_requests = atomic_load(&sNandRequests);
    out->nand_failures = atomic_load(&sNandFailures);
}

static void advance(struct timespec* t, int64_t ns) {
    t->tv_nsec += ns % 1000000000;
    t->tv_sec += ns / 1000000000;
    if (t->tv_nsec >= 1000000000) {
        t->tv_nsec -= 1000000000;
        t->tv_sec++;
    }
}

static int64_t elapsed_ns(const struct timespec* from, const struct timespec* to) {
    return (int64_t)(to->tv_sec - from->tv_sec) * 1000000000 + (to->tv_nsec - from->tv_nsec);
}

/* ---- the Wii Remote: connected with a Nunchuk, a report every 10 ms with the page's controls ----
   It connects at the game's first read of its remotes (wpad_sample, each frame), not when the game starts WPAD: the
   game starts WPAD (KPADInit) in WPadHolder's constructor and sets its connect and extension callbacks at that
   constructor's end, so a connection handled in between is not heard, and the game takes the Nunchuk for missing.
   On a phone it was (the game said the Nunchuk's connection was lost); a console's remote connects much later. */

static _Atomic uint32_t sWpadReport = 0x30;
static atomic_int sWpadStarted, sWpadSampled;
static pthread_mutex_t sWpadLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sWpadWake = PTHREAD_COND_INITIALIZER;

static uint8_t report_id_for_format(uint32_t format) {
    switch (format) {
    case 0:
        return 0x30; /* WPAD_FMT_CORE */
    case 1:
        return 0x31; /* WPAD_FMT_CORE_ACC */
    case 2:
        return 0x33; /* WPAD_FMT_CORE_ACC_DPD */
    default:
        return 0x37; /* the Nunchuk's formats: core, accelerometer, IR and extension */
    }
}

/* The stand-in remote's calibration (Nebula's): IR, then the accelerometer's 0 g and 1 g, each twice in its EEPROM;
   the Nunchuk's accelerometer 0 g and 1 g, and its stick's maximum, minimum and centre. */
static void idle_connection(NebulaWiimoteConnection* c) {
    static const uint8_t ir[11] = {0x7f, 0xa2, 0x8b, 0x80, 0xa2, 0x80, 0x5d, 0x30, 0x7f, 0x5d, 0x0c};
    static const uint8_t accel[10] = {0x80, 0x80, 0x80, 0x00, 0x99, 0x99, 0x99, 0x00, 0x00, 0xa0};
    static const uint8_t nunchuk[16] = {0x7d, 0x7e, 0x7a, 0x3c, 0xb1, 0xaf, 0xae, 0x1b,
                                        0xe0, 0x15, 0x7b, 0xe6, 0x1d, 0x81, 0x23, 0x78};
    memset(c, 0, sizeof(*c));
    memcpy(c->eeprom, ir, sizeof(ir));
    memcpy(c->eeprom + 0x0B, ir, sizeof(ir));
    memcpy(c->eeprom + 0x16, accel, sizeof(accel));
    memcpy(c->eeprom + 0x20, accel, sizeof(accel));
    memcpy(c->extension_calibration, nunchuk, sizeof(nunchuk));
    c->extension = 1; /* WPAD_DEV_FREESTYLE: the Nunchuk */
    c->battery = 4;   /* full */
}

/* The controls the page writes (web/shell/controls.js), SMG_INPUT_* words: read for each report */
static _Atomic int32_t sInput[SMG_INPUT_WORDS];

int32_t* smg_console_input(void) {
    return (int32_t*)sInput;
}

/* Two IR dots, side by side as the sensor bar's, where the game's own KPAD decoding puts the pointer at (x, y)
   (-1..1, x right, y down): Nebula's placement (runtime/src/native_input.cpp, publish_wiimote_ir_dots), 226 camera
   units a pointer unit around (510, 487), the dots 160 apart. */
static void pointer_dots(int32_t x10000, int32_t y10000, uint16_t dots[2][2]) {
    const double x = x10000 < -10000 ? -1.0 : x10000 > 10000 ? 1.0 : x10000 / 10000.0;
    const double y = y10000 < -10000 ? -1.0 : y10000 > 10000 ? 1.0 : y10000 / 10000.0;
    long cx = lround(510.0 - x * 226.0);
    long cy = lround(487.0 + y * 226.0);
    cx = cx < 26 + 80 ? 26 + 80 : cx > 997 - 80 ? 997 - 80 : cx;
    cy = cy < 26 ? 26 : cy > 725 ? 725 : cy;
    dots[0][0] = (uint16_t)(cx - 80);
    dots[0][1] = (uint16_t)cy;
    dots[1][0] = (uint16_t)(cx + 80);
    dots[1][1] = (uint16_t)cy;
}

/* The Nunchuk's stick byte for -100..100, from its calibration's minimum, centre and maximum */
static uint8_t stick_byte(int32_t value, int minimum, int centre, int maximum) {
    value = value < -100 ? -100 : value > 100 ? 100 : value;
    return (uint8_t)(centre + (value >= 0 ? value * (maximum - centre) : value * (centre - minimum)) / 100);
}

/* The report `id` for the controls now (the format the game asked for): the core buttons, the accelerometer at rest
   (1 g on Z), the IR dots of the pointer when it is on the screen, the Nunchuk (stick, C and Z, level). Returns its
   size; *pointer is set, and 1 returned in *exact, when the report carries the pointer. */
size_t smg_console_report(uint8_t id, const int32_t* input, uint8_t* report, NebulaWpadPointer* pointer, int* exact) {
    const uint32_t buttons = (uint32_t)input[SMG_INPUT_BUTTONS];
    const int onScreen = input[SMG_INPUT_POINTER] != 0;
    uint16_t dots[2][2];
    pointer_dots(input[SMG_INPUT_POINTER_X], input[SMG_INPUT_POINTER_Y], dots);
    *exact = 0;
    memset(report, 0, 22);
    report[0] = id;
    report[1] = (uint8_t)(buttons & 0x1F);        /* the D-pad and Plus */
    report[2] = (uint8_t)((buttons >> 8) & 0x9F); /* 2, 1, B, A, Minus, Home */
    if (id == 0x30) {
        return 3;
    }
    report[3] = 0x80;
    report[4] = 0x80;
    report[5] = 0x99;
    if (id == 0x31) {
        return 6;
    }
    if (id == 0x33) {
        /* Extended IR: 3 bytes a dot, 4 dots */
        memset(report + 6, 0xFF, 12);
        if (onScreen) {
            for (int i = 0; i < 2; i++) {
                const uint16_t x = dots[i][0], y = dots[i][1];
                report[6 + i * 3] = (uint8_t)x;
                report[7 + i * 3] = (uint8_t)y;
                report[8 + i * 3] = (uint8_t)(((y >> 8) & 3) << 6 | ((x >> 8) & 3) << 4 | 8);
            }
        }
    } else {
        /* Basic IR: 5 bytes a pair of dots, 2 pairs; then the Nunchuk's 6 bytes */
        memset(report + 6, 0xFF, 10);
        if (onScreen) {
            const uint16_t x1 = dots[0][0], y1 = dots[0][1], x2 = dots[1][0], y2 = dots[1][1];
            report[6] = (uint8_t)x1;
            report[7] = (uint8_t)y1;
            report[8] = (uint8_t)(((y1 >> 8) & 3) << 6 | ((x1 >> 8) & 3) << 4 | ((y2 >> 8) & 3) << 2 | ((x2 >> 8) & 3));
            report[9] = (uint8_t)x2;
            report[10] = (uint8_t)y2;
        }
        report[16] = stick_byte(input[SMG_INPUT_STICK_X], 0x15, 0x7b, 0xe0);
        report[17] = stick_byte(input[SMG_INPUT_STICK_Y], 0x1d, 0x81, 0xe6);
        report[18] = 0x7d; /* the Nunchuk level: its calibration's 0 g on X and Y, 1 g on Z */
        report[19] = 0x7e;
        report[20] = 0xae;
        report[21] = (uint8_t)(0x03 & ~((buttons & 0x2000 ? 1 : 0) | (buttons & 0x4000 ? 2 : 0))); /* Z, C: 0 held */
    }
    if (onScreen) {
        pointer->x = (float)(input[SMG_INPUT_POINTER_X] / 10000.0);
        pointer->y = (float)(input[SMG_INPUT_POINTER_Y] / 10000.0);
        *exact = 1;
    }
    return id == 0x33 ? 18 : 22;
}

static void* wpad_main(void* arg) {
    (void)arg;
    pthread_mutex_lock(&sWpadLock);
    while (!atomic_load(&sWpadSampled)) {
        pthread_cond_wait(&sWpadWake, &sWpadLock);
    }
    pthread_mutex_unlock(&sWpadLock);
    NebulaWiimoteConnection connection;
    idle_connection(&connection);
    nebula_game_wpad_connect(0, 1, &connection);
    fprintf(stderr, "[host] Wii Remote 1 connected, with a Nunchuk (the page's controls)\n");
    uint32_t shake = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        advance(&next, 10000000);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_ns(&next, &now) > 100000000) {
            next = now; /* after a stall past 100 ms (the game busy), no burst of the reports missed */
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        int32_t input[SMG_INPUT_WORDS];
        for (int i = 0; i < SMG_INPUT_WORDS; i++) {
            input[i] = atomic_load(&sInput[i]);
        }
        const uint32_t held = (uint32_t)input[SMG_INPUT_SHAKE] & 3;
        if (held != shake) {
            shake = held;
            nebula_game_wpad_shake(0, held); /* each press is one swing */
        }
        uint8_t report[22];
        NebulaWpadPointer pointer;
        int exact;
        const size_t size = smg_console_report((uint8_t)atomic_load(&sWpadReport), input, report, &pointer, &exact);
        nebula_game_wpad_report(0, report, size, exact ? &pointer : NULL);
        atomic_fetch_add(&sWpadReports, 1);
    }
    return NULL;
}

void smg_console_wpad_control(void* user, uint32_t chan, uint32_t what, uint32_t value) {
    (void)user;
    if (chan != 0) {
        return;
    }
    if (what == NEBULA_WPAD_INIT && atomic_exchange(&sWpadStarted, 1) == 0) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, wpad_main, NULL) != 0) {
            fprintf(stderr, "[host] the Wii Remote's thread cannot start\n");
            return;
        }
        pthread_detach(thread);
    } else if (what == NEBULA_WPAD_FORMAT) {
        atomic_store(&sWpadReport, report_id_for_format(value));
    }
}

void smg_console_wpad_sample(void* user, uint32_t chan) {
    (void)user;
    atomic_fetch_add(&sWpadSamples, 1);
    if (chan == 0 && atomic_load(&sWpadStarted) && !atomic_load(&sWpadSampled)) {
        pthread_mutex_lock(&sWpadLock);
        atomic_store(&sWpadSampled, 1);
        pthread_cond_signal(&sWpadWake);
        pthread_mutex_unlock(&sWpadLock);
    }
}

/* ---- the audio interface: each latched DMA block taken at its block's end, then the AI interrupt ---- */

static pthread_mutex_t sAiLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sAiWake = PTHREAD_COND_INITIALIZER;
static uint32_t sAiSize, sAiRate = 32000;
static int sAiRunning;

void smg_console_ai_dma(void* user, uint32_t start, uint32_t size) {
    (void)user;
    (void)start; /* no sound output yet: the samples are not read */
    pthread_mutex_lock(&sAiLock);
    sAiSize = size;
    pthread_cond_signal(&sAiWake);
    pthread_mutex_unlock(&sAiLock);
}

void smg_console_ai_control(void* user, uint32_t what, uint32_t value) {
    (void)user;
    pthread_mutex_lock(&sAiLock);
    if (what == NEBULA_AI_START) {
        sAiRunning = 1;
    } else if (what == NEBULA_AI_STOP) {
        sAiRunning = 0;
    } else if (what == NEBULA_AI_RATE) {
        sAiRate = value;
    }
    pthread_cond_signal(&sAiWake);
    pthread_mutex_unlock(&sAiLock);
}

static void* ai_main(void* arg) {
    (void)arg;
    struct timespec next;
    int scheduled = 0;
    for (;;) {
        pthread_mutex_lock(&sAiLock);
        if (!(sAiRunning && sAiSize != 0)) {
            /* Stopped, or nothing latched yet: the schedule starts again from the block that resumes it */
            while (!(sAiRunning && sAiSize != 0)) {
                pthread_cond_wait(&sAiWake, &sAiLock);
            }
            scheduled = 0;
        }
        const uint64_t size = sAiSize;
        const uint64_t rate = sAiRate != 0 ? sAiRate : 32000;
        pthread_mutex_unlock(&sAiLock);
        if (!scheduled) {
            clock_gettime(CLOCK_MONOTONIC, &next);
            scheduled = 1;
        }
        atomic_fetch_add(&sAiBlocks, 1);
        nebula_game_ai_interrupt();
        /* A block lasts its stereo 16-bit samples (4 bytes each) at the output rate */
        advance(&next, (int64_t)(size / 4 * 1000000000ull / rate));
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_ns(&next, &now) > 100000000) {
            next = now; /* a stall past 100 ms starts the schedule again instead of catching up in a burst */
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

/* ---- the NAND, in memory: the console's absolute paths, folders and files ---- */

enum { MaxNodes = 512, MaxPath = 96, MaxFds = 16 };

typedef struct {
    int used;
    int directory;
    char path[MaxPath];
    uint8_t* data;
    size_t size;
    size_t capacity;
} Node;

typedef struct {
    int node; /* -1: closed */
    int writable;
    size_t position;
    int safe; /* a safe open for writing: the writes go to `copy`, which the safe close puts in place */
    uint8_t* copy;
    size_t copySize;
    size_t copyCapacity;
} OpenFile;

static Node sNodes[MaxNodes];
static OpenFile sFds[MaxFds];

/* A path the NAND accepts: absolute, no empty, "." or ".." component, no trailing '/', not too long */
static int valid_path(const char* path) {
    if (path == NULL || path[0] != '/' || strlen(path) >= MaxPath) {
        return 0;
    }
    if (strcmp(path, "/") == 0) {
        return 1;
    }
    for (const char* part = path + 1;;) {
        const char* end = strchr(part, '/');
        const size_t length = end != NULL ? (size_t)(end - part) : strlen(part);
        if (length == 0 || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.')) {
            return 0;
        }
        if (end == NULL) {
            return 1;
        }
        part = end + 1;
    }
}

static int find(const char* path) {
    for (int i = 0; i < MaxNodes; i++) {
        if (sNodes[i].used && strcmp(sNodes[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/* Whether `path` is under the folder `folder` */
static int inside(const char* path, const char* folder) {
    const size_t length = strlen(folder);
    return strncmp(path, folder, length) == 0 && path[length] == '/';
}

static int parent_is_directory(const char* path) {
    char parent[MaxPath];
    snprintf(parent, sizeof(parent), "%s", path);
    char* slash = strrchr(parent, '/');
    if (slash == parent) {
        return 1; /* the root */
    }
    *slash = '\0';
    const int node = find(parent);
    return node >= 0 && sNodes[node].directory;
}

static int add_node(const char* path, int directory) {
    for (int i = 0; i < MaxNodes; i++) {
        if (!sNodes[i].used) {
            memset(&sNodes[i], 0, sizeof(sNodes[i]));
            sNodes[i].used = 1;
            sNodes[i].directory = directory;
            snprintf(sNodes[i].path, sizeof(sNodes[i].path), "%s", path);
            return i;
        }
    }
    return -1;
}

static void remove_node(int node) {
    free(sNodes[node].data);
    memset(&sNodes[node], 0, sizeof(sNodes[node]));
    for (int fd = 0; fd < MaxFds; fd++) {
        if (sFds[fd].node == node) {
            sFds[fd].node = -1; /* its file is gone: further reads and writes are refused */
        }
    }
}

static int reserve(uint8_t** data, size_t* capacity, size_t size) {
    if (size <= *capacity) {
        return 1;
    }
    size_t grown = *capacity != 0 ? *capacity : 256;
    while (grown < size) {
        grown *= 2;
    }
    uint8_t* bigger = realloc(*data, grown);
    if (bigger == NULL) {
        return 0;
    }
    memset(bigger + *capacity, 0, grown - *capacity);
    *data = bigger;
    *capacity = grown;
    return 1;
}

static OpenFile* open_fd(int32_t fd) {
    return fd >= 0 && fd < MaxFds && sFds[fd].node >= 0 ? &sFds[fd] : NULL;
}

static int32_t fs_run(const NebulaFsRequest* r) {
    switch (r->op) {
    case NEBULA_FS_OPEN:
    case NEBULA_FS_SAFE_OPEN: {
        if (!valid_path(r->path)) {
            return -8; /* NAND_RESULT_INVALID */
        }
        const int node = find(r->path);
        if (node < 0 || sNodes[node].directory) {
            return -12; /* NAND_RESULT_NOEXISTS */
        }
        int32_t fd = 0;
        while (fd < MaxFds && sFds[fd].node >= 0) {
            fd++;
        }
        if (fd == MaxFds) {
            return -10; /* NAND_RESULT_MAXFD */
        }
        OpenFile* f = &sFds[fd];
        memset(f, 0, sizeof(*f));
        f->node = node;
        f->writable = r->mode != 1; /* NAND_ACCESS_READ 1, WRITE 2, RW 3 */
        if (r->op == NEBULA_FS_SAFE_OPEN && f->writable) {
            f->safe = 1;
            if (!reserve(&f->copy, &f->copyCapacity, sNodes[node].size)) {
                f->node = -1;
                return -2; /* NAND_RESULT_ALLOC_FAILED */
            }
            if (sNodes[node].size != 0) {
                memcpy(f->copy, sNodes[node].data, sNodes[node].size);
            }
            f->copySize = sNodes[node].size;
        }
        return fd;
    }
    case NEBULA_FS_CLOSE:
    case NEBULA_FS_SAFE_CLOSE: {
        if (r->fd < 0 || r->fd >= MaxFds || (sFds[r->fd].node < 0 && !sFds[r->fd].safe)) {
            return -8;
        }
        OpenFile* f = &sFds[r->fd];
        int32_t result = 0;
        if (f->safe) {
            if (f->node >= 0) {
                Node* n = &sNodes[f->node];
                free(n->data);
                n->data = f->copy;
                n->size = f->copySize;
                n->capacity = f->copyCapacity;
                f->copy = NULL;
            } else {
                result = -1; /* NAND_RESULT_ACCESS: the file went away meanwhile */
            }
        }
        free(f->copy);
        memset(f, 0, sizeof(*f));
        f->node = -1;
        return result;
    }
    case NEBULA_FS_READ:
    case NEBULA_FS_WRITE: {
        OpenFile* f = open_fd(r->fd);
        if (f == NULL) {
            return -8;
        }
        Node* n = &sNodes[f->node];
        uint8_t** data = f->safe ? &f->copy : &n->data;
        size_t* size = f->safe ? &f->copySize : &n->size;
        size_t* capacity = f->safe ? &f->copyCapacity : &n->capacity;
        if (r->op == NEBULA_FS_READ) {
            const size_t available = f->position < *size ? *size - f->position : 0;
            const size_t count = r->size < available ? r->size : available;
            if (count != 0) {
                memcpy(r->buffer, *data + f->position, count);
            }
            f->position += count;
            return (int32_t)count;
        }
        if (!f->writable) {
            return -1;
        }
        if (!reserve(data, capacity, f->position + r->size)) {
            return -2;
        }
        memcpy(*data + f->position, r->buffer, r->size);
        f->position += r->size;
        if (f->position > *size) {
            *size = f->position;
        }
        return (int32_t)r->size;
    }
    case NEBULA_FS_SEEK:
    case NEBULA_FS_LENGTH: {
        OpenFile* f = open_fd(r->fd);
        if (f == NULL) {
            return -8;
        }
        const size_t size = f->safe ? f->copySize : sNodes[f->node].size;
        if (r->op == NEBULA_FS_LENGTH) {
            return (int32_t)size;
        }
        const int64_t base = r->whence == 0 ? 0 : r->whence == 1 ? (int64_t)f->position : (int64_t)size;
        const int64_t position = base + r->offset;
        if (position < 0 || position > INT32_MAX) {
            return -8;
        }
        f->position = (size_t)position;
        return (int32_t)position;
    }
    case NEBULA_FS_CREATE:
    case NEBULA_FS_CREATE_DIR: {
        if (!valid_path(r->path) || strcmp(r->path, "/") == 0) {
            return -8;
        }
        if (find(r->path) >= 0) {
            return -6; /* NAND_RESULT_EXISTS */
        }
        if (!parent_is_directory(r->path)) {
            return -12;
        }
        return add_node(r->path, r->op == NEBULA_FS_CREATE_DIR) >= 0 ? 0 : -11; /* NAND_RESULT_MAXFILES */
    }
    case NEBULA_FS_DELETE: {
        if (!valid_path(r->path)) {
            return -8;
        }
        const int node = find(r->path);
        if (node < 0) {
            return -12;
        }
        for (int i = 0; i < MaxNodes; i++) {
            if (sNodes[i].used && inside(sNodes[i].path, r->path)) {
                remove_node(i);
            }
        }
        remove_node(node);
        return 0;
    }
    case NEBULA_FS_RENAME: {
        if (!valid_path(r->path) || !valid_path(r->path2)) {
            return -8;
        }
        const int from = find(r->path);
        if (from < 0) {
            return -12;
        }
        if (strcmp(r->path, r->path2) == 0) {
            return 0;
        }
        if (inside(r->path2, r->path) || !parent_is_directory(r->path2)) {
            return -8;
        }
        const int to = find(r->path2);
        if (to >= 0) {
            for (int i = 0; i < MaxNodes; i++) {
                if (sNodes[i].used && inside(sNodes[i].path, r->path2)) {
                    return -13; /* NAND_RESULT_NOTEMPTY */
                }
            }
            remove_node(to); /* a file, or an empty folder: replaced */
        }
        /* The node and everything under it take the new path */
        const size_t oldLength = strlen(r->path);
        for (int i = 0; i < MaxNodes; i++) {
            if (sNodes[i].used && (i == from || inside(sNodes[i].path, r->path))) {
                char renamed[MaxPath];
                if (snprintf(renamed, sizeof(renamed), "%s%s", r->path2, sNodes[i].path + oldLength) >= MaxPath) {
                    return -8;
                }
                memcpy(sNodes[i].path, renamed, sizeof(renamed));
            }
        }
        return 0;
    }
    default:
        return -8;
    }
}

/* Requests run in order on the NAND's thread, which completes each (nebula_game_fs_complete) */
enum { QueueSize = 32 };

typedef struct {
    NebulaFsRequest request;
    uint32_t slot;
} Job;

static Job sQueue[QueueSize];
static uint32_t sHead, sTail;
static pthread_mutex_t sNandLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sNandWake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t sNandRoom = PTHREAD_COND_INITIALIZER;

void smg_console_fs_request(void* user, const NebulaFsRequest* request, uint32_t slot) {
    (void)user;
    pthread_mutex_lock(&sNandLock);
    while (sTail - sHead == QueueSize) {
        pthread_cond_wait(&sNandRoom, &sNandLock);
    }
    sQueue[sTail % QueueSize] = (Job){*request, slot};
    sTail++;
    pthread_cond_signal(&sNandWake);
    pthread_mutex_unlock(&sNandLock);
}

/* ---- the NAND kept by the page between sessions (IndexedDB, web/shell/app.js) ----
   A copy of every folder and file but /tmp's, in a blob the page stores as it is: "SMGN", the node count (u32), then
   per node its kind (u8: 1 folder, 0 file), its path's length (u16) and path, its size (u32) and bytes; little-endian.
   Written after each change the game has finished (a file closed after writes, a create, delete or rename). */
static void (*sNandChanged)(const uint8_t* blob, size_t size);
static int sNandDirty; /* NAND thread only */

void smg_console_on_nand_change(void (*changed)(const uint8_t* blob, size_t size)) {
    sNandChanged = changed;
}

static int kept(const Node* n) {
    return n->used && strcmp(n->path, "/tmp") != 0 && strncmp(n->path, "/tmp/", 5) != 0;
}

static void put_le(uint8_t* p, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; i++) {
        p[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint32_t get_le(const uint8_t* p, int bytes) {
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) {
        value |= (uint32_t)p[i] << (8 * i);
    }
    return value;
}

size_t smg_console_nand_export(uint8_t** out) {
    size_t size = 8;
    uint32_t count = 0;
    for (int i = 0; i < MaxNodes; i++) {
        if (kept(&sNodes[i])) {
            size += 1 + 2 + strlen(sNodes[i].path) + 4 + sNodes[i].size;
            count++;
        }
    }
    uint8_t* blob = malloc(size);
    if (blob == NULL) {
        *out = NULL;
        return 0;
    }
    memcpy(blob, "SMGN", 4);
    put_le(blob + 4, count, 4);
    size_t at = 8;
    for (int i = 0; i < MaxNodes; i++) {
        const Node* n = &sNodes[i];
        if (!kept(n)) {
            continue;
        }
        const size_t length = strlen(n->path);
        blob[at] = (uint8_t)n->directory;
        put_le(blob + at + 1, (uint32_t)length, 2);
        memcpy(blob + at + 3, n->path, length);
        put_le(blob + at + 3 + length, (uint32_t)n->size, 4);
        if (n->size != 0) {
            memcpy(blob + at + 7 + length, n->data, n->size);
        }
        at += 7 + length + n->size;
    }
    *out = blob;
    return size;
}

static void create_folders(const char* path);

int smg_console_nand_import(const uint8_t* blob, size_t size) {
    if (size < 8 || memcmp(blob, "SMGN", 4) != 0) {
        return -1;
    }
    const uint32_t count = get_le(blob + 4, 4);
    size_t at = 8;
    for (uint32_t k = 0; k < count; k++) {
        if (size - at < 3) {
            return -1;
        }
        const int directory = blob[at] != 0;
        const size_t length = get_le(blob + at + 1, 2);
        if (length == 0 || length >= MaxPath || size - at < 7 + length) {
            return -1;
        }
        char path[MaxPath];
        memcpy(path, blob + at + 3, length);
        path[length] = '\0';
        const size_t bytes = get_le(blob + at + 3 + length, 4);
        if (size - at - 7 - length < bytes || !valid_path(path)) {
            return -1;
        }
        const uint8_t* data = blob + at + 7 + length;
        at += 7 + length + bytes;
        if (directory) {
            create_folders(path);
            continue;
        }
        char parent[MaxPath];
        memcpy(parent, path, length + 1);
        *strrchr(parent, '/') = '\0';
        if (parent[0] != '\0') {
            create_folders(parent);
        }
        int node = find(path);
        if (node < 0 && (node = add_node(path, 0)) < 0) {
            return -1;
        }
        Node* n = &sNodes[node];
        n->size = 0;
        if (!reserve(&n->data, &n->capacity, bytes)) {
            return -1;
        }
        if (bytes != 0) {
            memcpy(n->data, data, bytes);
        }
        n->size = bytes;
    }
    return (int)count;
}

static void* nand_main(void* arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&sNandLock);
        while (sHead == sTail) {
            pthread_cond_wait(&sNandWake, &sNandLock);
        }
        const Job job = sQueue[sHead % QueueSize];
        sHead++;
        pthread_cond_signal(&sNandRoom);
        pthread_mutex_unlock(&sNandLock);
        const int32_t result = fs_run(&job.request);
        atomic_fetch_add(&sNandRequests, 1);
        if (result < 0) {
            atomic_fetch_add(&sNandFailures, 1);
        }
        /* The page's copy follows each change once its file is closed (not every write) */
        const uint32_t op = job.request.op;
        if (result >= 0 && op != NEBULA_FS_OPEN && op != NEBULA_FS_SAFE_OPEN && op != NEBULA_FS_READ &&
            op != NEBULA_FS_SEEK && op != NEBULA_FS_LENGTH) {
            sNandDirty = 1;
        }
        const int settled = op != NEBULA_FS_WRITE;
        if (sNandDirty && settled && result >= 0 && sNandChanged != NULL) {
            uint8_t* blob;
            const size_t size = smg_console_nand_export(&blob);
            if (blob != NULL) {
                sNandChanged(blob, size);
                free(blob);
            }
            sNandDirty = 0;
        }
        nebula_game_fs_complete(job.slot, result);
    }
    return NULL;
}

/* ---- start ---- */

static void create_folders(const char* path) {
    char partial[MaxPath];
    for (const char* slash = strchr(path + 1, '/');; slash = strchr(slash + 1, '/')) {
        const size_t length = slash != NULL ? (size_t)(slash - path) : strlen(path);
        snprintf(partial, sizeof(partial), "%.*s", (int)length, path);
        if (find(partial) < 0) {
            add_node(partial, 1);
        }
        if (slash == NULL) {
            return;
        }
    }
}

void smg_console_start(const char* game_id) {
    atomic_store(&sInput[SMG_INPUT_MAGIC_WORD], SMG_INPUT_MAGIC);
    for (int fd = 0; fd < MaxFds; fd++) {
        sFds[fd].node = -1;
    }
    /* /tmp, where saves stage their files; the game's save folder, /title/00010000/<its 4-letter code in hex>/data */
    create_folders("/tmp");
    char saves[MaxPath];
    snprintf(saves, sizeof(saves), "/title/00010000/%02x%02x%02x%02x/data", (uint8_t)game_id[0], (uint8_t)game_id[1],
             (uint8_t)game_id[2], (uint8_t)game_id[3]);
    create_folders(saves);
    pthread_t thread;
    if (pthread_create(&thread, NULL, nand_main, NULL) != 0 || pthread_detach(thread) != 0 ||
        pthread_create(&thread, NULL, ai_main, NULL) != 0 || pthread_detach(thread) != 0) {
        fprintf(stderr, "[host] the NAND's or the audio interface's thread cannot start\n");
        exit(2);
    }
    fprintf(stderr, "[host] NAND in memory, kept by the page between sessions: %s\n", saves);
}
