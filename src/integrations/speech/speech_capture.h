#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file speech_capture.h
 * @brief Taps a named OBS audio source and reports complete spoken utterances.
 *
 * Resamples the source's audio to 16 kHz mono (see vad.h) and runs it through
 * a simple voice-activity detector; when a pause closes an utterance, the
 * configured callback is invoked with the buffered samples.
 */

/**
 * @brief Called once per detected utterance.
 *
 * Invoked directly from the OBS audio thread — implementations must not
 * block here; hand the buffer off to a worker thread instead (see
 * integrations/twitch/speech_announcer.c).
 *
 * @param param        Opaque context passed to speech_capture_start().
 * @param samples      16 kHz mono float samples for the utterance. Ownership
 *                     transfers to the callback, which must free it with bfree().
 * @param sample_count Number of samples in @p samples.
 */
typedef void (*speech_capture_utterance_cb)(void *param, float *samples, size_t sample_count);

/**
 * @brief Start capturing audio from the named source and detecting utterances.
 *
 * Safe to call again after speech_capture_stop(); replaces any prior capture.
 *
 * @param source_name         Name of an existing OBS source to tap (e.g. a mic input).
 * @param silence_duration_ms Trailing silence, in milliseconds, that closes an utterance.
 * @param callback            Invoked with each completed utterance's samples.
 * @param param               Opaque context passed back to @p callback.
 * @return true if the source was found and capture was wired up, false otherwise.
 */
bool speech_capture_start(const char *source_name, int silence_duration_ms, speech_capture_utterance_cb callback,
			   void *param);

/** @brief Stop capturing audio and release the tapped source. Safe to call when not started. */
void speech_capture_stop(void);

#ifdef __cplusplus
}
#endif
