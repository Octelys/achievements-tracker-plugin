#pragma once

#include <openssl/evp.h>
#include <stdint.h>

/**
 * @file types.h
 * @brief Common type definitions and utilities for the achievements tracker plugin.
 *
 * This umbrella header re-exports common types used throughout the plugin.
 * The includes below may appear unused within this file but are intentionally
 * kept so that consumers can include a single header for all common types.
 */
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
 * @brief Frees a cJSON object safely.
 *
 * Convenience macro around @c cJSON_Delete().
 *
 * @note Safe to pass NULL.
 * @note Does not set the pointer to NULL after freeing.
 *
 * @param p Pointer to a cJSON object to be freed.
 */
#define FREE_JSON(p) \
    if (p)            \
    cJSON_Delete(p);

/**
 * @brief Transfers ownership of an allocated pointer to an output parameter or frees it.
 *
 * If @p dst is non-NULL, assigns @p src to @c *dst. Otherwise, frees @p src
 * using @ref FREE.
 *
 * This is typically used to return a freshly allocated object either via an
 * out-parameter or to clean it up when the caller isn't interested.
 *
 * @param src Pointer to transfer to the caller or free.
 * @param dst Address of the destination pointer, or NULL to trigger cleanup.
 */
#define COPY_OR_FREE(src, dst) \
    if (dst)                  \
        *dst = src;           \
    else                      \
        FREE(src);

#if defined(_WIN32)
#include <windows.h>

/**
 * @brief Sleeps for the specified number of milliseconds.
 *
 * Cross-platform sleep helper that wraps the platform-specific sleep API.
 *
 * @param ms Duration in milliseconds.
 */
static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}

/**
 * @brief Case-insensitive string comparison.
 *
 * Provides POSIX-compatible @c strcasecmp on Windows by aliasing to @c _stricmp.
 */
#define strcasecmp _stricmp
#else
#include <unistd.h>

/**
 * @brief Sleeps for the specified number of milliseconds.
 *
 * Cross-platform sleep helper that wraps the platform-specific sleep API.
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
 * This structure is returned by authentication helpers to provide a stable ABI
 * and a single place for error details.
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
 * @brief Common configuration for text-based sources.
 *
 * Contains all the shared configuration fields used across text sources.
 */
typedef struct text_source_config {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t    active_top_color;
    uint32_t    active_bottom_color;
    /** Alternate color for locked achievements (0xRRGGBBAA format). */
    uint32_t    inactive_top_color;
    uint32_t    inactive_bottom_color;
} text_source_config_t;

/**
 * @brief Configuration used by the gamerscore overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamerscore_configuration {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Top gradient color in 0xRRGGBBAA format. */
    uint32_t    top_color;
    /** Bottom gradient color in 0xRRGGBBAA format. */
    uint32_t    bottom_color;
} gamerscore_configuration_t;

/**
 * @brief Configuration used by the gamertag overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamertag_configuration {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Top gradient color in 0xRRGGBBAA format. */
    uint32_t    top_color;
    /** Bottom gradient color in 0xRRGGBBAA format. */
    uint32_t    bottom_color;
} gamertag_configuration_t;

/**
 * @brief Configuration used by the achievement name overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_name_configuration {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Top gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t    active_top_color;
    /** Bottom gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t    active_bottom_color;
    /** Top gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t    inactive_top_color;
    /** Bottom gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t    inactive_bottom_color;
} achievement_name_configuration_t;

/**
 * @brief Configuration used by the achievement description overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_description_configuration {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Top gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t    active_top_color;
    /** Bottom gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t    active_bottom_color;
    /** Top gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t    inactive_top_color;
    /** Bottom gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t    inactive_bottom_color;
} achievement_description_configuration_t;

/**
 * @brief Configuration used by the achievements count overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievements_count_configuration {
    const char *font_face;
    const char *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t    font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t    top_color;
    uint32_t    bottom_color;
} achievements_count_configuration_t;

/**
 * @brief Dummy type to ensure OpenSSL public types are available to consumers.
 *
 * This header intentionally re-exports OpenSSL's @c EVP_PKEY type.
 */
typedef EVP_PKEY *types_evp_pkey_t;

#ifdef __cplusplus
}
#endif
