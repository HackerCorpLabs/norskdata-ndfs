/**
 * Tests for ND-100 packed timestamp encode/decode.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include <string.h>

static int test_decode_known_value(void)
{
    /* 0xB9469AE4 / 3108412132, cross-verified against the ndfs-ts and
     * ndfs-py implementations and a real disk-dump sample: 1996-05-03 09:43:36. */
    ndfs_calendar_t cal;
    TEST_ASSERT(ndfs_nd_time_to_calendar(3108412132u, &cal) == true);
    TEST_ASSERT_EQUAL(1996, cal.year);
    TEST_ASSERT_EQUAL(5, cal.month);
    TEST_ASSERT_EQUAL(3, cal.day);
    TEST_ASSERT_EQUAL(9, cal.hour);
    TEST_ASSERT_EQUAL(43, cal.minute);
    TEST_ASSERT_EQUAL(36, cal.second);
    return 0;
}

static int test_encode_matches_known_value(void)
{
    ndfs_calendar_t cal;
    cal.year = 1996;
    cal.month = 5;
    cal.day = 3;
    cal.hour = 9;
    cal.minute = 43;
    cal.second = 36;
    TEST_ASSERT_EQUAL_UINT(3108412132u, ndfs_calendar_to_nd_time(&cal));
    return 0;
}

static int test_zero_is_not_set(void)
{
    ndfs_calendar_t cal;
    TEST_ASSERT(ndfs_nd_time_to_calendar(0, &cal) == false);
    return 0;
}

static int test_round_trip_epoch_boundaries(void)
{
    ndfs_calendar_t original, decoded;
    uint32_t raw;

    original.year = 1950;
    original.month = 1;
    original.day = 1;
    original.hour = 0;
    original.minute = 0;
    original.second = 0;
    raw = ndfs_calendar_to_nd_time(&original);
    TEST_ASSERT(raw != 0); /* year offset 0 still leaves nonzero bits from month/day */
    TEST_ASSERT(ndfs_nd_time_to_calendar(raw, &decoded) == true);
    TEST_ASSERT_EQUAL(1950, decoded.year);
    TEST_ASSERT_EQUAL(1, decoded.month);
    TEST_ASSERT_EQUAL(1, decoded.day);

    original.year = 2013;
    original.month = 12;
    original.day = 31;
    original.hour = 23;
    original.minute = 59;
    original.second = 59;
    raw = ndfs_calendar_to_nd_time(&original);
    TEST_ASSERT(ndfs_nd_time_to_calendar(raw, &decoded) == true);
    TEST_ASSERT_EQUAL(2013, decoded.year);
    TEST_ASSERT_EQUAL(23, decoded.hour);
    TEST_ASSERT_EQUAL(59, decoded.minute);
    TEST_ASSERT_EQUAL(59, decoded.second);
    return 0;
}

static int test_encode_year_out_of_range_returns_zero(void)
{
    ndfs_calendar_t cal;
    cal.year = 1949;
    cal.month = 1;
    cal.day = 1;
    cal.hour = 0;
    cal.minute = 0;
    cal.second = 0;
    TEST_ASSERT_EQUAL_UINT(0u, ndfs_calendar_to_nd_time(&cal));

    cal.year = 2014;
    TEST_ASSERT_EQUAL_UINT(0u, ndfs_calendar_to_nd_time(&cal));
    return 0;
}

static int test_hour_24_to_31_clamps_to_zero(void)
{
    /* Raw hour field (bits 16-12) can hold 0-31; the source system is known
     * to encode values 24-31 in this field, which decode as hour 0 rather
     * than being rejected. */
    ndfs_calendar_t base, decoded;
    uint32_t raw;

    base.year = 1996;
    base.month = 5;
    base.day = 3;
    base.hour = 24; /* out of calendar range, but a legal 5-bit field value */
    base.minute = 43;
    base.second = 36;
    raw = ndfs_calendar_to_nd_time(&base);

    TEST_ASSERT(ndfs_nd_time_to_calendar(raw, &decoded) == true);
    TEST_ASSERT_EQUAL(0, decoded.hour);
    return 0;
}

static int test_decode_null_out_is_safe(void)
{
    TEST_ASSERT(ndfs_nd_time_to_calendar(3108412132u, NULL) == false);
    return 0;
}

static int test_format_known_value(void)
{
    char buf[32];
    TEST_ASSERT(ndfs_nd_time_format(3108412132u, buf, sizeof(buf)) == true);
    TEST_ASSERT_EQUAL_STRING("1996-05-03 09:43:36", buf);
    return 0;
}

static int test_format_not_set(void)
{
    char buf[32];
    TEST_ASSERT(ndfs_nd_time_format(0, buf, sizeof(buf)) == false);
    TEST_ASSERT_EQUAL_STRING("(not set)", buf);
    return 0;
}

void run_nd_time_tests(void)
{
    TEST_SUITE_BEGIN("ND Time Tests");

    RUN_TEST(test_decode_known_value);
    RUN_TEST(test_encode_matches_known_value);
    RUN_TEST(test_zero_is_not_set);
    RUN_TEST(test_round_trip_epoch_boundaries);
    RUN_TEST(test_encode_year_out_of_range_returns_zero);
    RUN_TEST(test_hour_24_to_31_clamps_to_zero);
    RUN_TEST(test_decode_null_out_is_safe);
    RUN_TEST(test_format_known_value);
    RUN_TEST(test_format_not_set);
}
