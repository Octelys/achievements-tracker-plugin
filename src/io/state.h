#pragma once

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the persisted plugin state from disk into memory.
 *
 * The implementation typically initializes the internal state cache (device,
 * tokens, identity, etc.) by reading from the configured storage location.
 *
 * This function is expected to be called during plugin initialization before
 * accessing any state_* getters.
 */
void io_load(void);

/**
 * @brief Get the current device information associated with the state.
 *
 * @return Pointer to the in-memory device object, or NULL if no device is
 *         available/loaded.
 */
device_t *state_get_device(void);

/**
 * @brief Set the current user's access token and refresh token.
 *
 * Implementations usually copy the tokens into internal storage and may persist
 * them.
 *
 * @param user_token    The access token to store (may be NULL to clear).
 * @param refresh_token The refresh token to store (may be NULL to clear).
 */
void state_set_user_token(const token_t *user_token, const token_t *refresh_token);

/**
 * @brief Get the current user's access token.
 *
 * @return Pointer to the stored token, or NULL if none is set.
 */
token_t *state_get_user_token(void);

/**
 * @brief Get the current user's refresh token.
 *
 * @return Pointer to the stored refresh token, or NULL if none is set.
 */
token_t *state_get_user_refresh_token(void);

/**
 * @brief Set the device token used for device authentication.
 *
 * @param device_token The device token to store (may be NULL to clear).
 */
void state_set_device_token(const token_t *device_token);

/**
 * @brief Get the currently stored device token.
 *
 * @return Pointer to the stored device token, or NULL if none is set.
 */
token_t *state_get_device_token(void);

/**
 * @brief Set the SISU token used during the Xbox authentication flow.
 *
 * @param sisu_token The SISU token to store (may be NULL to clear).
 */
void state_set_sisu_token(const token_t *sisu_token);

/**
 * @brief Get the currently stored SISU token.
 *
 * @return Pointer to the stored SISU token, or NULL if none is set.
 */
token_t *state_get_sisu_token(void);

/**
 * @brief Set the Xbox identity information for the currently authenticated user.
 *
 * @param xbox_identity Identity object to store (may be NULL to clear).
 */
void state_set_xbox_identity(const xbox_identity_t *xbox_identity);

/**
 * @brief Get the currently stored Xbox identity information.
 *
 * @return Pointer to the stored identity, or NULL if none is set.
 */
xbox_identity_t *state_get_xbox_identity(void);

/**
 * @brief Clear all in-memory state (and typically any persisted state).
 *
 * After calling this, all getters are expected to return NULL until new values
 * are set (or io_load() is called again).
 */
void state_clear(void);

#ifdef __cplusplus
}
#endif
