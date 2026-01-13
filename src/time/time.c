#include "time/time.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static bool parse_n_digits(const char **p, int digits, int *out_value) {
    int v = 0;

    for (int i = 0; i < digits; i++) {
        char c = (*p)[0];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
        (*p)++;
    }

    *out_value = v;
    return true;
}

static bool is_leap_year(int y) {
    /* Gregorian calendar rules */
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static int get_days_in_month(int y, int m) {
    static const int dpm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (m < 1 || m > 12)
        return 0;

    if (m == 2)
        return dpm[m - 1] + (is_leap_year(y) ? 1 : 0);

    return dpm[m - 1];
}

/*
 * Convert a civil date to days since Unix epoch (1970-01-01).
 * Based on Howard Hinnant's algorithm (public domain-like), implemented standalone.
 */
static int64_t get_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int      era       = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe       = (unsigned)(y - era * 400); /* [0, 399] */
    const unsigned doy       = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe       = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t  days_1970 = (int64_t)era * 146097 + (int64_t)doe - 719468;

    return days_1970;
}

bool time_iso8601_utc_to_unix(const char *iso8601, int64_t *out_unix_seconds, int32_t *out_fraction_ns) {
    if (!iso8601 || !out_unix_seconds || !out_fraction_ns)
        return false;

    const char *p = iso8601;

    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;

    if (!parse_n_digits(&p, 4, &year))
        return false;
    if (*p++ != '-')
        return false;
    if (!parse_n_digits(&p, 2, &month))
        return false;
    if (*p++ != '-')
        return false;
    if (!parse_n_digits(&p, 2, &day))
        return false;
    if (*p++ != 'T')
        return false;
    if (!parse_n_digits(&p, 2, &hour))
        return false;
    if (*p++ != ':')
        return false;
    if (!parse_n_digits(&p, 2, &minute))
        return false;
    if (*p++ != ':')
        return false;
    if (!parse_n_digits(&p, 2, &second))
        return false;

    /* Validate date/time ranges */
    if (month < 1 || month > 12)
        return false;
    const int dim = get_days_in_month(year, month);
    if (day < 1 || day > dim)
        return false;
    if (hour < 0 || hour > 23)
        return false;
    if (minute < 0 || minute > 59)
        return false;
    if (second < 0 || second > 60) {
        /* allow 60 only if you want leap seconds; keep strict-ish */
        return false;
    }

    int32_t fraction_ns = 0;

    if (*p == '.') {
        p++;

        int digits = 0;
        while (digits < 9 && isdigit((unsigned char)*p)) {
            fraction_ns = fraction_ns * 10 + (*p - '0');
            p++;
            digits++;
        }

        /* If there are more than 9 fractional digits, we don't support that */
        if (isdigit((unsigned char)*p))
            return false;

        /* scale to nanoseconds */
        while (digits < 9) {
            fraction_ns *= 10;
            digits++;
        }
    }

    if (*p++ != 'Z')
        return false;

    /* no extra chars */
    if (*p != '\0')
        return false;

    const int64_t days         = get_days_from_civil(year, (unsigned)month, (unsigned)day);
    int64_t       unix_seconds = days * 86400 + hour * 3600 + minute * 60 + second;

    *out_unix_seconds = unix_seconds;
    *out_fraction_ns  = fraction_ns;

    return true;
}
