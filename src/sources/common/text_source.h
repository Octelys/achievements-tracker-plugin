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
    /** Duration of each fade phase in seconds. */
    float                   duration;
} text_transition_state_t;

/**
 * @brief Base structure for text-based sources.
 *
 * Embed this structure in source-specific structs to inherit common fields.
 */
typedef struct text_source_base {
    /** OBS source instance. */
    obs_source_t *source;

    /** Output dimensions. */
    source_size_t size;

    /** Transition state for fade animations. */
    text_transition_state_t transition;

    /** Pending text to display after fade-out completes. */
    char *pending_text;

} text_source_base_t;

/**
 * @brief Create and initialize a text source base structure.
 *
 * Allocates a new text_source_base_t and initializes it with the given values.
 *
 * @param source OBS source instance.
 * @param size   Dimensions (width and height in pixels).
 * @return Newly allocated text_source_base_t, or NULL on failure. Caller must free with bfree().
 */
text_source_base_t *text_source_create(obs_source_t *source, source_size_t size);

/**
 * @brief Reload text context if needed, with fade transition support.
 *
 * When must_reload is set and a context already exists, this initiates a fade-out
 * transition and stores the new text as pending. The actual reload happens when
 * the fade-out completes (handled by text_source_tick).
 *
 * If no context exists, creates the context immediately and starts a fade-in.
 *
 * @param ctx         Pointer to the text context pointer (will be updated).
 * @param must_reload Pointer to the reload flag (will be cleared on reload).
 * @param config      Text source configuration (font, size, color, alignment).
 * @param base        Text source base containing canvas dimensions and transition state.
 * @param text        Text string to render.
 * @return true if context is valid and ready to render, false otherwise.
 */
bool text_source_reload(text_context_t **ctx, bool *must_reload, const text_source_config_t *config,
                                  text_source_base_t *base, const char *text);

/**
 * @brief Render text with inverse scaling to prevent OBS transform scaling.
 *
 * Extracts the current translation from the OBS transform matrix and renders
 * the text at that position without any scaling. This ensures text always
 * renders at its actual pixel size regardless of source transforms.
 * Applies the current transition opacity for fade animations.
 *
 * @param ctx    Text context to render.
 * @param base   Text source base containing transition state.
 * @param effect Effect to use for rendering. Pass NULL to use default effect.
 */
void text_source_render(text_context_t *ctx, text_source_base_t *base, gs_effect_t *effect);

/**
 * @brief Update the transition animation state.
 *
 * Call this from the video_tick callback to advance fade animations.
 * When a fade-out completes and pending text exists, triggers a reload
 * and begins the fade-in phase.
 *
 * @param base        Text source base containing transition state.
 * @param ctx         Pointer to the text context pointer (will be updated on text switch).
 * @param config      Text source configuration.
 * @param seconds     Time elapsed since last tick.
 */
void text_source_tick(text_source_base_t *base, text_context_t **ctx,
                      const text_source_config_t *config, float seconds);

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
 */
void text_source_add_properties(obs_properties_t *props);

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

#ifdef __cplusplus
}
#endif
