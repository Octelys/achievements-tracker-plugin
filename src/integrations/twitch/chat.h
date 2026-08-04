#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file chat.h
 * @brief Posts chat messages to the authenticated user's Twitch channel.
 */

/**
 * @brief Send a chat message to the authenticated user's own Twitch channel.
 *
 * Synchronous — performs a blocking HTTP call (and a token refresh if needed).
 * Callers must invoke this off any latency-sensitive thread (OBS render thread,
 * monitor threads); see achievement_announcer.c for the detached-thread pattern
 * used to call this.
 *
 * Also applies a small client-side minimum spacing between sends to stay
 * safely under Twitch's per-user chat rate limits.
 *
 * @param message Message text to post.
 * @return true if the message was accepted by Twitch, false otherwise (including
 *         when no Twitch account is signed in).
 */
bool twitch_send_chat_message(const char *message);

#ifdef __cplusplus
}
#endif
