/**
 * Tests for BitFile.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/bit_file.h>
#include <string.h>

static int test_init_and_destroy(void)
{
    ndfs_bit_file_t bf;
    memset(&bf, 0, sizeof(bf));

    TEST_ASSERT_OK(ndfs_bf_init(&bf, 100));
    TEST_ASSERT_EQUAL(100, bf.total_pages);
    TEST_ASSERT_NOT_NULL(bf.bitmap);
    TEST_ASSERT_EQUAL(13, bf.bitmap_size); /* ceil(100/8) = 13 */

    ndfs_bf_destroy(&bf);
    TEST_ASSERT_NULL(bf.bitmap);
    TEST_ASSERT_EQUAL(0, bf.total_pages);
    return 0;
}

static int test_mark_used_and_free(void)
{
    ndfs_bit_file_t bf;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 64);

    TEST_ASSERT(!ndfs_bf_is_used(&bf, 10));
    ndfs_bf_mark_used(&bf, 10);
    TEST_ASSERT(ndfs_bf_is_used(&bf, 10));
    ndfs_bf_mark_free(&bf, 10);
    TEST_ASSERT(!ndfs_bf_is_used(&bf, 10));

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_count_used(void)
{
    ndfs_bit_file_t bf;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 32);
    TEST_ASSERT_EQUAL(0, ndfs_bf_count_used(&bf));

    ndfs_bf_mark_used(&bf, 0);
    ndfs_bf_mark_used(&bf, 1);
    ndfs_bf_mark_used(&bf, 15);
    TEST_ASSERT_EQUAL(3, ndfs_bf_count_used(&bf));
    TEST_ASSERT_EQUAL(29, ndfs_bf_count_free(&bf));

    ndfs_bf_destroy(&bf);
    return 0;
}

/* Allocation scans HIGH -> LOW, matching genuine SINTRAN (TESTP 51355B only ever does
 * `AAX -1`). These tests previously asserted the old upward answers. */
static int test_find_free(void)
{
    ndfs_bit_file_t bf;
    uint32_t block;
    uint32_t i;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 32);   /* alloc_ceiling defaults to 32 -> top page is 31 */

    /* Mark blocks 0-9 as used */
    for (i = 0; i < 10; i++) {
        ndfs_bf_mark_used(&bf, i);
    }

    /* Downward scan starts at the top of the window, not just above the used run. */
    TEST_ASSERT_OK(ndfs_bf_find_free(&bf, &block));
    TEST_ASSERT_EQUAL(31, block);

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_find_free_respects_capacity_ceiling(void)
{
    ndfs_bit_file_t bf;
    uint32_t block;
    memset(&bf, 0, sizeof(bf));

    /* Device has 1000 pages, but the directory only declares 900 of them. The 100-page
     * gap is the drive's bad-sector spare region: free in the bitmap, never allocated. */
    ndfs_bf_init(&bf, 1000);
    bf.alloc_ceiling = 900;

    TEST_ASSERT_OK(ndfs_bf_find_free(&bf, &block));
    TEST_ASSERT_EQUAL(899, block);   /* capacity - 1, NOT device - 1 */

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_find_free_range(void)
{
    ndfs_bit_file_t bf;
    uint32_t start;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 64);   /* top page is 63 */

    /* Mark blocks 0-6 (system), 10, 12 */
    {
        uint32_t i;
        for (i = 0; i < 7; i++) ndfs_bf_mark_used(&bf, i);
    }
    ndfs_bf_mark_used(&bf, 10);
    ndfs_bf_mark_used(&bf, 12);

    /* Downward: the highest 2-page run is 62-63, so the run STARTS at 62. */
    TEST_ASSERT_OK(ndfs_bf_find_free_range(&bf, 2, &start));
    TEST_ASSERT_EQUAL(62, start);

    /* Highest 5-page run is 59-63 -> starts at 59. */
    TEST_ASSERT_OK(ndfs_bf_find_free_range(&bf, 5, &start));
    TEST_ASSERT_EQUAL(59, start);

    ndfs_bf_destroy(&bf);
    return 0;
}

/* Blocks 0-6 are system-reserved and must never be handed out, even when the rest of the
 * volume is full. */
static int test_find_free_stops_at_block7_floor(void)
{
    ndfs_bit_file_t bf;
    uint32_t block;
    uint32_t i;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 20);
    for (i = NDFS_FIRST_ALLOC_BLOCK; i < 20; i++) ndfs_bf_mark_used(&bf, i);

    TEST_ASSERT_EQUAL(NDFS_ERR_NO_SPACE, ndfs_bf_find_free(&bf, &block));

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_allocate_blocks(void)
{
    ndfs_bit_file_t bf;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 32);

    /* Cannot allocate below FIRST_ALLOC_BLOCK */
    TEST_ASSERT(ndfs_bf_allocate(&bf, 0, 3) != NDFS_OK);

    /* Allocate blocks 7-9 */
    TEST_ASSERT_OK(ndfs_bf_allocate(&bf, 7, 3));
    TEST_ASSERT(ndfs_bf_is_used(&bf, 7));
    TEST_ASSERT(ndfs_bf_is_used(&bf, 8));
    TEST_ASSERT(ndfs_bf_is_used(&bf, 9));
    TEST_ASSERT(!ndfs_bf_is_used(&bf, 10));

    /* Cannot allocate already used blocks */
    TEST_ASSERT(ndfs_bf_allocate(&bf, 8, 2) != NDFS_OK);

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_free_range(void)
{
    ndfs_bit_file_t bf;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 32);
    ndfs_bf_allocate(&bf, 10, 5);
    TEST_ASSERT_EQUAL(5, ndfs_bf_count_used(&bf));

    ndfs_bf_free_range(&bf, 10, 5);
    TEST_ASSERT_EQUAL(0, ndfs_bf_count_used(&bf));

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_bitmap_bit_layout(void)
{
    /* Verify: byte[blockId/8] & (1 << (blockId % 8)) */
    ndfs_bit_file_t bf;
    const uint8_t *data;
    size_t len;
    memset(&bf, 0, sizeof(bf));

    ndfs_bf_init(&bf, 16);
    ndfs_bf_mark_used(&bf, 0);  /* bit 0 of byte 0 */
    ndfs_bf_mark_used(&bf, 7);  /* bit 7 of byte 0 */
    ndfs_bf_mark_used(&bf, 8);  /* bit 0 of byte 1 */

    ndfs_bf_get_data(&bf, &data, &len);
    TEST_ASSERT_EQUAL(2, len);
    TEST_ASSERT_EQUAL(0x81, data[0]); /* bits 0 and 7 */
    TEST_ASSERT_EQUAL(0x01, data[1]); /* bit 0 */

    ndfs_bf_destroy(&bf);
    return 0;
}

static int test_destroy_null(void)
{
    /* Should not crash */
    ndfs_bf_destroy(NULL);
    return 0;
}

void run_bit_file_tests(void)
{
    TEST_SUITE_BEGIN("BitFile Tests");
    RUN_TEST(test_init_and_destroy);
    RUN_TEST(test_mark_used_and_free);
    RUN_TEST(test_count_used);
    RUN_TEST(test_find_free);
    RUN_TEST(test_find_free_respects_capacity_ceiling);
    RUN_TEST(test_find_free_range);
    RUN_TEST(test_find_free_stops_at_block7_floor);
    RUN_TEST(test_allocate_blocks);
    RUN_TEST(test_free_range);
    RUN_TEST(test_bitmap_bit_layout);
    RUN_TEST(test_destroy_null);
}
