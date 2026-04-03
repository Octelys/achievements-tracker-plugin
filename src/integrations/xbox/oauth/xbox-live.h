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
 * @param data     Opaque pointer passed back to @p callback.
 * @param callback Function invoked on completion (must be non-NULL).
 *
 * @return true if the authentication flow was started successfully;
 *         false otherwise.
 */
bool xbox_live_authenticate(void *data, on_xbox_live_authenticated_t callback);

/**
 * @brief Get the currently persisted Xbox identity (if any).
 *
 * This is a convenience accessor that returns the Xbox identity data previously
 * obtained during authentication.
 *
 * Ownership/lifetime:
 *  - The returned pointer is owned by the caller.
 *  - The exact allocation/free contract depends on the implementation. In the
 *    current codebase, identity objects are typically allocated and returned by
 *    the state subsystem (see state_get_xbox_identity()), and should be freed by
 *    the caller when no longer needed.
 *
 * @return An xbox_identity_t on success, or NULL if no identity is available.
 */
xbox_identity_t *xbox_live_get_identity(void);

#ifdef __cplusplus
}
#endif
