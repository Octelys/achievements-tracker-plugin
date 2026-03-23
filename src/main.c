#include <obs-module.h>
#include <diagnostics/log.h>

#include "sources/common/achievement_cycle.h"
#include "ui/xbox_account_config.h"
#include "sources/gamerpic.h"
#include "sources/game_cover.h"
#include "sources/gamerscore.h"
#include "sources/gamertag.h"

#include "io/state.h"
#include "sources/achievement_name.h"
#include "sources/achievement_description.h"
#include "sources/achievement_icon.h"
#include "sources/achievements_count.h"
#include "drawing/image.h"
#include "integrations/monitoring_service.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void) {
    obs_log(LOG_INFO, "Loading plugin (version %s)", PLUGIN_VERSION);
    io_load();

    xbox_account_config_register();
    monitoring_start();

    xbox_gamerpic_source_register();
    game_cover_source_register();
    xbox_gamerscore_source_register();
    xbox_gamertag_source_register();

    /* Initialize the shared achievement display cycle before registering achievement sources */
    achievement_cycle_init();

    xbox_achievement_name_source_register();
    xbox_achievement_description_source_register();
    xbox_achievement_icon_source_register();
    xbox_achievements_count_source_register();

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);

    return true;
}

void obs_module_unload(void) {
    xbox_account_config_unregister();

    achievement_cycle_destroy();
    image_cleanup();

    /* Clean up source configurations */
    xbox_achievement_name_source_cleanup();
    xbox_achievement_description_source_cleanup();
    xbox_achievement_icon_source_cleanup();
    xbox_achievements_count_source_cleanup();
    game_cover_source_cleanup();
    xbox_gamerpic_source_cleanup();
    xbox_gamerscore_source_cleanup();
    xbox_gamertag_source_cleanup();

    monitoring_stop();
    io_cleanup();

    obs_log(LOG_INFO, "Plugin unloaded");
}
