/**
 * Tests for BlockPointer.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/block_pointer.h>

static int test_from_native_contiguous(void)
{
    /* Type=0 (contiguous), blockId=100 -> raw = 0x00000064 */
    ndfs_block_pointer_t bp = ndfs_bp_from_native(100);
    TEST_ASSERT_EQUAL(100, bp.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_CONTIGUOUS, bp.type);
    return 0;
}

static int test_from_native_indexed(void)
{
    /* Type=1 (indexed), blockId=42 -> raw = 0x4000002A */
    uint32_t raw = (1u << 30) | 42;
    ndfs_block_pointer_t bp = ndfs_bp_from_native(raw);
    TEST_ASSERT_EQUAL(42, bp.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_INDEXED, bp.type);
    return 0;
}

static int test_from_native_subindexed(void)
{
    uint32_t raw = (2u << 30) | 999;
    ndfs_block_pointer_t bp = ndfs_bp_from_native(raw);
    TEST_ASSERT_EQUAL(999, bp.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_SUBINDEXED, bp.type);
    return 0;
}

static int test_to_native_roundtrip(void)
{
    ndfs_block_pointer_t bp;
    uint32_t native;

    bp.block_id = 12345;
    bp.type = NDFS_PTR_INDEXED;
    native = ndfs_bp_to_native(&bp);

    {
        ndfs_block_pointer_t bp2 = ndfs_bp_from_native(native);
        TEST_ASSERT_EQUAL(12345, bp2.block_id);
        TEST_ASSERT_EQUAL(NDFS_PTR_INDEXED, bp2.type);
    }
    return 0;
}

static int test_bytes_roundtrip(void)
{
    uint8_t buf[8] = {0};
    ndfs_block_pointer_t bp, bp2;

    bp.block_id = 0x3FFFFF;
    bp.type = NDFS_PTR_SUBINDEXED;
    ndfs_bp_to_bytes(&bp, buf, 2);

    bp2 = ndfs_bp_from_bytes(buf, 2);
    TEST_ASSERT_EQUAL(0x3FFFFF, bp2.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_SUBINDEXED, bp2.type);
    return 0;
}

static int test_is_valid(void)
{
    ndfs_block_pointer_t bp;

    bp.block_id = 10;
    bp.type = NDFS_PTR_CONTIGUOUS;
    TEST_ASSERT(ndfs_bp_is_valid(&bp));

    bp.block_id = 0;
    TEST_ASSERT(!ndfs_bp_is_valid(&bp));

    bp.block_id = 10;
    bp.type = NDFS_PTR_RESERVED;
    TEST_ASSERT(!ndfs_bp_is_valid(&bp));
    return 0;
}

static int test_big_endian_encoding(void)
{
    /* Verify exact byte layout: type=1, blockId=5 -> 0x40000005 */
    uint8_t buf[4] = {0};
    ndfs_block_pointer_t bp;

    bp.block_id = 5;
    bp.type = NDFS_PTR_INDEXED;
    ndfs_bp_to_bytes(&bp, buf, 0);

    TEST_ASSERT_EQUAL(0x40, buf[0]);
    TEST_ASSERT_EQUAL(0x00, buf[1]);
    TEST_ASSERT_EQUAL(0x00, buf[2]);
    TEST_ASSERT_EQUAL(0x05, buf[3]);
    return 0;
}

void run_block_pointer_tests(void)
{
    TEST_SUITE_BEGIN("BlockPointer Tests");
    RUN_TEST(test_from_native_contiguous);
    RUN_TEST(test_from_native_indexed);
    RUN_TEST(test_from_native_subindexed);
    RUN_TEST(test_to_native_roundtrip);
    RUN_TEST(test_bytes_roundtrip);
    RUN_TEST(test_is_valid);
    RUN_TEST(test_big_endian_encoding);
}
