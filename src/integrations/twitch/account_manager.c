#include "integrations/twitch/account_manager.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include "io/state.h"
#include "integrations/twitch/oauth/twitch-oauth.h"

#define PENDING_CODE_TEXT_SIZE 512

static pthread_mutex_t g_pending_code_mutex                        = PTHREAD_MUTEX_INITIALIZER;
static char            g_pending_code_text[PENDING_CODE_TEXT_SIZE] = {0};

static void on_twitch_code_ready(const char *user_code, const char *verification_uri, void *data) {
    UNUSED_PARAMETER(data);

    pthread_mutex_lock(&g_pending_code_mutex);
    snprintf(g_pending_code_text,
             sizeof(g_pending_code_text),
             "Go to %s and enter code: %s",
             verification_uri,
             user_code);
    pthread_mutex_unlock(&g_pending_code_mutex);
}

static void on_twitch_signed_in(void *data) {
    UNUSED_PARAMETER(data);

    pthread_mutex_lock(&g_pending_code_mutex);
    g_pending_code_text[0] = '\0';
    pthread_mutex_unlock(&g_pending_code_mutex);
}

bool twitch_account_sign_in(void) {
    if (!twitch_authenticate(NULL, &on_twitch_code_ready, &on_twitch_signed_in)) {
        obs_log(LOG_WARNING, "[TwitchAccount] Sign-in failed");
        return false;
    }
    return true;
}

void twitch_account_sign_out(void) {
    state_clear_twitch_identity();
}

bool twitch_account_is_signed_in(void) {
    twitch_identity_t *identity  = twitch_get_identity();
    const bool         signed_in = identity != NULL;
    free_twitch_identity(&identity);
    return signed_in;
}

void twitch_account_get_status_text(char *buffer, size_t buffer_size) {
    twitch_identity_t *identity = twitch_get_identity();

    if (identity && identity->display_name && *identity->display_name) {
        snprintf(buffer, buffer_size, "Signed in as %s", identity->display_name);
    } else {
        snprintf(buffer, buffer_size, "Not connected.");
    }

    free_twitch_identity(&identity);
}

void twitch_account_get_pending_code_text(char *buffer, size_t buffer_size) {
    pthread_mutex_lock(&g_pending_code_mutex);
    snprintf(buffer, buffer_size, "%s", g_pending_code_text);
    g_pending_code_text[0] = '\0';
    pthread_mutex_unlock(&g_pending_code_mutex);
}
