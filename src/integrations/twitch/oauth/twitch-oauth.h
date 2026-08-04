#pragma once

#include <stdbool.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file twitch-oauth.h
 * @brief Twitch authentication flow entry point.
 *
 * Drives Twitch's OAuth 2.0 device code flow (https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow)
 * and returns the result via asynchronous callbacks.
 *
 * Unlike Xbox's device code flow, Twitch's verification page does not accept
 * an embedded code in the URL — the user must type the short user_code shown
 * on screen. @ref on_twitch_device_code_ready_t surfaces that code to the
 * caller so it can be displayed before/while the browser opens.
 */

/**
 * @brief Callback invoked once the device code has been obtained, before polling begins.
 *
 * Fires on the background authentication thread — must not touch UI toolkits
 * directly from this callback.
 *
 * @param user_code        Short code the user must enter on @p verification_uri.
 * @param verification_uri Twitch page where the user completes sign-in.
 * @param data             Opaque user pointer provided to twitch_authenticate().
 */
typedef void (*on_twitch_device_code_ready_t)(const char *user_code, const char *verification_uri, void *data);

/**
 * @brief Callback invoked when the Twitch authentication flow completes (success or failure).
 *
 * @param data Opaque user pointer provided to twitch_authenticate().
 */
typedef void (*on_twitch_authenticated_t)(void *data);

/**
 * @brief Start the Twitch device-code authentication flow.
 *
 * Non-blocking from the caller's perspective; the flow runs on a background thread.
 *
 * @param data         Opaque pointer passed back to both callbacks.
 * @param on_code_ready Invoked once the user_code/verification_uri are available (may be NULL).
 * @param on_completed  Invoked once the flow finishes, successfully or not (must be non-NULL).
 *
 * @return true if the authentication flow was started successfully; false otherwise.
 */
bool twitch_authenticate(void *data, on_twitch_device_code_ready_t on_code_ready,
                         on_twitch_authenticated_t on_completed);

/**
 * @brief Get the currently persisted Twitch identity, refreshing the access token if expired.
 *
 * Synchronous — token refresh (if needed) blocks until complete.
 *
 * @return A twitch_identity_t on success, or NULL if no identity is available or refresh failed.
 *         Caller owns the returned object and must free it with free_twitch_identity().
 */
twitch_identity_t *twitch_get_identity(void);

#ifdef __cplusplus
}
#endif
