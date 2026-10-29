/* The console's devices without a renderer (console_services.h), after Nebula's host without a renderer
   (third_party/nebula/native/host/host_main.cpp): its stand-in Wii Remote at rest, its audio interface clock, its NAND
   operations (here on folders and files kept in memory instead of the host's disk). */
#include "console_services.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Atomic uint64_t sWpadReports, sAiBlocks, sNandRequests, sNandFailures;

void smg_console_counters(SmgConsoleCounters* out) {
    out->wpad_reports = atomic_load(&sWpadReports);
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

/* ---- the Wii Remote: connected with a Nunchuk when the game starts WPAD, a report every 10 ms, nothing pressed ---- */

static _Atomic uint32_t sWpadReport = 0x30;
static atomic_int sWpadStarted;

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

/* A report of the remote at rest, from its id: no buttons, 1 g on Z, no IR points, the Nunchuk level with its stick at
   the centre and C and Z up. Returns its size. */
static size_t idle_report(uint8_t id, uint8_t* report) {
    memset(report, 0, 22);
    report[0] = id;
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
        memset(report + 6, 0xFF, 12);
        return 18;
    }
    memset(report + 6, 0xFF, 10);
    static const uint8_t nunchuk[6] = {0x7b, 0x81, 0x7d, 0x7e, 0xae, 0x03};
    memcpy(report + 16, nunchuk, sizeof(nunchuk));
    return 22;
}

static void* wpad_main(void* arg) {
    (void)arg;
    NebulaWiimoteConnection connection;
    idle_connection(&connection);
    nebula_game_wpad_connect(0, 1, &connection);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        advance(&next, 10000000);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        uint8_t report[22];
        const size_t size = idle_report((uint8_t)atomic_load(&sWpadReport), report);
        nebula_game_wpad_report(0, report, size, NULL);
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
        fprintf(stderr, "[host] Wii Remote 1 connected, with a Nunchuk (at rest: no input yet)\n");
    } else if (what == NEBULA_WPAD_FORMAT) {
        atomic_store(&sWpadReport, report_id_for_format(value));
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
    fprintf(stderr, "[host] NAND in memory (saves last until the page closes): %s\n", saves);
}
