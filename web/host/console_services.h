/* The console's devices a host gives the game without a renderer, as Nebula's host without a renderer gives them
   (third_party/nebula/native/host/host_main.cpp): a Wii Remote with a Nunchuk at rest, the audio interface's clock and
   a NAND. The NAND is kept in memory: what the game saves lasts until the page is closed. */
#pragma once

#include <stddef.h>
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

/* The controls, as the page writes them in the engine's memory (web/shell/controls.js), 32-bit words:
   the Wii Remote's and Nunchuk's buttons (WPAD_BUTTON_*: C 0x4000 and Z 0x2000 the Nunchuk's), the Nunchuk's stick
   (-100..100, x right, y up), the pointer (on the screen or not; x and y in ten-thousandths, -10000..10000 across the
   picture, x right, y down) and the shakes held (bit 0 the remote's, bit 1 the Nunchuk's); a last word the host sets
   to SMG_INPUT_MAGIC, which the page's tests read through its view of these words. */
enum {
    SMG_INPUT_BUTTONS,
    SMG_INPUT_STICK_X,
    SMG_INPUT_STICK_Y,
    SMG_INPUT_POINTER,
    SMG_INPUT_POINTER_X,
    SMG_INPUT_POINTER_Y,
    SMG_INPUT_SHAKE,
    SMG_INPUT_MAGIC_WORD,
    SMG_INPUT_WORDS
};
#define SMG_INPUT_MAGIC 0x534D4749 /* "SMGI" */
int32_t* smg_console_input(void);

/* The input report `id` (0x30, 0x31, 0x33 or 0x37) for the controls `input`; its size. The pointer it stands for in
   *pointer, with *exact 1, when the pointer is on the screen and the report carries IR. */
size_t smg_console_report(uint8_t id, const int32_t* input, uint8_t* report, NebulaWpadPointer* pointer, int* exact);

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
