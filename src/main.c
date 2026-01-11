#include <obs-module.h>
#include <diagnostics/log.h>

#include "sources/xbox_achievements_text_source.h"
#include "io/state.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void) {
    obs_log(LOG_INFO, "loading plugin (version %s)", PLUGIN_VERSION);
    io_load();

    xbox_achievements_text_source_register();

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

    return true;
}

void obs_module_unload(void) {
    obs_log(LOG_INFO, "plugin unloaded");
}
