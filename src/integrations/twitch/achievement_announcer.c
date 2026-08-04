#include "integrations/twitch/achievement_announcer.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include <string.h>

#include "common/achievement.h"
#include "common/identity.h"
#include "io/state.h"
#include "integrations/monitoring_service.h"
#include "integrations/twitch/account_manager.h"
#include "integrations/twitch/chat.h"
#include "integrations/twitch/stream_status.h"
#include "integrations/twitch/oauth/twitch-oauth.h"

/** Snapshot of the achievement list as of the last processed achievements-changed event. */
static achievement_t *g_previous_snapshot    = NULL;
/** Whether a baseline snapshot has been captured for the current game yet. */
static bool           g_baseline_established = false;
/** Whether mastery has already been announced (or suppressed as pre-existing) for the current game. */
static bool           g_mastery_announced    = false;

/** Context handed to the detached posting thread; freed at the end of that thread. */
typedef struct announce_ctx {
    char *message;
    bool  only_when_live;
} announce_ctx_t;

/**
 * @brief Replace every occurrence of @p needle in @p input with @p replacement.
 *
 * A small private helper — this is a one-off, three-placeholder substitution
 * need, not a general templating engine.
 *
 * @return Newly allocated string (caller must bfree()).
 */
static char *replace_all(const char *input, const char *needle, const char *replacement) {

    size_t needle_len = strlen(needle);

    if (needle_len == 0) {
        return bstrdup(input);
    }

    size_t      count = 0;
    const char *scan  = input;
    while ((scan = strstr(scan, needle)) != NULL) {
        count++;
        scan += needle_len;
    }

    if (count == 0) {
        return bstrdup(input);
    }

    size_t replacement_len = strlen(replacement);
    /* Upper bound: ignores the needle bytes removed, which only overestimates. */
    size_t out_size = strlen(input) + count * replacement_len + 1;
    char  *out      = bzalloc(out_size);

    const char *cursor = input;
    char       *write   = out;
    while ((scan = strstr(cursor, needle)) != NULL) {
        size_t prefix_len = (size_t)(scan - cursor);
        memcpy(write, cursor, prefix_len);
        write += prefix_len;
        memcpy(write, replacement, replacement_len);
        write += replacement_len;
        cursor = scan + needle_len;
    }
    strcpy(write, cursor);

    return out;
}

/**
 * @brief Build the announcement text from the configured template and the unlocked achievement.
 */
static char *build_announcement_message(const char *message_template, const achievement_t *achievement) {

    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d", achievement->value);

    const identity_t *identity = monitoring_get_current_active_identity();
    const char        *gamertag = (identity && identity->name) ? identity->name : "";

    char *step1 = replace_all(message_template, "{name}", achievement->name ? achievement->name : "");
    char *step2 = replace_all(step1, "{value}", value_str);
    bfree(step1);
    char *step3 = replace_all(step2, "{gamertag}", gamertag);
    bfree(step2);

    return step3;
}

/**
 * @brief Detached-thread entry point: gates on sign-in/live status, then posts.
 *
 * Runs off the monitoring service's own thread so a slow/blocking network call
 * here never stalls that monitor's event loop.
 */
static void *post_announcement_thread(void *param) {

    announce_ctx_t *ctx = param;

    if (!twitch_account_is_signed_in()) {
        obs_log(LOG_DEBUG, "[TwitchAnnouncer] No Twitch account signed in, skipping post");
        goto cleanup;
    }

    if (ctx->only_when_live) {
        twitch_identity_t *identity = twitch_get_identity();
        bool                live     = identity && twitch_is_stream_live(identity->user_id);
        free_twitch_identity(&identity);

        if (!live) {
            obs_log(LOG_DEBUG, "[TwitchAnnouncer] Channel not live, skipping post");
            goto cleanup;
        }
    }

    twitch_send_chat_message(ctx->message);

cleanup:
    bfree(ctx->message);
    bfree(ctx);
    return NULL;
}

/**
 * @brief Build the message for a newly-unlocked achievement and spawn the posting thread.
 */
static void announce_unlock(const achievement_t *achievement) {

    twitch_configuration_t *config = state_get_twitch_configuration();

    if (!config->enabled) {
        state_free_twitch_configuration(&config);
        return;
    }

    announce_ctx_t *ctx = bzalloc(sizeof(announce_ctx_t));
    ctx->message        = build_announcement_message(config->message_template, achievement);
    ctx->only_when_live = config->only_when_live;

    state_free_twitch_configuration(&config);

    pthread_t thread;
    if (pthread_create(&thread, NULL, post_announcement_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[TwitchAnnouncer] Unable to start the posting thread");
        bfree(ctx->message);
        bfree(ctx);
        return;
    }

    pthread_detach(thread);
}

/** id (or title, when no id is provided) of the last game announced, to avoid re-announcing on reconnects. */
static char *g_last_announced_game_id = NULL;

/**
 * @brief Build an announcement from a template supporting {game} and {gamertag}.
 *
 * Shared by the game-change and mastery announcements — both need exactly
 * this substitution.
 */
static char *build_game_announcement_message(const char *message_template, const char *game_title) {

    const identity_t *identity = monitoring_get_current_active_identity();
    const char        *gamertag = (identity && identity->name) ? identity->name : "";

    char *step1 = replace_all(message_template, "{game}", game_title ? game_title : "");
    char *step2 = replace_all(step1, "{gamertag}", gamertag);
    bfree(step1);

    return step2;
}

/**
 * @brief Build the message for a new game being played and spawn the posting thread.
 *
 * Deduplicates against the last-announced game so a re-notification of the
 * same game (e.g. a monitor reconnect) doesn't re-post it.
 */
static void announce_game_change(const game_t *game) {

    if (!game || !game->title) {
        bfree(g_last_announced_game_id);
        g_last_announced_game_id = NULL;
        return;
    }

    const char *game_key = (game->id && game->id[0]) ? game->id : game->title;

    if (g_last_announced_game_id && strcmp(g_last_announced_game_id, game_key) == 0) {
        return;
    }

    bfree(g_last_announced_game_id);
    g_last_announced_game_id = bstrdup(game_key);

    twitch_configuration_t *config = state_get_twitch_configuration();

    if (!config->announce_game_changes) {
        state_free_twitch_configuration(&config);
        return;
    }

    announce_ctx_t *ctx = bzalloc(sizeof(announce_ctx_t));
    ctx->message        = build_game_announcement_message(config->game_announcement_template, game->title);
    ctx->only_when_live = config->only_when_live;

    state_free_twitch_configuration(&config);

    pthread_t thread;
    if (pthread_create(&thread, NULL, post_announcement_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[TwitchAnnouncer] Unable to start the posting thread");
        bfree(ctx->message);
        bfree(ctx);
        return;
    }

    pthread_detach(thread);
}

/**
 * @brief Build the message for mastering the current game (100% unlocked) and spawn the posting thread.
 */
static void announce_mastery(void) {

    twitch_configuration_t *config = state_get_twitch_configuration();

    if (!config->announce_mastery) {
        state_free_twitch_configuration(&config);
        return;
    }

    const game_t *game = monitoring_get_current_active_game();

    announce_ctx_t *ctx = bzalloc(sizeof(announce_ctx_t));
    ctx->message = build_game_announcement_message(config->mastery_announcement_template, game ? game->title : NULL);
    ctx->only_when_live = config->only_when_live;

    state_free_twitch_configuration(&config);

    pthread_t thread;
    if (pthread_create(&thread, NULL, post_announcement_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[TwitchAnnouncer] Unable to start the posting thread");
        bfree(ctx->message);
        bfree(ctx);
        return;
    }

    pthread_detach(thread);
}

/**
 * @brief Whether every achievement in the list is unlocked (and there is at least one).
 */
static bool is_fully_mastered(const achievement_t *achievements) {
    int total = count_achievements(achievements);
    return total > 0 && count_unlocked_achievements(achievements) == total;
}

static void reset_baseline(void) {
    free_achievement(&g_previous_snapshot);
    g_baseline_established = false;
    g_mastery_announced    = false;
}

static void on_game_played(const game_t *game) {
    reset_baseline();
    announce_game_change(game);
}

static bool was_previously_unlocked(const char *id) {
    for (const achievement_t *node = g_previous_snapshot; node; node = node->next) {
        if (node->id && id && strcmp(node->id, id) == 0) {
            return node->unlocked_timestamp != 0;
        }
    }
    return false;
}

/**
 * @brief Capture the baseline once the *real* achievement list has fully loaded.
 *
 * Fires after the achievements-changed event that carried the final list (see
 * monitoring_service.c: RetroAchievements only calls notify_session_ready()
 * once count > 0, skipping its own empty "loading placeholder" list; Xbox
 * fires it right after replacing the list with the fetched achievements).
 * Deliberately NOT done on the first achievements-changed event after a game
 * starts — RetroAchievements in particular first pushes an empty placeholder
 * list before the real one arrives, and treating that empty list as the
 * baseline caused every already-unlocked achievement in the real list to look
 * "new" once it arrived, flooding chat.
 */
static void on_session_ready(void) {
    const achievement_t *current = monitoring_get_current_game_achievements();

    free_achievement(&g_previous_snapshot);
    g_previous_snapshot    = copy_achievement(current);
    g_baseline_established = true;

    /* If the game is already 100% complete as of the baseline (achievements
     * earned in a previous session), that is history, not a fresh mastery —
     * suppress it the same way pre-existing unlocks are suppressed above. */
    g_mastery_announced = is_fully_mastered(current);
}

static void on_achievements_changed(void) {

    if (!g_baseline_established) {
        /* Still waiting for session-ready (the real list) — ignore placeholder
         * or partial updates so they never get captured as the baseline. */
        return;
    }

    const achievement_t *current = monitoring_get_current_game_achievements();

    for (const achievement_t *node = current; node; node = node->next) {
        if (node->unlocked_timestamp != 0 && !was_previously_unlocked(node->id)) {
            announce_unlock(node);
        }
    }

    if (!g_mastery_announced && is_fully_mastered(current)) {
        g_mastery_announced = true;
        announce_mastery();
    }

    free_achievement(&g_previous_snapshot);
    g_previous_snapshot = copy_achievement(current);
}

void twitch_achievement_announcer_start(void) {
    monitoring_subscribe_game_played(&on_game_played);
    monitoring_subscribe_session_ready(&on_session_ready);
    monitoring_subscribe_achievements_changed(&on_achievements_changed);
}
