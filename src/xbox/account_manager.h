#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts Xbox monitoring if a persisted identity is already available.
 */
void xbox_account_start_monitoring_if_needed(void);

/**
 * @brief Start the Xbox authentication flow.
 *
 * This launches the existing asynchronous Xbox Live device-code flow.
 *
 * @return true if the flow could be started; false otherwise.
 */
bool xbox_account_sign_in(void);

/**
 * @brief Sign the current Xbox user out and stop monitoring.
 */
void xbox_account_sign_out(void);

/**
 * @brief Returns whether an Xbox identity is currently stored.
 */
bool xbox_account_is_signed_in(void);

/**
 * @brief Formats the current Xbox account status into the provided buffer.
 *
 * The buffer always receives a NUL-terminated string when @p buffer_size is
 * greater than zero.
 */
void xbox_account_get_status_text(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
