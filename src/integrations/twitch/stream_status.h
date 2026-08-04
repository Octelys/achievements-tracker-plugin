#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file stream_status.h
 * @brief Checks whether a Twitch channel is currently live.
 */

/**
 * @brief Check whether the given Twitch user's channel is currently live.
 *
 * Synchronous — performs a blocking HTTP call. Used only when the "only when
 * live" configuration toggle is enabled, from the same background thread that
 * posts the chat message.
 *
 * @param user_id Twitch user id (broadcaster) to check.
 * @return true if the channel is live, false if offline or the check failed.
 */
bool twitch_is_stream_live(const char *user_id);

#ifdef __cplusplus
}
#endif
