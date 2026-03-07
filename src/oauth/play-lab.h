#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file play-lab.h
 * @brief Play Lab authentication using Sign in with Apple.
 *
 * Drives the Sign in with Apple OAuth 2.0 authorization-code flow for a
 * native (non-web) application:
 *
 *   1. Start a short-lived HTTP server on localhost to receive the redirect.
 *   2. Open the system browser to Apple's authorization endpoint.
 *   3. The browser posts the authorization code back to localhost.
 *   4. Exchange the code for an id_token at Apple's token endpoint.
 *   5. Validate the id_token, extract the user identity, and call the callback.
 */

/**
 * @brief Callback invoked when Play Lab / Apple authentication completes.
 *
 * @param data Opaque user pointer supplied to play_lab_authenticate().
 */
typedef void (*on_play_lab_authenticated_t)(void *data);

/**
 * @brief Start the Play Lab Sign-in with Apple flow (non-blocking).
 *
 * Spawns a background thread that opens the browser and waits for the
 * authorization callback. The caller's @p callback is invoked on completion.
 *
 * @param data     Opaque pointer forwarded to @p callback.
 * @param callback Completion handler (must be non-NULL).
 *
 * @return true if the flow was started successfully; false otherwise.
 */
bool play_lab_authenticate(void *data, on_play_lab_authenticated_t callback);

/**
 * @brief Return the display name of the currently signed-in user, or NULL.
 *
 * The returned string is a newly allocated copy owned by the caller (bfree()).
 */
char *play_lab_get_display_name(void);

/**
 * @brief Return true when a valid (non-expired) session exists.
 */
bool play_lab_is_signed_in(void);

/**
 * @brief Clear the current session (sign out).
 */
void play_lab_sign_out(void);

#ifdef __cplusplus
}
#endif

