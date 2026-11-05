/* The disc paths the game asked for and the disc does not have, said when the game stops (linked with
   --wrap=DVDConvertPathToEntrynum, web/cmake/SmgGame.cmake).

   When a file the game needs is missing, FileRipper::loadToMainRAM stops it with "File isn't exist." and no name
   (the same on the console). MR::isFileExist asks DVDConvertPathToEntrynum first, from another object file, so the
   wrapper sees it. The game asks for many files that are not there on purpose (a language folder's version first,
   then the common one), so the misses are not logged as they happen: the last 32 are kept, and the host says them
   when the game stops (web/host/headless_host.c, svc_fatal, smg_dvd_missing_report). Calls inside dvdfs.c itself
   (DVDOpen) are not wrapped. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int32_t __real_DVDConvertPathToEntrynum(const char* path);

#define SMG_MISSING_KEPT 32
static char sMissing[SMG_MISSING_KEPT][96];
static unsigned sMissingNext; /* total misses; the ring's next slot is sMissingNext % SMG_MISSING_KEPT */
static pthread_mutex_t sMissingLock = PTHREAD_MUTEX_INITIALIZER;

int32_t __wrap_DVDConvertPathToEntrynum(const char* path) {
    const int32_t entry = __real_DVDConvertPathToEntrynum(path);
    if (entry < 0 && path != NULL) {
        pthread_mutex_lock(&sMissingLock);
        snprintf(sMissing[sMissingNext % SMG_MISSING_KEPT], sizeof(sMissing[0]), "%s", path);
        sMissingNext++;
        pthread_mutex_unlock(&sMissingLock);
    }
    return entry;
}

/* The last misses, oldest first, one line each */
void smg_dvd_missing_report(void) {
    pthread_mutex_lock(&sMissingLock);
    const unsigned count = sMissingNext < SMG_MISSING_KEPT ? sMissingNext : SMG_MISSING_KEPT;
    fprintf(stderr, "[host] disc paths not found, the last %u of %u (the game asks for some on purpose):\n", count,
            sMissingNext);
    for (unsigned i = sMissingNext - count; i < sMissingNext; i++) {
        fprintf(stderr, "[host]   %s\n", sMissing[i % SMG_MISSING_KEPT]);
    }
    pthread_mutex_unlock(&sMissingLock);
}
