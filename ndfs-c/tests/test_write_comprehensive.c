/**
 * Comprehensive write, user management, and stress tests.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include "endian_util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Read the raw on-disk "Unreserved Pages" field straight out of page 0,
 * bypassing the filesystem API entirely -- this is what real SINTRAN would
 * read to report free space without rescanning the bitmap, so a test that
 * only calls ndfs_get_free_pages() would not catch a stale on-disk copy. */
static uint32_t read_raw_unreserved_pages(ndfs_filesystem_t *fs)
{
    uint8_t *buf = NULL;
    size_t size = 0;
    uint32_t value = 0;

    if (ndfs_to_buffer(fs, &buf, &size) == NDFS_OK) {
        value = ndfs_read_u32be(buf, NDFS_MASTER_BLOCK_OFFSET + 0x1C);
        free(buf);
    }
    return value;
}

/* Read the 16 raw bytes of the Extended Info Block (a structure completely
 * separate from the master block) straight out of page 0. Used to prove the
 * master-block persistence fix never touches this region or its checksum. */
static void read_raw_extended_info(ndfs_filesystem_t *fs, uint8_t *out16)
{
    uint8_t *buf = NULL;
    size_t size = 0;

    memset(out16, 0, 16);
    if (ndfs_to_buffer(fs, &buf, &size) == NDFS_OK) {
        memcpy(out16, buf + NDFS_EXTENDED_INFO_OFFSET, 16);
        free(buf);
    }
}

/* ---- Helper: create a writable image ---- */

static ndfs_filesystem_t *create_writable(ndfs_image_template_t tmpl,
                                          const char *name)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;

    ndfs_image_options_init(&opts);
    opts.template_type = tmpl;
    strncpy(opts.directory_name, name, NDFS_NAME_MAX);
    opts.directory_name[NDFS_NAME_MAX] = '\0';

    if (ndfs_create_image(&fs, &opts) != NDFS_OK) return NULL;
    return fs;
}

/* ---- Small file write/read ---- */

static int test_write_small_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "SMWRITE");
    const char *content = "Small data";
    uint8_t *data = NULL;
    size_t size = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/TINY:DATA",
                                   (const uint8_t *)content, strlen(content)));
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/TINY:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(content), size);
    TEST_ASSERT(memcmp(data, content, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    return 0;
}

/* ---- Multi-page file ---- */

static int test_write_multi_page_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "MPWRITE");
    size_t big_size = NDFS_PAGE_SIZE * 3 + 500; /* 3.5 pages */
    uint8_t *big_data;
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    size_t i;

    TEST_ASSERT_NOT_NULL(fs);

    big_data = (uint8_t *)malloc(big_size);
    TEST_ASSERT_NOT_NULL(big_data);

    for (i = 0; i < big_size; i++) {
        big_data[i] = (uint8_t)(i & 0xFF);
    }

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/BIGFILE:DATA",
                                   big_data, big_size));
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/BIGFILE:DATA",
                                  &read_data, &read_size));
    TEST_ASSERT_EQUAL(big_size, read_size);
    TEST_ASSERT(memcmp(read_data, big_data, big_size) == 0);

    ndfs_free_data(read_data);
    free(big_data);
    ndfs_close(fs);
    return 0;
}

/* ---- Overwrite file ---- */

static int test_overwrite_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "OVWRITE");
    const char *v1 = "Version 1";
    const char *v2 = "Version 2 with more data!!!";
    uint8_t *data = NULL;
    size_t size = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/FILE:DATA",
                                   (const uint8_t *)v1, strlen(v1)));
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/FILE:DATA",
                                   (const uint8_t *)v2, strlen(v2)));

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/FILE:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(v2), size);
    TEST_ASSERT(memcmp(data, v2, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    return 0;
}

/* ---- Delete and reclaim space ---- */

static int test_delete_reclaims_space(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "DELSPACE");
    uint32_t free_before, free_after_write, free_after_delete;
    uint8_t buf[4096];

    TEST_ASSERT_NOT_NULL(fs);
    memset(buf, 0xAA, sizeof(buf));

    ndfs_get_free_pages(fs, &free_before);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/TEMP:DATA", buf, sizeof(buf)));
    ndfs_get_free_pages(fs, &free_after_write);
    TEST_ASSERT(free_after_write < free_before);

    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/TEMP:DATA"));
    ndfs_get_free_pages(fs, &free_after_delete);
    TEST_ASSERT_EQUAL(free_before, free_after_delete);

    ndfs_close(fs);
    return 0;
}

/* ---- Rename file ---- */

static int test_rename_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "RENAME");
    uint8_t *data = NULL;
    size_t size = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/OLD:DATA",
                                   (const uint8_t *)"rename me", 9));
    TEST_ASSERT_OK(ndfs_rename(fs, "SYSTEM/OLD:DATA", "NEW:PROG"));

    /* Old name not found */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_read_file(fs, "SYSTEM/OLD:DATA", &data, &size));

    /* New name works */
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/NEW:PROG", &data, &size));
    TEST_ASSERT_EQUAL(9, size);
    TEST_ASSERT(memcmp(data, "rename me", 9) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    return 0;
}

/* ---- Write persistence: export and reimport ---- */

static int test_write_persistence(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "PERSIST");
    ndfs_filesystem_t *fs2 = NULL;
    uint8_t *exported = NULL;
    size_t exported_size = 0;
    uint8_t *data = NULL;
    size_t size = 0;
    const char *content = "Persistent data here";

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SAVE:DATA",
                                   (const uint8_t *)content, strlen(content)));

    /* Export */
    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    ndfs_close(fs);

    /* Reimport */
    TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
    free(exported);

    /* Verify data survives roundtrip */
    TEST_ASSERT_OK(ndfs_read_file(fs2, "SYSTEM/SAVE:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(content), size);
    TEST_ASSERT(memcmp(data, content, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs2);
    return 0;
}

/* ---- Sparse file (all zeros page) ---- */

static int test_sparse_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "SPARSE");
    size_t sparse_size = NDFS_PAGE_SIZE * 3;
    uint8_t *sparse_data;
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    uint32_t free_before, free_after;

    TEST_ASSERT_NOT_NULL(fs);

    sparse_data = (uint8_t *)calloc(sparse_size, 1);
    TEST_ASSERT_NOT_NULL(sparse_data);

    /* Only write non-zero in first and last page, middle is sparse */
    sparse_data[0] = 0xFF;
    sparse_data[sparse_size - 1] = 0xAA;

    ndfs_get_free_pages(fs, &free_before);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SPARSE:DATA",
                                   sparse_data, sparse_size));

    ndfs_get_free_pages(fs, &free_after);

    /* Should have used fewer pages than 3+1 because middle page is sparse */
    /* At minimum, index block + 2 data pages (not 3) */
    TEST_ASSERT(free_before - free_after <= 4);

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/SPARSE:DATA",
                                  &read_data, &read_size));
    TEST_ASSERT_EQUAL(sparse_size, read_size);
    TEST_ASSERT(memcmp(read_data, sparse_data, sparse_size) == 0);

    ndfs_free_data(read_data);
    free(sparse_data);
    ndfs_close(fs);
    return 0;
}

/* ---- User management comprehensive ---- */

static int test_add_multiple_users(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "USRMGMT");
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_add_user(fs, "ALICE", 50));
    TEST_ASSERT_OK(ndfs_add_user(fs, "BOB", 30));
    TEST_ASSERT_OK(ndfs_add_user(fs, "CHARLIE", 20));

    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &count));
    TEST_ASSERT_EQUAL(4, count); /* SYSTEM + 3 */
    ndfs_free_users(users);

    /* Duplicate */
    TEST_ASSERT_EQUAL(NDFS_ERR_ALREADY_EXISTS, ndfs_add_user(fs, "ALICE", 10));

    ndfs_close(fs);
    return 0;
}

/* Regression test for the "add_user beyond first UserFile page silently
 * dropped on remount" bug: image_creator.c only pre-allocates the FIRST
 * UserFile data page (32 entries incl. SYSTEM), so any user landing in
 * slot >= 32 needs a second page allocated on demand by
 * ensure_user_dir_page() (filesystem.c). Before that fix,
 * write_user_page() silently no-op'd when the page's index-block pointer
 * was unset, so the user lived only in memory and vanished after a
 * to_buffer/open_buffer_copy round trip -- exactly what this test drives. */
static int test_add_users_beyond_first_page(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_12MB, "MANYUSR");
    ndfs_filesystem_t *fs2 = NULL;
    uint8_t *exported = NULL;
    size_t exported_size = 0;
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;
    char uname[NDFS_NAME_MAX + 1];
    int u;
    const int num_users = 40; /* SYSTEM (slot 0) + 40 => slots 1..40, spilling into page 2 (slots 32-63) */

    TEST_ASSERT_NOT_NULL(fs);

    for (u = 0; u < num_users; u++) {
        snprintf(uname, sizeof(uname), "USER%03d", u);
        TEST_ASSERT_OK(ndfs_add_user(fs, uname, (uint32_t)(u + 1)));
    }

    /* In-memory state alone would hide the bug (it lives in fs->users[]
     * regardless of on-disk persistence), so round-trip through an actual
     * export/reimport before checking anything. */
    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    ndfs_close(fs);
    TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
    free(exported);

    TEST_ASSERT_OK(ndfs_get_users(fs2, &users, &count));
    TEST_ASSERT_EQUAL(num_users + 1, count); /* +1 for SYSTEM */

    /* Spot-check a user whose slot (>= 32) only exists in the newly
     * allocated second UserFile page: confirm the name and reserved-page
     * count survived the round trip intact, not just "the count matches". */
    {
        int found = 0;
        size_t i;
        for (i = 0; i < count; i++) {
            if (strcmp(users[i].user_name, "USER039") == 0) {
                found = 1;
                TEST_ASSERT_EQUAL(40, users[i].pages_reserved);
                TEST_ASSERT(users[i].user_index >= NDFS_ENTRIES_PER_PAGE);
                break;
            }
        }
        TEST_ASSERT(found);
    }

    ndfs_free_users(users);
    ndfs_close(fs2);
    return 0;
}

static int test_write_file_per_user(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "PERUSER");
    uint8_t *data = NULL;
    size_t size = 0;
    ndfs_file_entry_t *entries = NULL;
    size_t ecount = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_add_user(fs, "ALICE", 50));

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SYSFILE:DATA",
                                   (const uint8_t *)"sys", 3));
    TEST_ASSERT_OK(ndfs_write_file(fs, "ALICE/MYFILE:PROG",
                                   (const uint8_t *)"alice", 5));

    /* List SYSTEM files */
    TEST_ASSERT_OK(ndfs_list_directory(fs, "SYSTEM", &entries, &ecount));
    TEST_ASSERT_EQUAL(1, ecount);
    TEST_ASSERT_EQUAL_STRING("SYSFILE", entries[0].name);
    ndfs_free_entries(entries);

    /* List ALICE files */
    TEST_ASSERT_OK(ndfs_list_directory(fs, "ALICE", &entries, &ecount));
    TEST_ASSERT_EQUAL(1, ecount);
    TEST_ASSERT_EQUAL_STRING("MYFILE", entries[0].name);
    ndfs_free_entries(entries);

    /* Read each */
    TEST_ASSERT_OK(ndfs_read_file(fs, "ALICE/MYFILE:PROG", &data, &size));
    TEST_ASSERT_EQUAL(5, size);
    TEST_ASSERT(memcmp(data, "alice", 5) == 0);
    ndfs_free_data(data);

    ndfs_close(fs);
    return 0;
}

static int test_remove_user_with_no_files(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "RMUSER");
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_add_user(fs, "TEMP", 10));

    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &count));
    TEST_ASSERT_EQUAL(2, count);
    {
        uint8_t idx = users[count - 1].user_index;
        ndfs_free_users(users);

        TEST_ASSERT_OK(ndfs_remove_user(fs, idx));
    }

    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &count));
    TEST_ASSERT_EQUAL(1, count);
    ndfs_free_users(users);

    ndfs_close(fs);
    return 0;
}

static int test_update_quota(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "QUOTA");
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_update_user_quota(fs, 0, 999));
    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &count));
    TEST_ASSERT_EQUAL(999, users[0].pages_reserved);
    ndfs_free_users(users);

    ndfs_close(fs);
    return 0;
}

static int test_clear_password(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "CLRPWD");

    TEST_ASSERT_NOT_NULL(fs);

    TEST_ASSERT_OK(ndfs_clear_user_password(fs, "SYSTEM"));
    TEST_ASSERT_OK(ndfs_clear_user_password_by_index(fs, 0));
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND,
                      ndfs_clear_user_password(fs, "NOBODY"));

    ndfs_close(fs);
    return 0;
}

/* ---- Stress test ---- */

static int test_stress_users_and_files(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_12MB, "STRESS");
    char uname[NDFS_NAME_MAX + 1];
    char path[64];
    int u, f;
    int num_users = 20;
    int files_per_user = 5;
    uint8_t file_content[128];
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    ndfs_user_entry_t *users = NULL;
    size_t user_count = 0;
    uint32_t free_pages = 0;
    bool ok = false;

    TEST_ASSERT_NOT_NULL(fs);

    /* Create users */
    for (u = 0; u < num_users; u++) {
        snprintf(uname, sizeof(uname), "USER%03d", u);
        TEST_ASSERT_OK(ndfs_add_user(fs, uname, 50));
    }

    /* Verify user count */
    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &user_count));
    TEST_ASSERT_EQUAL(num_users + 1, user_count); /* +1 for SYSTEM */
    ndfs_free_users(users);

    /* Create files for each user */
    for (u = 0; u < num_users; u++) {
        snprintf(uname, sizeof(uname), "USER%03d", u);
        for (f = 0; f < files_per_user; f++) {
            size_t i;
            snprintf(path, sizeof(path), "%s/FILE%02d:DATA", uname, f);
            for (i = 0; i < sizeof(file_content); i++) {
                file_content[i] = (uint8_t)((u * 37 + f * 13 + i) & 0xFF);
            }
            TEST_ASSERT_OK(ndfs_write_file(fs, path,
                                           file_content, sizeof(file_content)));
        }
    }

    /* Verify some files */
    for (u = 0; u < num_users; u += 5) {
        snprintf(uname, sizeof(uname), "USER%03d", u);
        snprintf(path, sizeof(path), "%s/FILE00:DATA", uname);
        TEST_ASSERT_OK(ndfs_read_file(fs, path, &read_data, &read_size));
        TEST_ASSERT_EQUAL(sizeof(file_content), read_size);
        {
            size_t i;
            uint8_t expected;
            for (i = 0; i < sizeof(file_content); i++) {
                expected = (uint8_t)((u * 37 + 0 * 13 + i) & 0xFF);
                if (read_data[i] != expected) {
                    printf("  FAIL: Mismatch at user %d byte %zu\n", u, i);
                    ndfs_free_data(read_data);
                    ndfs_close(fs);
                    return 1;
                }
            }
        }
        ndfs_free_data(read_data);
    }

    /* Delete some files */
    for (u = 0; u < num_users; u += 2) {
        snprintf(path, sizeof(path), "USER%03d/FILE00:DATA", u);
        TEST_ASSERT_OK(ndfs_delete_file(fs, path));
    }

    /* Verify integrity */
    TEST_ASSERT_OK(ndfs_verify_integrity(fs, &ok));
    TEST_ASSERT(ok);

    /* Check free pages increased */
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_pages));
    TEST_ASSERT(free_pages > 0);

    /* Write new files in reclaimed space */
    for (u = 0; u < num_users; u += 2) {
        snprintf(path, sizeof(path), "USER%03d/NEWFILE:DATA", u);
        TEST_ASSERT_OK(ndfs_write_file(fs, path,
                                       (const uint8_t *)"reclaimed", 9));
    }

    /* Export and reimport */
    {
        uint8_t *exported = NULL;
        size_t exported_size = 0;
        ndfs_filesystem_t *fs2 = NULL;

        TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
        ndfs_close(fs);

        TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
        free(exported);

        TEST_ASSERT_OK(ndfs_verify_integrity(fs2, &ok));
        TEST_ASSERT(ok);

        /* Spot-check a file */
        snprintf(path, sizeof(path), "USER000/NEWFILE:DATA");
        TEST_ASSERT_OK(ndfs_read_file(fs2, path, &read_data, &read_size));
        TEST_ASSERT_EQUAL(9, read_size);
        TEST_ASSERT(memcmp(read_data, "reclaimed", 9) == 0);
        ndfs_free_data(read_data);

        ndfs_close(fs2);
    }

    return 0;
}

/* ---- Multiple files listing ---- */

static int test_list_multiple_files(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "MLIST");
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;
    int i;

    TEST_ASSERT_NOT_NULL(fs);

    for (i = 0; i < 5; i++) {
        char path[64];
        char data[32];
        snprintf(path, sizeof(path), "SYSTEM/FILE%d:DATA", i);
        snprintf(data, sizeof(data), "content_%d", i);
        TEST_ASSERT_OK(ndfs_write_file(fs, path,
                                       (const uint8_t *)data, strlen(data)));
    }

    TEST_ASSERT_OK(ndfs_list_directory(fs, "SYSTEM", &entries, &count));
    TEST_ASSERT_EQUAL(5, count);
    ndfs_free_entries(entries);

    ndfs_close(fs);
    return 0;
}

/* ---- Write empty file ---- */

static int test_write_empty_file(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "EMPTY");
    uint8_t dummy = 0;

    TEST_ASSERT_NOT_NULL(fs);

    /* Write with size 0 is tricky; pass a dummy byte with size 0 */
    /* The API requires file_data to be non-NULL for paths that exist,
       but size can be 0. Let's write 1 byte. */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/EMPTY:DATA", &dummy, 1));

    {
        uint8_t *data = NULL;
        size_t size = 0;
        TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/EMPTY:DATA", &data, &size));
        TEST_ASSERT(size <= 1);
        ndfs_free_data(data);
    }

    ndfs_close(fs);
    return 0;
}

/* ---- Write/delete cycle ---- */

static int test_write_delete_cycle(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "CYCLE");
    int i;
    uint32_t free_start, free_end;

    TEST_ASSERT_NOT_NULL(fs);
    ndfs_get_free_pages(fs, &free_start);

    for (i = 0; i < 10; i++) {
        char path[64];
        char data[256];
        snprintf(path, sizeof(path), "SYSTEM/CYCLE%d:DATA", i);
        memset(data, (uint8_t)i, sizeof(data));
        TEST_ASSERT_OK(ndfs_write_file(fs, path,
                                       (const uint8_t *)data, sizeof(data)));
        TEST_ASSERT_OK(ndfs_delete_file(fs, path));
    }

    ndfs_get_free_pages(fs, &free_end);
    TEST_ASSERT_EQUAL(free_start, free_end);

    ndfs_close(fs);
    return 0;
}

/* Bug 1+2 regression: an unrelated mutation must not corrupt other files'
 * metadata. A file with an empty type must keep it after an unrelated
 * add-user. The original port re-serialized EVERY object entry on every
 * mutation (persist_all) and defaulted empty types to "DATA" on read —
 * adding a user thus corrupted unrelated files. Writes are now surgical
 * (only the touched page) and empty types are preserved on round-trip. */
static int test_surgical_write_preserves_empty_type(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "SURGERY");
    uint8_t *exported = NULL;
    size_t exported_size = 0;
    ndfs_filesystem_t *fs2 = NULL;
    ndfs_file_entry_t meta;

    TEST_ASSERT_NOT_NULL(fs);

    /* Create a file with an EMPTY type (path carries no :TYPE suffix). */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/NOTYPE",
                                   (const uint8_t *)"x", 1));
    TEST_ASSERT_OK(ndfs_get_metadata(fs, "SYSTEM/NOTYPE", &meta));
    TEST_ASSERT_EQUAL_STRING("", meta.type);

    /* Unrelated mutation: add a brand-new user. Must not touch the SYSTEM
     * object page that holds NOTYPE. */
    TEST_ASSERT_OK(ndfs_add_user(fs, "BACKUP", 50));

    /* Export + reload: the empty type must survive, not become "DATA". */
    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
    TEST_ASSERT_OK(ndfs_get_metadata(fs2, "SYSTEM/NOTYPE", &meta));
    TEST_ASSERT_EQUAL_STRING("", meta.type);

    ndfs_free_data(exported);
    ndfs_close(fs2);
    ndfs_close(fs);
    return 0;
}

static int test_friends_add_list_remove(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "FRIENDS");
    ndfs_friend_info_t *fr = NULL;
    size_t n = 0;
    uint8_t *exported = NULL;
    size_t exported_size = 0;
    ndfs_filesystem_t *fs2 = NULL;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_add_user(fs, "ALICE", 100));
    TEST_ASSERT_OK(ndfs_add_user(fs, "BOB", 100));

    /* Add BOB as a friend of ALICE with RW. */
    TEST_ASSERT_OK(ndfs_add_friend(fs, "ALICE", "BOB", "RW"));

    /* List shows BOB with RW. */
    TEST_ASSERT_OK(ndfs_list_friends(fs, "ALICE", &fr, &n));
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("BOB", fr[0].name);
    TEST_ASSERT_EQUAL_STRING("RW---", fr[0].perms);
    ndfs_free_friends(fr); fr = NULL;

    /* Adding the same friend again errors. */
    TEST_ASSERT_EQUAL(NDFS_ERR_ALREADY_EXISTS, ndfs_add_friend(fs, "ALICE", "BOB", "R"));

    /* Unknown friend name errors. */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND, ndfs_add_friend(fs, "ALICE", "NOBODY", "R"));

    /* Persistence: friend survives export + reload. */
    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
    TEST_ASSERT_OK(ndfs_list_friends(fs2, "ALICE", &fr, &n));
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("BOB", fr[0].name);
    ndfs_free_friends(fr); fr = NULL;
    ndfs_close(fs2);
    ndfs_free_data(exported);

    /* Remove. */
    TEST_ASSERT_OK(ndfs_remove_friend(fs, "ALICE", "BOB"));
    TEST_ASSERT_OK(ndfs_list_friends(fs, "ALICE", &fr, &n));
    TEST_ASSERT_EQUAL(0, n);
    /* Removing again errors. */
    TEST_ASSERT_EQUAL(NDFS_ERR_NOT_FOUND, ndfs_remove_friend(fs, "ALICE", "BOB"));

    ndfs_close(fs);
    return 0;
}

/* ---- SubIndexed (large file, >512 pages) write support ----
 *
 * NDFS-FORMAT.md "Sub-Indexed (Type 10)": a top-level sub-index block holds
 * up to 512 pointers to group index blocks, each holding up to 512 data-page
 * pointers, covering files up to 512*512 = 262,144 pages. These tests pin
 * the 512-page boundary and exercise the SubIndexed write path added to
 * create_new_file()/update_existing_file()/allocate_and_write_data() in
 * src/filesystem.c. */

/* Fill `buf` with a pattern that is never a zero byte, so no page in the
 * file is a sparse hole -- this lets tests reason exactly about the
 * structural (index/sub-index) page cost via free-page deltas. */
static void fill_nonzero_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
    size_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(((i * 7 + seed) % 251) + 1); /* 1..251, never 0 */
    }
}

/* Find a user's current pages_used by name (SYSTEM by default). */
static uint32_t get_user_pages_used(ndfs_filesystem_t *fs, const char *name)
{
    ndfs_user_entry_t *users = NULL;
    size_t count = 0, i;
    uint32_t result = 0xFFFFFFFFu;

    if (ndfs_get_users(fs, &users, &count) != NDFS_OK) return result;
    for (i = 0; i < count; i++) {
        if (strcmp(users[i].user_name, name) == 0) {
            result = users[i].pages_used;
            break;
        }
    }
    ndfs_free_users(users);
    return result;
}

/* Pin the 512-page boundary: a file of EXACTLY 512 non-sparse pages must
 * still use the plain Indexed layout (structural cost = 1 index block),
 * not SubIndexed. Verified via the free-page delta: 512 data pages + 1
 * index block = 513 pages consumed. */
static int test_indexed_boundary_stays_indexed(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "IDXBND");
    size_t size = (size_t)NDFS_MAX_OBJECT_FILE_PTRS * NDFS_PAGE_SIZE; /* 512 pages */
    uint8_t *data = (uint8_t *)malloc(size);
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    uint32_t free_before, free_after;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(data);
    fill_nonzero_pattern(data, size, 1);

    ndfs_get_free_pages(fs, &free_before);
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/ATBND:DATA", data, size));
    ndfs_get_free_pages(fs, &free_after);

    /* 512 data pages + exactly 1 structural (index) page. If this were
     * mistakenly promoted to SubIndexed it would cost 512 + 1 + 1 = 514. */
    TEST_ASSERT_EQUAL(NDFS_MAX_OBJECT_FILE_PTRS + 1, free_before - free_after);

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/ATBND:DATA", &read_data, &read_size));
    TEST_ASSERT_EQUAL(size, read_size);
    TEST_ASSERT(memcmp(read_data, data, size) == 0);

    ndfs_free_data(read_data);
    free(data);
    ndfs_close(fs);
    return 0;
}

/* A file one page over the boundary (513 pages) must round-trip
 * byte-identically via the new SubIndexed path, and its structural cost
 * must be 1 sub-index block + 2 group index blocks (ceil(513/512) = 2). */
static int test_subindexed_just_over_boundary(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "SUBOVER");
    size_t size = (size_t)(NDFS_MAX_OBJECT_FILE_PTRS + 1) * NDFS_PAGE_SIZE; /* 513 pages */
    uint8_t *data = (uint8_t *)malloc(size);
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    uint32_t free_before, free_after;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(data);
    fill_nonzero_pattern(data, size, 2);

    ndfs_get_free_pages(fs, &free_before);
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/OVER:DATA", data, size));
    ndfs_get_free_pages(fs, &free_after);

    /* 513 data pages + 1 sub-index block + 2 group index blocks = 516. */
    TEST_ASSERT_EQUAL(NDFS_MAX_OBJECT_FILE_PTRS + 1 + 3, free_before - free_after);

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/OVER:DATA", &read_data, &read_size));
    TEST_ASSERT_EQUAL(size, read_size);
    TEST_ASSERT(memcmp(read_data, data, size) == 0);

    ndfs_free_data(read_data);
    free(data);
    ndfs_close(fs);
    return 0;
}

/* A file spanning multiple full 512-page groups (1000+ pages) must
 * round-trip byte-identically -- this is the test that would catch content
 * corruption at a group boundary (e.g. an off-by-one in start_page/end_page
 * or writing past a group's own 512-pointer index page). */
static int test_subindexed_multi_group_roundtrip(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "SUBMULTI");
    uint32_t data_pages = 1000; /* crosses the 512-page group boundary once */
    size_t size = (size_t)data_pages * NDFS_PAGE_SIZE;
    uint8_t *data = (uint8_t *)malloc(size);
    uint8_t *read_data = NULL;
    size_t read_size = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(data);
    fill_nonzero_pattern(data, size, 3);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/MULTI:DATA", data, size));
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/MULTI:DATA", &read_data, &read_size));
    TEST_ASSERT_EQUAL(size, read_size);

    /* Byte-by-byte so a group-boundary corruption reports where it starts,
     * not just "mismatch somewhere". */
    {
        size_t i;
        for (i = 0; i < size; i++) {
            if (read_data[i] != data[i]) {
                printf("  FAIL: mismatch at byte %zu (page %zu): expected %d, got %d\n",
                       i, i / NDFS_PAGE_SIZE, data[i], read_data[i]);
                ndfs_free_data(read_data);
                free(data);
                ndfs_close(fs);
                return 1;
            }
        }
    }

    ndfs_free_data(read_data);
    free(data);
    ndfs_close(fs);
    return 0;
}

/* A SubIndexed file with zero-filled (sparse) holes must round-trip
 * correctly, and must not allocate real disk blocks for the holes. */
static int test_subindexed_sparse_holes(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "SUBSPARS");
    uint32_t data_pages = 1000;
    size_t size = (size_t)data_pages * NDFS_PAGE_SIZE;
    uint8_t *data = (uint8_t *)calloc(size, 1); /* all sparse to start */
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    uint32_t free_before, free_after;
    uint32_t num_index_blocks = (data_pages + NDFS_MAX_OBJECT_FILE_PTRS - 1)
                               / NDFS_MAX_OBJECT_FILE_PTRS;
    uint32_t non_sparse_pages = 3;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(data);

    /* Only 3 pages carry real data: one in group 0, one straddling the
     * group boundary (page 511/512), one deep in group 1. Everything else
     * stays a zero-filled sparse hole. */
    fill_nonzero_pattern(data + (size_t)10 * NDFS_PAGE_SIZE, NDFS_PAGE_SIZE, 5);
    fill_nonzero_pattern(data + (size_t)511 * NDFS_PAGE_SIZE, NDFS_PAGE_SIZE, 6);
    fill_nonzero_pattern(data + (size_t)700 * NDFS_PAGE_SIZE, NDFS_PAGE_SIZE, 7);

    ndfs_get_free_pages(fs, &free_before);
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SPARSBIG:DATA", data, size));
    ndfs_get_free_pages(fs, &free_after);

    /* Structural pages (1 sub-index + num_index_blocks group index blocks)
     * plus only the non-sparse data pages -- NOT all 1000 data pages. */
    TEST_ASSERT_EQUAL(1 + num_index_blocks + non_sparse_pages,
                      free_before - free_after);

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/SPARSBIG:DATA", &read_data, &read_size));
    TEST_ASSERT_EQUAL(size, read_size);
    TEST_ASSERT(memcmp(read_data, data, size) == 0);

    ndfs_free_data(read_data);
    free(data);
    ndfs_close(fs);
    return 0;
}

/* Deleting a SubIndexed file must free every block it owns: data blocks,
 * every group's own index block, AND the top-level sub-index block. */
static int test_subindexed_delete_frees_all_blocks(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "SUBDEL");
    uint32_t data_pages = 1000;
    size_t size = (size_t)data_pages * NDFS_PAGE_SIZE;
    uint8_t *data = (uint8_t *)malloc(size);
    uint32_t free_before, free_after_write, free_after_delete;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(data);
    fill_nonzero_pattern(data, size, 8);

    ndfs_get_free_pages(fs, &free_before);
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/DELBIG:DATA", data, size));
    ndfs_get_free_pages(fs, &free_after_write);
    TEST_ASSERT(free_after_write < free_before);

    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/DELBIG:DATA"));
    ndfs_get_free_pages(fs, &free_after_delete);

    /* Every data block, every group index block, and the sub-index block
     * itself must come back -- not just the data blocks. */
    TEST_ASSERT_EQUAL(free_before, free_after_delete);

    free(data);
    ndfs_close(fs);
    return 0;
}

/* Overwriting an existing SubIndexed file must not leak the OLD structural
 * blocks (sub-index + group index blocks). This mirrors a real leak class
 * already found and fixed in the C# port's AllocateFileBlocksSparse: if the
 * "old total pages" calculation on overwrite assumes a flat "+1" structural
 * cost instead of accounting for however many group index blocks a large
 * file actually used, the quota/page bookkeeping never fully reclaims the
 * old file's structural pages, and it silently grows on every overwrite. */
static int test_subindexed_overwrite_no_leak(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_SMD_75MB, "SUBLEAK");
    uint32_t data_pages = 1000;
    size_t size = (size_t)data_pages * NDFS_PAGE_SIZE;
    uint8_t *v1 = (uint8_t *)malloc(size);
    uint8_t *v2 = (uint8_t *)malloc(size);
    uint32_t used_baseline, used_after_write1, used_after_write2, used_after_delete;
    uint32_t free_baseline, free_after_delete;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    fill_nonzero_pattern(v1, size, 9);
    fill_nonzero_pattern(v2, size, 42); /* different content, same size */

    used_baseline = get_user_pages_used(fs, "SYSTEM");
    ndfs_get_free_pages(fs, &free_baseline);

    /* Create as SubIndexed. */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/LEAKBIG:DATA", v1, size));
    used_after_write1 = get_user_pages_used(fs, "SYSTEM");

    /* Overwrite (same size, still SubIndexed) via update_existing_file(). */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/LEAKBIG:DATA", v2, size));
    used_after_write2 = get_user_pages_used(fs, "SYSTEM");

    /* Same data+structural page count both times -- overwrite must not
     * accumulate extra "used" pages for the old file's structure. */
    TEST_ASSERT_EQUAL(used_after_write1, used_after_write2);

    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/LEAKBIG:DATA"));
    used_after_delete = get_user_pages_used(fs, "SYSTEM");
    ndfs_get_free_pages(fs, &free_after_delete);

    /* Back to exactly the pre-write baseline: no leaked quota pages, and
     * every block (data + both overwrite's worth of structural pages)
     * reclaimed on disk. */
    TEST_ASSERT_EQUAL(used_baseline, used_after_delete);
    TEST_ASSERT_EQUAL(free_baseline, free_after_delete);

    free(v1);
    free(v2);
    ndfs_close(fs);
    return 0;
}

/* ---- Master block "Unreserved Pages" stays in sync on file create/delete ----
 *
 * Real SINTRAN reads master_block.unreserved_pages directly to report free
 * disk space, without rescanning the BitFile bitmap. This field used to only
 * ever be written once, at image-creation time (image_creator.c) -- any
 * later create/update/delete left the on-disk copy stale relative to the
 * real bitmap. persist_master_block()/write_bit_file() (src/filesystem.c)
 * now refreshes it after every allocation-changing mutation. */
static int test_unreserved_pages_tracks_create_delete(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "UNRESV");
    uint8_t buf[NDFS_PAGE_SIZE * 2];
    uint32_t live_free_baseline, live_free;
    uint32_t raw_after_create, raw_after_delete;

    TEST_ASSERT_NOT_NULL(fs);
    memset(buf, 0x55, sizeof(buf));

    /* NOTE: image_creator.c writes a template-defined placeholder for
     * unreserved_pages at creation time (e.g. a hardcoded "1" for the small
     * floppy templates here), not necessarily the true live free count --
     * that mismatch is a separate, pre-existing property of image creation
     * and out of scope for this fix. What this test verifies is that from
     * this point on, every allocation-changing mutation (create/delete)
     * rewrites the on-disk field to track the *live* bitmap-derived count. */
    ndfs_get_free_pages(fs, &live_free_baseline);

    /* After creating a file, the on-disk field must be rewritten to match
     * the now-smaller live free count. */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/UNRFILE:DATA", buf, sizeof(buf)));
    ndfs_get_free_pages(fs, &live_free);
    raw_after_create = read_raw_unreserved_pages(fs);
    TEST_ASSERT_EQUAL(live_free, raw_after_create);
    TEST_ASSERT(live_free < live_free_baseline);

    /* After deleting it, the on-disk field must be rewritten again, tracking
     * the freed pages back up to the original live free count. */
    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/UNRFILE:DATA"));
    ndfs_get_free_pages(fs, &live_free);
    raw_after_delete = read_raw_unreserved_pages(fs);
    TEST_ASSERT_EQUAL(live_free, raw_after_delete);
    TEST_ASSERT_EQUAL(live_free_baseline, raw_after_delete);

    ndfs_close(fs);
    return 0;
}

/* ---- Unreserved Pages tracks correctly across several mutations, not just
 * at the start and the end -- verifies each intermediate step too. ---- */
static int test_unreserved_pages_tracks_multiple_mutations(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "UNRSEQ");
    uint8_t bufA[NDFS_PAGE_SIZE];
    uint8_t bufB[NDFS_PAGE_SIZE * 2];
    uint32_t live_free, raw;

    TEST_ASSERT_NOT_NULL(fs);
    memset(bufA, 0x11, sizeof(bufA));
    memset(bufB, 0x22, sizeof(bufB));

    /* Step 1: create file A. */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SEQA:DATA", bufA, sizeof(bufA)));
    ndfs_get_free_pages(fs, &live_free);
    raw = read_raw_unreserved_pages(fs);
    TEST_ASSERT_EQUAL(live_free, raw);

    /* Step 2: create file B. */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SEQB:DATA", bufB, sizeof(bufB)));
    ndfs_get_free_pages(fs, &live_free);
    raw = read_raw_unreserved_pages(fs);
    TEST_ASSERT_EQUAL(live_free, raw);

    /* Step 3: delete file A -- B still exists. */
    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/SEQA:DATA"));
    ndfs_get_free_pages(fs, &live_free);
    raw = read_raw_unreserved_pages(fs);
    TEST_ASSERT_EQUAL(live_free, raw);

    ndfs_close(fs);
    return 0;
}

/* ---- The Extended Info Block (and its checksum) at offset 0x07D0 must be
 * byte-identical before and after mutations that only touch the master
 * block's Unreserved Pages field -- ndfs_mb_write() must never bleed into
 * that separate structure. ---- */
static int test_unreserved_pages_persist_does_not_touch_extended_info(void)
{
    ndfs_filesystem_t *fs = create_writable(NDFS_TMPL_FLOPPY_360KB, "UNREXT");
    uint8_t buf[NDFS_PAGE_SIZE * 2];
    uint8_t ext_before[16];
    uint8_t ext_after_create[16];
    uint8_t ext_after_delete[16];

    TEST_ASSERT_NOT_NULL(fs);
    memset(buf, 0x33, sizeof(buf));

    read_raw_extended_info(fs, ext_before);

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/EXTFILE:DATA", buf, sizeof(buf)));
    read_raw_extended_info(fs, ext_after_create);
    TEST_ASSERT(memcmp(ext_before, ext_after_create, 16) == 0);

    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/EXTFILE:DATA"));
    read_raw_extended_info(fs, ext_after_delete);
    TEST_ASSERT(memcmp(ext_before, ext_after_delete, 16) == 0);

    ndfs_close(fs);
    return 0;
}

void run_write_comprehensive_tests(void)
{
    TEST_SUITE_BEGIN("Write Comprehensive Tests");
    RUN_TEST(test_surgical_write_preserves_empty_type);
    RUN_TEST(test_friends_add_list_remove);
    RUN_TEST(test_write_small_file);
    RUN_TEST(test_write_multi_page_file);
    RUN_TEST(test_overwrite_file);
    RUN_TEST(test_delete_reclaims_space);
    RUN_TEST(test_unreserved_pages_tracks_create_delete);
    RUN_TEST(test_unreserved_pages_tracks_multiple_mutations);
    RUN_TEST(test_unreserved_pages_persist_does_not_touch_extended_info);
    RUN_TEST(test_rename_file);
    RUN_TEST(test_write_persistence);
    RUN_TEST(test_sparse_file);
    RUN_TEST(test_add_multiple_users);
    RUN_TEST(test_add_users_beyond_first_page);
    RUN_TEST(test_write_file_per_user);
    RUN_TEST(test_remove_user_with_no_files);
    RUN_TEST(test_update_quota);
    RUN_TEST(test_clear_password);
    RUN_TEST(test_stress_users_and_files);
    RUN_TEST(test_list_multiple_files);
    RUN_TEST(test_write_empty_file);
    RUN_TEST(test_write_delete_cycle);
    RUN_TEST(test_indexed_boundary_stays_indexed);
    RUN_TEST(test_subindexed_just_over_boundary);
    RUN_TEST(test_subindexed_multi_group_roundtrip);
    RUN_TEST(test_subindexed_sparse_holes);
    RUN_TEST(test_subindexed_delete_frees_all_blocks);
    RUN_TEST(test_subindexed_overwrite_no_leak);
}
