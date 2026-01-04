/*
Plugin Name

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

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

