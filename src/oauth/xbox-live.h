#pragma once

#include <stdbool.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file xbox-live.h
 * @brief Xbox Live authentication flow entry point.
 *
 * This module drives the Xbox Live authentication flow using the provided
 * emulated device identity (UUID/serial/keypair) and returns the result via an
 * asynchronous callback.
 */

/**
 * @brief Callback invoked when Xbox Live authentication completes.
 *
 * The callback is invoked by the authentication flow once the user completes the
 * login/verification steps and the plugin has obtained the required tokens.
 *
 * @param data Opaque user pointer provided to xbox_live_authenticate().
 */
typedef void (*on_xbox_live_authenticated_t)(void *data);

/**
 * @brief Start the Xbox Live authentication flow.
 *
 * This function initiates the authentication process (typically involving an
 * external browser and polling until completion). Completion is reported via
 * @p callback.
 *
 * @note The function is non-blocking from the perspective of the caller; it
 *       returns whether the flow could be started.
 *
 * @param device   Device identity used for authentication (must remain valid for
 *                 the duration of the flow).
 * @param data     Opaque pointer passed back to @p callback.
 * @param callback Function invoked on completion (must be non-NULL).
 *
 * @return true if the authentication flow was started successfully;
 *         false otherwise.
 */
bool xbox_live_authenticate(const device_t *device, void *data, on_xbox_live_authenticated_t callback);

#ifdef __cplusplus
}
#endif
