#include "state.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/config-file.h>
#include <util/platform.h>

#include "crypto/crypto.h"
#include "util/uuid.h"

#define PERSIST_FILE "achievements-tracker-state.json"

#define USER_ACCESS_TOKEN "user_access_token"
#define USER_ACCESS_TOKEN_EXPIRY "user_access_token_expiry"
#define USER_REFRESH_TOKEN "user_refresh_token"

#define DEVICE_UUID "device_uuid"
#define DEVICE_SERIAL_NUMBER "device_serial_number"
#define DEVICE_KEYS "device_keys"
#define DEVICE_TOKEN "device_token"
#define DEVICE_CODE "device_code"

#define SISU_TOKEN "sisu_token"

#define XBOX_IDENTITY_GTG "xbox_gamertag"
#define XBOX_IDENTITY_ID "xbox_id"
#define XBOX_IDENTITY_UHS "xbox_uhs"
#define XBOX_TOKEN "xbox_token"
#define XBOX_TOKEN_EXPIRY "xbox_token_expiry"

#define GAMERSCORE_CONFIGURATION_TOP_COLOR "source_gamerscore_top_color"
#define GAMERSCORE_CONFIGURATION_BOTTOM_COLOR "source_gamerscore_bottom_color"
#define GAMERSCORE_CONFIGURATION_SIZE "source_gamerscore_size"
#define GAMERSCORE_CONFIGURATION_FONT_FACE "source_gamerscore_font_face"
#define GAMERSCORE_CONFIGURATION_FONT_STYLE "source_gamerscore_font_style"

#define GAMERTAG_CONFIGURATION_TOP_COLOR "source_gamertag_top_color"
#define GAMERTAG_CONFIGURATION_BOTTOM_COLOR "source_gamertag_bottom_color"
#define GAMERTAG_CONFIGURATION_SIZE "source_gamertag_size"
#define GAMERTAG_CONFIGURATION_FONT_FACE "source_gamertag_font_face"
#define GAMERTAG_CONFIGURATION_FONT_STYLE "source_gamertag_font_style"

#define ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_TOP_COLOR "source_achievement_name_active_top_color"
#define ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_BOTTOM_COLOR "source_achievement_name_active_bottom_color"
#define ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_TOP_COLOR "source_achievement_name_inactive_top_color"
#define ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_BOTTOM_COLOR "source_achievement_name_inactive_bottom_color"
#define ACHIEVEMENT_NAME_CONFIGURATION_SIZE "source_achievement_name_size"
#define ACHIEVEMENT_NAME_CONFIGURATION_FONT_FACE "source_achievement_name_font_face"
#define ACHIEVEMENT_NAME_CONFIGURATION_FONT_STYLE "source_achievement_name_font_style"

#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_TOP_COLOR "source_achievement_description_active_top_color"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_BOTTOM_COLOR "source_achievement_description_active_bottom_color"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_TOP_COLOR "source_achievement_description_inactive_top_color"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_BOTTOM_COLOR "source_achievement_description_inactive_bottom_color"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_SIZE "source_achievement_description_size"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_FACE "source_achievement_description_font_face"
#define ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_STYLE "source_achievement_description_font_style"

#define ACHIEVEMENTS_COUNT_CONFIGURATION_TOP_COLOR "source_achievements_count_top_color"
#define ACHIEVEMENTS_COUNT_CONFIGURATION_BOTTOM_COLOR "source_achievements_count_bottom_color"
#define ACHIEVEMENTS_COUNT_CONFIGURATION_SIZE "source_achievements_count_size"
#define ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_FACE "source_achievements_count_font_face"
#define ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_STYLE "source_achievements_count_font_style"

/**
 * @brief Global in-memory persisted state.
 *
 * The state is backed by an OBS obs_data_t object loaded from and saved to
 * JSON on disk. Most getters return pointers to strings owned by this object.
 *
 * @note This must be initialized by calling io_load() before any state_* APIs
 *       are used.
 */
static obs_data_t *g_state = NULL;

/**
 * @brief Build the full path to the persisted JSON state file.
 *
 * The state is stored under OBS's module config directory:
 *   <OBS config dir>/plugins/<plugin_name>/achievements-tracker-state.json
 *
 * The directory is created if it doesn't exist.
 *
 * @return Newly allocated path string (caller must bfree()), or NULL on failure.
 */
static char *get_state_path(void) {
    /* Put state under: <OBS config dir>/plugins/<plugin_name>/ */
    char *dir = obs_module_config_path("");

    if (!dir) {
        return NULL;
    }

    /* Ensure directory exists */
    os_mkdirs(dir);

    char *path = (char *)bzalloc(1024);
    snprintf(path, 1024, "%s/%s", dir, PERSIST_FILE);
    bfree(dir);

    return path;
}

/**
 * @brief Read the persisted state from disk.
 *
 * If the state file doesn't exist or cannot be parsed, this returns a new empty
 * object.
 *
 * @return Newly created obs_data_t object (caller owns it), or NULL on allocation
 *         failure.
 */
static obs_data_t *load_state(void) {
    char *path = get_state_path();

    if (!path) {
        return NULL;
    }

    obs_log(LOG_INFO, "Loading state from %s", path);

    obs_data_t *data = obs_data_create_from_json_file(path);
    bfree(path);

    /* If the file does not exist yet, return an empty object */
    if (!data) {
        obs_log(LOG_INFO, "No state found: creating a new one");
        data = obs_data_create();
    }

    return data;
}

/**
 * @brief Persist the current state to disk.
 *
 * Uses obs_data_save_json_safe() to write the JSON file with a temporary file and
 * backup.
 *
 * @param data State object to save. No-op if NULL.
 */
static void save_state(obs_data_t *data) {

    if (!data) {
        return;
    }

    char *path = get_state_path();

    if (!path) {
        return;
    }

    obs_data_save_json_safe(data, path, ".tmp", ".bak");
    bfree(path);
}

void io_load(void) {
    g_state = load_state();

    /* Read values */
    const char *token     = obs_data_get_string(g_state, "oauth_token");
    int64_t     last_sync = obs_data_get_int(g_state, "last_sync_unix");

    (void)token;
    (void)last_sync;
}

void io_cleanup(void) {
    if (g_state) {
        obs_data_release(g_state);
        g_state = NULL;
    }
}

void state_clear(void) {
    /* Considering how sensitive the Xbox live API appears, let's always keep the UUID / Serial / Keys constant */
    /*obs_data_set_string(g_state, DEVICE_UUID, "");*/
    /*obs_data_set_string(g_state, DEVICE_SERIAL_NUMBER, "");*/
    /*obs_data_set_string(g_state, DEVICE_KEYS, "");*/

    obs_data_set_string(g_state, DEVICE_CODE, "");
    obs_data_set_string(g_state, USER_ACCESS_TOKEN, "");
    obs_data_set_int(g_state, USER_ACCESS_TOKEN_EXPIRY, 0);
    obs_data_set_string(g_state, USER_REFRESH_TOKEN, "");
    obs_data_set_string(g_state, XBOX_TOKEN_EXPIRY, "");
    obs_data_set_string(g_state, DEVICE_TOKEN, "");
    obs_data_set_string(g_state, XBOX_IDENTITY_GTG, "");
    obs_data_set_string(g_state, XBOX_IDENTITY_UHS, "");
    obs_data_set_string(g_state, XBOX_IDENTITY_ID, "");
    obs_data_set_string(g_state, XBOX_TOKEN, "");
    obs_data_set_string(g_state, XBOX_TOKEN_EXPIRY, "");
    save_state(g_state);
}

/**
 * @brief Generate and persist a new device UUID.
 *
 * @return Pointer to the stored UUID string owned by the internal state object.
 */
static const char *create_device_uuid() {
    /* Generate a random device UUID */
    char new_device_uuid[37];
    uuid_get_random(new_device_uuid);

    obs_data_set_string(g_state, DEVICE_UUID, new_device_uuid);
    save_state(g_state);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_UUID);
}

/**
 * @brief Generate and persist a new device serial number.
 *
 * @return Pointer to the stored serial number string owned by the internal state object.
 */
static const char *create_device_serial_number() {
    /* Generate a random device UUID */
    char new_device_serial_number[37];
    uuid_get_random(new_device_serial_number);

    obs_data_set_string(g_state, DEVICE_SERIAL_NUMBER, new_device_serial_number);
    save_state(g_state);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_SERIAL_NUMBER);
}

/**
 * @brief Generate and persist a new EC keypair for the emulated device.
 *
 * Generates a P-256 keypair, serializes it to JSON (including the private part)
 * and stores it in the state.
 *
 * @return Pointer to the stored serialized key string owned by the internal state object.
 */
static const char *create_device_keys() {
    /* Generate a random key pair */
    EVP_PKEY *device_key = crypto_generate_keys();

    char *serialized_keys = crypto_to_string(device_key, true);

    obs_data_set_string(g_state, DEVICE_KEYS, serialized_keys);
    save_state(g_state);

    bfree(serialized_keys);
    EVP_PKEY_free(device_key);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_KEYS);
}

device_t *state_get_device(void) {

    /* Retrieves the device UUID & serial number */
    const char *device_uuid          = obs_data_get_string(g_state, DEVICE_UUID);
    const char *device_serial_number = obs_data_get_string(g_state, DEVICE_SERIAL_NUMBER);

    /* Retrieves the device's public & private keys */
    const char *device_keys = obs_data_get_string(g_state, DEVICE_KEYS);

    if (!device_uuid || strlen(device_uuid) == 0) {
        obs_log(LOG_INFO, "No device UUID found. Creating new one");
        device_uuid          = create_device_uuid();
        device_serial_number = create_device_serial_number();

        /* Forces the keys to be recreated if the device UUID is new */
        device_keys = NULL;
    }

    if (!device_keys || strlen(device_keys) == 0) {
        obs_log(LOG_INFO, "No device keys found. Creating new one pair");
        device_keys = create_device_keys();
    }

    /* Retrieves the keys from the serialized string */
    EVP_PKEY *device_evp_pkeys = crypto_from_string(device_keys, true);

    if (!device_evp_pkeys) {
        obs_log(LOG_ERROR, "Could not load device keys from state");
        return NULL;
    }

    device_t *device      = bzalloc(sizeof(device_t));
    device->uuid          = bstrdup(device_uuid);
    device->serial_number = bstrdup(device_serial_number);
    device->keys          = device_evp_pkeys;

    return device;
}

void state_set_device_token(const token_t *device_token) {
    obs_data_set_string(g_state, DEVICE_TOKEN, device_token->value);
    save_state(g_state);
}

token_t *state_get_device_token(void) {

    const char *device_token = obs_data_get_string(g_state, DEVICE_TOKEN);

    if (!device_token || strlen(device_token) == 0) {
        obs_log(LOG_INFO, "No device token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup(device_token);

    return token;
}

void state_set_sisu_token(const token_t *sisu_token) {
    obs_data_set_string(g_state, SISU_TOKEN, sisu_token->value);
    save_state(g_state);
}

token_t *state_get_sisu_token(void) {

    const char *sisu_token = obs_data_get_string(g_state, SISU_TOKEN);

    if (!sisu_token || strlen(sisu_token) == 0) {
        obs_log(LOG_INFO, "No sisu token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup(sisu_token);

    return token;
}

void state_set_user_token(const char *device_code, const token_t *user_token, const token_t *refresh_token) {

    if (!device_code || !user_token || !refresh_token) {
        return;
    }

    obs_data_set_string(g_state, DEVICE_CODE, device_code);
    obs_data_set_string(g_state, USER_ACCESS_TOKEN, user_token->value);
    obs_data_set_int(g_state, USER_ACCESS_TOKEN_EXPIRY, user_token->expires);
    obs_data_set_string(g_state, USER_REFRESH_TOKEN, refresh_token->value);
    save_state(g_state);
}

char *state_get_device_code(void) {

    const char *device_code = obs_data_get_string(g_state, DEVICE_CODE);

    if (!device_code || strlen(device_code) == 0) {
        obs_log(LOG_INFO, "No device code found in the cache");
        return NULL;
    }

    return bstrdup(device_code);
}

void state_set_gamerscore_configuration(const gamerscore_configuration_t *gamerscore_configuration) {

    if (!gamerscore_configuration) {
        return;
    }

    obs_data_set_int(g_state, GAMERSCORE_CONFIGURATION_TOP_COLOR, gamerscore_configuration->top_color);
    obs_data_set_int(g_state, GAMERSCORE_CONFIGURATION_BOTTOM_COLOR, gamerscore_configuration->bottom_color);
    obs_data_set_int(g_state, GAMERSCORE_CONFIGURATION_SIZE, gamerscore_configuration->font_size);
    obs_data_set_string(g_state, GAMERSCORE_CONFIGURATION_FONT_FACE, gamerscore_configuration->font_face);
    obs_data_set_string(g_state, GAMERSCORE_CONFIGURATION_FONT_STYLE, gamerscore_configuration->font_style);

    save_state(g_state);
}

gamerscore_configuration_t *state_get_gamerscore_configuration() {

    uint32_t    top_color    = (uint32_t)obs_data_get_int(g_state, GAMERSCORE_CONFIGURATION_TOP_COLOR);
    uint32_t    bottom_color = (uint32_t)obs_data_get_int(g_state, GAMERSCORE_CONFIGURATION_BOTTOM_COLOR);
    uint32_t    size         = (uint32_t)obs_data_get_int(g_state, GAMERSCORE_CONFIGURATION_SIZE);
    const char *font_face    = obs_data_get_string(g_state, GAMERSCORE_CONFIGURATION_FONT_FACE);
    const char *font_style   = obs_data_get_string(g_state, GAMERSCORE_CONFIGURATION_FONT_STYLE);

    gamerscore_configuration_t *gamerscore_configuration = bzalloc(sizeof(gamerscore_configuration_t));

    gamerscore_configuration->top_color    = top_color == 0 ? 0xFFFFFFFF : top_color;
    gamerscore_configuration->bottom_color = bottom_color == 0 ? 0xFFFFFFFF : bottom_color;
    gamerscore_configuration->font_size    = size == 0 ? 48 : size;
    gamerscore_configuration->font_face    = bstrdup(font_face);
    gamerscore_configuration->font_style   = bstrdup(font_style);

    return gamerscore_configuration;
}

void state_set_gamertag_configuration(const gamertag_configuration_t *configuration) {

    if (!configuration) {
        return;
    }

    obs_data_set_int(g_state, GAMERTAG_CONFIGURATION_TOP_COLOR, configuration->top_color);
    obs_data_set_int(g_state, GAMERTAG_CONFIGURATION_BOTTOM_COLOR, configuration->bottom_color);
    obs_data_set_int(g_state, GAMERTAG_CONFIGURATION_SIZE, configuration->font_size);
    obs_data_set_string(g_state, GAMERTAG_CONFIGURATION_FONT_FACE, configuration->font_face);
    obs_data_set_string(g_state, GAMERTAG_CONFIGURATION_FONT_STYLE, configuration->font_style);

    save_state(g_state);
}

gamertag_configuration_t *state_get_gamertag_configuration() {

    uint32_t    top_color    = (uint32_t)obs_data_get_int(g_state, GAMERTAG_CONFIGURATION_TOP_COLOR);
    uint32_t    bottom_color = (uint32_t)obs_data_get_int(g_state, GAMERTAG_CONFIGURATION_BOTTOM_COLOR);
    uint32_t    size         = (uint32_t)obs_data_get_int(g_state, GAMERTAG_CONFIGURATION_SIZE);
    const char *font_face    = obs_data_get_string(g_state, GAMERTAG_CONFIGURATION_FONT_FACE);
    const char *font_style   = obs_data_get_string(g_state, GAMERTAG_CONFIGURATION_FONT_STYLE);

    gamertag_configuration_t *configuration = bzalloc(sizeof(gamertag_configuration_t));

    configuration->top_color    = top_color == 0 ? 0xFFFFFFFF : top_color;
    configuration->bottom_color = bottom_color == 0 ? 0xFFFFFFFF : bottom_color;
    configuration->font_size    = size == 0 ? 48 : size;
    configuration->font_face    = bstrdup(font_face);
    configuration->font_style   = bstrdup(font_style);

    return configuration;
}

void state_set_achievement_name_configuration(const achievement_name_configuration_t *configuration) {

    if (!configuration) {
        return;
    }

    obs_data_set_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_TOP_COLOR, configuration->active_top_color);
    obs_data_set_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_BOTTOM_COLOR, configuration->active_bottom_color);
    obs_data_set_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_TOP_COLOR, configuration->inactive_top_color);
    obs_data_set_int(g_state,
                     ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_BOTTOM_COLOR,
                     configuration->inactive_bottom_color);
    obs_data_set_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_SIZE, configuration->font_size);
    obs_data_set_string(g_state, ACHIEVEMENT_NAME_CONFIGURATION_FONT_FACE, configuration->font_face);
    obs_data_set_string(g_state, ACHIEVEMENT_NAME_CONFIGURATION_FONT_STYLE, configuration->font_style);

    save_state(g_state);
}

achievement_name_configuration_t *state_get_achievement_name_configuration() {

    uint32_t active_top_color = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_TOP_COLOR);
    uint32_t active_bottom_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_ACTIVE_BOTTOM_COLOR);
    uint32_t inactive_top_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_TOP_COLOR);
    uint32_t inactive_bottom_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_INACTIVE_BOTTOM_COLOR);
    uint32_t    size       = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_NAME_CONFIGURATION_SIZE);
    const char *font_face  = obs_data_get_string(g_state, ACHIEVEMENT_NAME_CONFIGURATION_FONT_FACE);
    const char *font_style = obs_data_get_string(g_state, ACHIEVEMENT_NAME_CONFIGURATION_FONT_STYLE);

    achievement_name_configuration_t *configuration = bzalloc(sizeof(achievement_name_configuration_t));

    configuration->active_top_color      = active_top_color == 0 ? 0xFFFFFFFF : active_top_color;
    configuration->active_bottom_color   = active_bottom_color == 0 ? 0xFFFFFFFF : active_bottom_color;
    configuration->inactive_top_color    = inactive_top_color == 0 ? 0x7F7F7FFF : inactive_top_color;
    configuration->inactive_bottom_color = inactive_bottom_color == 0 ? 0x7F7F7FFF : inactive_bottom_color;
    configuration->font_size             = size == 0 ? 12 : size;
    configuration->font_face             = bstrdup(font_face);
    configuration->font_style            = bstrdup(font_style);

    return configuration;
}

void state_set_achievement_description_configuration(const achievement_description_configuration_t *configuration) {

    if (!configuration) {
        return;
    }

    obs_data_set_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_TOP_COLOR, configuration->active_top_color);
    obs_data_set_int(g_state,
                     ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_BOTTOM_COLOR,
                     configuration->active_bottom_color);
    obs_data_set_int(g_state,
                     ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_TOP_COLOR,
                     configuration->inactive_top_color);
    obs_data_set_int(g_state,
                     ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_BOTTOM_COLOR,
                     configuration->inactive_bottom_color);
    obs_data_set_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_SIZE, configuration->font_size);
    obs_data_set_string(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_FACE, configuration->font_face);
    obs_data_set_string(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_STYLE, configuration->font_style);

    save_state(g_state);
}

achievement_description_configuration_t *state_get_achievement_description_configuration() {

    uint32_t active_top_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_TOP_COLOR);
    uint32_t active_bottom_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_ACTIVE_BOTTOM_COLOR);
    uint32_t inactive_top_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_TOP_COLOR);
    uint32_t inactive_bottom_color =
        (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_INACTIVE_BOTTOM_COLOR);
    uint32_t    size       = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_SIZE);
    const char *font_face  = obs_data_get_string(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_FACE);
    const char *font_style = obs_data_get_string(g_state, ACHIEVEMENT_DESCRIPTION_CONFIGURATION_FONT_STYLE);

    achievement_description_configuration_t *configuration = bzalloc(sizeof(achievement_description_configuration_t));

    configuration->active_top_color      = active_top_color == 0 ? 0xFFFFFFFF : active_top_color;
    configuration->active_bottom_color   = active_bottom_color == 0 ? 0xFFFFFFFF : active_bottom_color;
    configuration->inactive_top_color    = inactive_top_color == 0 ? 0x7F7F7FFF : inactive_top_color;
    configuration->inactive_bottom_color = inactive_bottom_color == 0 ? 0x7F7F7FFF : inactive_bottom_color;
    configuration->font_size             = size == 0 ? 12 : size;
    configuration->font_face             = bstrdup(font_face);
    configuration->font_style            = bstrdup(font_style);

    return configuration;
}

void state_set_achievements_count_configuration(const achievements_count_configuration_t *configuration) {

    if (!configuration) {
        return;
    }

    obs_data_set_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_TOP_COLOR, configuration->top_color);
    obs_data_set_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_BOTTOM_COLOR, configuration->bottom_color);
    obs_data_set_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_SIZE, configuration->font_size);
    obs_data_set_string(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_FACE, configuration->font_face);
    obs_data_set_string(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_STYLE, configuration->font_style);

    save_state(g_state);
}

achievements_count_configuration_t *state_get_achievements_count_configuration() {

    uint32_t    top_color    = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_TOP_COLOR);
    uint32_t    bottom_color = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_BOTTOM_COLOR);
    uint32_t    size         = (uint32_t)obs_data_get_int(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_SIZE);
    const char *font_face    = obs_data_get_string(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_FACE);
    const char *font_style   = obs_data_get_string(g_state, ACHIEVEMENTS_COUNT_CONFIGURATION_FONT_STYLE);

    achievements_count_configuration_t *configuration = bzalloc(sizeof(achievements_count_configuration_t));

    configuration->top_color    = top_color == 0 ? 0xFFFFFFFF : top_color;
    configuration->bottom_color = bottom_color == 0 ? 0xFFFFFFFF : bottom_color;
    configuration->font_size    = size == 0 ? 48 : size;
    configuration->font_face    = bstrdup(font_face);
    configuration->font_style   = bstrdup(font_style);

    return configuration;
}

token_t *state_get_user_token(void) {

    const char *user_token = obs_data_get_string(g_state, USER_ACCESS_TOKEN);

    if (!user_token || strlen(user_token) == 0) {
        obs_log(LOG_INFO, "No user token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup(user_token);

    return token;
}

token_t *state_get_user_refresh_token(void) {
    const char *refresh_token = obs_data_get_string(g_state, USER_REFRESH_TOKEN);

    if (!refresh_token || strlen(refresh_token) == 0) {
        obs_log(LOG_INFO, "No refresh token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup(refresh_token);

    return token;
}

void state_set_xbox_identity(const xbox_identity_t *xbox_identity) {
    obs_data_set_string(g_state, XBOX_IDENTITY_GTG, xbox_identity->gamertag);
    obs_data_set_string(g_state, XBOX_IDENTITY_ID, xbox_identity->xid);
    obs_data_set_string(g_state, XBOX_IDENTITY_UHS, xbox_identity->uhs);
    obs_data_set_string(g_state, XBOX_TOKEN, xbox_identity->token->value);
    obs_data_set_int(g_state, XBOX_TOKEN_EXPIRY, xbox_identity->token->expires);
    save_state(g_state);
}

xbox_identity_t *state_get_xbox_identity(void) {

    const char *gtg = obs_data_get_string(g_state, XBOX_IDENTITY_GTG);

    if (!gtg || strlen(gtg) == 0) {
        obs_log(LOG_INFO, "No gamertag found in the cache");
        return NULL;
    }

    const char *xid = obs_data_get_string(g_state, XBOX_IDENTITY_ID);

    if (!xid || strlen(xid) == 0) {
        obs_log(LOG_INFO, "No user ID found in the cache");
        return NULL;
    }

    const char *uhs = obs_data_get_string(g_state, XBOX_IDENTITY_UHS);

    if (!uhs || strlen(uhs) == 0) {
        obs_log(LOG_INFO, "No user hash found in the cache");
        return NULL;
    }

    const char *xbox_token = obs_data_get_string(g_state, XBOX_TOKEN);

    if (!xbox_token || strlen(xbox_token) == 0) {
        obs_log(LOG_INFO, "No xbox token found in the cache");
        return NULL;
    }

    int64_t xbox_token_expiry = (int64_t)obs_data_get_int(g_state, XBOX_TOKEN_EXPIRY);

    if (xbox_token_expiry == 0) {
        obs_log(LOG_INFO, "No xbox token expiry found in the cache");
        return NULL;
    }

    obs_log(LOG_DEBUG, "Xbox identity found in the cache: %s (%s)", gtg, xid);

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup(xbox_token);
    token->expires = xbox_token_expiry;

    xbox_identity_t *identity = bzalloc(sizeof(xbox_identity_t));
    identity->gamertag        = bstrdup(gtg);
    identity->xid             = bstrdup(xid);
    identity->uhs             = bstrdup(uhs);
    identity->token           = token;

    return identity;
}

void state_free_device(device_t **device) {
    if (!device || !*device) {
        return;
    }

    if ((*device)->keys) {
        EVP_PKEY_free((EVP_PKEY *)(*device)->keys);
    }

    bfree(*device);
    *device = NULL;
}

void state_free_token(token_t **token) {
    if (!token || !*token) {
        return;
    }

    if ((*token)->value) {
        bfree((void *)(*token)->value);
    }

    bfree(*token);
    *token = NULL;
}

void state_free_xbox_identity(xbox_identity_t **identity) {
    if (!identity || !*identity) {
        return;
    }

    if ((*identity)->gamertag) {
        bfree((void *)(*identity)->gamertag);
    }

    if ((*identity)->xid) {
        bfree((void *)(*identity)->xid);
    }

    if ((*identity)->uhs) {
        bfree((void *)(*identity)->uhs);
    }

    if ((*identity)->token) {
        state_free_token(&(*identity)->token);
    }

    bfree(*identity);
    *identity = NULL;
}

void state_free_gamerscore_configuration(gamerscore_configuration_t **config) {
    if (!config || !*config) {
        return;
    }

    if ((*config)->font_face) {
        bfree((void *)(*config)->font_face);
    }

    if ((*config)->font_style) {
        bfree((void *)(*config)->font_style);
    }

    bfree(*config);
    *config = NULL;
}

void state_free_gamertag_configuration(gamertag_configuration_t **config) {
    if (!config || !*config) {
        return;
    }

    if ((*config)->font_face) {
        bfree((void *)(*config)->font_face);
    }

    if ((*config)->font_style) {
        bfree((void *)(*config)->font_style);
    }

    bfree(*config);
    *config = NULL;
}

void state_free_achievement_name_configuration(achievement_name_configuration_t **config) {
    if (!config || !*config) {
        return;
    }

    if ((*config)->font_face) {
        bfree((void *)(*config)->font_face);
    }

    if ((*config)->font_style) {
        bfree((void *)(*config)->font_style);
    }

    bfree(*config);
    *config = NULL;
}

void state_free_achievement_description_configuration(achievement_description_configuration_t **config) {
    if (!config || !*config) {
        return;
    }

    if ((*config)->font_face) {
        bfree((void *)(*config)->font_face);
    }

    if ((*config)->font_style) {
        bfree((void *)(*config)->font_style);
    }

    bfree(*config);
    *config = NULL;
}

void state_free_achievements_count_configuration(achievements_count_configuration_t **config) {
    if (!config || !*config) {
        return;
    }

    if ((*config)->font_face) {
        bfree((void *)(*config)->font_face);
    }

    if ((*config)->font_style) {
        bfree((void *)(*config)->font_style);
    }

    bfree(*config);
    *config = NULL;
}
