#include <stdint.h>
#include <string.h>

#include <util/bmem.h>

#include "encoding/base64.h"

#include "unity.h"

static void assert_str_eq(const char *expected, const char *actual) {
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_STRING(expected, actual);
}

void setUp(void) {}
void tearDown(void) {}

void test_encode_base64_null_data_returns_null(void) {
    TEST_ASSERT_NULL(base64_encode(NULL, 3));
}

void test_encode_base64_zero_len_returns_null(void) {
    const uint8_t dummy = 0;
    TEST_ASSERT_NULL(base64_encode(&dummy, 0));
}

void test_encode_base64_ascii_hello(void) {
    const uint8_t data[] = {'h', 'e', 'l', 'l', 'o'};
    char         *out    = base64_encode(data, sizeof(data));

    assert_str_eq("aGVsbG8=", out);
    bfree(out);
}

void test_encode_base64_single_byte(void) {
    const uint8_t data[] = {0xFF};
    char         *out    = base64_encode(data, sizeof(data));

    // Base64 for 0xFF is /w==
    assert_str_eq("/w==", out);
    bfree(out);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_encode_base64_null_data_returns_null);
    RUN_TEST(test_encode_base64_zero_len_returns_null);
    RUN_TEST(test_encode_base64_ascii_hello);
    RUN_TEST(test_encode_base64_single_byte);

    return UNITY_END();
}
