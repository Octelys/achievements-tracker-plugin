#include "oauth/xbox-live.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "net/json/json.h"
#include "crypto/crypto.h"
#include "encoding/base64.h"
#include "io/state.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOKEN_ENDPOINT "https://login.live.com/oauth20_token.srf"
#define AUTHORIZE_ENDPOINT "https://login.live.com/oauth20_authorize.srf"
#define CONNECT_ENDPOINT "https://login.live.com/oauth20_connect.srf"
#define REGISTER_ENDPOINT "https://login.live.com/oauth20_remoteconnect.srf?otc="
#define GRANT_TYPE "urn:ietf:params:oauth:grant-type:device_code"
#define XBOX_LIVE_AUTHENTICATE "https://user.auth.xboxlive.com/user/authenticate"
#define DEVICE_AUTHENTICATE "https://device.auth.xboxlive.com/device/authenticate"
#define SISU_AUTHENTICATE "https://sisu.xboxlive.com/authorize"
#define TITLE_AUTHENTICATE "https://title.auth.xboxlive.com/title/authenticate"

#define CLIENT_ID "000000004c12ae6f"
#define SCOPE "service::user.auth.xboxlive.com::MBI_SSL"

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}
#else
#include <unistd.h>
static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}
#endif

#define CLEAR(p)	\
	if (p)			\
		p = NULL;

#define COPY_OR_FREE(src, dst)	\
	if (dst)					\
		*dst = src;				\
	else						\
		bfree(src);

/* */
typedef struct authentication_ctx {
    /* Input parameters */
    const device_t *device;

    char *device_code;
    long  interval_in_seconds;
    long  sleep_time;
    long  expires_in_seconds;

    pthread_t thread;

    xbox_live_authenticate_result_t result;

    /* Returned values */
    token_t *user_token;
    token_t *device_token;
    token_t *sisu_token;

} authentication_ctx_t;

/*	********************************************************************************************************************
    Finally retrieve the xsts token
    *******************************************************************************************************************/

/*
static void retrieve_xsts_token(
    struct device_flow_ctx *ctx
)
{
    char json_body[8192];
    snprintf(
        json_body,
        sizeof(json_body),
        "{"
        "\"AccessToken\":\"t=%s\","
        "\"AppId\":\"%s\","
        "\"DeviceToken\":\"%s\","
        "\"Sandbox\":\"RETAIL\","
        "\"UseModernGamertag\":true,"
        "\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RelyingParty\":\"http://xboxlive.com\"}"
        "}",
        ctx->xbox_live_token,
        CLIENT_ID,
        ctx->device_token,
    );

    long http_code = 0;
    char *xsts_json = http_post_json(
        SISU_AUTHORIZE,
        json_body,
        NULL,
        &http_code
    );

    if (http_code < 200 || http_code >= 300) {
        obs_log(4, "SISU authentication failed. Received status code: %sd",
http_code); bfree(xbl_json); return;
    }

    if (!xsts_json) {
        obs_log(4, "SISU authentication failed (no response)");
        return;
    }

    char *sisu_token = json_get_string_value(xsts_json, "Token");

    if (!sisu_token) {
        obs_log(4, "Could not parse XBL token");
        obs_log(3, "XBL response: %s", xsts_json);
        bfree(xsts_json);
        bfree(sisu_token);
        return;
    }

    obs_log(2, "SISU authentication succeeded");

    strncpy(ctx->sisu_token, sisu_token, sizeof(ctx->sisu_token) - 1);
    ctx->sisu_token[sizeof(ctx->sisu_token) - 1] = '\0';
    bfree(sisu_token);
}
*/
/**
 * Retrieves the title token
 *
 * @param ctx
 */
static void retrieve_sisu_token(struct authentication_ctx *ctx) {

    char json_body[16384];
    snprintf(json_body,
             sizeof(json_body),
             "{\"AccessToken\":\"t=%s\",\"AppId\":\"%s\",\"DeviceToken\":\"%s\",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true},\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://xboxlive.com\",\"ProofKey\":\"%s\"}",
             ctx->user_token->value,
             CLIENT_ID,
             ctx->device_token->value,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_INFO, "Body: %s", json_body);

    size_t   signature_len = 0;
    uint8_t *signature     = crypto_sign(ctx->device->keys, SISU_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a sisu token: signing failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    char *signature_b64 = base64_encode(signature, signature_len);

    if (!signature_b64) {
        ctx->result.error_message = "Unable retrieve a sisu token: encoding of the signature failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    obs_log(LOG_INFO, "Signature (base64): %s", signature_b64);

    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             signature_b64);

    obs_log(LOG_INFO, "Sending request for sisu token: %s", json_body);

    long  http_code       = 0;
    char *sisu_token_json = http_post(SISU_AUTHENTICATE, json_body, extra_headers, &http_code);

    bfree(signature_b64);

    if (!sisu_token_json) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a sisu token: received status code '%d'", http_code);
        bfree(sisu_token_json);
        return;
    }

    obs_log(LOG_INFO, "Response\n%s", sisu_token_json);

    char *sisu_token = json_read_string_from_path(sisu_token_json, "AuthorizationToken.Token");

    if (!sisu_token) {
        ctx->result.error_message = "Unable to retrieve a sisu token: no token found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        return;
    }

    char *xid = json_read_string(sisu_token_json, "xid");

    if (!xid) {
        ctx->result.error_message = "Unable to retrieve the xid: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        return;
    }

    char *uhs = json_read_string(sisu_token_json, "uhs");

    if (!uhs) {
        ctx->result.error_message = "Unable to retrieve the uhs: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        bfree(xid);
        return;
    }

    char *not_after = json_read_string(sisu_token_json, "NotAfter");

    if (!not_after) {
        ctx->result.error_message = "Unable to retrieve the NotAfter: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        bfree(xid);
        bfree(uhs);
        return;
    }

    char *gtg = json_read_string(sisu_token_json, "gtg");

    if (!gtg) {
        ctx->result.error_message = "Unable to retrieve the gtg: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        return;
    }

    obs_log(LOG_INFO, "sisu authentication succeeded!");

    obs_log(LOG_INFO, "gtg: %s\n", gtg);
    obs_log(LOG_INFO, "XID: %s\n", xid);
    obs_log(LOG_INFO, "Hash: %s\n", uhs);

    token_t *sisu = (token_t *)bzalloc(sizeof(token_t));
    sisu->value   = bstrdup_n(sisu_token, strlen(sisu_token));

    state_set_sisu_token(sisu);

    ctx->sisu_token = sisu;

    bfree(sisu_token_json);
    bfree(sisu_token);
    bfree(xid);
    bfree(uhs);
    bfree(not_after);
}

/**
 * Retrieves the title token
 *
 * @param ctx
 */
/*
static void retrieve_title_token(struct authentication_ctx *ctx) {

    char json_body[16384];
    snprintf(json_body,
             sizeof(json_body),
             "{\"Properties\":{\"AuthMethod\":\"RPS\",\"DeviceToken\":\"%s\",\"RpsTicket\":\"t=%s\",\"SiteName\":\"user.auth.xboxlive.com\",\"ProofKey\":%s},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
             ctx->device_token->value,
             ctx->user_token->value,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_INFO, "Body: %s", json_body);

    size_t   signature_len = 0;
    uint8_t *signature     = crypto_sign(ctx->device->keys, TITLE_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a title token: signing failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    char *signature_b64 = base64_encode(signature, signature_len);

    if (!signature_b64) {
        ctx->result.error_message = "Unable retrieve a title token: encoding of the signature failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    obs_log(LOG_INFO, "Signature (base64): %s", signature_b64);

    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             signature_b64);

    obs_log(LOG_INFO, "Sending request for title token: %s", json_body);

    long  http_code        = 0;
    char *title_token_json = http_post(TITLE_AUTHENTICATE, json_body, extra_headers, &http_code);

    bfree(signature_b64);

    if (!title_token_json) {
        ctx->result.error_message = "Unable to retrieve a title token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a title token: received error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a title token: received status code '%d'", http_code);
        bfree(title_token_json);
        return;
    }

    char *title_token = json_read_string(title_token_json, "Token");

    if (!title_token) {
        ctx->result.error_message = "Unable to retrieve a title token: no token found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(title_token_json);
        return;
    }

    //	TODO
    //	Need to retrieve the NotAfter datetime
    //	"IssueInstant": "2026-01-11T01:20:09.7849404Z",
    //	"NotAfter": "2026-01-25T01:20:09.7849404Z",

    obs_log(LOG_INFO, "Title authentication succeeded: %s", title_token);

    strncpy(ctx->title_token, title_token, sizeof(ctx->title_token) - 1);
    ctx->title_token[sizeof(ctx->title_token) - 1] = '\0';
    bfree(title_token);
}
*/

/**
 * Retrieves the device token
 *
 * @param ctx
 */
static void retrieve_device_token(struct authentication_ctx *ctx) {

    token_t *existing_device_token = state_get_device_token();

    if (existing_device_token) {
        obs_log(LOG_INFO, "Using cached device token");
        ctx->device_token = existing_device_token;
        retrieve_sisu_token(ctx);
        return;
    }

    obs_log(LOG_INFO, "No device token cached found");

    char json_body[8192];
    snprintf(json_body,
             sizeof(json_body),
             "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{%s}\",\"DeviceType\":\"iOS\",\"SerialNumber\":\"{%s}\",\"Version\":\"1.0.0\",\"ProofKey\":%s},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
             ctx->device->uuid,
             ctx->device->serial_number,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_INFO, "Requesting device token: %s", json_body);

    size_t   signature_len = 0;
    uint8_t *signature     = crypto_sign(ctx->device->keys, DEVICE_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a device token: signing failed";
        obs_log(LOG_ERROR, "Unable to sign the request for a device token");
        return;
    }

    char *encoded_signature = base64_encode(signature, signature_len);

    if (!encoded_signature) {
        ctx->result.error_message = "Unable retrieve a device token: signature encoding failed";
        obs_log(LOG_ERROR, "Unable to base64-encode the request signature");
        return;
    }

    obs_log(LOG_INFO, "Encoded signature: %s", encoded_signature);

    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             encoded_signature);

    long  http_code         = 0;
    char *device_token_json = http_post(DEVICE_AUTHENTICATE, json_body, extra_headers, &http_code);

    bfree(encoded_signature);

    if (!device_token_json) {
        ctx->result.error_message = "Unable retrieve a device token: server returned no response";
        obs_log(LOG_ERROR, "Device authentication failed (no response)");
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable retrieve a device token: server returned an error";
        obs_log(LOG_ERROR, "Device authentication failed. Received status code: %d", http_code);
        bfree(device_token_json);
        return;
    }

    char *token = json_read_string(device_token_json, "Token");

    if (!token) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the token from the response";
        obs_log(LOG_ERROR, "Unable parse the device token from the response: %s", device_token_json);
        bfree(device_token_json);
        return;
    }

    //	TODO
    //	Need to retrieve the NotAfter datetime
    //	"IssueInstant": "2026-01-11T01:20:09.7849404Z",
    //	"NotAfter": "2026-01-25T01:20:09.7849404Z",

    obs_log(LOG_INFO, "Device authentication succeeded!");

    token_t *device = (token_t *)bzalloc(sizeof(token_t));
    device->value   = bstrdup_n(token, strlen(token));

    state_set_device_token(device);

    bfree(token);

    ctx->device_token = device;

    retrieve_sisu_token(ctx);
}

/*
static void retrieve_xbox_token(struct authentication_ctx *ctx) {

    char json_body[8192];
    snprintf(json_body,
             sizeof(json_body),
             "{\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\",\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"t=%s\"}}",
             ctx->user_token);

    long  http_code = 0;
    char *xbl_json  = http_post_json(XBOX_LIVE_AUTHENTICATE, json_body, NULL, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(4, "Xbox live authentication failed. Received status code: %sd", http_code);
        bfree(xbl_json);
        return;
    }

    if (!xbl_json) {
        obs_log(4, "Xbox live authentication failed (no response)");
        return;
    }

    obs_log(2, "Xbox live authentication succeeded");

    char *xbl_token = json_read_string(xbl_json, "Token");

    if (!xbl_token) {
        obs_log(4, "Could not parse XBL token");
        obs_log(3, "XBL response: %s", xbl_json);
        bfree(xbl_json);
        bfree(xbl_token);
        return;
    }

    strncpy(ctx->xbox_live_token, xbl_token, sizeof(ctx->xbox_live_token) - 1);
    ctx->xbox_live_token[sizeof(ctx->xbox_live_token) - 1] = '\0';
    bfree(xbl_token);
}
*/

/**
 *
 * @param ctx
 * @return
 */
static void wait_for_user_token_loop(authentication_ctx_t *ctx) {

    char get_token_form_url_encoded[8192];
    snprintf(get_token_form_url_encoded,
             sizeof(get_token_form_url_encoded),
             "client_id=%s&device_code=%s&grant_type=%s",
             CLIENT_ID,
             ctx->device_code,
             GRANT_TYPE);

    obs_log(LOG_INFO, "Waiting for the user to validate the code");

    time_t       start_time = time(NULL);
    long         code       = 0;
    unsigned int interval   = (unsigned int)ctx->interval_in_seconds * 1000;
    long         expires_in = ctx->expires_in_seconds * 1000;

    while (time(NULL) - start_time < expires_in) {

        sleep_ms(interval);

        char *json = http_get(TOKEN_ENDPOINT, NULL, get_token_form_url_encoded, &code);

        if (code != 200) {
            obs_log(LOG_INFO,
                    "Device not validated yet. Received code %d, Waiting %d second before retrying...",
                    code,
                    interval);
        } else {

            char *token = json_read_string(json, "access_token");

            if (token) {
                token_t *user_token = (token_t *)bzalloc(sizeof(token_t));
                user_token->value   = bstrdup_n(token, strlen(token));
                ctx->user_token     = user_token;

                /* Saves the user token */
                state_set_user_token(user_token);

                bfree(token);
                obs_log(LOG_INFO, "User token received");
                bfree(json);
                break;
            }

            ctx->result.error_message = "Could not parse access_token from token response";
            obs_log(LOG_ERROR, ctx->result.error_message);
        }

        if (json) {
            bfree(json);
        }
    }

    if (!ctx->user_token) {
        return;
    }

    retrieve_device_token(ctx);
}

/**
 *
 * @param param
 * @return
 */
static void *start_authentication_flow(void *param) {

    authentication_ctx_t *ctx = (authentication_ctx_t *)param;

    token_t *user_token = state_get_user_token();

    if (user_token) {
        obs_log(LOG_INFO, "Using cached user token");
        ctx->user_token = user_token;
        retrieve_device_token(ctx);
        return (void *)false;
    }

    obs_log(LOG_INFO, "Starting Xbox sign-in in browser");

    /* ******************************* */
    /* Builds the www-form-url-encoded */
    /* ******************************* */
    char *scope_enc = http_urlencode(SCOPE);

    if (!scope_enc) {
        obs_log(LOG_WARNING, ctx->result.error_message);
        return (void *)false;
    }

    char form_url_encoded[8192];
    snprintf(form_url_encoded,
             sizeof(form_url_encoded),
             "client_id=%s&response_type=device_code&scope=%s",
             CLIENT_ID,
             scope_enc);

    bfree(scope_enc);

    /* ************************************************ */
    /* Request a device code from the connect endpoint. */
    /* ************************************************ */
    long http_code = 0;

    char *token_json = http_post_form(CONNECT_ENDPOINT, form_url_encoded, &http_code);

    if (!token_json) {
        ctx->result.error_message = "Unable to retrieve a user token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return (void *)false;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a user token: received an error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a user token:  %ld", http_code);
        bfree(token_json);
        return (void *)false;
    }

    /* ******************************************* */
    /* Retrieves the information from the response */
    /* ******************************************* */
    char *user_code = json_read_string(token_json, "user_code");

    if (!user_code) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the user_code from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(token_json);
        return (void *)false;
    }

    char *device_code = json_read_string(token_json, "device_code");

    if (!device_code) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the device_code from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    long *interval = json_get_long_value(token_json, "interval");

    if (!interval) {
        ctx->result.error_message = "Unable to received a user token: could not parse the interval from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    long *expires_in = json_get_long_value(token_json, "expires_in");

    if (!expires_in) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the expires_in from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(expires_in);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    ctx->device_code         = device_code;
    ctx->interval_in_seconds = *interval;
    ctx->expires_in_seconds  = *expires_in;

    /* ***************************************** */
    /* Open the browser to the verification URL. */
    /* ***************************************** */
    char verification_uri[4096];
    snprintf(verification_uri, sizeof(verification_uri), "%s%s", REGISTER_ENDPOINT, user_code);

    obs_log(LOG_DEBUG, "Open browser for OAuth verification at URL: %s", verification_uri);

    if (!open_url(verification_uri)) {
        ctx->result.error_message = "Unable to received a user token: could not open the browser";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(expires_in);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    /* ************************************* */
    /* Starts the loop waiting for the token */
    /* ************************************* */
    wait_for_user_token_loop(ctx);

    free(expires_in);
    bfree(interval);
    bfree(token_json);
    bfree(user_code);

    return (void *)true;
}

//	--

static char *oauth_exchange_code_for_access_token(const char *client_id, const char *redirect_uri, const char *code,
                                                  const char *code_verifier) {
    char *code_enc     = http_urlencode(code);
    char *redir_enc    = http_urlencode(redirect_uri);
    char *verifier_enc = http_urlencode(code_verifier);
    if (!code_enc || !redir_enc || !verifier_enc) {
        bfree(code_enc);
        bfree(redir_enc);
        bfree(verifier_enc);
        obs_log(4, "Failed to URL-encode token exchange parameters");
        return NULL;
    }

    /*
     * Public client token exchange (no client_secret).
     * PKCE is what protects this flow.
     *
     * IMPORTANT: Azure will return AADSTS70002 (client_secret required) if the
     * app registration is configured as a confidential client (or otherwise
     * requires a secret for the token endpoint). In that case, the fix is to
     * register the app as a Public client (mobile/desktop) and enable the
     * redirect URI we use.
     */
    char post_fields[8192];
    snprintf(post_fields,
             sizeof(post_fields),
             "client_id=%s&grant_type=authorization_code&code=%s&redirect_uri=%s&code_verifier=%s",
             client_id,
             code_enc,
             redir_enc,
             verifier_enc);
    bfree(code_enc);
    bfree(redir_enc);
    bfree(verifier_enc);

    long  http_code  = 0;
    /*
     * IMPORTANT: The authorize and token endpoints must match the app audience.
     *
     * - If the app is "Accounts in any organizational directory" (AAD), /common
     *   works.
     * - If the app is "Personal Microsoft accounts only" (MSA), Azure requires
     *   /consumers and will return AADSTS9002331 if you use /common.
     */
    char *token_json = http_post_form(TOKEN_ENDPOINT, post_fields, &http_code);
    if (!token_json) {
        obs_log(4, "Token exchange failed (no response)");
        return NULL;
    }
    if (http_code < 200 || http_code >= 300) {
        obs_log(4, "Token exchange HTTP %ld: %s", http_code, token_json);
        if (strstr(token_json, "AADSTS70002"))
            obs_log(
                4,
                "Token exchange indicates client_secret is required. Configure your Azure app as a Public client (no secret) and enable the loopback redirect URI.");
        if (strstr(token_json, "AADSTS9002331"))
            obs_log(
                4,
                "Token exchange endpoint mismatch: this app is configured for Microsoft Accounts only; use the /consumers endpoint for both authorize and token.");
        bfree(token_json);
        return NULL;
    }

    char *access_token = json_read_string(token_json, "access_token");
    if (!access_token) {
        obs_log(4, "Could not parse access_token from token response");
        obs_log(3, "Token response: %s", token_json);
    }

    bfree(token_json);
    return access_token;
}

static bool xbl_authenticate(const char *ms_access_token, char **out_xbl_token) {
    if (out_xbl_token)
        *out_xbl_token = NULL;

    obs_log(4, "XBL authenticate using MSAL '%s'", ms_access_token);

    char body[8192];
    snprintf(body,
             sizeof(body),
             "{"
             "\"RelyingParty\":\"http://auth.xboxlive.com\","
             "\"TokenType\":\"JWT\","
             "\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=%s\"}"
             "}",
             ms_access_token);

    long  http_code = 0;
    char *xbl_json  = http_post_json("https://user.auth.xboxlive.com/user/authenticate", body, NULL, &http_code);
    if (!xbl_json) {
        obs_log(4, "XBL authenticate failed (no response)");
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        obs_log(4, "XBL authenticate HTTP %ld: %s", http_code, xbl_json);
        bfree(xbl_json);
        return false;
    }

    obs_log(4, "Received: %s", xbl_json);

    char *xbl_token = json_read_string(xbl_json, "Token");

    if (!xbl_token) {
        obs_log(4, "Could not parse XBL token");
        obs_log(3, "XBL response: %s", xbl_json);
        bfree(xbl_json);
        bfree(xbl_token);
        return false;
    }

    obs_log(4, "XBL token: %s", xbl_token);

    bfree(xbl_json);

    if (out_xbl_token)
        *out_xbl_token = xbl_token;
    else
        bfree(xbl_token);

    return true;
}

static void xsts_authorize(const char *xbl_token, char **out_xsts_token, char **out_uhs, char **out_xid) {
    if (out_xsts_token)
        *out_xsts_token = NULL;
    if (out_uhs)
        *out_uhs = NULL;
    if (out_xid)
        *out_xid = NULL;

    char body[8192];
    snprintf(body,
             sizeof(body),
             "{"
             "\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
             "\"RelyingParty\":\"http://xboxlive.com\","
             "\"TokenType\":\"JWT\""
             "}",
             xbl_token);

    long  http_code = 0;
    char *xsts_json = http_post_json("https://xsts.auth.xboxlive.com/xsts/authorize", body, NULL, &http_code);

    if (!xsts_json) {
        obs_log(4, "XSTS authorize failed (no response)");
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        /*
         * XSTS errors are usually JSON like:
         * {"XErr":2148916233,"Message":"...","Redirect":"..."}
         */
        char *message = json_read_string(xsts_json, "Message");
        xbl_token     = xbl_token; /* silence unused in some builds */
        obs_log(4, "XSTS authorize HTTP %ld: %s", http_code, xsts_json[0] ? xsts_json : "<empty body>");
        if (message) {
            obs_log(4, "XSTS error message: %s", message);
            bfree(message);
        }
        bfree(xsts_json);
        return;
    }

    char *xsts_token = json_read_string(xsts_json, "Token");
    if (!xsts_token) {
        obs_log(4, "Could not parse XSTS token");
        obs_log(3, "XSTS response: %s", xsts_json);
    }

    char       *uhs     = NULL;
    const char *uhs_pos = strstr(xsts_json, "\"uhs\"");
    if (uhs_pos)
        uhs = json_read_string(uhs_pos, "uhs");

    char       *xid     = NULL;
    const char *xid_pos = strstr(xsts_json, "\"xid\"");
    if (xid_pos)
        xid = json_read_string(xid_pos, "xid");

    obs_log(4, "XSTS token: %s", xsts_token);

    if (out_xsts_token)
        *out_xsts_token = xsts_token;
    else
        bfree(xsts_token);

    if (out_uhs)
        *out_uhs = uhs;
    else
        bfree(uhs);

    if (out_xid)
        *out_xid = xid;
    else
        bfree(xid);

    bfree(xsts_json);
}

bool xbox_live_get_authenticate(const device_t *device) {

    /* Defines the structure that will filled up by the different authentication steps */
    authentication_ctx_t *ctx = bzalloc(sizeof(authentication_ctx_t));
    ctx->device               = device;

    return pthread_create(&ctx->thread, NULL, start_authentication_flow, ctx) == 0;
}
