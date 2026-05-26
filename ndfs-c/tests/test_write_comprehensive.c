/**
 * Comprehensive write, user management, and stress tests.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

void run_write_comprehensive_tests(void)
{
    TEST_SUITE_BEGIN("Write Comprehensive Tests");
    RUN_TEST(test_surgical_write_preserves_empty_type);
    RUN_TEST(test_friends_add_list_remove);
    RUN_TEST(test_write_small_file);
    RUN_TEST(test_write_multi_page_file);
    RUN_TEST(test_overwrite_file);
    RUN_TEST(test_delete_reclaims_space);
    RUN_TEST(test_rename_file);
    RUN_TEST(test_write_persistence);
    RUN_TEST(test_sparse_file);
    RUN_TEST(test_add_multiple_users);
    RUN_TEST(test_write_file_per_user);
    RUN_TEST(test_remove_user_with_no_files);
    RUN_TEST(test_update_quota);
    RUN_TEST(test_clear_password);
    RUN_TEST(test_stress_users_and_files);
    RUN_TEST(test_list_multiple_files);
    RUN_TEST(test_write_empty_file);
    RUN_TEST(test_write_delete_cycle);
}
