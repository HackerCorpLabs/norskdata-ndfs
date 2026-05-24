/**
 * Image creator tests.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>
#include <string.h>
#include <stdlib.h>

/* ---- Helper: verify common post-creation properties ---- */

static int verify_image_basics(ndfs_filesystem_t *fs, const char *expected_name,
                               uint32_t min_pages)
{
    const ndfs_master_block_t *mb = NULL;
    char name[NDFS_NAME_MAX + 1];
    ndfs_user_entry_t *users = NULL;
    size_t user_count = 0;
    uint32_t free_pages = 0;
    bool ok = false;

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_NOT_NULL(mb);
    TEST_ASSERT_EQUAL_STRING(expected_name, mb->directory_name);

    /* Pointers must be valid */
    TEST_ASSERT(ndfs_bp_is_valid(&mb->object_file_ptr));
    TEST_ASSERT(ndfs_bp_is_valid(&mb->user_file_ptr));
    TEST_ASSERT(ndfs_bp_is_valid(&mb->bit_file_ptr));

    /* Directory name */
    TEST_ASSERT_OK(ndfs_get_directory_name(fs, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING(expected_name, name);

    /* SYSTEM user must exist */
    TEST_ASSERT_OK(ndfs_get_users(fs, &users, &user_count));
    TEST_ASSERT(user_count >= 1);
    TEST_ASSERT_EQUAL_STRING("SYSTEM", users[0].user_name);
    ndfs_free_users(users);

    /* Free pages should be reasonable */
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_pages));
    TEST_ASSERT(free_pages > 0);
    TEST_ASSERT(free_pages < min_pages);

    /* Integrity check */
    TEST_ASSERT_OK(ndfs_verify_integrity(fs, &ok));
    TEST_ASSERT(ok);

    /* Bitmap: page 0 should be used */
    TEST_ASSERT(ndfs_is_block_used(fs, 0));

    return 0;
}

static int test_create_floppy_360kb(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    const ndfs_master_block_t *mb;
    int r;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_360KB;
    strcpy(opts.directory_name, "FLP360");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_NOT_NULL(fs);

    r = verify_image_basics(fs, "FLP360", 154);
    if (r != 0) { ndfs_close(fs); return r; }

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_EQUAL(149, mb->object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(151, mb->user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(153, mb->bit_file_ptr.block_id);

    ndfs_close(fs);
    return 0;
}

static int test_create_floppy_12mb(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    const ndfs_master_block_t *mb;
    int r;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_12MB;
    strcpy(opts.directory_name, "FLP12M");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_NOT_NULL(fs);

    r = verify_image_basics(fs, "FLP12M", 616);
    if (r != 0) { ndfs_close(fs); return r; }

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_EQUAL(611, mb->object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(613, mb->user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(615, mb->bit_file_ptr.block_id);

    ndfs_close(fs);
    return 0;
}

static int test_create_smd_75mb(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    const ndfs_master_block_t *mb;
    int r;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_SMD_75MB;
    strcpy(opts.directory_name, "SMD75");
    opts.include_extended_info = true;
    opts.system_number = 42;
    opts.flag_word = 0x1234;

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_NOT_NULL(fs);

    r = verify_image_basics(fs, "SMD75", 38400);
    if (r != 0) { ndfs_close(fs); return r; }

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_EQUAL(18684, mb->object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(18686, mb->user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(18472, mb->bit_file_ptr.block_id);

    ndfs_close(fs);
    return 0;
}

static int test_create_winchester_74mb(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    const ndfs_master_block_t *mb;
    int r;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_WINCHESTER_74MB;
    strcpy(opts.directory_name, "WIN74");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_NOT_NULL(fs);

    r = verify_image_basics(fs, "WIN74", 36360);
    if (r != 0) { ndfs_close(fs); return r; }

    TEST_ASSERT_OK(ndfs_get_master_block(fs, &mb));
    TEST_ASSERT_EQUAL(32771, mb->object_file_ptr.block_id);
    TEST_ASSERT_EQUAL(32769, mb->user_file_ptr.block_id);
    TEST_ASSERT_EQUAL(18198, mb->bit_file_ptr.block_id);

    ndfs_close(fs);
    return 0;
}

static int test_create_custom(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    uint32_t free_pages = 0;
    int r;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_CUSTOM;
    opts.custom_pages = 100;
    strcpy(opts.directory_name, "CUSTOM");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_NOT_NULL(fs);

    r = verify_image_basics(fs, "CUSTOM", 100);
    if (r != 0) { ndfs_close(fs); return r; }

    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_pages));
    TEST_ASSERT(free_pages > 80); /* Most pages should be free */

    ndfs_close(fs);
    return 0;
}

static int test_create_custom_too_small(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_CUSTOM;
    opts.custom_pages = 10; /* Too small */

    TEST_ASSERT_EQUAL(NDFS_ERR_TOO_SMALL, ndfs_create_image(&fs, &opts));
    return 0;
}

static int test_create_null_args(void)
{
    ndfs_image_options_t opts;
    ndfs_filesystem_t *fs = NULL;

    ndfs_image_options_init(&opts);
    TEST_ASSERT_EQUAL(NDFS_ERR_NULL_PTR, ndfs_create_image(NULL, &opts));
    TEST_ASSERT_EQUAL(NDFS_ERR_NULL_PTR, ndfs_create_image(&fs, NULL));
    return 0;
}

static int test_write_to_created_image(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    uint8_t *data = NULL;
    size_t size = 0;
    const char *content = "Hello from created image!";

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_360KB;
    strcpy(opts.directory_name, "WRTEST");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));

    /* Write a file */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/TEST:DATA",
                                   (const uint8_t *)content, strlen(content)));

    /* Read it back */
    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/TEST:DATA", &data, &size));
    TEST_ASSERT_EQUAL(strlen(content), size);
    TEST_ASSERT(memcmp(data, content, size) == 0);

    ndfs_free_data(data);
    ndfs_close(fs);
    return 0;
}

static int test_create_and_export(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_filesystem_t *fs2 = NULL;
    ndfs_image_options_t opts;
    uint8_t *exported = NULL;
    size_t exported_size = 0;
    char name[NDFS_NAME_MAX + 1];

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_360KB;
    strcpy(opts.directory_name, "EXPORT");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));

    /* Write a file */
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/HELLO:DATA",
                                   (const uint8_t *)"world", 5));

    /* Export */
    TEST_ASSERT_OK(ndfs_to_buffer(fs, &exported, &exported_size));
    ndfs_close(fs);

    /* Re-open */
    TEST_ASSERT_OK(ndfs_open_buffer_copy(exported, exported_size, true, &fs2));
    free(exported);

    TEST_ASSERT_OK(ndfs_get_directory_name(fs2, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("EXPORT", name);

    /* File should still be there */
    {
        uint8_t *data = NULL;
        size_t sz = 0;
        TEST_ASSERT_OK(ndfs_read_file(fs2, "SYSTEM/HELLO:DATA", &data, &sz));
        TEST_ASSERT_EQUAL(5, sz);
        TEST_ASSERT(memcmp(data, "world", 5) == 0);
        ndfs_free_data(data);
    }

    ndfs_close(fs2);
    return 0;
}

static int test_directory_name_uppercased(void)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;
    char name[NDFS_NAME_MAX + 1];

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_360KB;
    strcpy(opts.directory_name, "lowercase");

    TEST_ASSERT_OK(ndfs_create_image(&fs, &opts));
    TEST_ASSERT_OK(ndfs_get_directory_name(fs, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("LOWERCASE", name);

    ndfs_close(fs);
    return 0;
}

void run_image_creator_tests(void)
{
    TEST_SUITE_BEGIN("Image Creator Tests");
    RUN_TEST(test_create_floppy_360kb);
    RUN_TEST(test_create_floppy_12mb);
    RUN_TEST(test_create_smd_75mb);
    RUN_TEST(test_create_winchester_74mb);
    RUN_TEST(test_create_custom);
    RUN_TEST(test_create_custom_too_small);
    RUN_TEST(test_create_null_args);
    RUN_TEST(test_write_to_created_image);
    RUN_TEST(test_create_and_export);
    RUN_TEST(test_directory_name_uppercased);
}
