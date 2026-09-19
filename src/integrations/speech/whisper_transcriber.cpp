#include "whisper_transcriber.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <whisper.h>

#include <cstring>
#include <mutex>

namespace {
std::mutex        g_mutex;
whisper_context  *g_context = nullptr;

/** Segments whisper.cpp judges more likely non-speech than this are discarded. */
constexpr float NO_SPEECH_THRESHOLD = 0.6f;

/**
 * @brief Detect whisper.cpp's non-speech placeholder tokens (e.g. "[BLANK_AUDIO]",
 * "[SILENCE]", "(music)") that the model can emit as literal segment text for
 * near-silent or noisy audio instead of leaving the segment empty.
 */
bool is_non_speech_tag(const std::string &text) {

    if (text.size() < 2) {
        return false;
    }

    char open  = text.front();
    char close = text.back();

    return (open == '[' && close == ']') || (open == '(' && close == ')');
}
} // namespace

bool whisper_transcriber_init(const char *model_path) {

    if (!model_path || model_path[0] == '\0') {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_context) {
        whisper_free(g_context);
        g_context = nullptr;
    }

    whisper_context_params ctx_params = whisper_context_default_params();
    g_context                         = whisper_init_from_file_with_params(model_path, ctx_params);

    if (!g_context) {
        obs_log(LOG_ERROR, "[WhisperTranscriber] Failed to load model from '%s'", model_path);
        return false;
    }

    return true;
}

void whisper_transcriber_shutdown(void) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_context) {
        whisper_free(g_context);
        g_context = nullptr;
    }
}

char *whisper_transcriber_transcribe(const float *samples, size_t sample_count) {

    if (!samples || sample_count == 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_context) {
        return nullptr;
    }

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language            = "en";
    params.print_progress      = false;
    params.print_special       = false;
    params.print_realtime      = false;
    params.print_timestamps    = false;
    params.no_context          = true;
    params.single_segment      = true;
    params.n_threads           = 4;
    params.no_speech_thold     = NO_SPEECH_THRESHOLD;

    if (whisper_full(g_context, params, samples, (int)sample_count) != 0) {
        obs_log(LOG_ERROR, "[WhisperTranscriber] Inference failed");
        return nullptr;
    }

    int segment_count = whisper_full_n_segments(g_context);
    if (segment_count <= 0) {
        return nullptr;
    }

    std::string text;
    for (int i = 0; i < segment_count; i++) {
        if (whisper_full_get_segment_no_speech_prob(g_context, i) >= NO_SPEECH_THRESHOLD) {
            continue;
        }
        text += whisper_full_get_segment_text(g_context, i);
    }

    /* Trim leading/trailing whitespace whisper.cpp tends to leave in segment text. */
    size_t start = text.find_first_not_of(" \t\r\n");
    size_t end   = text.find_last_not_of(" \t\r\n");

    if (start == std::string::npos) {
        return nullptr;
    }

    std::string trimmed = text.substr(start, end - start + 1);
    if (trimmed.empty() || is_non_speech_tag(trimmed)) {
        return nullptr;
    }

    return bstrdup(trimmed.c_str());
}
