/* Neutral answers for every host service (nebula_game_api.h) of a web host: no picture, no sound, no input, the
   console's defaults. A host fills a table with them, then sets the services it implements. */
#pragma once

#include <nebula_game_api.h>

#ifdef __cplusplus
extern "C" {
#endif

void smg_host_default_services(NebulaHostServices* services);

#ifdef __cplusplus
}
#endif
