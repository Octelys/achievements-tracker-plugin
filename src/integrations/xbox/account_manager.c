#include "integrations/xbox/account_manager.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <stdio.h>

#include "io/state.h"
#include "integrations/xbox/oauth/xbox-live.h"
#include "integrations/xbox/xbox_monitor.h"

static void on_xbox_signed_in(void *data) {
    UNUSED_PARAMETER(data);

    xbox_monitoring_start();
}

bool xbox_account_sign_in(void) {
    if (!xbox_live_authenticate(NULL, &on_xbox_signed_in)) {
        obs_log(LOG_WARNING, "[XboxAccount] Sign-in failed");
        return false;
    }

    return true;
}

void xbox_account_sign_out(void) {
    state_clear();
    xbox_monitoring_stop();
}

bool xbox_account_is_signed_in(void) {
    xbox_identity_t *identity  = xbox_live_get_identity();
    const bool       signed_in = identity != NULL;

    free_identity(&identity);

    return signed_in;
}

void xbox_account_get_status_text(char *buffer, size_t buffer_size) {
    xbox_identity_t *identity;

    if (!buffer || buffer_size == 0) {
        return;
    }

    identity = xbox_live_get_identity();

    if (identity && identity->gamertag && *identity->gamertag) {
        snprintf(buffer, buffer_size, "Signed in as %s", identity->gamertag);
    } else {
        snprintf(buffer, buffer_size, "Not connected.");
    }

    free_identity(&identity);
}
