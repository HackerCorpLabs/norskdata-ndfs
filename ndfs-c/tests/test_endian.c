/**
 * Tests for endian helpers.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include "endian_util.h"

static int test_read_u16be(void)
{
    uint8_t data[] = { 0x12, 0x34 };
    TEST_ASSERT_EQUAL_UINT(0x1234, ndfs_read_u16be(data, 0));
    return 0;
}

static int test_read_u32be(void)
{
    uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT_EQUAL_UINT(0xDEADBEEFu, ndfs_read_u32be(data, 0));
    return 0;
}

static int test_write_u16be(void)
{
    uint8_t data[2] = {0};
    ndfs_write_u16be(data, 0, 0xABCD);
    TEST_ASSERT_EQUAL(0xAB, data[0]);
    TEST_ASSERT_EQUAL(0xCD, data[1]);
    return 0;
}

static int test_write_u32be(void)
{
    uint8_t data[4] = {0};
    ndfs_write_u32be(data, 0, 0x01020304u);
    TEST_ASSERT_EQUAL(0x01, data[0]);
    TEST_ASSERT_EQUAL(0x02, data[1]);
    TEST_ASSERT_EQUAL(0x03, data[2]);
    TEST_ASSERT_EQUAL(0x04, data[3]);
    return 0;
}

static int test_roundtrip_u16(void)
{
    uint8_t data[4] = {0};
    ndfs_write_u16be(data, 1, 0x5678);
    TEST_ASSERT_EQUAL_UINT(0x5678, ndfs_read_u16be(data, 1));
    return 0;
}

static int test_roundtrip_u32(void)
{
    uint8_t data[8] = {0};
    ndfs_write_u32be(data, 2, 0xCAFEBABEu);
    TEST_ASSERT_EQUAL_UINT(0xCAFEBABEu, ndfs_read_u32be(data, 2));
    return 0;
}

void run_endian_tests(void)
{
    TEST_SUITE_BEGIN("Endian Tests");
    RUN_TEST(test_read_u16be);
    RUN_TEST(test_read_u32be);
    RUN_TEST(test_write_u16be);
    RUN_TEST(test_write_u32be);
    RUN_TEST(test_roundtrip_u16);
    RUN_TEST(test_roundtrip_u32);
}
