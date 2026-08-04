#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievement_announcer.h
 * @brief Announces newly-unlocked achievements to Twitch chat.
 *
 * Subscribes to the monitoring service and diffs the achievement list to
 * detect specific unlocks (the monitoring service's own achievements-changed
 * signal carries no payload). A per-game baseline is established on first
 * sight of a game's achievement list so a game's *history* is never announced
 * — only unlocks that happen after that baseline.
 */

/**
 * @brief Subscribe to the monitoring service to start announcing unlocks.
 *
 * Call once during plugin load. Safe to call even when no Twitch account is
 * signed in or announcements are disabled — those checks happen per-unlock,
 * off the calling thread.
 */
void twitch_achievement_announcer_start(void);

#ifdef __cplusplus
}
#endif
