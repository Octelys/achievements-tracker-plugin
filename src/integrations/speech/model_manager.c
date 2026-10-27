#include "model_manager.h"

#include <obs-module.h>
#include <util/platform.h>
#include <diagnostics/log.h>

#include "net/http/http.h"
#include "common/memory.h"

#include <stdio.h>
#include <string.h>

/** Small, English-only whisper.cpp ggml model — good latency/accuracy tradeoff for short chat utterances. */
#define SPEECH_MODEL_FILE_NAME "ggml-tiny.en.bin"
#define SPEECH_MODEL_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin"

char *speech_model_get_path(void) {

    char *config_path = obs_module_config_path("models/" SPEECH_MODEL_FILE_NAME);

    if (!config_path) {
        return bstrdup("");
    }

    return config_path;
}

bool speech_model_is_downloaded(void) {

    char *path = speech_model_get_path();

    bool exists = path && path[0] != '\0' && os_file_exists(path);

    bfree(path);

    return exists;
}

bool speech_model_ensure_downloaded(void) {

    if (speech_model_is_downloaded()) {
        return true;
    }

    char *path = speech_model_get_path();
    if (!path || path[0] == '\0') {
        obs_log(LOG_ERROR, "[SpeechModel] Unable to resolve model path");
        bfree(path);
        return false;
    }

    char *dir = obs_module_config_path("models");
    if (dir) {
        os_mkdirs(dir);
        bfree(dir);
    }

    uint8_t *data = NULL;
    size_t   size = 0;

    if (!http_download(SPEECH_MODEL_URL, &data, &size) || !data || size == 0) {
        obs_log(LOG_ERROR, "[SpeechModel] Failed to download model from %s", SPEECH_MODEL_URL);
        bfree(data);
        bfree(path);
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        obs_log(LOG_ERROR, "[SpeechModel] Unable to open '%s' for writing", path);
        bfree(data);
        bfree(path);
        return false;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    bfree(data);
    bfree(path);

    if (written != size) {
        obs_log(LOG_ERROR, "[SpeechModel] Incomplete write of model file");
        return false;
    }

    return true;
}
