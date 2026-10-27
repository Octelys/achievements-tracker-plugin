#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file whisper_transcriber.h
 * @brief Thin C wrapper around whisper.cpp for local speech-to-text.
 */

/**
 * @brief Load the whisper model from disk.
 *
 * @param model_path Path to a ggml whisper model file.
 * @return true on success. Safe to call again to reload a different model
 *         (frees any previously loaded one first).
 */
bool whisper_transcriber_init(const char *model_path);

/** @brief Release the loaded model. Safe to call when not initialized. */
void whisper_transcriber_shutdown(void);

/**
 * @brief Transcribe a buffer of 16 kHz mono float samples.
 *
 * Blocking — runs inference synchronously; call off any latency-sensitive thread.
 *
 * @param samples      16 kHz mono float samples.
 * @param sample_count Number of samples in @p samples.
 * @return Newly allocated, trimmed transcription text (caller must bfree()),
 *         or NULL if not initialized, the input was empty, or nothing was recognized.
 */
char *whisper_transcriber_transcribe(const float *samples, size_t sample_count);

#ifdef __cplusplus
}
#endif
