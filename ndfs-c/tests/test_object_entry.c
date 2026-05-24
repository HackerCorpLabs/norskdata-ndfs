/**
 * Tests for ObjectEntry.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/object_entry.h>
#include <string.h>

static int test_init(void)
{
    ndfs_object_entry_t oe;
    ndfs_oe_init(&oe);
    TEST_ASSERT_EQUAL(NDFS_OBJECT_IN_USE, oe.header);
    TEST_ASSERT_EQUAL_STRING("DATA", oe.type);
    TEST_ASSERT_EQUAL(0, oe.pages_in_file);
    TEST_ASSERT_EQUAL(0, oe.bytes_in_file);
    return 0;
}

static int test_roundtrip(void)
{
    ndfs_object_entry_t oe, oe2;
    uint8_t buf[NDFS_ENTRY_SIZE];
    ndfs_error_t err;

    ndfs_oe_init(&oe);
    strcpy(oe.object_name, "MYFILE");
    strcpy(oe.type, "PROG");
    oe.user_index = 2;
    oe.file_type = 1; /* PROG */
    oe.pages_in_file = 5;
    oe.bytes_in_file = 9000;
    oe.file_pointer.block_id = 42;
    oe.file_pointer.type = NDFS_PTR_INDEXED;

    err = ndfs_oe_to_bytes(&oe, buf, NDFS_ENTRY_SIZE, 0);
    TEST_ASSERT_OK(err);

    err = ndfs_oe_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &oe2);
    TEST_ASSERT_OK(err);
    TEST_ASSERT_EQUAL_STRING("MYFILE", oe2.object_name);
    TEST_ASSERT_EQUAL_STRING("PROG", oe2.type);
    TEST_ASSERT_EQUAL(2, oe2.user_index);
    TEST_ASSERT_EQUAL(1, oe2.file_type);
    TEST_ASSERT_EQUAL(5, oe2.pages_in_file);
    TEST_ASSERT_EQUAL(9000, oe2.bytes_in_file);
    TEST_ASSERT_EQUAL(42, oe2.file_pointer.block_id);
    TEST_ASSERT_EQUAL(NDFS_PTR_INDEXED, oe2.file_pointer.type);
    return 0;
}

static int test_bytes_in_file_encoding(void)
{
    /* bytes_in_file = stored_value + 1 */
    ndfs_object_entry_t oe, oe2;
    uint8_t buf[NDFS_ENTRY_SIZE];

    ndfs_oe_init(&oe);
    strcpy(oe.object_name, "TEST");
    oe.bytes_in_file = 1; /* Should store 0 in bytes 56-59 */
    oe.file_pointer.block_id = 10;
    oe.file_pointer.type = NDFS_PTR_CONTIGUOUS;

    ndfs_oe_to_bytes(&oe, buf, NDFS_ENTRY_SIZE, 0);

    /* Bytes 56-59 should be 0 (bytes_in_file - 1 = 0) */
    TEST_ASSERT_EQUAL(0, buf[56]);
    TEST_ASSERT_EQUAL(0, buf[57]);
    TEST_ASSERT_EQUAL(0, buf[58]);
    TEST_ASSERT_EQUAL(0, buf[59]);

    ndfs_oe_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &oe2);
    TEST_ASSERT_EQUAL(1, oe2.bytes_in_file);
    return 0;
}

static int test_not_in_use(void)
{
    ndfs_object_entry_t oe;
    uint8_t buf[NDFS_ENTRY_SIZE];
    memset(buf, 0, NDFS_ENTRY_SIZE);
    buf[0] = 0x00; /* Not in use */

    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_oe_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &oe));
    return 0;
}

static int test_full_name(void)
{
    ndfs_object_entry_t oe;
    char buf[32];

    ndfs_oe_init(&oe);
    strcpy(oe.object_name, "MYFILE");
    strcpy(oe.type, "PROG");

    ndfs_oe_full_name(&oe, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("MYFILE:PROG", buf);
    return 0;
}

static int test_name_uppercase(void)
{
    ndfs_object_entry_t oe, oe2;
    uint8_t buf[NDFS_ENTRY_SIZE];

    ndfs_oe_init(&oe);
    strcpy(oe.object_name, "lowercase");
    strcpy(oe.type, "data");
    oe.file_pointer.block_id = 10;
    oe.file_pointer.type = NDFS_PTR_CONTIGUOUS;

    ndfs_oe_to_bytes(&oe, buf, NDFS_ENTRY_SIZE, 0);
    ndfs_oe_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &oe2);

    TEST_ASSERT_EQUAL_STRING("LOWERCASE", oe2.object_name);
    TEST_ASSERT_EQUAL_STRING("DATA", oe2.type);
    return 0;
}

void run_object_entry_tests(void)
{
    TEST_SUITE_BEGIN("ObjectEntry Tests");
    RUN_TEST(test_init);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_bytes_in_file_encoding);
    RUN_TEST(test_not_in_use);
    RUN_TEST(test_full_name);
    RUN_TEST(test_name_uppercase);
}
