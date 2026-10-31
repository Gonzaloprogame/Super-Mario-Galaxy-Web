/* The console's devices of the web host (web/host/console_services.c), against the game module's side stubbed here:
   the NAND's operations and results, the Wii Remote's connection and its reports every 10 ms, the audio interface's
   interrupt at each block's end. Node.js, with pthreads (run_os_tests.sh). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console_services.h"

static int failures;
static atomic_int sNandChanges;
static void nand_changed(const uint8_t* blob, size_t size) {
    (void)blob;
    (void)size;
    atomic_fetch_add(&sNandChanges, 1);
}

static void check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

static void sleep_ms(int ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}

/* ---- the game module's side ---- */

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sDone = PTHREAD_COND_INITIALIZER;
static int sCompleted;
static int32_t sResult;

void nebula_game_fs_complete(uint32_t slot, int32_t result) {
    (void)slot;
    pthread_mutex_lock(&sLock);
    sResult = result;
    sCompleted = 1;
    pthread_cond_signal(&sDone);
    pthread_mutex_unlock(&sLock);
}

static atomic_int sConnects, sExtension, sReports, sLastId, sLastSize, sInterrupts;

void nebula_game_wpad_connect(uint32_t chan, int connected, const NebulaWiimoteConnection* connection) {
    (void)chan;
    atomic_fetch_add(&sConnects, connected);
    atomic_store(&sExtension, (int)connection->extension);
}

void nebula_game_wpad_report(uint32_t chan, const uint8_t* report, size_t size, const NebulaWpadPointer* pointer) {
    (void)chan;
    (void)pointer;
    atomic_fetch_add(&sReports, 1);
    atomic_store(&sLastId, report[0]);
    atomic_store(&sLastSize, (int)size);
}

void nebula_game_wpad_shake(uint32_t chan, uint32_t held) {
    (void)chan;
    (void)held;
}

void nebula_game_ai_interrupt(void) {
    atomic_fetch_add(&sInterrupts, 1);
}

/* One NAND request, waited for */
static int32_t fs(uint32_t op, const char* path, const char* path2, int32_t fd, void* buffer, uint32_t size,
                  int32_t offset, int32_t whence, uint32_t mode) {
    const NebulaFsRequest request = {op, path, path2, fd, buffer, size, offset, whence, mode, 0};
    pthread_mutex_lock(&sLock);
    sCompleted = 0;
    pthread_mutex_unlock(&sLock);
    smg_console_fs_request(NULL, &request, 7);
    pthread_mutex_lock(&sLock);
    while (!sCompleted) {
        pthread_cond_wait(&sDone, &sLock);
    }
    const int32_t result = sResult;
    pthread_mutex_unlock(&sLock);
    return result;
}

#define DATA "/title/00010000/524d4745/data"

int main(void) {
    smg_console_start("RMGE01");
    smg_console_on_nand_change(nand_changed);

    /* NAND */
    char buffer[64] = {0};
    check(fs(NEBULA_FS_CREATE_DIR, DATA, NULL, 0, NULL, 0, 0, 0, 0) == -6, "NAND: the game's save folder exists");
    check(fs(NEBULA_FS_CREATE_DIR, "/tmp", NULL, 0, NULL, 0, 0, 0, 0) == -6, "NAND: /tmp exists");
    check(fs(NEBULA_FS_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 1) == -12, "NAND: a missing file: NOEXISTS");
    check(fs(NEBULA_FS_CREATE, "/nowhere/file", NULL, 0, NULL, 0, 0, 0, 0) == -12,
          "NAND: creating in a missing folder: NOEXISTS");
    check(fs(NEBULA_FS_OPEN, DATA "/../x", NULL, 0, NULL, 0, 0, 0, 1) == -8, "NAND: '..' in a path: INVALID");
    check(fs(NEBULA_FS_CREATE, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 0) == 0, "NAND: file created");
    check(fs(NEBULA_FS_CREATE, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 0) == -6, "NAND: created twice: EXISTS");
    const int32_t fd = fs(NEBULA_FS_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 3);
    check(fd >= 0, "NAND: opened for reading and writing");
    check(fs(NEBULA_FS_WRITE, NULL, NULL, fd, "Galaxy", 6, 0, 0, 0) == 6, "NAND: 6 bytes written");
    check(fs(NEBULA_FS_LENGTH, NULL, NULL, fd, NULL, 0, 0, 0, 0) == 6, "NAND: length 6");
    check(fs(NEBULA_FS_SEEK, NULL, NULL, fd, NULL, 0, 2, 0, 0) == 2, "NAND: seek to 2");
    check(fs(NEBULA_FS_READ, NULL, NULL, fd, buffer, 32, 0, 0, 0) == 4 && memcmp(buffer, "laxy", 4) == 0,
          "NAND: the rest read back ('laxy')");
    check(fs(NEBULA_FS_CLOSE, NULL, NULL, fd, NULL, 0, 0, 0, 0) == 0, "NAND: closed");
    check(fs(NEBULA_FS_READ, NULL, NULL, fd, buffer, 1, 0, 0, 0) == -8, "NAND: a closed descriptor: INVALID");

    /* A safe open: its writes appear only at its safe close */
    const int32_t safe = fs(NEBULA_FS_SAFE_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 2);
    fs(NEBULA_FS_WRITE, NULL, NULL, safe, "Mario!", 6, 0, 0, 0);
    const int32_t reader = fs(NEBULA_FS_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 1);
    memset(buffer, 0, sizeof(buffer));
    fs(NEBULA_FS_READ, NULL, NULL, reader, buffer, 6, 0, 0, 0);
    check(memcmp(buffer, "Galaxy", 6) == 0, "NAND: a safe open's writes are not seen before its close");
    check(fs(NEBULA_FS_SAFE_CLOSE, NULL, NULL, safe, NULL, 0, 0, 0, 0) == 0, "NAND: safe close");
    fs(NEBULA_FS_SEEK, NULL, NULL, reader, NULL, 0, 0, 0, 0);
    fs(NEBULA_FS_READ, NULL, NULL, reader, buffer, 6, 0, 0, 0);
    check(memcmp(buffer, "Mario!", 6) == 0, "NAND: ... and are after it");
    fs(NEBULA_FS_CLOSE, NULL, NULL, reader, NULL, 0, 0, 0, 0);

    /* Folders: a rename carries what they hold; a delete removes it */
    fs(NEBULA_FS_CREATE_DIR, "/tmp/stage", NULL, 0, NULL, 0, 0, 0, 0);
    fs(NEBULA_FS_CREATE, "/tmp/stage/banner.bin", NULL, 0, NULL, 0, 0, 0, 0);
    check(fs(NEBULA_FS_RENAME, "/tmp/stage", DATA "/stage", 0, NULL, 0, 0, 0, 0) == 0, "NAND: folder renamed");
    check(fs(NEBULA_FS_OPEN, "/tmp/stage/banner.bin", NULL, 0, NULL, 0, 0, 0, 1) == -12 &&
              fs(NEBULA_FS_CLOSE, NULL, NULL, fs(NEBULA_FS_OPEN, DATA "/stage/banner.bin", NULL, 0, NULL, 0, 0, 0, 1),
                 NULL, 0, 0, 0, 0) == 0,
          "NAND: its file moved with it");
    check(fs(NEBULA_FS_DELETE, DATA "/stage", NULL, 0, NULL, 0, 0, 0, 0) == 0 &&
              fs(NEBULA_FS_OPEN, DATA "/stage/banner.bin", NULL, 0, NULL, 0, 0, 0, 1) == -12,
          "NAND: folder deleted with its file");

    /* The NAND kept between sessions: a change reaches the page once its file is closed; the blob holds every folder
       and file but /tmp's and brings them back */
    {
        const int before = atomic_load(&sNandChanges);
        const int32_t f = fs(NEBULA_FS_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 3);
        fs(NEBULA_FS_WRITE, NULL, NULL, f, "Luigi!", 6, 0, 0, 0);
        const int afterWrite = atomic_load(&sNandChanges);
        fs(NEBULA_FS_CLOSE, NULL, NULL, f, NULL, 0, 0, 0, 0);
        check(afterWrite == before && atomic_load(&sNandChanges) == before + 1,
              "NAND kept: the page gets it when the written file is closed, not at each write");
        fs(NEBULA_FS_CREATE, "/tmp/staging.bin", NULL, 0, NULL, 0, 0, 0, 0);
        uint8_t* blob;
        const size_t size = smg_console_nand_export(&blob);
        int hasTmp = 0;
        for (size_t i = 0; i + 4 < size; i++) {
            hasTmp |= memcmp(blob + i, "/tmp", 4) == 0;
        }
        check(blob != NULL && memcmp(blob, "SMGN", 4) == 0 && !hasTmp, "NAND kept: a blob without /tmp");
        fs(NEBULA_FS_DELETE, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 0);
        const int restored = smg_console_nand_import(blob, size);
        const int32_t g = fs(NEBULA_FS_OPEN, DATA "/GameData.bin", NULL, 0, NULL, 0, 0, 0, 1);
        memset(buffer, 0, sizeof(buffer));
        fs(NEBULA_FS_READ, NULL, NULL, g, buffer, 6, 0, 0, 0);
        fs(NEBULA_FS_CLOSE, NULL, NULL, g, NULL, 0, 0, 0, 0);
        printf("     %d folders and files in the blob (%zu bytes)\n", restored, size);
        check(restored >= 5 && memcmp(buffer, "Luigi!", 6) == 0, "NAND kept: the blob brings back a deleted save");
        check(smg_console_nand_import((const uint8_t*)"SMGX", 4) == -1 && smg_console_nand_import(blob, size - 3) == -1,
              "NAND kept: a malformed or cut blob is refused");
        free(blob);
    }

    /* Wii Remote: connected with its Nunchuk at the game's first read of its remotes after starting WPAD (not when
       WPAD starts: its callbacks are not set yet), then a report every 10 ms */
    sleep_ms(50);
    smg_console_wpad_sample(NULL, 0);
    sleep_ms(50);
    check(atomic_load(&sConnects) == 0, "Wii Remote: nothing before the game starts WPAD");
    smg_console_wpad_control(NULL, 0, NEBULA_WPAD_INIT, 0);
    smg_console_wpad_control(NULL, 0, NEBULA_WPAD_FORMAT, 5);
    sleep_ms(100);
    check(atomic_load(&sConnects) == 0 && atomic_load(&sReports) == 0,
          "Wii Remote: not connected yet when WPAD starts (the game has not set its callbacks)");
    smg_console_wpad_sample(NULL, 0);
    sleep_ms(500);
    const int reports = atomic_load(&sReports);
    check(atomic_load(&sConnects) == 1 && atomic_load(&sExtension) == 1, "Wii Remote: connected once, with a Nunchuk");
    printf("     %d reports in 500 ms\n", reports);
    check(reports >= 35 && reports <= 55, "Wii Remote: about 100 reports a second");
    check(atomic_load(&sLastId) == 0x37 && atomic_load(&sLastSize) == 22,
          "Wii Remote: the report the format asks for (0x37, 22 bytes)");

    /* The reports from the page's controls */
    {
        int32_t in[SMG_INPUT_WORDS] = {0};
        uint8_t r[22];
        NebulaWpadPointer p;
        int exact;
        /* At rest: the remote's report as before, no pointer */
        size_t n = smg_console_report(0x37, in, r, &p, &exact);
        static const uint8_t rest[22] = {0x37, 0, 0, 0x80, 0x80, 0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7b, 0x81, 0x7d, 0x7e, 0xae, 0x03};
        check(n == 22 && memcmp(r, rest, 22) == 0 && !exact, "report 0x37 at rest: no buttons, no IR, Nunchuk centred");
        /* A and B, Plus and Up; C held; the stick full right and full down */
        in[SMG_INPUT_BUTTONS] = 0x0800 | 0x0400 | 0x0010 | 0x0008 | 0x4000;
        in[SMG_INPUT_STICK_X] = 100;
        in[SMG_INPUT_STICK_Y] = -100;
        smg_console_report(0x37, in, r, &p, &exact);
        check(r[1] == 0x18 && r[2] == 0x0C, "buttons: Plus and Up in byte 1, A and B in byte 2");
        check(r[16] == 0xe0 && r[17] == 0x1d, "stick: its calibrated maximum on X and minimum on Y");
        check(r[21] == 0x01, "C held (bit 1 low), Z up (bit 0 high)");
        /* The pointer in the middle: dots at x 430 and 590, y 487 (KPAD's neutral) */
        in[SMG_INPUT_POINTER] = 1;
        smg_console_report(0x37, in, r, &p, &exact);
        check(exact && p.x == 0.0f && p.y == 0.0f, "pointer in the middle: passed to the game as exact (0, 0)");
        check(r[6] == (430 & 0xFF) && r[7] == (487 & 0xFF) && r[9] == (590 & 0xFF) && r[10] == (487 & 0xFF) &&
                  r[8] == ((1 << 6) | (1 << 4) | (1 << 2) | 2),
              "pointer in the middle: IR dots (430, 487) and (590, 487)");
        /* Top left corner: x 510 + 226 = 736, y 487 - 226 = 261 */
        in[SMG_INPUT_POINTER_X] = -10000;
        in[SMG_INPUT_POINTER_Y] = -10000;
        smg_console_report(0x33, in, r, &p, &exact);
        check(exact && p.x == -1.0f && p.y == -1.0f && r[6] == ((736 - 80) & 0xFF) && r[7] == (261 & 0xFF) &&
                  r[8] == ((1 << 6) | (2 << 4) | 8) && r[9] == ((736 + 80) & 0xFF) && r[12] == 0xFF,
              "report 0x33, pointer at the top left: extended IR dots (656, 261) and (816, 261)");
        in[SMG_INPUT_POINTER] = 0;
        n = smg_console_report(0x31, in, r, &p, &exact);
        check(n == 6 && !exact && r[2] == 0x0C, "report 0x31: buttons and accelerometer only");
    }

    /* Audio interface: 640-byte blocks at 32 kHz (5 ms each) */
    smg_console_ai_control(NULL, NEBULA_AI_RATE, 32000);
    smg_console_ai_dma(NULL, 0x10000000, 640);
    sleep_ms(50);
    check(atomic_load(&sInterrupts) == 0, "audio: no interrupt before the AI starts");
    smg_console_ai_control(NULL, NEBULA_AI_START, 0);
    sleep_ms(500);
    const int interrupts = atomic_load(&sInterrupts);
    printf("     %d interrupts in 500 ms\n", interrupts);
    check(interrupts >= 80 && interrupts <= 115, "audio: an interrupt every 5 ms (a block's length)");
    smg_console_ai_control(NULL, NEBULA_AI_STOP, 0);
    sleep_ms(20);
    const int stopped = atomic_load(&sInterrupts);
    sleep_ms(100);
    check(atomic_load(&sInterrupts) == stopped, "audio: none once stopped");

    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
