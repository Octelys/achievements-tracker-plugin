#include <obs-module.h>
#include <diagnostics/log.h>

#include "sources/common/achievement_cycle.h"
#include "sources/xbox/account.h"
#include "sources/xbox/gamerpic.h"
#include "sources/xbox/game_cover.h"
#include "sources/xbox/gamerscore.h"
#include "sources/xbox/gamertag.h"

#include "io/state.h"
#include "sources/xbox/achievement_name.h"
#include "sources/xbox/achievement_description.h"
#include "sources/xbox/achievement_icon.h"
#include "sources/xbox/achievements_unlocked_count.h"
#include "sources/xbox/achievements_total_count.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void) {
    obs_log(LOG_INFO, "loading plugin (version %s)", PLUGIN_VERSION);
    io_load();

    xbox_account_source_register();
    xbox_gamerpic_source_register();
    xbox_game_cover_source_register();
    xbox_gamerscore_source_register();
    xbox_gamertag_source_register();

    /* Initialize the shared achievement display cycle before registering achievement sources */
    achievement_cycle_init();

    xbox_achievement_name_source_register();
    xbox_achievement_description_source_register();
    xbox_achievement_icon_source_register();
    xbox_achievements_unlocked_count_source_register();
    xbox_achievements_total_count_source_register();

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

    return true;
}

void obs_module_unload(void) {
    achievement_cycle_destroy();
    obs_log(LOG_INFO, "plugin unloaded");
}
