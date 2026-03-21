#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file util.h
 * @brief OAuth / PKCE helper utilities.
 *
 * This module provides small helpers used during OAuth device/code flows and
 * PKCE (Proof Key for Code Exchange).
 *
 * All functions write NUL-terminated ASCII strings into caller-provided buffers.
 * If the provided buffer is too small, the function will write an empty string
 * (or at minimum ensure NUL-termination) and return.
 *
 * Typical output sizes (including the trailing NUL):
 *  - state:     33 bytes (32 chars + NUL)
 *  - verifier:  65 bytes (64 chars + NUL)
 *  - challenge: 128 bytes is ample for S256 base64url output
 */

/**
 * @brief Generate a cryptographically-random OAuth "state" value.
 *
 * The state is used to correlate request/response and protect against CSRF.
 * The output is an ASCII string suitable for use as the OAuth `state` parameter.
 *
 * @param out      Output buffer.
 * @param out_size Size of @p out in bytes. Recommended: at least 33.
 */
void oauth_random_state(char *out, size_t out_size);

/**
 * @brief Generate a PKCE code verifier.
 *
 * The verifier is a high-entropy string used as the PKCE `code_verifier`.
 *
 * @param out      Output buffer.
 * @param out_size Size of @p out in bytes. Recommended: at least 65.
 */
void oauth_pkce_verifier(char *out, size_t out_size);

/**
 * @brief Compute the PKCE S256 code challenge for a given verifier.
 *
 * This computes the `code_challenge` as:
 *  - SHA-256(verifier)
 *  - base64url encoding, without '=' padding
 *
 * @param verifier PKCE verifier string (NUL-terminated).
 * @param out      Output buffer.
 * @param out_size Size of @p out in bytes. Recommended: 128.
 */
void oauth_pkce_challenge_s256(const char *verifier, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
