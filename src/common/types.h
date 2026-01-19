#pragma once

#include <stdbool.h>
#include <openssl/evp.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREE(p)	\
if (p)			\
    bfree(p);

#define FREE_JSON(p)	\
if (p)			        \
    cJSON_Delete(p);

#define COPY_OR_FREE(src, dst)	\
if (dst)					    \
    *dst = src;				    \
else						    \
    FREE(src);

typedef struct device {
    /* unique identifier for the device */
    const char     *uuid;
    const char     *serial_number;
    /* proof of ownership key pair */
    const EVP_PKEY *keys;
} device_t;

typedef struct token {
    const char *value;
    /* unix timestamp */
    int64_t     expires;
} token_t;

typedef struct xbox_live_authenticate_result {
    const char *error_message;
} xbox_live_authenticate_result_t;

typedef struct xbox_identity {
    const char    *gamertag;
    const char    *xid;
    const char    *uhs;
    const token_t *token;
} xbox_identity_t;

typedef struct game {
    const char *id;
    const char *title;
} game_t;

typedef struct gamerscore_configuration {
    const char *font_sheet_path;
    uint32_t    offset_x;
    uint32_t    offset_y;
    uint32_t    font_width;
    uint32_t    font_height;
} gamerscore_configuration_t;

//[3,0,{"serviceConfigId":"00000000-0000-0000-0000-00007972ac43","progression":[{"id":"1","requirements":[{"id":"00000000-0000-0000-0000-000000000000","current":"100","target":"100","operationType":"Sum","valueType":"Integer","ruleParticipationType":"Individual"}],"progressState":"Achieved","timeUnlocked":"2026-01-18T02:48:21.707Z"}],"contractVersion":1}]
//[3,0,{"devicetype":"XboxOne","titleid":0,"string1":"Vu en dernier il y a 1 min : Application
// Xbox","string2":"","presenceState":"Offline","presenceText":"Vu en dernier il y a 1 min : Application
// Xbox","presenceDetails":[{"isBroadcasting":false,"device":"iOS","presenceText":"Vu en dernier il y a 1 min :
// Application
// Xbox","state":"LastSeen","titleId":"328178078","isGame":false,"isPrimary":true,"richPresenceText":""}],"xuid":2533274953419891}]
typedef struct media_asset {
    const char         *url;
    struct media_asset *next;
} media_asset_t;

typedef struct reward {
    const char    *value;
    struct reward *next;
} reward_t;

typedef struct achievement {
    const char          *id;
    const char          *service_config_id;
    const char          *name;
    const char          *progress_state;
    const media_asset_t *media_assets;
    bool                 is_secret;
    const char          *description;
    const char          *locked_description;
    const reward_t      *rewards;
    struct achievement  *next;
} achievement_t;

typedef struct achievements_progress {
    const char                   *service_config_id;
    const char                   *id;
    const char                   *progress_state;
    struct achievements_progress *next;
} achievements_progress_t;

bool is_token_expired(const token_t *token);

int count_achievements(const achievement_t *achievements);

#ifdef __cplusplus
}
#endif
