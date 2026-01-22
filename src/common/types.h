#pragma once

#include <stdbool.h>
#include <openssl/evp.h>
#include <stdint.h>

#include "common/memory.h"
#include "common/achievement.h"
#include "common/device.h"
#include "common/game.h"
#include "common/gamerscore.h"
#include "common/token.h"
#include "common/unlocked_achievement.h"
#include "common/xbox_identity.h"
#include "common/xbox_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FREE_JSON(p)	\
if (p)			        \
    cJSON_Delete(p);

#define COPY_OR_FREE(src, dst)	\
if (dst)					    \
    *dst = src;				    \
else						    \
    FREE(src);

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}

#define strcasecmp _stricmp
#else
#include <unistd.h>
static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}
#endif

typedef struct xbox_live_authenticate_result {
    const char *error_message;
} xbox_live_authenticate_result_t;

typedef struct gamerscore_configuration {
    const char *font_sheet_path;
    uint32_t    offset_x;
    uint32_t    offset_y;
    uint32_t    font_width;
    uint32_t    font_height;
} gamerscore_configuration_t;

typedef struct achievements_progress {
    const char                   *service_config_id;
    const char                   *id;
    const char                   *progress_state;
    struct achievements_progress *next;
} achievements_progress_t;

#ifdef __cplusplus
}
#endif
