#pragma once

#include <obs-module.h>
#include "common/types.h"
#include "drawing/text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file text_source.h
 * @brief Common functionality for text-based OBS sources.
 *
 * This module provides shared rendering and property management for sources
 * that display text (gamerscore, gamertag, achievement name, achievement description).
 * It eliminates code duplication by centralizing:
 * - Text context reload logic
 * - Unscaled rendering (preventing OBS transform scaling)
 * - Common properties UI (font, color, size, alignment)
 * - Fade transitions when text changes
 */

/**
 * @brief Transition phase for text fade animations.
 */
typedef enum text_transition_phase {
    /** No transition active, text is fully visible. */
    TEXT_TRANSITION_NONE = 0,
    /** Fading out the old text. */
    TEXT_TRANSITION_FADE_OUT,
    /** Fading in the new text. */
    TEXT_TRANSITION_FADE_IN,
} text_transition_phase_t;

/**
 * @brief Transition state for text fade animations.
 */
typedef struct text_transition_state {
    /** Current transition phase. */
    text_transition_phase_t phase;
    /** Current opacity (0.0 to 1.0). */
    float                   opacity;
    float                   last_opacity;
    /** Duration of each fade phase in seconds. */
    float                   duration;
} text_transition_state_t;

/**
 * @brief Base structure for text-based sources.
 *
 * Embed this structure in source-specific structs to inherit common fields.
 */
typedef struct text_source {
    char *name;

    /** OBS source instance. */
    obs_source_t *obs_source;

    /** Internal OBS text source for rendering. */
    obs_source_t *private_obs_source;
    obs_data_t   *private_obs_source_settings;

    /** Texture render for opacity effects. */
    gs_texrender_t *texrender;

    /** Transition state for fade animations. */
    text_transition_state_t transition;

    /** Pending text to display after fade-out completes. */
    char *pending_text;
    bool  pending_use_active_color;

    /** Current text being displayed. */
    char *current_text;
    bool  use_active_color;

} text_source_t;

/**
 * @brief Create and initialize a text source base structure.
 *
 * Allocates a new text_source_base_t and initializes it with default values.
 *
 * @param source OBS source instance.
 * @return Newly allocated text_source_base_t, or NULL on failure. Caller must free with bfree().
 */
text_source_t *text_source_create(obs_source_t *source, const char *name);

/**
 * @brief Destroy a text source base structure.
 *
 * Releases the internal OBS text source and frees all allocated memory.
 *
 * @param text_source Text source base to destroy. Safe to call with NULL.
 */
void text_source_destroy(text_source_t *text_source);

/**
 * @brief Reload text source if needed, with fade transition support.
 *
 * When must_reload is set and a text source already exists, this initiates a fade-out
 * transition and stores the new text as pending. The actual reload happens when
 * the fade-out completes (handled by text_source_tick).
 *
 * If no text source exists, creates it immediately and starts a fade-in.
 *
 * @param text_source        Text source base containing the OBS text source and transition state.
 * @param force_reload Pointer to the reload flag (will be cleared on reload).
 * @param config      Text source configuration (font, size, color, alignment).
 * @param text        Text string to render.
 * @param use_active_color
 * @return true if the text source is valid and ready to render, false otherwise.
 */
bool text_source_update_text(text_source_t *text_source, bool *force_reload, const text_source_config_t *config,
                             const char *text, bool use_active_color);

/**
 * @brief Render text source with opacity for fade animations.
 *
 * Renders the internal OBS text source with the current transition opacity.
 *
 * @param text_source   Text source base containing the OBS text source and transition state.
 * @param effect Effect to use for rendering. Pass NULL to use the default effect.
 */
void text_source_render(text_source_t *text_source, const text_source_config_t *config, gs_effect_t *effect);

/**
 * @brief Update the transition animation state.
 *
 * Call this from the video_tick callback to advance fade animations.
 * When a fade-out completes and pending text exists, triggers a reload
 * and begins the fade-in phase.
 *
 * @param text_source        Text source base containing a transition state.
 * @param config      Text source configuration.
 * @param seconds     Time has elapsed since the last tick.
 */
void text_source_tick(text_source_t *text_source, const text_source_config_t *config, float seconds);

/**
 * @brief Add common text properties to a properties panel.
 *
 * Adds the following properties:
 * - Font dropdown (text_font): List of available system fonts
 * - Text color picker (text_color): RGBA color selector
 * - Text size slider (text_size): Integer from 10 to 164 pixels
 * - Text alignment dropdown (text_align): Left or Right alignment
 *
 * @param props Properties panel to add controls to.
 * @param supports_inactive_color
 */
void text_source_add_properties(obs_properties_t *props, bool supports_inactive_color);

/**
 * @brief Add alternate color property for locked achievements.
 *
 * Adds a color picker for the locked achievement text color.
 * Call this after text_source_add_properties for sources that show achievements.
 *
 * @param props Properties panel to add the control to.
 */
void text_source_add_alternate_color_property(obs_properties_t *props);

/**
 * @brief Process common text property updates.
 *
 * Checks for changes to text_color, text_size, text_font, and text_align
 * properties and updates the provided configuration values accordingly.
 *
 * @param settings     OBS settings data.
 * @param config       Text source configuration to update.
 * @param must_reload  Pointer to reload flag (set to true if any property changed).
 */
void text_source_update_properties(obs_data_t *settings, text_source_config_t *config, bool *must_reload);

/**
 * @brief Get the width of the rendered text.
 *
 * Queries the natural width of the internal FreeType text source.
 * This allows the parent source to scale properly without distortion.
 *
 * @param base Text source base containing the OBS text source.
 * @return Width in pixels, or 0 if no text source exists.
 */
uint32_t text_source_get_width(text_source_t *base);

/**
 * @brief Get the height of the rendered text.
 *
 * Queries the natural height of the internal FreeType text source.
 * This allows the parent source to scale properly without distortion.
 *
 * @param base Text source base containing the OBS text source.
 * @return Height in pixels, or 0 if no text source exists.
 */
uint32_t text_source_get_height(text_source_t *base);

#ifdef __cplusplus
}
#endif
