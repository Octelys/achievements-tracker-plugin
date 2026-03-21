#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file retro_achievements_monitor.h
 * @brief WebSocket client that connects to the RetroArch game-state server.
 *
 * This module connects to the RetroArch WebSocket server introduced in
 * https://github.com/Octelys/retro-arch/pull/4. That server runs on
 * 127.0.0.1 and sends a JSON game-state message whenever a game starts or
 * stops. The same message is pushed to every newly connected client.
 *
 * Message shapes:
 *   - Game playing   : { "type":"game_playing", "game_id":"...",
 *                        "game_name":"...", "game_path":"...",
 *                        "console_id":"...", "console_name":"...",
 *                        "core_name":"...", "db_name":"..." }
 *   - No game        : { "type":"no_game" }
 *   - Achievements   : { "type":"achievements",
 *                        "items":[{ "id":1, "name":"...", "points":5,
 *                                   "status":"unlocked",
 *                                   "badge_url":"..." }, ...] }
 *
 * Threading:
 *  - Callbacks may be invoked from the monitor's background thread.
 *  - Callbacks must return quickly and must not perform OBS graphics
 *    operations directly (use obs_enter_graphics/obs_leave_graphics or
 *    schedule work on the OBS main thread).
 *
 * Ownership/lifetime:
 *  - Pointers passed to callbacks are owned by the monitor and remain valid
 *    only for the duration of the callback. Make a deep copy if you need to
 *    keep the data beyond the callback return.
 */

/** Default TCP port used by the RetroArch WebSocket server. */
#define RETRO_ACHIEVEMENTS_WS_PORT 55437

/** Default host (loopback only – the server never binds on a public interface). */
#define RETRO_ACHIEVEMENTS_WS_HOST "127.0.0.1"

/* -------------------------------------------------------------------------
 * Game-state record
 * ---------------------------------------------------------------------- */

/**
 * @brief Game information received from the RetroArch WebSocket server.
 *
 * Field sizes mirror those defined in the RetroArch game_state.h header so
 * that messages always fit without truncation.
 */
typedef struct {
    char game_id[64];       /**< CRC-32 checksum of the ROM as a hex string.   */
    char game_name[512];    /**< Base filename of the ROM without extension.   */
    char game_path[4096];   /**< Full filesystem path to the ROM.              */
    char console_id[64];    /**< Short platform identifier (e.g. "snes").      */
    char console_name[256]; /**< Human-readable platform name.                 */
    char core_name[256];    /**< Name of the libretro core running the game.   */
    char db_name[512];      /**< Playlist/database name for the content.       */
} retro_game_t;

/* -------------------------------------------------------------------------
 * Achievement record
 * ---------------------------------------------------------------------- */

/**
 * @brief A single achievement entry received from the RetroArch WebSocket
 *        server inside an @c "achievements" message.
 */
typedef struct {
    uint32_t id;            /**< Numeric achievement ID.                          */
    char     name[256];     /**< Achievement title.                               */
    uint32_t points;        /**< Point value of the achievement.                  */
    char     status[16];    /**< "unlocked" or "locked".                          */
    char     badge_url[512]; /**< Unlocked badge image URL; empty when absent.    */
} retro_achievement_t;

/* -------------------------------------------------------------------------
 * Callback types
 * ---------------------------------------------------------------------- */

/**
 * @brief Invoked when a game is being played.
 *
 * @param game  Non-NULL pointer to the current game information.
 *              Valid only for the duration of the callback.
 */
typedef void (*on_retro_game_playing_t)(const retro_game_t *game);

/**
 * @brief Invoked when no game is currently being played.
 */
typedef void (*on_retro_no_game_t)(void);

/**
 * @brief Invoked when the connection status changes.
 *
 * @param connected     true if the connection was just established;
 *                      false if it was lost.
 * @param error_message Human-readable error description when @p connected is
 *                      false; NULL when the disconnect was clean.
 */
typedef void (*on_retro_connection_changed_t)(bool connected, const char *error_message);

/**
 * @brief Invoked when the achievements list is received.
 *
 * @param achievements  Pointer to an array of @p count achievement records.
 *                      Valid only for the duration of the callback.
 * @param count         Number of entries in @p achievements.
 */
typedef void (*on_retro_achievements_t)(const retro_achievement_t *achievements, size_t count);

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Start the RetroArch WebSocket monitor.
 *
 * Spawns a background thread that connects to the RetroArch WebSocket server
 * and begins processing incoming game-state messages. Reconnection is
 * attempted automatically with exponential back-off if the connection is
 * lost.
 *
 * @return true if the monitor started successfully; false otherwise.
 */
bool retro_achievements_monitor_start(void);

/**
 * @brief Stop the RetroArch WebSocket monitor.
 *
 * Signals the background thread to exit, waits for it to finish, and
 * releases all resources. Safe to call when the monitor is not running.
 */
void retro_achievements_monitor_stop(void);

/**
 * @brief Check whether the monitor is currently active.
 *
 * @return true if the background thread is running; false otherwise.
 */
bool retro_achievements_monitor_is_active(void);

/* -------------------------------------------------------------------------
 * Subscriptions
 * ---------------------------------------------------------------------- */

/**
 * @brief Subscribe to game-playing events.
 *
 * Ignored if @p callback is NULL.
 *
 * @param callback Invoked whenever a "game_playing" message is received.
 */
void retro_achievements_subscribe_game_playing(on_retro_game_playing_t callback);

/**
 * @brief Subscribe to no-game events.
 *
 * Ignored if @p callback is NULL.
 *
 * @param callback Invoked whenever a "no_game" message is received.
 */
void retro_achievements_subscribe_no_game(on_retro_no_game_t callback);

/**
 * @brief Subscribe to connection-state change events.
 *
 * Ignored if @p callback is NULL.
 *
 * @param callback Invoked whenever the WebSocket connection is established or
 *                 lost.
 */
void retro_achievements_subscribe_connection_changed(on_retro_connection_changed_t callback);

/**
 * @brief Subscribe to achievements-list events.
 *
 * Ignored if @p callback is NULL.
 *
 * @param callback Invoked whenever an "achievements" message is received,
 *                 passing the full list of achievement records.
 */
void retro_achievements_subscribe_achievements(on_retro_achievements_t callback);

#ifdef __cplusplus
}
#endif
