#include "sources/game_cover.h"

/**
 * @file game_cover.c
 * @brief OBS source that renders the cover art for the currently played game.
 *
 * Responsibilities:
 *  - Subscribe to game-played events via the monitoring service.
 *  - Download cover art when the game changes (using the cover_url from game_t).
 *  - Load the image into an OBS gs_texture_t on the graphics thread.
 *  - Render the texture in the source's video_render callback.
 *
 * Threading notes:
 *  - Downloading happens on the calling thread of on_game_played() (currently
 *    synchronous).
 *  - Texture creation/destruction must happen on the OBS graphics thread; this
 *    file uses obs_enter_graphics()/obs_leave_graphics() to ensure that.
 */

#include <obs-module.h>
#include <graphics/graphics.h>
#include <string.h>

#include <diagnostics/log.h>

#include "sources/common/image_source.h"
#include "sources/common/visibility_cycle.h"
#include "drawing/image.h"
#include "integrations/monitoring_service.h"
#include "common/game.h"

/** Property keys for the per-orientation border images. */
#define BORDER_SQUARE_PROPERTY    "border_square_path"
#define BORDER_PORTRAIT_PROPERTY  "border_portrait_path"
#define BORDER_LANDSCAPE_PROPERTY "border_landscape_path"

/** File dialog filter for border image pickers. */
#define BORDER_FILE_FILTER "Image files (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*.*)"

/**
 * @brief Aspect-ratio thresholds used to classify a cover into an orientation bucket.
 *
 * A cover wider than @ref COVER_LANDSCAPE_THRESHOLD is treated as Landscape, one
 * narrower than @ref COVER_PORTRAIT_THRESHOLD as Portrait, and anything in between
 * as Square. The band around 1.0 absorbs near-square jewel-case art.
 */
#define COVER_LANDSCAPE_THRESHOLD 1.15f
#define COVER_PORTRAIT_THRESHOLD  0.87f

/**
 * @brief Padding, in source-space pixels, left between the cover art and the
 *        border frame on every side.
 *
 * Only applied when a border is drawn: the cover is fitted inside the frame
 * shrunk by this amount on each edge, so the artwork never runs up against the
 * frame. Square frames use a wider margin than landscape/portrait ones. Without
 * a border the cover fills the reported size as before.
 */
#define COVER_BORDER_PADDING_SQUARE    30.0f
#define COVER_BORDER_PADDING_PORTRAIT  10.0f
#define COVER_BORDER_PADDING_LANDSCAPE 15.0f

/**
 * @brief Global singleton cover cache.
 *
 * This source is implemented as a singleton that stores the current cover art in
 * a global cache.
 */
static image_t                  g_game_cover;
static auto_visibility_config_t g_auto_visibility = {
    .enabled       = false,
    .show_duration = AUTO_VISIBILITY_DEFAULT_SHARED_SHOW_DURATION,
    .hide_duration = AUTO_VISIBILITY_DEFAULT_SHARED_HIDE_DURATION,
    .fade_duration = AUTO_VISIBILITY_DEFAULT_SHARED_FADE_DURATION,
};

/**
 * @brief User-configured border frames, one per orientation bucket.
 *
 * Each border is a local image file (a frame with a transparent centre) selected
 * in the source properties. They reuse the image_t cache purely for its
 * path-based texture loading; the download-related fields stay unused. A border
 * with an empty path / NULL texture is simply not drawn.
 */
static image_t g_border_square;
static image_t g_border_portrait;
static image_t g_border_landscape;

/** @brief Axis-aligned rectangle in source space (pixels). */
typedef struct {
    float x;
    float y;
    float width;
    float height;
} cover_rect_t;

//  --------------------------------------------------------------------------------------------------------------------
//	Rendering helpers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Compute the largest rectangle preserving the texture aspect ratio that
 *        fits inside the reserved box, centred (letterbox / pillarbox).
 *
 * @param box_w Reserved box width in pixels.
 * @param box_h Reserved box height in pixels.
 * @param tex_w Texture width in pixels.
 * @param tex_h Texture height in pixels.
 * @return The fitted, centred rectangle in source space.
 */
static cover_rect_t fit_rect(uint32_t box_w, uint32_t box_h, uint32_t tex_w, uint32_t tex_h) {

    cover_rect_t rect = {0.0f, 0.0f, (float)box_w, (float)box_h};

    if (tex_w == 0 || tex_h == 0 || box_w == 0 || box_h == 0) {
        return rect;
    }

    const float box_aspect = (float)box_w / (float)box_h;
    const float tex_aspect = (float)tex_w / (float)tex_h;

    if (tex_aspect > box_aspect) {
        /* Cover is wider than the box: fit to width, letterbox vertically. */
        rect.width  = (float)box_w;
        rect.height = (float)box_w / tex_aspect;
        rect.x      = 0.0f;
        rect.y      = ((float)box_h - rect.height) * 0.5f;
    } else {
        /* Cover is taller than the box: fit to height, pillarbox horizontally. */
        rect.height = (float)box_h;
        rect.width  = (float)box_h * tex_aspect;
        rect.y      = 0.0f;
        rect.x      = ((float)box_w - rect.width) * 0.5f;
    }

    return rect;
}

/**
 * @brief Fit a texture inside an arbitrary source-space rectangle, centred.
 *
 * Like @ref fit_rect but the target area can have a non-zero origin, so the
 * result is offset to sit inside @p area (used to place the cover inside the
 * border's interior).
 *
 * @param area  Target rectangle in source space.
 * @param tex_w Texture width in pixels.
 * @param tex_h Texture height in pixels.
 * @return The fitted, centred rectangle in source space.
 */
static cover_rect_t fit_within(cover_rect_t area, uint32_t tex_w, uint32_t tex_h) {

    cover_rect_t rect = fit_rect((uint32_t)(area.width + 0.5f), (uint32_t)(area.height + 0.5f), tex_w, tex_h);

    rect.x += area.x;
    rect.y += area.y;

    return rect;
}

/**
 * @brief Classify a cover by aspect ratio and return the matching border cache.
 *
 * @param tex_w Cover texture width in pixels.
 * @param tex_h Cover texture height in pixels.
 * @return The border for the cover's orientation, or NULL if it cannot be classified.
 */
static image_t *select_border(uint32_t tex_w, uint32_t tex_h) {

    if (tex_h == 0) {
        return NULL;
    }

    const float ratio = (float)tex_w / (float)tex_h;

    if (ratio > COVER_LANDSCAPE_THRESHOLD) {
        return &g_border_landscape;
    }

    if (ratio < COVER_PORTRAIT_THRESHOLD) {
        return &g_border_portrait;
    }

    return &g_border_square;
}

/**
 * @brief Padding between the cover and the frame for a given border.
 *
 * @param border The border selected for the current cover (may be NULL).
 * @return The per-orientation padding in source-space pixels.
 */
static float select_border_padding(const image_t *border) {

    if (border == &g_border_square) {
        return COVER_BORDER_PADDING_SQUARE;
    }

    if (border == &g_border_portrait) {
        return COVER_BORDER_PADDING_PORTRAIT;
    }

    return COVER_BORDER_PADDING_LANDSCAPE;
}

/**
 * @brief Draw a texture into a source-space rectangle with the given opacity.
 *
 * Positions the sprite via the model matrix so it lands at @p rect, then defers
 * to the shared opacity draw path.
 */
static void draw_fitted(gs_texture_t *texture, cover_rect_t rect, gs_effect_t *effect, float opacity) {

    if (!texture) {
        return;
    }

    gs_matrix_push();
    gs_matrix_translate3f(rect.x, rect.y, 0.0f);
    draw_texture_with_opacity(texture, (uint32_t)(rect.width + 0.5f), (uint32_t)(rect.height + 0.5f), effect, opacity);
    gs_matrix_pop();
}

/**
 * @brief Update a border cache from a newly-configured file path.
 *
 * Schedules a texture reload only when the path actually changed. An empty path
 * clears the border (texture destroyed on the next reload).
 */
static void update_border(image_t *border, const char *new_path) {

    const char *path = new_path ? new_path : "";

    if (strcmp(border->cache_path, path) == 0) {
        return;
    }

    snprintf(border->cache_path, sizeof(border->cache_path), "%s", path);
    border->must_reload = true;
}

/**
 * @brief Apply the source settings (auto-visibility + border paths).
 *
 * Called from both @ref on_source_create and @ref on_source_update. OBS passes the
 * full saved settings to @c create when a scene is loaded, and only calls @c update
 * on later edits (and not at all on load in some builds), so the border paths must
 * be applied from @c create too — otherwise a freshly-loaded scene never schedules
 * the border texture reload and the frames stay blank.
 */
static void apply_source_settings(obs_data_t *settings) {

    auto_visibility_update_toggle(settings, &g_auto_visibility);

    update_border(&g_border_square, obs_data_get_string(settings, BORDER_SQUARE_PROPERTY));
    update_border(&g_border_portrait, obs_data_get_string(settings, BORDER_PORTRAIT_PROPERTY));
    update_border(&g_border_landscape, obs_data_get_string(settings, BORDER_LANDSCAPE_PROPERTY));
}

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Event handler called when a new game starts being played.
 *
 * Uses the cover_url from the game_t to download the cover art.
 *
 * @param game Currently played game information.
 */
static void on_game_played(const game_t *game) {

    if (!game) {
        obs_log(LOG_INFO, "[Game Cover] No game played");
        image_source_clear(&g_game_cover);
        return;
    }

    obs_log(LOG_INFO, "[Game Cover] Playing game %s (%s)", game->title, game->id);

    if (!game->cover_url || game->cover_url[0] == '\0') {
        obs_log(LOG_INFO, "[Game Cover] No cover URL available");
        image_source_clear(&g_game_cover);
        return;
    }

    obs_log(LOG_INFO, "[Game Cover] Cover URL is %s", game->cover_url);

    snprintf(g_game_cover.url, sizeof(g_game_cover.url), "%s", game->cover_url);
    snprintf(g_game_cover.id, sizeof(g_game_cover.id), "%s", game->id);

    image_source_download(&g_game_cover);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/** @brief OBS callback returning the source width. */
static uint32_t source_get_width(void *data) {
    const image_source_t *s = data;
    return s->size.width;
}

/** @brief OBS callback returning the source height. */
static uint32_t source_get_height(void *data) {
    const image_source_t *s = data;
    return s->size.height;
}

/** @brief OBS callback returning the display name for the source type. */
static const char *source_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Game Cover";
}

/**
 * @brief OBS callback creating a new source instance.
 *
 * @param settings OBS settings object (currently unused).
 * @param source   OBS source instance.
 * @return Newly allocated image_source_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    image_source_t *s = bzalloc(sizeof(*s));
    s->source         = source;
    s->size.width     = 800;
    s->size.height    = 200;

    apply_source_settings(settings);

    return s;
}

/**
 * @brief OBS callback destroying a source instance.
 *
 * Frees the per-instance data. Note: Global cover image (g_game_cover)
 * is cleaned up during plugin unload, not per-source-instance.
 */
static void on_source_destroy(void *data) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

/**
 * @brief OBS callback invoked when source settings change.
 *
 * Currently unused (no editable settings).
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    apply_source_settings(settings);
}

static void source_get_defaults(obs_data_t *settings) {
    auto_visibility_set_defaults(settings);
}

/**
 * @brief OBS callback to render the source.
 *
 * The source advertises a *fixed* box — the square border's dimensions — so its
 * footprint never changes as covers come and go and the scene item stops
 * resizing. Every orientation is composited inside that same box: the square
 * border fills it exactly, while landscape/portrait borders are fitted
 * (centred, aspect preserved) so they sit inside the box with transparent
 * padding on the short axis. The cover is then fitted inside whichever border,
 * inset by the per-orientation padding. Until a square border is configured the
 * box falls back to the selected border (or the bare cover). Everything respects
 * the auto-visibility opacity so it fades together.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load textures if needed (deferred load in graphics context) */
    image_source_reload_if_needed(&g_game_cover);
    image_source_reload_if_needed(&g_border_square);
    image_source_reload_if_needed(&g_border_portrait);
    image_source_reload_if_needed(&g_border_landscape);

    /*
     * Pin the reported size to the square-border box even before any cover has
     * loaded, so the scene item never jumps when a game starts.
     */
    if (g_border_square.texture) {
        source->size.width  = gs_texture_get_width(g_border_square.texture);
        source->size.height = gs_texture_get_height(g_border_square.texture);
    }

    if (!g_game_cover.texture) {
        return;
    }

    const float    opacity = auto_visibility_get_opacity(&g_auto_visibility);
    const uint32_t cover_w = gs_texture_get_width(g_game_cover.texture);
    const uint32_t cover_h = gs_texture_get_height(g_game_cover.texture);

    const image_t *border     = select_border(cover_w, cover_h);
    const bool     has_border = border && border->texture;

    /*
     * Fixed source box: the square border's dimensions. Every orientation is
     * composited inside this same box so the source never resizes. Fall back to
     * the selected border (or the bare cover) only while the square border is
     * unset.
     */
    uint32_t box_w;
    uint32_t box_h;

    if (g_border_square.texture) {
        box_w = gs_texture_get_width(g_border_square.texture);
        box_h = gs_texture_get_height(g_border_square.texture);
    } else if (has_border) {
        box_w = gs_texture_get_width(border->texture);
        box_h = gs_texture_get_height(border->texture);
    } else {
        box_w = cover_w;
        box_h = cover_h;
    }

    source->size.width  = box_w;
    source->size.height = box_h;

    /*
     * Place the border inside the box, preserving its aspect ratio (centred).
     * A landscape/portrait border sits inside the square box with transparent
     * padding; a square border fills it exactly.
     */
    cover_rect_t border_rect = {0.0f, 0.0f, (float)box_w, (float)box_h};

    if (has_border) {
        border_rect = fit_rect(box_w,
                               box_h,
                               gs_texture_get_width(border->texture),
                               gs_texture_get_height(border->texture));
    }

    /*
     * Fit the cover inside the border, preserving its aspect ratio. When a
     * border is drawn, shrink the fit area by the per-orientation padding on
     * every side so the artwork keeps a margin from the frame.
     */
    cover_rect_t cover_area = border_rect;

    if (has_border) {
        const float padding = select_border_padding(border);

        if (cover_area.width > 2.0f * padding && cover_area.height > 2.0f * padding) {
            cover_area.x += padding;
            cover_area.y += padding;
            cover_area.width -= 2.0f * padding;
            cover_area.height -= 2.0f * padding;
        }
    }

    const cover_rect_t cover_rect = fit_within(cover_area, cover_w, cover_h);
    draw_fitted(g_game_cover.texture, cover_rect, effect, opacity);

    /* The border frames the cover, centred inside the fixed box. */
    if (has_border) {
        draw_fitted(border->texture, border_rect, effect, opacity);
    }
}

static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    obs_properties_t *p = obs_properties_create();
    auto_visibility_add_toggle_property(p);
    obs_properties_add_path(p, BORDER_SQUARE_PROPERTY, "Square border", OBS_PATH_FILE, BORDER_FILE_FILTER, NULL);
    obs_properties_add_path(p, BORDER_PORTRAIT_PROPERTY, "Portrait border", OBS_PATH_FILE, BORDER_FILE_FILTER, NULL);
    obs_properties_add_path(p, BORDER_LANDSCAPE_PROPERTY, "Landscape border", OBS_PATH_FILE, BORDER_FILE_FILTER, NULL);
    return p;
}

/**
 * @brief obs_source_info for the Game Cover source.
 */
static struct obs_source_info game_cover_source_info = {
    .id             = "xbox_game_cover_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = source_get_name,
    .create         = on_source_create,
    .destroy        = on_source_destroy,
    .update         = on_source_update,
    .get_defaults   = source_get_defaults,
    .video_render   = on_source_video_render,
    .get_properties = source_get_properties,
    .get_width      = source_get_width,
    .get_height     = source_get_height,
    .video_tick     = NULL,
};

/**
 * @brief Get a pointer to this source type's obs_source_info.
 */
static const struct obs_source_info *game_cover_source_get(void) {
    return &game_cover_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void game_cover_source_register(void) {

    snprintf(g_game_cover.display_name, sizeof(g_game_cover.display_name), "Game Cover");
    g_game_cover.id[0] = '\0';
    snprintf(g_game_cover.type, sizeof(g_game_cover.type), "game_cover");

    snprintf(g_border_square.display_name, sizeof(g_border_square.display_name), "Game Cover Border (Square)");
    snprintf(g_border_portrait.display_name, sizeof(g_border_portrait.display_name), "Game Cover Border (Portrait)");
    snprintf(g_border_landscape.display_name, sizeof(g_border_landscape.display_name), "Game Cover Border (Landscape)");

    obs_register_source(game_cover_source_get());

    auto_visibility_register_config(&g_auto_visibility);

    monitoring_subscribe_game_played(&on_game_played);
}

void game_cover_source_cleanup(void) {
    image_source_destroy(&g_game_cover);
    image_source_destroy(&g_border_square);
    image_source_destroy(&g_border_portrait);
    image_source_destroy(&g_border_landscape);
}
