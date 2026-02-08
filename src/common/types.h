#pragma once

#include <openssl/evp.h>
#include <stdint.h>

// Umbrella header: the includes below intentionally re-export common public types
// used across the plugin. Some may look unused within this file, but are kept so
// consumers can include a single header.
#include "common/memory.h"
#include "common/achievement.h"
#include "common/achievement_progress.h"
#include "common/device.h"
#include "common/game.h"
#include "common/gamerscore.h"
#include "common/token.h"
#include "common/unlocked_achievement.h"
#include "common/xbox_identity.h"
#include "common/xbox_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frees a cJSON object.
 *
 * Convenience macro around @c cJSON_Delete().
 *
 * Notes:
 * - Safe to pass NULL.
 * - Does not set the caller pointer to NULL.
 *
 * @param p Pointer to a cJSON object.
 */
#define FREE_JSON(p) \
    if (p)            \
    cJSON_Delete(p);

/**
 * @brief Assigns ownership of an allocated pointer to an output parameter or frees it.
 *
 * If @p dst is non-NULL, assigns @p src into @c *dst. Otherwise frees @p src
 * using @ref FREE.
 *
 * Typical use: return a freshly allocated object either via an out-parameter or
 * by cleaning it up when the caller isn't interested.
 *
 * @param src Pointer to pass to the caller or free.
 * @param dst Address of the destination pointer, or NULL.
 */
#define COPY_OR_FREE(src, dst) \
    if (dst)                  \
        *dst = src;           \
    else                      \
        FREE(src);

#if defined(_WIN32)
#include <windows.h>

/**
 * @brief Sleeps for a number of milliseconds.
 *
 * Cross-platform helper used by this plugin.
 *
 * @param ms Duration in milliseconds.
 */
static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}

/**
 * @brief Case-insensitive string comparison.
 *
 * Alias to Windows' @c _stricmp to provide POSIX-compatible @c strcasecmp.
 */
#define strcasecmp _stricmp
#else
#include <unistd.h>

/**
 * @brief Sleeps for a number of milliseconds.
 *
 * Cross-platform helper used by this plugin.
 *
 * @param ms Duration in milliseconds.
 */
static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}
#endif

/**
 * @brief Result type for Xbox Live authentication.
 *
 * This is returned by authentication helpers to provide a stable ABI and a
 * single place for error details.
 */
typedef struct xbox_live_authenticate_result {
    /** Human-readable error message when authentication fails, otherwise NULL. */
    const char *error_message;
} xbox_live_authenticate_result_t;

/**
 * @brief Size structure for sources.
 *
 * Groups width and height dimensions together.
 */
typedef struct source_size {
    /** Width in pixels. */
    uint32_t width;
    /** Height in pixels. */
    uint32_t height;
} source_size_t;

/**
 * @brief Text alignment options.
 */
typedef enum text_align {
    /** Align text to the left edge of the canvas. */
    TEXT_ALIGN_LEFT  = 0,
    /** Align text to the right edge of the canvas. */
    TEXT_ALIGN_RIGHT = 1,
} text_align_t;

/**
 * @brief Common configuration for text-based sources.
 *
 * Contains all the shared configuration fields used across text sources.
 */
typedef struct text_source_config {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} text_source_config_t;

/**
 * @brief Configuration used by the gamerscore overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamerscore_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} gamerscore_configuration_t;

/**
 * @brief Configuration used by the gamertag overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamertag_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} gamertag_configuration_t;

/**
 * @brief Configuration used by the achievement name overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_name_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} achievement_name_configuration_t;

/**
 * @brief Configuration used by the achievement description overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_description_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} achievement_description_configuration_t;

/**
 * @brief Configuration used by the achievements unlocked count overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievements_unlocked_count_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} achievements_unlocked_count_configuration_t;

/**
 * @brief Configuration used by the achievements total count overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievements_total_count_configuration {
    /** Font file path to load (e.g., "/Library/Fonts/SF-Pro.ttf"). */
    const char  *font_path;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t     font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t     color;
    /** Text alignment. */
    text_align_t align;
} achievements_total_count_configuration_t;

/**
 * @brief Dummy type to ensure OpenSSL public types are available to consumers.
 *
 * This header intentionally re-exports OpenSSL's @c EVP_PKEY type.
 */
typedef EVP_PKEY *types_evp_pkey_t;

#ifdef __cplusplus
}
#endif
