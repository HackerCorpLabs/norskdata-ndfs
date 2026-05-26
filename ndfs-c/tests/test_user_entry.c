/**
 * Tests for UserEntry.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/user_entry.h>
#include <string.h>

static int test_init(void)
{
    ndfs_user_entry_t ue;
    ndfs_ue_init(&ue);
    TEST_ASSERT_EQUAL(0, ue.user_index);
    TEST_ASSERT_EQUAL_STRING("", ue.user_name);
    TEST_ASSERT_EQUAL(0, ue.password);
    TEST_ASSERT_EQUAL(0x04FF, ue.default_file_access);
    return 0;
}

static int test_roundtrip(void)
{
    ndfs_user_entry_t ue, ue2;
    uint8_t buf[NDFS_ENTRY_SIZE];
    ndfs_error_t err;

    ndfs_ue_init(&ue);
    strcpy(ue.user_name, "TESTUSER");
    ue.user_index = 3;
    ue.password = 0x1234;
    ue.enter_count = 5;
    ue.pages_reserved = 1000;
    ue.pages_used = 42;
    ue.directory_index = 1;
    ue.default_file_access = 0x04FF;

    ndfs_ue_to_bytes(&ue, buf);

    /* Verify flag byte */
    TEST_ASSERT_EQUAL(NDFS_USER_ENTRY_FLAG, buf[0]);

    /* Parse back */
    err = ndfs_ue_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &ue2);
    TEST_ASSERT_OK(err);
    TEST_ASSERT_EQUAL_STRING("TESTUSER", ue2.user_name);
    TEST_ASSERT_EQUAL(3, ue2.user_index);
    TEST_ASSERT_EQUAL(0x1234, ue2.password);
    TEST_ASSERT_EQUAL(5, ue2.enter_count);
    TEST_ASSERT_EQUAL(1000, ue2.pages_reserved);
    TEST_ASSERT_EQUAL(42, ue2.pages_used);
    TEST_ASSERT_EQUAL(1, ue2.directory_index);
    return 0;
}

static int test_invalid_flag(void)
{
    ndfs_user_entry_t ue;
    uint8_t buf[NDFS_ENTRY_SIZE];
    memset(buf, 0, NDFS_ENTRY_SIZE);
    buf[0] = 0x00; /* Invalid flag */

    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_ue_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &ue));
    return 0;
}

static int test_too_small(void)
{
    ndfs_user_entry_t ue;
    uint8_t buf[10];
    memset(buf, 0, sizeof(buf));

    TEST_ASSERT_EQUAL(NDFS_ERR_TOO_SMALL,
                      ndfs_ue_from_bytes(buf, sizeof(buf), 0, &ue));
    return 0;
}

static int test_over_quota(void)
{
    ndfs_user_entry_t ue;
    ndfs_ue_init(&ue);
    ue.pages_reserved = 100;
    ue.pages_used = 50;
    TEST_ASSERT(!ndfs_ue_is_over_quota(&ue));
    TEST_ASSERT_EQUAL(50, ndfs_ue_free_pages(&ue));

    ue.pages_used = 150;
    TEST_ASSERT(ndfs_ue_is_over_quota(&ue));
    return 0;
}

static int test_friends_roundtrip(void)
{
    ndfs_user_entry_t ue, ue2;
    uint8_t buf[NDFS_ENTRY_SIZE];

    ndfs_ue_init(&ue);
    strcpy(ue.user_name, "ALICE");
    ue.user_index = 0;

    /* Add a friend */
    ue.friends[0] = ndfs_uf_create(5, true, true, false, false, false);

    ndfs_ue_to_bytes(&ue, buf);
    ndfs_ue_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &ue2);

    TEST_ASSERT(ndfs_uf_is_active(&ue2.friends[0]));
    TEST_ASSERT_EQUAL(5, ndfs_uf_friend_index(&ue2.friends[0]));
    TEST_ASSERT(ndfs_uf_read_access(&ue2.friends[0]));
    TEST_ASSERT(ndfs_uf_write_access(&ue2.friends[0]));
    TEST_ASSERT(!ndfs_uf_append_access(&ue2.friends[0]));

    return 0;
}

static int test_name_uppercase(void)
{
    /* ndfs_write_name (called by ndfs_ue_to_bytes) should uppercase */
    ndfs_user_entry_t ue, ue2;
    uint8_t buf[NDFS_ENTRY_SIZE];

    ndfs_ue_init(&ue);
    strcpy(ue.user_name, "lowercase");
    ue.user_index = 0;

    ndfs_ue_to_bytes(&ue, buf);
    ndfs_ue_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &ue2);

    TEST_ASSERT_EQUAL_STRING("LOWERCASE", ue2.user_name);
    return 0;
}

/* Regression: default file access lives at offset 40 (not 38) and friends
 * start at offset 48 (not 40). Verified against real ND images. */
static int test_access_and_friend_offsets(void)
{
    uint8_t buf[NDFS_ENTRY_SIZE];
    ndfs_user_entry_t ue;
    size_t i;

    for (i = 0; i < NDFS_ENTRY_SIZE; i++) buf[i] = 0;
    buf[0] = NDFS_USER_ENTRY_FLAG;
    buf[37] = 0;                  /* user index */
    buf[38] = 0x00; buf[39] = 0x00;  /* offset 38 is unused -> must be ignored */
    buf[40] = 0x04; buf[41] = 0xFF;  /* default access at offset 40 */
    buf[48] = 0x87; buf[49] = 0x01;  /* first friend at offset 48 */

    TEST_ASSERT_OK(ndfs_ue_from_bytes(buf, NDFS_ENTRY_SIZE, 0, &ue));
    TEST_ASSERT_EQUAL(0x04FF, ue.default_file_access);
    TEST_ASSERT(ndfs_uf_is_active(&ue.friends[0]));

    /* Round-trip must put the bytes back at the same offsets. */
    {
        uint8_t out[NDFS_ENTRY_SIZE];
        ndfs_ue_to_bytes(&ue, out);
        TEST_ASSERT_EQUAL(0x04, out[40]);
        TEST_ASSERT_EQUAL(0xFF, out[41]);
        TEST_ASSERT_EQUAL(0x87, out[48]);
        TEST_ASSERT_EQUAL(0x01, out[49]);
        TEST_ASSERT_EQUAL(0x00, out[38]);
    }
    return 0;
}

void run_user_entry_tests(void)
{
    TEST_SUITE_BEGIN("UserEntry Tests");
    RUN_TEST(test_access_and_friend_offsets);
    RUN_TEST(test_init);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_invalid_flag);
    RUN_TEST(test_too_small);
    RUN_TEST(test_over_quota);
    RUN_TEST(test_friends_roundtrip);
    RUN_TEST(test_name_uppercase);
}
