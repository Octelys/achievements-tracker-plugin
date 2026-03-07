#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file google.h
 * @brief Google authentication using OAuth 2.0 authorization-code flow with PKCE.
 *
 * Drives a secure Google Sign-In flow for a native (public) application.
 * The client secret is NEVER embedded in the desktop binary; all token
 * exchange is delegated to a trusted backend server:
 *
 *   1. Generate a random `state` nonce and a PKCE code-verifier / code-challenge.
 *   2. Start a short-lived HTTP server on localhost to receive the redirect.
 *   3. Open the system browser to Google's authorization endpoint.
 *   4. Google redirects the browser to http://127.0.0.1:<port>/callback with the
 *      authorization code in the query string.
 *   5. POST `{ client_id, code, code_verifier, redirect_uri }` to the backend.
 *      The backend performs the secret-bearing token exchange server-side and
 *      returns `{ sub, name, email, expires_at }`.
 *   6. Store the identity locally and call the completion callback.
 */

/**
 * @brief Callback invoked when Google authentication completes.
 *
 * @param data Opaque user pointer supplied to google_authenticate().
 */
typedef void (*on_google_authenticated_t)(void *data);

/**
 * @brief Start the Google Sign-In flow (non-blocking).
 *
 * Spawns a background thread that opens the browser and waits for the
 * authorization callback. The caller's @p callback is invoked on completion.
 *
 * @param data     Opaque pointer forwarded to @p callback.
 * @param callback Completion handler (must be non-NULL).
 *
 * @return true if the flow was started successfully; false otherwise.
 */
bool google_authenticate(void *data, on_google_authenticated_t callback);

/**
 * @brief Return the display name of the currently signed-in user, or NULL.
 *
 * The returned string is a newly allocated copy owned by the caller (bfree()).
 */
char *google_get_display_name(void);

/**
 * @brief Return true when a valid (non-expired) session exists.
 */
bool google_is_signed_in(void);

/**
 * @brief Clear the current session (sign out).
 */
void google_sign_out(void);

#ifdef __cplusplus
}
#endif

