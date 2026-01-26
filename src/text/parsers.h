#pragma once
#include <stdbool.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file parsers.h
 * @brief Lightweight JSON message classifiers and parsers.
 *
 * These helpers parse Xbox presence/achievement messages received as JSON text
 * and convert them into strongly-typed structures used by the rest of the
 * plugin.
 *
 * Allocation/ownership:
 *  - Parsing functions return newly allocated structures on success.
 *  - The caller owns the returned objects and must free them using the
 *    appropriate free helpers for the corresponding types (if available), or
 *    by freeing the struct and its owned strings/children consistently with the
 *    implementation.
 *
 * Error handling:
 *  - Functions return NULL/false on invalid JSON, unexpected shape, or missing
 *    required fields.
 *  - These parsers are intended for known message formats; they are not general
 *    purpose JSON parsers.
 */

/**
 * @brief Check whether a JSON message is a presence update.
 *
 * @param json_string NUL-terminated JSON string.
 * @return true if the message appears to be a presence message; false otherwise.
 */
bool is_presence_message(const char *json_string);

/**
 * @brief Check whether a JSON message is an achievement progress update.
 *
 * @param json_string NUL-terminated JSON string.
 * @return true if the message appears to be an achievement message; false otherwise.
 */
bool is_achievement_message(const char *json_string);

/**
 * @brief Parse a game description from a JSON message.
 *
 * The game object typically contains identifiers and a display title.
 *
 * @param json_string NUL-terminated JSON string.
 * @return Newly allocated game_t on success; NULL on failure.
 */
game_t *parse_game(const char *json_string);

/**
 * @brief Parse achievement progress information from a JSON message.
 *
 * @param json_string NUL-terminated JSON string.
 * @return Newly allocated achievement_progress_t on success; NULL on failure.
 */
achievement_progress_t *parse_achievement_progress(const char *json_string);

/**
 * @brief Parse achievements information from a JSON message.
 *
 * Depending on the message type, this may represent a single achievement or a
 * container describing multiple achievements.
 *
 * @param json_string NUL-terminated JSON string.
 * @return Newly allocated achievement_t on success; NULL on failure.
 */
achievement_t *parse_achievements(const char *json_string);

#ifdef __cplusplus
}
#endif
