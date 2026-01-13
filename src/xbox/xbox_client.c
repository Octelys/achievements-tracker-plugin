#include "xbox_client.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "net/http/http.h"
#include "net/json/json.h"

#define XBOX_PROFILE_SETTINGS_ENDPOINT_FMT "https://profile.xboxlive.com/users/batch/profile/settings"
#define XBOX_PROFILE_CONTRACT_VERSION      "2"
#define GAMERSCORE_SETTING                 "Gamerscore"

/*
 *
 */
bool xbox_fetch_gamerscore(int64_t *out_gamerscore) {
    if (!out_gamerscore) {
        return false;
    }

    /*
     * Retrieves the user's xbox identity
     */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        return false;
    }

    bool  result          = false;
    char *json            = NULL;
    char *gamerscore_text = NULL;
    char *end             = NULL;

    /*
     * Creates the request
     */
    char json_body[4096];
    snprintf(json_body,
             sizeof(json_body),
             "{\"userIds\":[\"%s\"],\"settings\":[\"%s\"]}",
             identity->xid,
             GAMERSCORE_SETTING);

    obs_log(LOG_INFO, "Body: %s", json_body);

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n",
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_INFO, "Headers: %s", headers);

    /*
     * Sends the request
     */
    long http_code = 0;
    json           = http_post(XBOX_PROFILE_SETTINGS_ENDPOINT_FMT, json_body, headers, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received status code %d", http_code);
        goto cleanup;
    }

    if (!json) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received no response");
        goto cleanup;
    }

    /*
     * Extracts the gamerscore from the response
     */
    gamerscore_text = json_read_string(json, "value");

    if (!gamerscore_text) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: unable to read the value");
        goto cleanup;
    }

    long long gamerscore = strtoll(gamerscore_text, &end, 10);

    result = end && end != gamerscore_text;

    if (result) {
        *out_gamerscore = (int64_t)gamerscore;
    }

cleanup:
    FREE(json);
    FREE(gamerscore_text);
    /*FREE(end);*/

    return result;
}

void get_presence(void) {
    /* TODO: implement presence retrieval. */
    // const char *xid = get_xid();
    // obs_log(LOG_DEBUG, "xid=%s", xid ? xid : "(null)");
}

char *xbox_fetch_achievements_json(long *out_http_code) {
    if (out_http_code)
        *out_http_code = 0;

    return NULL;
    // const char *xid  = get_xid();
    // const char *auth = get_xsts_token();
    // if (!xid || !*xid || !auth || !*auth) {
    //     obs_log(LOG_WARNING, "Missing XUID/XSTS token; user not signed in?");
    //     return NULL;
    // }
    //
    // char url[512];
    // snprintf(url,
    //          sizeof(url),
    //          "https://achievements.xboxlive.com/users/xuid(%s)/achievements?titleId=1879711255&maxItems=1000",
    //          xid);
    //
    // char headers[2048];
    // snprintf(headers,
    //          sizeof(headers),
    //          "Authorization: %s\n"
    //          "Accept: application/json\n"
    //          "Accept-Language: en-CA\n"
    //          "Content-Type: application/json\n"
    //          "x-xbl-contract-version: 2\n",
    //          auth);
    //
    // long  code = 0;
    // char *json = http_get(url, headers, NULL, &code);
    // if (out_http_code)
    //     *out_http_code = code;
    //
    // if (!json) {
    //     obs_log(LOG_WARNING, "Achievements GET failed (no response)");
    //     return NULL;
    // }
    // if (code < 200 || code >= 300) {
    //     obs_log(LOG_WARNING, "Achievements GET HTTP %ld: %s", code, json);
    //     /* Return body to caller anyway for debugging */
    // }
    //
    // return json;
}

char *xbox_fetch_presence_json(long *out_http_code) {

    if (out_http_code)
        *out_http_code = 0;

    /*
    const char *xid  = get_xid();
    const char *auth = get_xsts_token();

    if (!xid || !*xid || !auth || !*auth) {
        obs_log(LOG_WARNING, "Missing XUID/XSTS token; user not signed in?");
        return NULL;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://userpresence.xboxlive.com/users/xuid(%s)/richpresence", xid);

    char headers[2048];
    snprintf(headers,
             sizeof(headers),
             "Authorization: %s\n"
             "Accept: application/json\n"
             "Accept-Language: en-CA\n"
             "Content-Type: application/json\n"
             "x-xbl-contract-version: 2\n",
             auth);

    long  code = 0;
    char *json = http_get(url, headers, NULL, &code);
    if (out_http_code)
        *out_http_code = code;

    obs_log(LOG_WARNING, "Presence %s", json);

    if (!json) {
        obs_log(LOG_WARNING, "Presence GET failed (no response)");
        return NULL;
    }

    if (code < 200 || code >= 300)
        obs_log(LOG_WARNING, "Presence GET HTTP %ld: %s", code, json);
    return json;*/
    return NULL;
}

/*
*xuid": "2533274953419891",
"state": "Online",
"devices": [
{
"type": "Scarlett",
"titles": [
{
"id": "750323071",
"name": "Home",
"placement": "Background",
"state": "Active",
"lastModified": "2026-01-05T01:52:35.0621013"
},
{
"id": "1879711255",
"name": "The Outer Worlds 2",
"placement": "Full",
"state": "Active",
"lastModified": "2026-01-05T01:52:35.0621013"
}
]
},*/
