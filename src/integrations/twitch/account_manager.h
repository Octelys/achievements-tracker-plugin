#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Twitch authentication flow.
 *
 * This launches the asynchronous Twitch device-code flow.
 *
 * @return true if the flow could be started; false otherwise.
 */
bool twitch_account_sign_in(void);

/**
 * @brief Sign the current Twitch user out.
 */
void twitch_account_sign_out(void);

/**
 * @brief Returns whether a Twitch identity is currently stored.
 */
bool twitch_account_is_signed_in(void);

/**
 * @brief Formats the current Twitch account status into the provided buffer.
 *
 * The buffer always receives a NUL-terminated string when @p buffer_size is
 * greater than zero.
 */
void twitch_account_get_status_text(char *buffer, size_t buffer_size);

/**
 * @brief Read (and clear) any pending device-code message for display.
 *
 * When a sign-in flow has just requested a device code, this returns a
 * human-readable "go to this URL and enter this code" message exactly once;
 * subsequent calls return an empty string until the next sign-in attempt.
 * Intended to be polled from the Twitch Account dialog's UI-thread refresh
 * timer (the device-code-ready callback itself fires on a background thread
 * and must not touch Qt directly).
 *
 * @param buffer      Destination buffer.
 * @param buffer_size Size of @p buffer.
 */
void twitch_account_get_pending_code_text(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
