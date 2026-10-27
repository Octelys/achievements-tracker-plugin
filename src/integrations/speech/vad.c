#include "vad.h"

#include <obs-module.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

struct vad_state {
    float  rms_threshold;
    int    silence_duration_ms;
    bool   speaking;
    int    silence_ms;
    float *buffer;
    size_t buffer_len;
    size_t buffer_cap;
};

vad_state_t *vad_create(float rms_threshold, int silence_duration_ms) {

    vad_state_t *state = bzalloc(sizeof(vad_state_t));

    state->rms_threshold      = rms_threshold;
    state->silence_duration_ms = silence_duration_ms;

    return state;
}

void vad_free(vad_state_t *state) {

    if (!state) {
        return;
    }

    bfree(state->buffer);
    bfree(state);
}

/** @brief Ensure the internal buffer can hold at least @p needed_len samples. */
static void ensure_capacity(vad_state_t *state, size_t needed_len) {

    if (needed_len <= state->buffer_cap) {
        return;
    }

    size_t new_cap = state->buffer_cap ? state->buffer_cap * 2 : 4096;
    while (new_cap < needed_len) {
        new_cap *= 2;
    }

    state->buffer     = brealloc(state->buffer, new_cap * sizeof(float));
    state->buffer_cap = new_cap;
}

static void append_samples(vad_state_t *state, const float *samples, size_t sample_count) {

    ensure_capacity(state, state->buffer_len + sample_count);

    for (size_t i = 0; i < sample_count; i++) {
        state->buffer[state->buffer_len + i] = samples[i];
    }

    state->buffer_len += sample_count;
}

static float compute_rms(const float *samples, size_t sample_count) {

    if (sample_count == 0) {
        return 0.0f;
    }

    double sum_squares = 0.0;
    for (size_t i = 0; i < sample_count; i++) {
        sum_squares += (double)samples[i] * (double)samples[i];
    }

    return (float)sqrt(sum_squares / (double)sample_count);
}

vad_event_t vad_push_samples(vad_state_t *state, const float *samples, size_t sample_count) {

    if (!state || !samples || sample_count == 0) {
        return VAD_EVENT_NONE;
    }

    bool is_speech_chunk = compute_rms(samples, sample_count) >= state->rms_threshold;
    int  chunk_ms        = (int)((sample_count * 1000) / VAD_SAMPLE_RATE_HZ);

    if (is_speech_chunk) {
        state->speaking    = true;
        state->silence_ms  = 0;
        append_samples(state, samples, sample_count);
        return VAD_EVENT_NONE;
    }

    if (!state->speaking) {
        /* Leading silence before any speech has been seen — ignore it. */
        return VAD_EVENT_NONE;
    }

    append_samples(state, samples, sample_count);
    state->silence_ms += chunk_ms;

    if (state->silence_ms >= state->silence_duration_ms) {
        state->speaking = false;
        return VAD_EVENT_UTTERANCE_READY;
    }

    return VAD_EVENT_NONE;
}

float *vad_take_utterance(vad_state_t *state, size_t *out_sample_count) {

    if (!state || state->buffer_len == 0) {
        if (out_sample_count) {
            *out_sample_count = 0;
        }
        return NULL;
    }

    float *out = bzalloc(state->buffer_len * sizeof(float));
    for (size_t i = 0; i < state->buffer_len; i++) {
        out[i] = state->buffer[i];
    }

    if (out_sample_count) {
        *out_sample_count = state->buffer_len;
    }

    state->buffer_len = 0;
    state->silence_ms = 0;

    return out;
}
