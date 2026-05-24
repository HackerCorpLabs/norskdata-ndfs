/**
 * Full filesystem integration tests.
 *
 * Creates a minimal NDFS disk image in memory and exercises the full API.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include "endian_util.h"
#include <string.h>
#include <stdlib.h>

/*
 * Minimal NDFS image layout (32 pages = 64KB):
 *
 * Page 0:  Boot/master block
 * Page 1:  User file index block (8 pointers, only first used -> page 2)
 * Page 2:  User data page (32 user entries, only first used: "SYSTEM")
 * Page 3:  Object file index block (512 pointers, only first used -> page 4)
 * Page 4:  Object data page (32 object entries, first: "HELLO:DATA")
 * Page 5:  Bitmap (bit file)
 * Page 6:  Reserved
 * Page 7+: File data for HELLO:DATA (1 page)
 *          Page 8: Index block for HELLO file
 *          Page 9+: Free
 */
#define TEST_TOTAL_PAGES 32
#define TEST_IMAGE_SIZE  (TEST_TOTAL_PAGES * NDFS_PAGE_SIZE)

static uint8_t *create_test_image(void)
{
    uint8_t *img = (uint8_t *)calloc(TEST_IMAGE_SIZE, 1);
    size_t off, i;
    ndfs_block_pointer_t bp;
    ndfs_user_entry_t ue;
    ndfs_object_entry_t oe;
    uint32_t bitmap_bytes;

    if (!img) return NULL;

    /* ── Page 0: Master block ── */
    off = NDFS_MASTER_BLOCK_OFFSET;

    /* Directory name: "TESTDISK" */
    {
        const char *name = "TESTDISK";
        size_t nlen = strlen(name);
        for (i = 0; i < nlen; i++) img[off + i] = (uint8_t)name[i];
        for (i = nlen; i < NDFS_NAME_MAX; i++) img[off + i] = 0x27;
    }

    /* Object file pointer: indexed, blockId=3 */
    bp.block_id = 3; bp.type = NDFS_PTR_INDEXED;
    ndfs_bp_to_bytes(&bp, img, off + 0x10);

    /* User file pointer: indexed, blockId=1 */
    bp.block_id = 1; bp.type = NDFS_PTR_INDEXED;
    ndfs_bp_to_bytes(&bp, img, off + 0x14);

    /* Bit file pointer: contiguous, blockId=5 */
    bp.block_id = 5; bp.type = NDFS_PTR_CONTIGUOUS;
    ndfs_bp_to_bytes(&bp, img, off + 0x18);

    /* Unreserved pages */
    ndfs_write_u32be(img, off + 0x1C, 20);

    /* ── Page 1: User file index block ── */
    /* Pointer 0 -> page 2 (user data) */
    bp.block_id = 2; bp.type = NDFS_PTR_CONTIGUOUS;
    ndfs_bp_to_bytes(&bp, img + 1 * NDFS_PAGE_SIZE, 0);

    /* ── Page 2: User data (one user: "SYSTEM") ── */
    ndfs_ue_init(&ue);
    strcpy(ue.user_name, "SYSTEM");
    ue.user_index = 0;
    ue.pages_reserved = 500;
    ue.pages_used = 2; /* 1 data page + 1 index block */
    ndfs_ue_to_bytes(&ue, img + 2 * NDFS_PAGE_SIZE);

    /* ── Page 3: Object file index block ── */
    /* Pointer 0 -> page 4 (object data) */
    bp.block_id = 4; bp.type = NDFS_PTR_CONTIGUOUS;
    ndfs_bp_to_bytes(&bp, img + 3 * NDFS_PAGE_SIZE, 0);

    /* ── Page 4: Object data (one file: "HELLO:DATA") ── */
    ndfs_oe_init(&oe);
    strcpy(oe.object_name, "HELLO");
    strcpy(oe.type, "DATA");
    oe.user_index = 0;
    oe.pages_in_file = 1;
    oe.bytes_in_file = 13; /* "Hello, NDFS!\n" */
    oe.file_pointer.block_id = 8; /* Index block at page 8 */
    oe.file_pointer.type = NDFS_PTR_INDEXED;
    ndfs_oe_to_bytes(&oe, img + 4 * NDFS_PAGE_SIZE, NDFS_PAGE_SIZE, 0);

    /* ── Page 8: Index block for HELLO file ── */
    /* Pointer 0 -> page 7 (data page) */
    bp.block_id = 7; bp.type = NDFS_PTR_CONTIGUOUS;
    ndfs_bp_to_bytes(&bp, img + 8 * NDFS_PAGE_SIZE, 0);

    /* ── Page 7: File data ── */
    memcpy(img + 7 * NDFS_PAGE_SIZE, "Hello, NDFS!\n", 13);

    /* ── Page 5: Bitmap ── */
    bitmap_bytes = (TEST_TOTAL_PAGES + 7) / 8;
    /* Mark pages 0-8 as used */
    for (i = 0; i <= 8; i++) {
        size_t byte_idx = i / 8;
        uint8_t bit_idx = (uint8_t)(i % 8);
        img[5 * NDFS_PAGE_SIZE + byte_idx] |= (uint8_t)(1u << bit_idx);
    }
    /* Unused: ensure rest is 0 (already calloc'd) */
    (void)bitmap_bytes;

    return img;
}

static int test_open_close(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_error_t err;

    TEST_ASSERT_NOT_NULL(img);

    err = ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);
    TEST_ASSERT_OK(err);
    TEST_ASSERT_NOT_NULL(fs);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_directory_name(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    char name[NDFS_NAME_MAX + 1];

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);
    ndfs_get_directory_name(fs, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("TESTDISK", name);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_list_root(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);
    ndfs_list_directory(fs, "", &entries, &count);

    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", entries[0].name);
    TEST_ASSERT(entries[0].is_directory);

    ndfs_free_entries(entries);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_list_user(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);
    ndfs_list_directory(fs, "SYSTEM", &entries, &count);

    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_STRING("HELLO", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("DATA", entries[0].type);
    TEST_ASSERT(!entries[0].is_directory);
    TEST_ASSERT_EQUAL(13, entries[0].size);

    ndfs_free_entries(entries);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_read_file(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *data = NULL;
    size_t size = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/HELLO:DATA", &data, &size));
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL(13, size);
    TEST_ASSERT(memcmp(data, "Hello, NDFS!\n", 13) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_read_file_not_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *data = NULL;
    size_t size = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_read_file(fs, "SYSTEM/NOEXIST:DATA", &data, &size));

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_write_file(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *data = NULL;
    size_t size = 0;
    const char *content = "New file content!";

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/NEWFILE:DATA",
                                   (const uint8_t *)content, strlen(content)));

    /* Read it back */
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/NEWFILE:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(content), size);
    TEST_ASSERT(memcmp(data, content, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_write_read_only(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_EQUAL(NDFS_ERR_READ_ONLY,
                      ndfs_write_file(fs, "SYSTEM/TEST:DATA",
                                      (const uint8_t *)"x", 1));

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_delete_file(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/HELLO:DATA"));

    /* Verify it's gone */
    ndfs_list_directory(fs, "SYSTEM", &entries, &count);
    TEST_ASSERT_EQUAL(0, count);

    ndfs_free_entries(entries);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_rename_file(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *data = NULL;
    size_t size = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_rename(fs, "SYSTEM/HELLO:DATA", "RENAMED:PROG"));

    /* Old name should not be found */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_read_file(fs, "SYSTEM/HELLO:DATA", &data, &size));

    /* New name should work */
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/RENAMED:PROG", &data, &size));
    TEST_ASSERT_EQUAL(13, size);

    ndfs_free_data(data);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_users(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", users[0].user_name);
    TEST_ASSERT_EQUAL(500, users[0].pages_reserved);

    ndfs_free_users(users);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_add_user(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_add_user(fs, "GUEST", 100));

    ndfs_get_users(fs, &users, &count);
    TEST_ASSERT_EQUAL(2, count);

    /* Check that duplicate is rejected */
    TEST_ASSERT_EQUAL(NDFS_ERR_ALREADY_EXISTS,
                      ndfs_add_user(fs, "GUEST", 100));

    ndfs_free_users(users);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_remove_user(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    /* Cannot remove user with files */
    TEST_ASSERT_EQUAL(NDFS_ERR_HAS_FILES, ndfs_remove_user(fs, 0));

    /* Add a user without files, then remove */
    ndfs_add_user(fs, "TEMP", 50);
    ndfs_get_users(fs, &users, &count);
    {
        uint8_t temp_idx = users[count - 1].user_index;
        ndfs_free_users(users);

        TEST_ASSERT_OK(ndfs_remove_user(fs, temp_idx));

        ndfs_get_users(fs, &users, &count);
        TEST_ASSERT_EQUAL(1, count);
        ndfs_free_users(users);
    }

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_update_quota(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_update_user_quota(fs, 0, 999));

    ndfs_get_users(fs, &users, &count);
    TEST_ASSERT_EQUAL(999, users[0].pages_reserved);

    ndfs_free_users(users);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_clear_password(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    TEST_ASSERT_OK(ndfs_clear_user_password(fs, "SYSTEM"));
    TEST_ASSERT_OK(ndfs_clear_user_password_by_index(fs, 0));

    /* Non-existent user */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_clear_user_password(fs, "NOBODY"));

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_bitmap_queries(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint32_t free_pages = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    /* Pages 0-8 should be used */
    TEST_ASSERT(ndfs_is_block_used(fs, 0));
    TEST_ASSERT(ndfs_is_block_used(fs, 5));
    TEST_ASSERT(ndfs_is_block_used(fs, 8));
    TEST_ASSERT(!ndfs_is_block_used(fs, 9));
    TEST_ASSERT(!ndfs_is_block_used(fs, 31));

    ndfs_get_free_pages(fs, &free_pages);
    TEST_ASSERT_EQUAL(TEST_TOTAL_PAGES - 9, free_pages);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_verify_integrity(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    bool ok = false;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_verify_integrity(fs, &ok));
    TEST_ASSERT(ok);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_generate_report(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    char *report = NULL;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_generate_report(fs, &report));
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT(strstr(report, "TESTDISK") != NULL);
    TEST_ASSERT(strstr(report, "SYSTEM") != NULL);
    TEST_ASSERT(strstr(report, "HELLO") != NULL);

    ndfs_free_string(report);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_to_buffer(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *exported = NULL;
    size_t exported_size = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    TEST_ASSERT_NOT_NULL(exported);
    TEST_ASSERT_EQUAL(TEST_IMAGE_SIZE, exported_size);

    ndfs_free_data(exported);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_strerror(void)
{
    TEST_ASSERT(strlen(ndfs_strerror(NDFS_OK)) > 0);
    TEST_ASSERT(strlen(ndfs_strerror(NDFS_ERR_NOT_FOUND)) > 0);
    TEST_ASSERT(strlen(ndfs_strerror(-999)) > 0); /* Unknown */
    return 0;
}

static int test_open_too_small(void)
{
    uint8_t small[100] = {0};
    ndfs_filesystem_t *fs = NULL;
    TEST_ASSERT_EQUAL(NDFS_ERR_TOO_SMALL,
                      ndfs_open_buffer_copy(small, sizeof(small), true, &fs));
    return 0;
}

static int test_open_not_aligned(void)
{
    uint8_t buf[NDFS_PAGE_SIZE + 100];
    ndfs_filesystem_t *fs = NULL;
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_ALIGNED,
                      ndfs_open_buffer_copy(buf, sizeof(buf), true, &fs));
    return 0;
}

static int test_close_null(void)
{
    ndfs_close(NULL); /* Should not crash */
    return 0;
}

static int test_free_null(void)
{
    ndfs_free_entries(NULL);
    ndfs_free_data(NULL);
    ndfs_free_users(NULL);
    ndfs_free_string(NULL);
    return 0;
}

static int test_overwrite_file(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint8_t *data = NULL;
    size_t size = 0;
    const char *new_content = "Overwritten content here!!";

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    /* Overwrite HELLO:DATA */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/HELLO:DATA",
                                   (const uint8_t *)new_content,
                                   strlen(new_content)));

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/HELLO:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(new_content), size);
    TEST_ASSERT(memcmp(data, new_content, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_write_and_delete_frees_space(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint32_t free_before, free_after_write, free_after_delete;
    const char *content = "Temporary file";

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, false, &fs);

    ndfs_get_free_pages(fs, &free_before);

    ndfs_write_file(fs, "SYSTEM/TEMP:DATA",
                    (const uint8_t *)content, strlen(content));
    ndfs_get_free_pages(fs, &free_after_write);
    TEST_ASSERT(free_after_write < free_before);

    ndfs_delete_file(fs, "SYSTEM/TEMP:DATA");
    ndfs_get_free_pages(fs, &free_after_delete);
    TEST_ASSERT_EQUAL(free_before, free_after_delete);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_master_block_access(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    const ndfs_master_block_t *mb = NULL;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_NOT_NULL(mb);
    TEST_ASSERT_EQUAL_STRING("TESTDISK", mb->directory_name);
    TEST_ASSERT_EQUAL(3, mb->object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(1, mb->user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(5, mb->bit_file_ptr.block_id);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_file_exists_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    bool exists = false;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_file_exists(fs, "SYSTEM/HELLO:DATA", &exists));
    TEST_ASSERT(exists);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_file_exists_not_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    bool exists = true;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_file_exists(fs, "SYSTEM/NOEXIST:DATA", &exists));
    TEST_ASSERT(!exists);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_metadata_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_file_entry_t entry;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_metadata(fs, "SYSTEM/HELLO:DATA", &entry));
    TEST_ASSERT_EQUAL_STRING("HELLO", entry.name);
    TEST_ASSERT_EQUAL_STRING("DATA", entry.type);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", entry.user_name);
    TEST_ASSERT_EQUAL(13, entry.size);
    TEST_ASSERT_EQUAL(1, entry.pages);
    TEST_ASSERT(!entry.is_directory);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_metadata_not_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_file_entry_t entry;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_get_metadata(fs, "SYSTEM/NOEXIST:DATA", &entry));

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_user_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t user;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_user(fs, 0, &user));
    TEST_ASSERT_EQUAL_STRING("SYSTEM", user.user_name);
    TEST_ASSERT_EQUAL(500, user.pages_reserved);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_user_not_found(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_user_entry_t user;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND, ndfs_get_user(fs, 99, &user));

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_used_pages(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    uint32_t used = 0;
    uint32_t free_pages = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_used_pages(fs, &used));
    TEST_ASSERT_EQUAL(9, used); /* Pages 0-8 are used */

    /* Verify used + free = total */
    ndfs_get_free_pages(fs, &free_pages);
    TEST_ASSERT_EQUAL(TEST_TOTAL_PAGES, used + free_pages);

    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_object_entries(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_object_entry_t *entries = NULL;
    size_t count = 0;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    TEST_ASSERT_OK(ndfs_get_object_entries(fs, &entries, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_STRING("HELLO", entries[0].object_name);
    TEST_ASSERT_EQUAL_STRING("DATA", entries[0].type);
    TEST_ASSERT_EQUAL(13, entries[0].bytes_in_file);

    ndfs_free_object_entries(entries);
    ndfs_close(fs);
    free(img);
    return 0;
}

static int test_get_object_entry(void)
{
    uint8_t *img = create_test_image();
    ndfs_filesystem_t *fs = NULL;
    ndfs_object_entry_t entry;

    ndfs_open_buffer_copy(img, TEST_IMAGE_SIZE, true, &fs);

    /* Found by name + user */
    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "HELLO:DATA", "SYSTEM", &entry));
    TEST_ASSERT_EQUAL_STRING("HELLO", entry.object_name);
    TEST_ASSERT_EQUAL_STRING("DATA", entry.type);
    TEST_ASSERT_EQUAL(13, entry.bytes_in_file);

    /* Found by name only (no user filter) */
    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "HELLO:DATA", NULL, &entry));
    TEST_ASSERT_EQUAL_STRING("HELLO", entry.object_name);

    /* Not found */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_get_object_entry(fs, "NOEXIST:DATA", "SYSTEM", &entry));

    ndfs_close(fs);
    free(img);
    return 0;
}

void run_filesystem_tests(void)
{
    TEST_SUITE_BEGIN("Filesystem Tests");
    RUN_TEST(test_open_close);
    RUN_TEST(test_directory_name);
    RUN_TEST(test_list_root);
    RUN_TEST(test_list_user);
    RUN_TEST(test_read_file);
    RUN_TEST(test_read_file_not_found);
    RUN_TEST(test_write_file);
    RUN_TEST(test_write_read_only);
    RUN_TEST(test_delete_file);
    RUN_TEST(test_rename_file);
    RUN_TEST(test_get_users);
    RUN_TEST(test_add_user);
    RUN_TEST(test_remove_user);
    RUN_TEST(test_update_quota);
    RUN_TEST(test_clear_password);
    RUN_TEST(test_bitmap_queries);
    RUN_TEST(test_verify_integrity);
    RUN_TEST(test_generate_report);
    RUN_TEST(test_to_buffer);
    RUN_TEST(test_strerror);
    RUN_TEST(test_open_too_small);
    RUN_TEST(test_open_not_aligned);
    RUN_TEST(test_close_null);
    RUN_TEST(test_free_null);
    RUN_TEST(test_overwrite_file);
    RUN_TEST(test_write_and_delete_frees_space);
    RUN_TEST(test_master_block_access);
    RUN_TEST(test_file_exists_found);
    RUN_TEST(test_file_exists_not_found);
    RUN_TEST(test_get_metadata_found);
    RUN_TEST(test_get_metadata_not_found);
    RUN_TEST(test_get_user_found);
    RUN_TEST(test_get_user_not_found);
    RUN_TEST(test_get_used_pages);
    RUN_TEST(test_get_object_entries);
    RUN_TEST(test_get_object_entry);
}
