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
 * Open a URL in the user's default browser.
 *
 * Returns true if the launch command was executed successfully, false otherwise.
 *
 * Note: "success" here means the OS command was invoked; it doesn't guarantee
 * the user completed authentication.
 */
bool plugin_open_url_in_browser(const char *url);

#ifdef __cplusplus
}
#endif

