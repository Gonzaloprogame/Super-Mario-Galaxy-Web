/* The console's devices of the web host (web/host/console_services.c), against the game module's side stubbed here:
   the NAND's operations and results, the Wii Remote's connection and its reports every 10 ms, the audio interface's
   interrupt at each block's end. Node.js, with pthreads (run_os_tests.sh). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "console_services.h"

static int failures;

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

    /* Wii Remote: connected with its Nunchuk when WPAD starts, then a report every 10 ms */
    sleep_ms(50);
    check(atomic_load(&sConnects) == 0, "Wii Remote: nothing before the game starts WPAD");
    smg_console_wpad_control(NULL, 0, NEBULA_WPAD_INIT, 0);
    smg_console_wpad_control(NULL, 0, NEBULA_WPAD_FORMAT, 5);
    sleep_ms(500);
    const int reports = atomic_load(&sReports);
    check(atomic_load(&sConnects) == 1 && atomic_load(&sExtension) == 1, "Wii Remote: connected once, with a Nunchuk");
    printf("     %d reports in 500 ms\n", reports);
    check(reports >= 35 && reports <= 55, "Wii Remote: about 100 reports a second");
    check(atomic_load(&sLastId) == 0x37 && atomic_load(&sLastSize) == 22,
          "Wii Remote: the report the format asks for (0x37, 22 bytes)");

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
