#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interactive sign-in flow for Xbox Live:
 *  - Starts a localhost loopback listener
 *  - Opens the user's browser to Microsoft sign-in
 *  - Exchanges the resulting authorization code for a Microsoft access token
 *  - Requests XBL token and then XSTS token
 *
 * Parameters:
 *  - client_id: Azure app client id
 *  - scope: OAuth scopes (e.g. "XboxLive.signin offline_access")
 *
 * Outputs:
 *  - out_uhs: user hash (allocated; caller must bfree)
 *  - out_xsts_token: XSTS token (allocated; caller must bfree)
 *
 * Returns:
 *  - true on success, false otherwise.
 */
bool xbox_auth_interactive_get_xsts(
	const char *client_id,
	const char *scope,
	char **out_uhs,
	char **out_xsts_token);

#ifdef __cplusplus
}
#endif

