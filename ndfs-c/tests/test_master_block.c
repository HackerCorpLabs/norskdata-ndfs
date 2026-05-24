/**
 * Tests for MasterBlock.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/master_block.h>
#include "endian_util.h"
#include <string.h>

/* Helper: create a minimal valid page 0 */
static void make_page0(uint8_t *page, const char *name,
                       uint32_t obj_ptr, uint32_t user_ptr,
                       uint32_t bit_ptr, uint32_t unreserved)
{
    size_t off = NDFS_MASTER_BLOCK_OFFSET;
    size_t i;
    size_t name_len;

    memset(page, 0, NDFS_PAGE_SIZE);

    /* Write name with 0x27 terminator padding */
    name_len = strlen(name);
    if (name_len > NDFS_NAME_MAX) name_len = NDFS_NAME_MAX;
    for (i = 0; i < name_len; i++) {
        page[off + i] = (uint8_t)name[i];
    }
    for (i = name_len; i < NDFS_NAME_MAX; i++) {
        page[off + i] = 0x27;
    }

    ndfs_write_u32be(page, off + 0x10, obj_ptr);
    ndfs_write_u32be(page, off + 0x14, user_ptr);
    ndfs_write_u32be(page, off + 0x18, bit_ptr);
    ndfs_write_u32be(page, off + 0x1C, unreserved);
}

static int test_parse_valid(void)
{
    uint8_t page[NDFS_PAGE_SIZE];
    ndfs_master_block_t mb;
    ndfs_error_t err;

    /* obj=indexed(3), user=indexed(4), bit=contiguous(5) */
    make_page0(page, "TESTDISK",
               (1u << 30) | 3,  /* indexed, blockId=3 */
               (1u << 30) | 4,  /* indexed, blockId=4 */
               5,               /* contiguous, blockId=5 */
               100);

    err = ndfs_mb_parse(page, &mb);
    TEST_ASSERT_OK(err);
    TEST_ASSERT_EQUAL_STRING("TESTDISK", mb.directory_name);
    TEST_ASSERT_EQUAL(3, mb.object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_INDEXED, mb.object_file_ptr.type);
    TEST_ASSERT_EQUAL(4, mb.user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(5, mb.bit_file_ptr.block_id);
    TEST_ASSERT_EQUAL(100, mb.unreserved_pages);
    return 0;
}

static int test_is_valid(void)
{
    ndfs_master_block_t mb;
    memset(&mb, 0, sizeof(mb));

    /* No name, no pointers -> invalid */
    TEST_ASSERT(!ndfs_mb_is_valid(&mb));

    /* With name only -> valid */
    strcpy(mb.directory_name, "DISK");
    TEST_ASSERT(ndfs_mb_is_valid(&mb));

    /* With pointer only -> valid */
    mb.directory_name[0] = '\0';
    mb.object_file_ptr.block_id = 3;
    mb.object_file_ptr.type = NDFS_PTR_INDEXED;
    TEST_ASSERT(ndfs_mb_is_valid(&mb));

    /* Non-printable name -> invalid */
    mb.directory_name[0] = 0x01;
    mb.directory_name[1] = '\0';
    mb.object_file_ptr.block_id = 0;
    TEST_ASSERT(!ndfs_mb_is_valid(&mb));

    return 0;
}

static int test_write_roundtrip(void)
{
    uint8_t page[NDFS_PAGE_SIZE];
    ndfs_master_block_t mb, mb2;
    ndfs_error_t err;

    memset(page, 0, NDFS_PAGE_SIZE);
    memset(&mb, 0, sizeof(mb));

    strcpy(mb.directory_name, "ROUNDTRIP");
    mb.object_file_ptr.block_id = 10;
    mb.object_file_ptr.type = NDFS_PTR_INDEXED;
    mb.user_file_ptr.block_id = 11;
    mb.user_file_ptr.type = NDFS_PTR_INDEXED;
    mb.bit_file_ptr.block_id = 12;
    mb.bit_file_ptr.type = NDFS_PTR_CONTIGUOUS;
    mb.unreserved_pages = 500;

    err = ndfs_mb_write(&mb, page);
    TEST_ASSERT_OK(err);

    err = ndfs_mb_parse(page, &mb2);
    TEST_ASSERT_OK(err);

    TEST_ASSERT_EQUAL_STRING("ROUNDTRIP", mb2.directory_name);
    TEST_ASSERT_EQUAL(10, mb2.object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(11, mb2.user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(12, mb2.bit_file_ptr.block_id);
    TEST_ASSERT_EQUAL(500, mb2.unreserved_pages);

    return 0;
}

static int test_null_args(void)
{
    ndfs_master_block_t mb;
    TEST_ASSERT_EQUAL(NDFS_ERR_NULL_PTR, ndfs_mb_parse(NULL, &mb));
    TEST_ASSERT_EQUAL(NDFS_ERR_NULL_PTR, ndfs_mb_write(&mb, NULL));
    return 0;
}

void run_master_block_tests(void)
{
    TEST_SUITE_BEGIN("MasterBlock Tests");
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_is_valid);
    RUN_TEST(test_write_roundtrip);
    RUN_TEST(test_null_args);
}
