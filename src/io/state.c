#include "state.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/config-file.h>
#include <util/platform.h>

#include "crypto/crypto.h"
#include "util/uuid.h"

#define PERSIST_FILE "state.json"
#define DEVICE_UUID "device_uuid"
#define DEVICE_SERIAL_NUMBER "device_serial_number"
#define DEVICE_KEYS "device_keys"
#define XSTS_TOKEN_KEY "xsts_token"
#define XID_KEY "xid"

static obs_data_t *g_state = NULL;

static char *get_state_path(void) {
    /* Put state under: <OBS config dir>/plugins/<plugin_name>/ */
    char *dir = obs_module_config_path("");

    if (!dir)
        return NULL;

    /* Ensure directory exists */
    os_mkdirs(dir);

    char *path = (char *)bzalloc(1024);
    snprintf(path, 1024, "%s/%s", dir, PERSIST_FILE);
    bfree(dir);

    return path;
}

/**
 * Reads the state from the disk.
 *
 * @return
 */
static obs_data_t *load_state(void) {
    char *path = get_state_path();

    if (!path)
        return NULL;

    obs_data_t *data = obs_data_create_from_json_file(path);
    bfree(path);

    /* If file doesn’t exist yet, return an empty object */
    if (!data) {
        obs_log(LOG_INFO, "no state found: creating a new one");
        data = obs_data_create();
    }

    return data;
}

/**
 * Persists the state to disk
 *
 * @param data
 */
static void save_state(obs_data_t *data) {
    if (!data)
        return;

    char *path = get_state_path();

    if (!path)
        return;

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

const char *get_xsts_token(void) {
    return obs_data_get_string(g_state, XSTS_TOKEN_KEY);
}

const char *get_xid(void) {
    return obs_data_get_string(g_state, XID_KEY);
}

void state_clear(void) {
    obs_data_set_string(g_state, DEVICE_UUID, "");
    obs_data_set_string(g_state, DEVICE_KEYS, "");
    obs_data_set_string(g_state, XID_KEY, "");
    obs_data_set_string(g_state, XSTS_TOKEN_KEY, "");

    /* Immediately save the state to disk */
    save_state(g_state);
}

static const char *create_device_uuid() {
    /* Generate a random device UUID */
    char new_device_uuid[37];
    uuid_get_random(new_device_uuid);

    /* Immediately save the state to disk */
    obs_data_set_string(g_state, DEVICE_UUID, new_device_uuid);
    save_state(g_state);

    obs_log(LOG_INFO, "New device UUID saved %s", new_device_uuid);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_UUID);
}

static const char *create_device_serial_number() {
    /* Generate a random device UUID */
    char new_device_serial_number[37];
    uuid_get_random(new_device_serial_number);

    /* Immediately save the state to disk */
    obs_data_set_string(g_state, DEVICE_SERIAL_NUMBER, new_device_serial_number);
    save_state(g_state);

    obs_log(LOG_INFO, "New device serial number saved %s", new_device_serial_number);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_SERIAL_NUMBER);
}

static const char *create_device_keys() {
    /* Generate a random key pair */
    EVP_PKEY *device_key = crypto_generate_keys();

    char *serialized_keys = crypto_to_string(device_key, true);

    obs_log(LOG_INFO, "Saving device keys: %s", serialized_keys);

    /* Immediately save the state to disk */
    obs_data_set_string(g_state, DEVICE_KEYS, serialized_keys);
    save_state(g_state);

    obs_log(LOG_INFO, "New device keys saved");

    bfree(serialized_keys);
    EVP_PKEY_free(device_key);

    /* Retrieves it from the state */
    return obs_data_get_string(g_state, DEVICE_KEYS);
}

/**
 * Gets an existing device or create one if needed.
 *
 * @return Device UUID
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

    obs_log(LOG_INFO, "Device UUID: %s", device_uuid);

    if (!device_keys || strlen(device_keys) == 0) {
        obs_log(LOG_INFO, "No device keys found. Creating new one pair");
        device_keys = create_device_keys();
    }

    obs_log(LOG_INFO, "Device keys: %s", device_keys);

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

void state_set_tokens(const char *xid, const char *token) {
    obs_data_set_string(g_state, XID_KEY, xid);
    obs_data_set_string(g_state, XSTS_TOKEN_KEY, token);

    /* Immediately save the state to disk */
    save_state(g_state);
}
