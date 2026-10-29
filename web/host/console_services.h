/* The console's devices a host gives the game without a renderer, as Nebula's host without a renderer gives them
   (third_party/nebula/native/host/host_main.cpp): a Wii Remote with a Nunchuk at rest, the audio interface's clock and
   a NAND. The NAND is kept in memory: what the game saves lasts until the page is closed. */
#pragma once

#include <stdint.h>

#include <nebula_game_api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Before the game runs: the NAND's folders (/tmp, and the save folder of the disc's game, from its 6-character ID)
   and the audio interface's thread. */
void smg_console_start(const char* game_id);

/* The host services (nebula_game_api.h) */
void smg_console_fs_request(void* user, const NebulaFsRequest* request, uint32_t slot);
void smg_console_wpad_control(void* user, uint32_t chan, uint32_t what, uint32_t value);
void smg_console_wpad_sample(void* user, uint32_t chan);
void smg_console_ai_dma(void* user, uint32_t start, uint32_t size);
void smg_console_ai_control(void* user, uint32_t what, uint32_t value);

/* What the devices did, for the host's report */
typedef struct {
    uint64_t wpad_reports;
    uint64_t ai_blocks;
    uint64_t nand_requests;
    uint64_t nand_failures; /* results other than NAND_RESULT_OK or a count (a missing file is one) */
} SmgConsoleCounters;
void smg_console_counters(SmgConsoleCounters* out);

#ifdef __cplusplus
}
#endif
