#include "speech_capture.h"
#include "vad.h"

#include <obs-module.h>
#include <media-io/audio-resampler.h>
#include <diagnostics/log.h>
#include <util/threading.h>

#include <string.h>

/** RMS level above which a resampled chunk is considered speech. */
#define SPEECH_CAPTURE_RMS_THRESHOLD 0.015f

static pthread_mutex_t             g_mutex   = PTHREAD_MUTEX_INITIALIZER;
static obs_source_t               *g_source  = NULL;
static audio_resampler_t          *g_resampler = NULL;
static vad_state_t                *g_vad     = NULL;
static speech_capture_utterance_cb g_callback = NULL;
static void                       *g_callback_param = NULL;

static void audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *audio_data,
				    bool muted) {
    UNUSED_PARAMETER(param);
    UNUSED_PARAMETER(source);

    if (muted || !audio_data || audio_data->frames == 0) {
        return;
    }

    pthread_mutex_lock(&g_mutex);

    if (!g_resampler || !g_vad) {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    uint8_t *resampled[MAX_AV_PLANES] = {0};
    uint32_t out_frames               = 0;
    uint64_t ts_offset                = 0;

    bool resampled_ok = audio_resampler_resample(g_resampler, resampled, &out_frames, &ts_offset,
                                                  (const uint8_t *const *)audio_data->data, audio_data->frames);

    if (resampled_ok && out_frames > 0) {
        vad_event_t event = vad_push_samples(g_vad, (const float *)resampled[0], out_frames);

        if (event == VAD_EVENT_UTTERANCE_READY && g_callback) {
            size_t sample_count = 0;
            float *utterance    = vad_take_utterance(g_vad, &sample_count);

            if (utterance && sample_count > 0) {
                g_callback(g_callback_param, utterance, sample_count);
            } else {
                bfree(utterance);
            }
        }
    }

    pthread_mutex_unlock(&g_mutex);
}

bool speech_capture_start(const char *source_name, int silence_duration_ms, speech_capture_utterance_cb callback,
			   void *param) {

    if (!source_name || source_name[0] == '\0' || !callback) {
        return false;
    }

    obs_source_t *source = obs_get_source_by_name(source_name);
    if (!source) {
        obs_log(LOG_WARNING, "[SpeechCapture] Audio source '%s' not found", source_name);
        return false;
    }

    struct obs_audio_info audio_info = {0};
    if (!obs_get_audio_info(&audio_info)) {
        obs_log(LOG_ERROR, "[SpeechCapture] Unable to read OBS audio settings");
        obs_source_release(source);
        return false;
    }

    struct resample_info src = {
        .samples_per_sec = audio_info.samples_per_sec,
        .format          = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers        = audio_info.speakers,
    };
    struct resample_info dst = {
        .samples_per_sec = VAD_SAMPLE_RATE_HZ,
        .format          = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers        = SPEAKERS_MONO,
    };

    audio_resampler_t *resampler = audio_resampler_create(&dst, &src);
    if (!resampler) {
        obs_log(LOG_ERROR, "[SpeechCapture] Unable to create audio resampler");
        obs_source_release(source);
        return false;
    }

    speech_capture_stop();

    pthread_mutex_lock(&g_mutex);
    g_source          = source;
    g_resampler       = resampler;
    g_vad             = vad_create(SPEECH_CAPTURE_RMS_THRESHOLD, silence_duration_ms);
    g_callback        = callback;
    g_callback_param  = param;
    pthread_mutex_unlock(&g_mutex);

    obs_source_add_audio_capture_callback(source, audio_capture_callback, NULL);

    return true;
}

void speech_capture_stop(void) {

    pthread_mutex_lock(&g_mutex);

    obs_source_t      *source    = g_source;
    audio_resampler_t *resampler = g_resampler;
    vad_state_t        *vad      = g_vad;

    g_source         = NULL;
    g_resampler      = NULL;
    g_vad            = NULL;
    g_callback       = NULL;
    g_callback_param = NULL;

    pthread_mutex_unlock(&g_mutex);

    if (source) {
        obs_source_remove_audio_capture_callback(source, audio_capture_callback, NULL);
        obs_source_release(source);
    }

    if (resampler) {
        audio_resampler_destroy(resampler);
    }

    vad_free(vad);
}
