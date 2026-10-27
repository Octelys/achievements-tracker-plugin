#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file vad.h
 * @brief A minimal, OBS-independent, RMS-based voice-activity detector.
 *
 * Buffers incoming 16 kHz mono float samples, tracks whether the signal is
 * currently "speech" (RMS above a threshold) or "silence", and reports an
 * utterance as ready once speech is followed by a configured amount of
 * trailing silence. Kept free of OBS/libobs dependencies so it can be
 * exercised directly by unit tests.
 */

/** Sample rate the detector (and downstream transcriber) operates at. */
#define VAD_SAMPLE_RATE_HZ 16000

typedef enum vad_event {
    /** No utterance is ready yet; keep feeding samples. */
    VAD_EVENT_NONE,
    /** Trailing silence closed an utterance; call vad_take_utterance() to retrieve it. */
    VAD_EVENT_UTTERANCE_READY,
} vad_event_t;

typedef struct vad_state vad_state_t;

/**
 * @brief Create a VAD state machine.
 *
 * @param rms_threshold      RMS level above which a chunk is considered speech (e.g. 0.01f).
 * @param silence_duration_ms Trailing silence, in milliseconds, that ends an utterance.
 * @return Newly allocated state. Caller must free with vad_free().
 */
vad_state_t *vad_create(float rms_threshold, int silence_duration_ms);

/** @brief Free a VAD state created with vad_create(). Safe to call with NULL. */
void vad_free(vad_state_t *state);

/**
 * @brief Feed a chunk of 16 kHz mono float samples into the detector.
 *
 * @param state         VAD state.
 * @param samples       Sample buffer.
 * @param sample_count  Number of samples in @p samples.
 * @return VAD_EVENT_UTTERANCE_READY if this call closed an utterance, VAD_EVENT_NONE otherwise.
 */
vad_event_t vad_push_samples(vad_state_t *state, const float *samples, size_t sample_count);

/**
 * @brief Take ownership of the utterance buffered up to the last VAD_EVENT_UTTERANCE_READY.
 *
 * Clears the internal buffer so the detector is ready for the next utterance.
 * Only meaningful to call right after vad_push_samples() returns
 * VAD_EVENT_UTTERANCE_READY.
 *
 * @param state          VAD state.
 * @param out_sample_count Receives the number of samples in the returned buffer.
 * @return Newly allocated sample buffer (caller must bfree()), or NULL if empty.
 */
float *vad_take_utterance(vad_state_t *state, size_t *out_sample_count);

#ifdef __cplusplus
}
#endif
