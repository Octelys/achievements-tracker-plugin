#include "unity.h"

#include "integrations/speech/vad.h"

#include <obs-module.h>
#include <stdlib.h>

#define TEST_RMS_THRESHOLD 0.1f
#define TEST_SILENCE_MS 100
/* At 16 kHz, 100ms == 1600 samples. */
#define SAMPLES_PER_CHUNK (VAD_SAMPLE_RATE_HZ / 10)

void setUp(void) {}
void tearDown(void) {}

static void fill(float *buffer, size_t count, float value) {
    for (size_t i = 0; i < count; i++) {
        buffer[i] = value;
    }
}

static void test_vad_reports_no_event_while_only_silence_seen(void) {
    //  Arrange.
    vad_state_t *vad = vad_create(TEST_RMS_THRESHOLD, TEST_SILENCE_MS);
    float        silence[SAMPLES_PER_CHUNK];
    fill(silence, SAMPLES_PER_CHUNK, 0.0f);

    //  Act.
    vad_event_t event = vad_push_samples(vad, silence, SAMPLES_PER_CHUNK);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(VAD_EVENT_NONE, event);

    vad_free(vad);
}

static void test_vad_reports_no_event_while_still_speaking(void) {
    //  Arrange.
    vad_state_t *vad = vad_create(TEST_RMS_THRESHOLD, TEST_SILENCE_MS);
    float        speech[SAMPLES_PER_CHUNK];
    fill(speech, SAMPLES_PER_CHUNK, 0.5f);

    //  Act.
    vad_event_t event = vad_push_samples(vad, speech, SAMPLES_PER_CHUNK);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(VAD_EVENT_NONE, event);

    vad_free(vad);
}

static void test_vad_reports_utterance_ready_after_trailing_silence(void) {
    //  Arrange.
    vad_state_t *vad = vad_create(TEST_RMS_THRESHOLD, TEST_SILENCE_MS);
    float        speech[SAMPLES_PER_CHUNK];
    float        silence[SAMPLES_PER_CHUNK];
    fill(speech, SAMPLES_PER_CHUNK, 0.5f);
    fill(silence, SAMPLES_PER_CHUNK, 0.0f);

    //  Act.
    vad_push_samples(vad, speech, SAMPLES_PER_CHUNK);
    vad_event_t event = vad_push_samples(vad, silence, SAMPLES_PER_CHUNK);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(VAD_EVENT_UTTERANCE_READY, event);

    size_t sample_count = 0;
    float *utterance     = vad_take_utterance(vad, &sample_count);

    TEST_ASSERT_NOT_NULL(utterance);
    TEST_ASSERT_EQUAL_UINT(SAMPLES_PER_CHUNK * 2, sample_count);

    bfree(utterance);
    vad_free(vad);
}

static void test_vad_ignores_leading_silence_before_speech(void) {
    //  Arrange.
    vad_state_t *vad = vad_create(TEST_RMS_THRESHOLD, TEST_SILENCE_MS);
    float        silence[SAMPLES_PER_CHUNK];
    float        speech[SAMPLES_PER_CHUNK];
    fill(silence, SAMPLES_PER_CHUNK, 0.0f);
    fill(speech, SAMPLES_PER_CHUNK, 0.5f);

    //  Act.
    vad_push_samples(vad, silence, SAMPLES_PER_CHUNK);
    vad_push_samples(vad, speech, SAMPLES_PER_CHUNK);
    vad_event_t event = vad_push_samples(vad, silence, SAMPLES_PER_CHUNK);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(VAD_EVENT_UTTERANCE_READY, event);

    size_t sample_count = 0;
    float *utterance     = vad_take_utterance(vad, &sample_count);

    /* Only the speech chunk and the trailing-silence chunk are kept, not the leading silence. */
    TEST_ASSERT_EQUAL_UINT(SAMPLES_PER_CHUNK * 2, sample_count);

    bfree(utterance);
    vad_free(vad);
}

static void test_vad_take_utterance_resets_buffer_for_next_utterance(void) {
    //  Arrange.
    vad_state_t *vad = vad_create(TEST_RMS_THRESHOLD, TEST_SILENCE_MS);
    float        speech[SAMPLES_PER_CHUNK];
    float        silence[SAMPLES_PER_CHUNK];
    fill(speech, SAMPLES_PER_CHUNK, 0.5f);
    fill(silence, SAMPLES_PER_CHUNK, 0.0f);

    vad_push_samples(vad, speech, SAMPLES_PER_CHUNK);
    vad_push_samples(vad, silence, SAMPLES_PER_CHUNK);
    size_t first_count = 0;
    float *first        = vad_take_utterance(vad, &first_count);
    bfree(first);

    //  Act.
    size_t second_count = 123;
    float *second        = vad_take_utterance(vad, &second_count);

    //  Assert.
    TEST_ASSERT_NULL(second);
    TEST_ASSERT_EQUAL_UINT(0, second_count);

    vad_free(vad);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vad_reports_no_event_while_only_silence_seen);
    RUN_TEST(test_vad_reports_no_event_while_still_speaking);
    RUN_TEST(test_vad_reports_utterance_ready_after_trailing_silence);
    RUN_TEST(test_vad_ignores_leading_silence_before_speech);
    RUN_TEST(test_vad_take_utterance_resets_buffer_for_next_utterance);
    return UNITY_END();
}
