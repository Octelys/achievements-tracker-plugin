#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OAuth/PKCE utilities.
 *
 * Typical output sizes (including NUL):
 *  - state:     33 bytes (32 chars + NUL)
 *  - verifier:  65 bytes (64 chars + NUL)
 *  - challenge: 128 bytes is plenty for S256 base64url output
 */

void oauth_random_state(char *out, size_t out_size);
void oauth_pkce_verifier(char *out, size_t out_size);
void oauth_pkce_challenge_s256(const char *verifier, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

