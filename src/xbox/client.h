#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void get_presence(void);

/*
 * Fetch achievements for a given titleId using the persisted XSTS token + XUID.
 * Returns a newly allocated JSON string (bfree() it) or NULL on error.
 */
char *xbox_fetch_achievements_json(long *out_http_code);

/* Fetch user presence for the stored XUID. Returns JSON string (bfree) or NULL.
 */
char *xbox_fetch_presence_json(long *out_http_code);

#ifdef __cplusplus
}
#endif
