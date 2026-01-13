#include "unity.h"

#include "time/time.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_time_iso8601_utc_z_to_unix_with_fraction(void) {
    //  Arrange.
    const int64_t expected_unix_seconds = 1769391470LL;
    const int32_t expected_fraction_ns  = 379172700;

    //  Act.
    int64_t actual_unix_seconds = 0;
    int32_t actual_fraction_ns  = 0;

    bool succeeded =
        time_iso8601_utc_to_unix("2026-01-26T01:37:50.3791727Z", &actual_unix_seconds, &actual_fraction_ns);

    //  Assert.
    TEST_ASSERT_TRUE(succeeded);

    /* Verified expected value for 2026-01-26T01:37:50Z */
    TEST_ASSERT_EQUAL_INT64(expected_unix_seconds, actual_unix_seconds);
    TEST_ASSERT_EQUAL_INT32(expected_fraction_ns, actual_fraction_ns);
}

static void test_time_iso8601_utc_z_to_unix_without_fraction(void) {
    //  Arrange.
    const int64_t expected_unix_seconds = 1769391470LL;
    const int32_t expected_fraction_ns  = 0;

    //  Act.
    int64_t actual_unix_seconds = 0;
    int32_t actual_fraction_ns  = 0;

    bool succeeded = time_iso8601_utc_to_unix("2026-01-26T01:37:50Z", &actual_unix_seconds, &actual_fraction_ns);

    //  Assert.
    TEST_ASSERT_TRUE(succeeded);

    TEST_ASSERT_EQUAL_INT64(expected_unix_seconds, actual_unix_seconds);
    TEST_ASSERT_EQUAL_INT32(expected_fraction_ns, actual_fraction_ns);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_time_iso8601_utc_z_to_unix_with_fraction);
    RUN_TEST(test_time_iso8601_utc_z_to_unix_without_fraction);
    return UNITY_END();
}
