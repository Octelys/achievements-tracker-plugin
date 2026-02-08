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
 * @param device_code   Device code required for the refresh.
 * @param user_token    The access token to store (may be NULL to clear).
 * @param refresh_token The refresh token to store (may be NULL to clear).
 */
void state_set_user_token(const char *device_code, const token_t *user_token, const token_t *refresh_token);

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
 * @brief Get the device code used to refresh the token.
 *
 * @return Pointer to the stored refresh token, or NULL if none is set.
 */
char *state_get_device_code(void);

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
 * @brief Set the gamerscore source configuration.
 *
 * Stores the configuration for the gamerscore display source, including font path,
 * text size, and color. The configuration is persisted to disk.
 *
 * @param gamerscore_configuration Configuration to store (may be NULL to clear).
 */
void state_set_gamerscore_configuration(const gamerscore_configuration_t *gamerscore_configuration);

/**
 * @brief Get the currently stored gamerscore source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFF (white)
 * - Default size: 12 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
gamerscore_configuration_t *state_get_gamerscore_configuration();

/**
 * @brief Set the gamertag source configuration.
 *
 * Stores the configuration for the gamertag display source, including font path,
 * text size, color, and alignment. The configuration is persisted to disk.
 *
 * @param configuration Configuration to store (may be NULL to clear).
 */
void state_set_gamertag_configuration(const gamertag_configuration_t *configuration);

/**
 * @brief Get the currently stored gamertag source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFF (white)
 * - Default size: 12 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
gamertag_configuration_t *state_get_gamertag_configuration();

/**
 * @brief Set the achievement name source configuration.
 *
 * Stores the configuration for the achievement name display source, including
 * font path, text size, and color. The configuration is persisted to disk.
 *
 * @param configuration Configuration to store (may be NULL to clear).
 */
void state_set_achievement_name_configuration(const achievement_name_configuration_t *configuration);

/**
 * @brief Get the currently stored achievement name source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFF (white)
 * - Default size: 12 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
achievement_name_configuration_t *state_get_achievement_name_configuration();

/**
 * @brief Set the achievement description source configuration.
 *
 * Stores the configuration for the achievement description display source, including
 * font path, text size, and color. The configuration is persisted to disk.
 *
 * @param configuration Configuration to store (may be NULL to clear).
 */
void state_set_achievement_description_configuration(const achievement_description_configuration_t *configuration);

/**
 * @brief Get the currently stored achievement description source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFF (white)
 * - Default size: 12 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
achievement_description_configuration_t *state_get_achievement_description_configuration();

/**
 * @brief Set the achievements unlocked count source configuration.
 *
 * Stores the configuration for the achievements unlocked count display source,
 * including font path, text size, and color. The configuration is persisted to disk.
 *
 * @param configuration Configuration to store (may be NULL to clear).
 */
void state_set_achievements_unlocked_count_configuration(
    const achievements_unlocked_count_configuration_t *configuration);

/**
 * @brief Get the currently stored achievements unlocked count source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFFFF (white)
 * - Default size: 48 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
achievements_unlocked_count_configuration_t *state_get_achievements_unlocked_count_configuration();

/**
 * @brief Set the achievements total count source configuration.
 *
 * Stores the configuration for the achievements total count display source,
 * including font path, text size, and color. The configuration is persisted to disk.
 *
 * @param configuration Configuration to store (may be NULL to clear).
 */
void state_set_achievements_total_count_configuration(const achievements_total_count_configuration_t *configuration);

/**
 * @brief Get the currently stored achievements total count source configuration.
 *
 * Retrieves the configuration with default values if none has been set:
 * - Default color: 0xFFFFFFFF (white)
 * - Default size: 48 pixels
 *
 * @return Newly allocated configuration structure. Caller must free it with bfree().
 */
achievements_total_count_configuration_t *state_get_achievements_total_count_configuration();

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
