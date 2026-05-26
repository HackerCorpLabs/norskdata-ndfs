/**
 * Golden byte-vector tests: real 64-byte entries extracted from a genuine
 * SINTRAN-authored image (tor-disk.img). These pin the on-disk format against
 * ground truth and prove lossless round-trips. If any of these fail, the
 * parser/serializer has drifted from what SINTRAN actually writes.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>

/* SINTRAN:DATA object entry, user SYSTEM (slot 0). */
static const uint8_t GOLDEN_OBJ[64] = {
    0x90,0x00,0x53,0x49,0x4e,0x54,0x52,0x41,0x4e,0x27,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x44,0x41,0x54,0x41,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x20,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x96,0x26,0x96,0x85,0x9a,0x08,0xa1,0x8b,
    0x9a,0x08,0xa1,0x8b,0x00,0x00,0x00,0x3f,0x00,0x01,0xe7,0xff,0x00,0x00,0x00,0x01
};

/* SYSTEM user entry. */
static const uint8_t GOLDEN_USR[64] = {
    0x81,0x03,0x53,0x59,0x53,0x54,0x45,0x4d,0x27,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x57,0x57,0x96,0x26,0x96,0x83,0xb2,0x2b,0x73,0xec,0x00,0x00,0x71,0x7a,
    0x00,0x00,0x4c,0xc2,0x00,0x00,0x00,0x00,0x04,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x87,0x01,0x87,0x07,0x81,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static int test_golden_object_fields(void)
{
    ndfs_object_entry_t oe;
    TEST_ASSERT_OK(ndfs_oe_from_bytes(GOLDEN_OBJ, 64, 0, &oe));

    TEST_ASSERT_EQUAL_STRING("SINTRAN", oe.object_name);
    TEST_ASSERT_EQUAL_STRING("DATA", oe.type);
    TEST_ASSERT_EQUAL(0, oe.next_version);
    TEST_ASSERT_EQUAL(0, oe.prev_version);
    TEST_ASSERT_EQUAL(0x07FF, oe.access_bits);
    TEST_ASSERT_EQUAL(0x0020, oe.file_type_flags);   /* AllocatedFile */
    TEST_ASSERT_EQUAL(0, oe.device_number);
    TEST_ASSERT_EQUAL(0, oe.user_index);
    TEST_ASSERT_EQUAL(0, oe.disk_object_index);
    TEST_ASSERT_EQUAL(0, oe.current_open_count);
    TEST_ASSERT_EQUAL(40, oe.total_open_count);
    TEST_ASSERT_EQUAL(0x96269685u, oe.date_created);
    TEST_ASSERT_EQUAL(0x9a08a18bu, oe.last_read_date);
    TEST_ASSERT_EQUAL(0x9a08a18bu, oe.last_write_date);
    TEST_ASSERT_EQUAL(63, oe.pages_in_file);
    TEST_ASSERT_EQUAL(124928, oe.bytes_in_file);
    return 0;
}

static int test_golden_object_roundtrip(void)
{
    ndfs_object_entry_t oe;
    uint8_t out[64];
    TEST_ASSERT_OK(ndfs_oe_from_bytes(GOLDEN_OBJ, 64, 0, &oe));
    TEST_ASSERT_OK(ndfs_oe_to_bytes(&oe, out, 64, 0));
    TEST_ASSERT(memcmp(GOLDEN_OBJ, out, 64) == 0);
    return 0;
}

static int test_golden_user_fields(void)
{
    ndfs_user_entry_t ue;
    TEST_ASSERT_OK(ndfs_ue_from_bytes(GOLDEN_USR, 64, 0, &ue));

    TEST_ASSERT_EQUAL_STRING("SYSTEM", ue.user_name);
    TEST_ASSERT_EQUAL(3, ue.enter_count);
    TEST_ASSERT_EQUAL(29050, ue.pages_reserved);
    TEST_ASSERT_EQUAL(19650, ue.pages_used);
    TEST_ASSERT_EQUAL(0, ue.user_index);
    TEST_ASSERT_EQUAL(0x04FF, ue.default_file_access);
    TEST_ASSERT(ndfs_uf_is_active(&ue.friends[0]));
    TEST_ASSERT(ndfs_uf_is_active(&ue.friends[1]));
    TEST_ASSERT(ndfs_uf_is_active(&ue.friends[2]));
    TEST_ASSERT(!ndfs_uf_is_active(&ue.friends[3]));
    return 0;
}

static int test_golden_user_roundtrip(void)
{
    ndfs_user_entry_t ue;
    uint8_t out[64];
    TEST_ASSERT_OK(ndfs_ue_from_bytes(GOLDEN_USR, 64, 0, &ue));
    ndfs_ue_to_bytes(&ue, out);
    TEST_ASSERT(memcmp(GOLDEN_USR, out, 64) == 0);
    return 0;
}

/* Object entry whose type field is intentionally empty on disk:
 * offset 18 = 0x27 (terminator) followed by NULs. SINTRAN writes such entries
 * (e.g. TERMINAL). Parsing must NOT default the empty type to "DATA", or the
 * round-trip clobbers 27 00 00 00 with 44 41 54 41. Regression for Bug 1. */
static const uint8_t GOLDEN_OBJ_EMPTY_TYPE[64] = {
    0x90,0x00,0x54,0x45,0x52,0x4d,0x49,0x4e,0x41,0x4c,0x27,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x27,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x20,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x96,0x26,0x96,0x85,0x9a,0x08,0xa1,0x8b,
    0x9a,0x08,0xa1,0x8b,0x00,0x00,0x00,0x3f,0x00,0x01,0xe7,0xff,0x00,0x00,0x00,0x01
};

static int test_golden_empty_type_preserved(void)
{
    ndfs_object_entry_t oe;
    TEST_ASSERT_OK(ndfs_oe_from_bytes(GOLDEN_OBJ_EMPTY_TYPE, 64, 0, &oe));
    TEST_ASSERT_EQUAL_STRING("TERMINAL", oe.object_name);
    /* Empty type must stay empty — not "DATA". */
    TEST_ASSERT_EQUAL_STRING("", oe.type);
    return 0;
}

static int test_golden_empty_type_roundtrip(void)
{
    ndfs_object_entry_t oe;
    uint8_t out[64];
    TEST_ASSERT_OK(ndfs_oe_from_bytes(GOLDEN_OBJ_EMPTY_TYPE, 64, 0, &oe));
    TEST_ASSERT_OK(ndfs_oe_to_bytes(&oe, out, 64, 0));
    /* The empty type field (27 00 00 00) must survive byte-for-byte. */
    TEST_ASSERT(memcmp(GOLDEN_OBJ_EMPTY_TYPE, out, 64) == 0);
    return 0;
}

void run_golden_tests(void)
{
    TEST_SUITE_BEGIN("Golden Byte-Vector Tests");
    RUN_TEST(test_golden_object_fields);
    RUN_TEST(test_golden_object_roundtrip);
    RUN_TEST(test_golden_user_fields);
    RUN_TEST(test_golden_user_roundtrip);
    RUN_TEST(test_golden_empty_type_preserved);
    RUN_TEST(test_golden_empty_type_roundtrip);
}
