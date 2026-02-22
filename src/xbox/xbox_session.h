#pragma once

#include "common/types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file xbox_session.h
 * @brief In-memory session state for Xbox monitoring.
 *
 * A session tracks the “currently played game” and any derived/cached state
 * associated with that game (achievements list, unlocked achievements, cached
 * gamerscore snapshot, etc.).
 *
 * Threading:
 *  - Not inherently thread-safe. Callers should ensure session mutation happens
 *    from a single thread or is externally synchronized.
 */

/**
 * @brief Checks whether the session is currently associated with the given game.
 *
 * Typically used to detect when the user has started playing a different title.
 *
 * @param session Session to inspect.
 * @param game Game to compare against.
 *
 * @return True if the session's current game matches @p game, false otherwise.
 */
bool xbox_session_is_game_played(xbox_session_t *session, const game_t *game);

/**
 * @brief Callback type invoked when icon prefetching completes.
 *
 * Called from the background prefetch thread. Implementations must be
 * thread-safe and return quickly.
 */
typedef void (*xbox_session_ready_callback_t)(void);

/**
 * @brief Updates the session to use a new current game.
 *
 * Implementations typically free/replace the previous @c session->game and reset
 * cached session state derived from that game (e.g., achievements list and
 * gamerscore).  Achievement icons are prefetched on a background thread; when
 * the prefetch completes @p on_ready is invoked (if non-NULL).
 *
 * Ownership:
 *  - The session makes its own copy of @p game. The caller retains ownership of
 *    the passed-in @p game and remains responsible for freeing it.
 *  - If @p game is NULL, the session is cleared.
 *
 * @param session  Session to update.
 * @param game     New game to set for this session.
 * @param on_ready Optional callback invoked from the prefetch thread when all
 *                 icons have been downloaded.  May be NULL.
 */
void xbox_session_change_game(xbox_session_t *session, game_t *game, xbox_session_ready_callback_t on_ready);

/**
 * @brief Applies an unlock/progress update to the session.
 *
 * Updates session-derived state (achievement list, unlocked achievements,
 * gamerscore deltas, etc.) based on the provided progress event.
 *
 * @param session Session to update.
 * @param progress Progress information for the achievement being unlocked.
 */
void xbox_session_unlock_achievement(xbox_session_t *session, const achievement_progress_t *progress);

/**
 * @brief Clears all session state.
 *
 * Resets the current game and any cached/derived state (achievements list,
 * unlocked achievements list, cached gamerscore, etc.) to an empty state.
 *
 * This is typically used when disconnecting, when the identity changes, or when
 * starting a fresh monitoring run.
 *
 * Ownership:
 *  - Frees any heap allocations owned by the session.
 *  - Does not free the session object itself.
 *
 * @param session Session to clear (must be non-NULL).
 */
void xbox_session_clear(xbox_session_t *session);

#ifdef __cplusplus
}
#endif
