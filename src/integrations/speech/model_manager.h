#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file model_manager.h
 * @brief Resolves and downloads the local whisper.cpp speech model.
 */

/**
 * @brief Path to the local whisper model file, downloaded or not.
 *
 * @return Newly allocated path (caller must bfree()).
 */
char *speech_model_get_path(void);

/** @brief Whether the model file has already been downloaded. */
bool speech_model_is_downloaded(void);

/**
 * @brief Download the whisper model file if it isn't already present.
 *
 * Synchronous — performs a blocking HTTP download; call off the UI thread.
 *
 * @return true if the model is present after this call (already there, or
 *         successfully downloaded), false on failure.
 */
bool speech_model_ensure_downloaded(void);

#ifdef __cplusplus
}
#endif
