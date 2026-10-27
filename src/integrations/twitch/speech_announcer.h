#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file speech_announcer.h
 * @brief Captures microphone speech, transcribes it locally, and posts it to Twitch chat.
 */

/**
 * @brief Start capturing and announcing speech, per the persisted configuration.
 *
 * No-op if speech-to-text is disabled or no audio source is configured; call
 * again (e.g. after a config change) to pick up a new source/setting —
 * automatically stops any prior capture first.
 */
void twitch_speech_announcer_start(void);

/** @brief Stop capturing audio and release resources. Safe to call when not started. */
void twitch_speech_announcer_stop(void);

#ifdef __cplusplus
}
#endif
