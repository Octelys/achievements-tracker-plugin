#pragma once

#include <openssl/evp.h>
#include <stdbool.h>
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
#include "integrations/xbox/contracts/xbox_achievement.h"
#include "integrations/xbox/contracts/xbox_achievement_progress.h"
#include "integrations/xbox/contracts/xbox_unlocked_achievement.h"
#include "common/device.h"
#include "common/game.h"
#include "common/gamerscore.h"
#include "common/identity.h"
#include "common/token.h"
#include "integrations/xbox/entities/xbox_identity.h"
#include "integrations/xbox/entities/xbox_session.h"
#include "integrations/twitch/entities/twitch_identity.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/** Default seconds to keep a source visible during auto visibility cycling. */
#define AUTO_VISIBILITY_DEFAULT_SHOW_DURATION 10.0f
/** Default seconds to keep a source hidden during auto visibility cycling. */
#define AUTO_VISIBILITY_DEFAULT_HIDE_DURATION 10.0f
/** Default seconds for fade in/out during auto visibility cycling. */
#define AUTO_VISIBILITY_DEFAULT_FADE_DURATION 0.35f

/**
 * @brief Shared auto visibility (show/hide/fade) configuration.
 */
typedef struct auto_visibility_config {
    bool  enabled;
    float show_duration;
    float hide_duration;
    float fade_duration;
} auto_visibility_config_t;

/**
 * @brief Horizontal alignment of text within its (auto-sized) box.
 *
 * The box width grows to fit the widest text seen; shorter text is then
 * positioned within that box according to this value. LEFT reproduces the
 * historical behaviour (text anchored to the source's left edge).
 */
typedef enum text_align {
    TEXT_ALIGN_LEFT   = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT  = 2,
} text_align_t;

/**
 * @brief Common configuration for text-based sources.
 *
 * Contains all the shared configuration fields used across text sources.
 */
typedef struct text_source_config {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t                 active_top_color;
    uint32_t                 active_bottom_color;
    /** Alternate color for locked achievements (0xRRGGBBAA format). */
    uint32_t                 inactive_top_color;
    uint32_t                 inactive_bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} text_source_config_t;

/**
 * @brief Configuration used by the gamerscore overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamerscore_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Top gradient color in 0xRRGGBBAA format. */
    uint32_t                 top_color;
    /** Bottom gradient color in 0xRRGGBBAA format. */
    uint32_t                 bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} gamerscore_configuration_t;

/**
 * @brief Configuration used by the gamertag overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct gamertag_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Top gradient color in 0xRRGGBBAA format. */
    uint32_t                 top_color;
    /** Bottom gradient color in 0xRRGGBBAA format. */
    uint32_t                 bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} gamertag_configuration_t;

/**
 * @brief Configuration used by the active game name overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct game_name_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Top gradient color in 0xRRGGBBAA format. */
    uint32_t                 top_color;
    /** Bottom gradient color in 0xRRGGBBAA format. */
    uint32_t                 bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} game_name_configuration_t;

/**
 * @brief Configuration used by the achievement name overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_name_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Top gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t                 active_top_color;
    /** Bottom gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t                 active_bottom_color;
    /** Top gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t                 inactive_top_color;
    /** Bottom gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t                 inactive_bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} achievement_name_configuration_t;

/**
 * @brief Configuration used by the achievement description overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievement_description_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Top gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t                 active_top_color;
    /** Bottom gradient color for unlocked achievements in 0xRRGGBBAA format. */
    uint32_t                 active_bottom_color;
    /** Top gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t                 inactive_top_color;
    /** Bottom gradient color for locked achievements in 0xRRGGBBAA format. */
    uint32_t                 inactive_bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} achievement_description_configuration_t;

/**
 * @brief Configuration used by the achievements count overlay/renderer.
 *
 * Ownership:
 * - Strings are treated as borrowed pointers unless otherwise documented by the
 *   caller.
 */
typedef struct achievements_count_configuration {
    const char              *font_face;
    const char              *font_style;
    /** Font size in pixels (height passed to FreeType). */
    uint32_t                 font_size;
    /** Packed RGBA color in 0xRRGGBBAA format. */
    uint32_t                 top_color;
    uint32_t                 bottom_color;
    /** Horizontal alignment of the text within its auto-sized box. */
    text_align_t             text_align;
    auto_visibility_config_t auto_visibility;
    /** Whether the drop shadow is rendered behind the text (independent of the outline). */
    bool                     shadow_enabled;
} achievements_count_configuration_t;

/** Default message template used to announce an achievement unlock in Twitch chat. */
#define TWITCH_DEFAULT_MESSAGE_TEMPLATE "\xF0\x9F\x8F\x86 Achievement unlocked: {name} ({value}G)"

/** Default message template used to announce a new game being played in Twitch chat. */
#define TWITCH_DEFAULT_GAME_ANNOUNCEMENT_TEMPLATE "\xF0\x9F\x8E\xAE Now playing: {game}"

/** Default message template used to announce a game being mastered (100% unlocked) in Twitch chat. */
#define TWITCH_DEFAULT_MASTERY_ANNOUNCEMENT_TEMPLATE \
    "\xF0\x9F\x8E\x89 {gamertag} just mastered {game} \xE2\x80\x94 100% achievements unlocked!"

/**
 * @brief Configuration for posting achievement-unlock, game-change, and mastery announcements to Twitch chat.
 *
 * Ownership:
 * - @c message_template, @c game_announcement_template, and
 *   @c mastery_announcement_template are owned by this structure and must be
 *   freed with bfree().
 */
typedef struct twitch_configuration {
    /** Whether achievement-unlock announcements are posted to Twitch chat at all. */
    bool  enabled;
    /** Whether a new game being played is announced to Twitch chat. Independent of @c enabled. */
    bool  announce_game_changes;
    /** Whether mastering a game (100% unlocked) is announced to Twitch chat. Independent of @c enabled. */
    bool  announce_mastery;
    /** When true, announcements are only posted while the channel is live. */
    bool  only_when_live;
    /** Message template supporting {name}, {value}, and {gamertag} placeholders. */
    char *message_template;
    /** Message template supporting {game} and {gamertag} placeholders. */
    char *game_announcement_template;
    /** Message template supporting {game} and {gamertag} placeholders. */
    char *mastery_announcement_template;
} twitch_configuration_t;

/** Default seconds for the auto-visibility show phase (shared across all sources). */
#define AUTO_VISIBILITY_DEFAULT_SHARED_SHOW_DURATION 30.0f
/** Default seconds for the auto-visibility hide phase (shared across all sources). */
#define AUTO_VISIBILITY_DEFAULT_SHARED_HIDE_DURATION 120.0f
/** Default seconds for the auto-visibility fade phase (shared across all sources). */
#define AUTO_VISIBILITY_DEFAULT_SHARED_FADE_DURATION 0.35f

/**
 * @brief Shared auto-visibility duration settings (global, not per-source).
 *
 * The show/hide/fade durations are configured once in the global Achievement Tracker
 * dialog and applied to every source that has its per-source toggle enabled.
 */
typedef struct auto_visibility_durations {
    float show_duration;
    float hide_duration;
    float fade_duration;
} auto_visibility_durations_t;

/** Minimum allowed value (seconds) for any achievement cycle duration setting. */
#define ACHIEVEMENT_CYCLE_MIN_DURATION 5

/** Default seconds to display the last-unlocked achievement. */
#define ACHIEVEMENT_CYCLE_DEFAULT_LAST_UNLOCKED_DURATION   45

/** Default seconds to display each random locked achievement. */
#define ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_EACH_DURATION     30

/** Default total seconds to spend in the locked-rotation phase. */
#define ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_TOTAL_DURATION   120

/**
 * @brief Configurable display-duration settings for the achievement cycle.
 */
typedef struct achievement_cycle_timings {
    /** Seconds to display the last-unlocked achievement. Default: ACHIEVEMENT_CYCLE_DEFAULT_LAST_UNLOCKED_DURATION. */
    int last_unlocked_duration;
    /** Seconds to display each random locked achievement. Default: ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_EACH_DURATION. */
    int locked_achievement_duration;
    /** Total seconds to spend in the locked-rotation phase. Default: ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_TOTAL_DURATION.
     */
    int locked_cycle_total_duration;
} achievement_cycle_timings_t;

/**
 * @brief Dummy type to ensure OpenSSL public types are available to consumers.
 *
 * This header intentionally re-exports OpenSSL's @c EVP_PKEY type.
 */
typedef EVP_PKEY *types_evp_pkey_t;

#ifdef __cplusplus
}
#endif
