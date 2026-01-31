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

#define SISU_TOKEN "sisu_token"

#define XBOX_IDENTITY_GTG "xbox_gamertag"
#define XBOX_IDENTITY_ID "xbox_id"
#define XBOX_IDENTITY_UHS "xbox_uhs"
#define XBOX_TOKEN "xbox_token"
#define XBOX_TOKEN_EXPIRY "xbox_token_expiry"

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

    obs_log(LOG_INFO, "loading state from %s", path);

    obs_data_t *data = obs_data_create_from_json_file(path);
    bfree(path);

    /* If the file does not exist yet, return an empty object */
    if (!data) {
        obs_log(LOG_INFO, "no state found: creating a new one");
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

/**
 * @brief Initialize the state subsystem.
 *
 * Loads the persisted state from disk into the global in-memory cache.
 *
 * @note Call once during plugin startup before using state_get_* / state_set_*.
 */
void io_load(void) {
    g_state = load_state();

    /* Read values */
    const char *token     = obs_data_get_string(g_state, "oauth_token");
    int64_t     last_sync = obs_data_get_int(g_state, "last_sync_unix");

    (void)token;
    (void)last_sync;
}

/**
 * @brief Clear volatile/authentication state.
 *
 * This clears OAuth/Xbox tokens and identity fields from the persisted state and
 * saves the file. Device-identifying values (UUID/serial/keys) are intentionally
 * kept stable.
 */
void state_clear(void) {
    /* Considering how sensitive the Xbox live API appears, let's always keep the UUID / Serial / Keys constant */
    /*obs_data_set_string(g_state, DEVICE_UUID, "");*/
    /*obs_data_set_string(g_state, DEVICE_SERIAL_NUMBER, "");*/
    /*obs_data_set_string(g_state, DEVICE_KEYS, "");*/

    obs_data_set_string(g_state, USER_ACCESS_TOKEN, "");
    obs_data_set_int(g_state, USER_ACCESS_TOKEN_EXPIRY, 0);
    obs_data_set_string(g_state, USER_REFRESH_TOKEN, "");
    obs_data_set_string(g_state, XBOX_TOKEN_EXPIRY, "");
    obs_data_set_string(g_state, DEVICE_TOKEN, "");
    obs_data_set_string(g_state, XBOX_IDENTITY_GTG, "");
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

/**
 * @brief Get the current device identity/key material, creating it if missing.
 *
 * The device UUID/serial and keypair are stored persistently. If any are missing,
 * they are generated and saved.
 *
 * Ownership/lifetime:
 *  - The returned device_t is allocated with bzalloc() and must be freed by the
 *    caller.
 *  - device->uuid and device->serial_number point to strings owned by the
 *    internal obs_data_t state; they must not be freed and remain valid while
 *    g_state remains loaded.
 *  - device->keys is a newly created EVP_PKEY that must be freed by the caller
 *    with EVP_PKEY_free().
 *
 * @return Newly allocated device_t on success, or NULL on failure.
 */
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

    device_t *device      = (device_t *)bzalloc(sizeof(device_t));
    device->uuid          = device_uuid;
    device->serial_number = device_serial_number;
    device->keys          = device_evp_pkeys;

    return device;
}

/**
 * @brief Store the device token.
 *
 * @param device_token Token to store. The value is persisted.
 */
void state_set_device_token(const token_t *device_token) {
    obs_data_set_string(g_state, DEVICE_TOKEN, device_token->value);
    save_state(g_state);
}

/**
 * @brief Retrieve the device token from the state.
 *
 * Ownership/lifetime:
 *  - Returns a token_t allocated with bzalloc() that the caller must free.
 *  - token->value points to a string owned by the internal state object.
 *
 * @return Token wrapper, or NULL if missing/expired.
 */
token_t *state_get_device_token(void) {

    const char *device_token = obs_data_get_string(g_state, DEVICE_TOKEN);

    if (!device_token || strlen(device_token) == 0) {
        obs_log(LOG_INFO, "No device token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = device_token;

    return token;
}

/**
 * @brief Store the SISU token.
 *
 * @param sisu_token Token to store. The value is persisted.
 */
void state_set_sisu_token(const token_t *sisu_token) {
    obs_data_set_string(g_state, SISU_TOKEN, sisu_token->value);
    save_state(g_state);
}

/**
 * @brief Retrieve the SISU token from the state.
 *
 * @return Token wrapper allocated with bzalloc(), or NULL if missing/expired.
 */
token_t *state_get_sisu_token(void) {

    const char *sisu_token = obs_data_get_string(g_state, SISU_TOKEN);

    if (!sisu_token || strlen(sisu_token) == 0) {
        obs_log(LOG_INFO, "No sisu token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = sisu_token;

    return token;
}

/**
 * @brief Store the user access token and refresh token.
 *
 * @param user_token    Access token (value + expiry).
 * @param refresh_token Refresh token.
 */
void state_set_user_token(const token_t *user_token, const token_t *refresh_token) {
    obs_data_set_string(g_state, USER_ACCESS_TOKEN, user_token->value);
    obs_data_set_int(g_state, USER_ACCESS_TOKEN_EXPIRY, user_token->expires);
    obs_data_set_string(g_state, USER_REFRESH_TOKEN, refresh_token->value);
    save_state(g_state);
}

/**
 * @brief Retrieve the user access token from the state.
 *
 * @return Token wrapper allocated with bzalloc(), or NULL if missing/expired.
 */
token_t *state_get_user_token(void) {

    const char *user_token = obs_data_get_string(g_state, USER_ACCESS_TOKEN);

    if (!user_token || strlen(user_token) == 0) {
        obs_log(LOG_INFO, "No user token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = user_token;

    return token;
}

/**
 * @brief Retrieve the user refresh token from the state.
 *
 * @return Token wrapper allocated with bzalloc(), or NULL if missing.
 */
token_t *state_get_user_refresh_token(void) {
    const char *refresh_token = obs_data_get_string(g_state, USER_REFRESH_TOKEN);

    if (!refresh_token || strlen(refresh_token) == 0) {
        obs_log(LOG_INFO, "No refresh token found in the cache");
        return NULL;
    }

    token_t *token = bzalloc(sizeof(token_t));
    token->value   = refresh_token;

    return token;
}

/**
 * @brief Store the Xbox identity and associated Xbox token.
 *
 * Persists gamertag, xid, uhs, and the Xbox token+expiry.
 *
 * @param xbox_identity Identity object containing user details and token.
 */
void state_set_xbox_identity(const xbox_identity_t *xbox_identity) {
    obs_data_set_string(g_state, XBOX_IDENTITY_GTG, xbox_identity->gamertag);
    obs_data_set_string(g_state, XBOX_IDENTITY_ID, xbox_identity->xid);
    obs_data_set_string(g_state, XBOX_IDENTITY_UHS, xbox_identity->uhs);
    obs_data_set_string(g_state, XBOX_TOKEN, xbox_identity->token->value);
    obs_data_set_int(g_state, XBOX_TOKEN_EXPIRY, xbox_identity->token->expires);
    save_state(g_state);
}

/**
 * @brief Retrieve the Xbox identity from the state.
 *
 * Ownership/lifetime:
 *  - Returns an xbox_identity_t allocated with bzalloc() which the caller must
 *    free, plus a nested token_t allocated with bzalloc().
 *  - identity->gamertag/xid/uhs and token->value point to strings owned by the
 *    internal state object.
 *
 * @return Identity object on success, or NULL if missing fields.
 */
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
    token->value   = xbox_token;
    token->expires = xbox_token_expiry;

    xbox_identity_t *identity = bzalloc(sizeof(xbox_identity_t));
    identity->gamertag        = gtg;
    identity->xid             = xid;
    identity->uhs             = uhs;
    identity->token           = token;

    return identity;
}
