/**
 * Tests for ND-100 even parity helpers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include <string.h>

static int test_strip_parity(void)
{
    uint8_t data[] = {0xC8, 0xE5, 0xEC, 0xEC, 0xEF}; /* Hello with parity */
    ndfs_strip_parity(data, 5);
    TEST_ASSERT_EQUAL(0x48, data[0]); /* H */
    TEST_ASSERT_EQUAL(0x65, data[1]); /* e */
    TEST_ASSERT_EQUAL(0x6C, data[2]); /* l */
    TEST_ASSERT_EQUAL(0x6C, data[3]); /* l */
    TEST_ASSERT_EQUAL(0x6F, data[4]); /* o */
    return 0;
}

static int test_strip_no_change(void)
{
    uint8_t data[] = {0x48, 0x65, 0x6C};
    ndfs_strip_parity(data, 3);
    TEST_ASSERT_EQUAL(0x48, data[0]);
    TEST_ASSERT_EQUAL(0x65, data[1]);
    TEST_ASSERT_EQUAL(0x6C, data[2]);
    return 0;
}

static int test_set_parity_known_values(void)
{
    uint8_t data[] = {
        0x48, /* H: 2 ones (even) -> 0x48 */
        0x65, /* e: 4 ones (even) -> 0x65 */
        0x20, /* ' ': 1 one (odd) -> 0xA0 */
        0x57, /* W: 5 ones (odd) -> 0xD7 */
        0x64, /* d: 3 ones (odd) -> 0xE4 */
    };
    ndfs_set_parity(data, 5);
    TEST_ASSERT_EQUAL(0x48, data[0]);
    TEST_ASSERT_EQUAL(0x65, data[1]);
    TEST_ASSERT_EQUAL(0xA0, data[2]);
    TEST_ASSERT_EQUAL(0xD7, data[3]);
    TEST_ASSERT_EQUAL(0xE4, data[4]);
    return 0;
}

static int test_set_parity_all_bytes_even(void)
{
    uint8_t data[256];
    int i, ones, b;
    for (i = 0; i < 256; i++) data[i] = (uint8_t)i;

    ndfs_set_parity(data, 256);

    for (i = 0; i < 256; i++) {
        ones = 0;
        b = data[i];
        while (b) { ones += b & 1; b >>= 1; }
        TEST_ASSERT(ones % 2 == 0);
    }
    return 0;
}

static int test_parity_round_trip(void)
{
    uint8_t original[] = "Hello World";
    size_t len = strlen((char *)original);
    uint8_t copy[32];

    memcpy(copy, original, len);
    ndfs_set_parity(copy, len);
    ndfs_strip_parity(copy, len);

    /* After round-trip, low 7 bits must match */
    {
        size_t i;
        for (i = 0; i < len; i++) {
            TEST_ASSERT_EQUAL(original[i] & 0x7F, copy[i]);
        }
    }
    return 0;
}

static int test_is_text_type_yes(void)
{
    TEST_ASSERT(ndfs_is_text_type("MODE") == true);
    TEST_ASSERT(ndfs_is_text_type("SYMB") == true);
    TEST_ASSERT(ndfs_is_text_type("TEXT") == true);
    TEST_ASSERT(ndfs_is_text_type("C") == true);
    TEST_ASSERT(ndfs_is_text_type("BATC") == true);
    TEST_ASSERT(ndfs_is_text_type("FORT") == true);
    return 0;
}

static int test_is_text_type_no(void)
{
    TEST_ASSERT(ndfs_is_text_type("PROG") == false);
    TEST_ASSERT(ndfs_is_text_type("BPUN") == false);
    TEST_ASSERT(ndfs_is_text_type("DATA") == false);
    TEST_ASSERT(ndfs_is_text_type("VTM") == false);
    return 0;
}

static int test_is_text_type_case(void)
{
    TEST_ASSERT(ndfs_is_text_type("mode") == true);
    TEST_ASSERT(ndfs_is_text_type("Mode") == true);
    return 0;
}

static int test_is_text_type_empty(void)
{
    TEST_ASSERT(ndfs_is_text_type("") == false);
    TEST_ASSERT(ndfs_is_text_type(NULL) == false);
    return 0;
}

static int test_strip_null_safe(void)
{
    ndfs_strip_parity(NULL, 10); /* should not crash */
    ndfs_set_parity(NULL, 10);
    return 0;
}

void run_parity_tests(void)
{
    TEST_SUITE_BEGIN("Parity Tests");

    RUN_TEST(test_strip_parity);
    RUN_TEST(test_strip_no_change);
    RUN_TEST(test_set_parity_known_values);
    RUN_TEST(test_set_parity_all_bytes_even);
    RUN_TEST(test_parity_round_trip);
    RUN_TEST(test_is_text_type_yes);
    RUN_TEST(test_is_text_type_no);
    RUN_TEST(test_is_text_type_case);
    RUN_TEST(test_is_text_type_empty);
    RUN_TEST(test_strip_null_safe);
}
