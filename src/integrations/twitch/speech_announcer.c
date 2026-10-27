#include "integrations/twitch/speech_announcer.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include <string.h>

#include "io/state.h"
#include "integrations/speech/speech_capture.h"
#include "integrations/speech/model_manager.h"
#include "integrations/speech/whisper_transcriber.h"
#include "integrations/twitch/chat.h"
#include "integrations/twitch/account_manager.h"
#include "integrations/twitch/stream_status.h"
#include "integrations/twitch/oauth/twitch-oauth.h"

/** Context handed to the detached transcribing/posting thread; freed at the end of that thread. */
typedef struct speech_announce_ctx {
    float  *samples;
    size_t  sample_count;
    char   *message_template;
    bool    only_when_live;
} speech_announce_ctx_t;

/**
 * @brief Replace every occurrence of {text} in @p input with @p replacement.
 *
 * A small, single-placeholder counterpart to achievement_announcer.c's
 * replace_all — kept local since each announcer builds its own message shape.
 */
static char *build_speech_message(const char *message_template, const char *text) {

    const char *needle     = "{text}";
    size_t      needle_len = strlen(needle);

    size_t      count = 0;
    const char *scan  = message_template;
    while ((scan = strstr(scan, needle)) != NULL) {
        count++;
        scan += needle_len;
    }

    if (count == 0) {
        return bstrdup(message_template);
    }

    size_t replacement_len = strlen(text);
    size_t out_size        = strlen(message_template) + count * replacement_len + 1;
    char  *out             = bzalloc(out_size);

    const char *cursor = message_template;
    char       *write  = out;
    while ((scan = strstr(cursor, needle)) != NULL) {
        size_t prefix_len = (size_t)(scan - cursor);
        memcpy(write, cursor, prefix_len);
        write += prefix_len;
        memcpy(write, text, replacement_len);
        write += replacement_len;
        cursor = scan + needle_len;
    }
    strcpy(write, cursor);

    return out;
}

/**
 * @brief Detached-thread entry point: transcribes the utterance and posts it to chat.
 *
 * Runs off the OBS audio thread that detected the utterance — both whisper
 * inference and the chat POST are far too slow to run inline there.
 */
static void *transcribe_and_post_thread(void *param) {

    speech_announce_ctx_t *ctx = param;

    if (!twitch_account_is_signed_in()) {
        obs_log(LOG_DEBUG, "[SpeechAnnouncer] No Twitch account signed in, discarding utterance");
        goto cleanup;
    }

    if (ctx->only_when_live) {
        twitch_identity_t *identity = twitch_get_identity();
        bool               live     = identity && twitch_is_stream_live(identity->user_id);
        free_twitch_identity(&identity);

        if (!live) {
            obs_log(LOG_DEBUG, "[SpeechAnnouncer] Channel not live, discarding utterance");
            goto cleanup;
        }
    }

    {
        char *text = whisper_transcriber_transcribe(ctx->samples, ctx->sample_count);

        if (text && text[0] != '\0') {
            char *message = build_speech_message(ctx->message_template, text);
            twitch_send_chat_message(message);
            bfree(message);
        }

        bfree(text);
    }

cleanup:
    bfree(ctx->samples);
    bfree(ctx->message_template);
    bfree(ctx);
    return NULL;
}

static void on_utterance_ready(void *param, float *samples, size_t sample_count) {
    UNUSED_PARAMETER(param);

    speech_to_text_configuration_t *config = state_get_speech_to_text_configuration();

    if (!config->enabled) {
        state_free_speech_to_text_configuration(&config);
        bfree(samples);
        return;
    }

    speech_announce_ctx_t *ctx = bzalloc(sizeof(speech_announce_ctx_t));
    ctx->samples               = samples;
    ctx->sample_count          = sample_count;
    ctx->message_template      = bstrdup(config->message_template);
    ctx->only_when_live        = config->only_when_live;

    state_free_speech_to_text_configuration(&config);

    pthread_t thread;
    if (pthread_create(&thread, NULL, transcribe_and_post_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[SpeechAnnouncer] Unable to start the transcribe/post thread");
        bfree(ctx->samples);
        bfree(ctx->message_template);
        bfree(ctx);
        return;
    }

    pthread_detach(thread);
}

void twitch_speech_announcer_start(void) {

    twitch_speech_announcer_stop();

    speech_to_text_configuration_t *config = state_get_speech_to_text_configuration();

    if (!config->enabled || !config->audio_source_name || config->audio_source_name[0] == '\0') {
        state_free_speech_to_text_configuration(&config);
        return;
    }

    if (!speech_model_is_downloaded()) {
        obs_log(LOG_WARNING, "[SpeechAnnouncer] Speech model not downloaded yet; not starting capture");
        state_free_speech_to_text_configuration(&config);
        return;
    }

    char *model_path  = speech_model_get_path();
    bool  model_loaded = whisper_transcriber_init(model_path);
    bfree(model_path);

    if (!model_loaded) {
        state_free_speech_to_text_configuration(&config);
        return;
    }

    speech_capture_start(config->audio_source_name, config->silence_duration_ms, on_utterance_ready, NULL);

    state_free_speech_to_text_configuration(&config);
}

void twitch_speech_announcer_stop(void) {
    speech_capture_stop();
    whisper_transcriber_shutdown();
}
