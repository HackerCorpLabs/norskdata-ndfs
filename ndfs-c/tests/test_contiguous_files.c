/**
 * Tests for contiguous file creation.
 *
 * Added 2026-07-30. Until then every create path in the library produced an
 * indexed file, so SINTRAN's "@CREATE-FILE name pages" -- the form that makes
 * the machine report CONTINUOUS FILE -- had nothing behind it, and the COSMOS
 * file-access server's Create-file operation could not be answered when it
 * carried a page count.
 *
 * A contiguous file allocates all of its pages up front, consecutive on the
 * pack, and cannot grow: the pages after the run belong to other files. A write
 * past the reservation therefore fails rather than silently converting the file
 * to an indexed one, which is what SINTRAN itself does.
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

static ndfs_filesystem_t *create_writable(const char *name)
{
    ndfs_filesystem_t *fs = NULL;
    ndfs_image_options_t opts;

    ndfs_image_options_init(&opts);
    opts.template_type = NDFS_TMPL_FLOPPY_12MB;
    strncpy(opts.directory_name, name, NDFS_NAME_MAX);
    opts.directory_name[NDFS_NAME_MAX] = '\0';

    if (ndfs_create_image(&fs, &opts) != NDFS_OK) return NULL;
    return fs;
}

/* ---- Creation ---- */

/* The three fields a SINTRAN client reads back off the object entry. Together
 * they are what make the machine print "CONTINUOUS FILE" with zero bytes. */
static int test_contiguous_entry_is_flagged_and_sized(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    ndfs_object_entry_t entry;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/RESERVED:DATA", 8));

    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "RESERVED:DATA", "SYSTEM", &entry));
    TEST_ASSERT((entry.file_type_flags & NDFS_FT_CONTIGUOUS) != 0);
    TEST_ASSERT((entry.file_type_flags & NDFS_FT_INDEXED) == 0);
    TEST_ASSERT_EQUAL_UINT(8, entry.pages_in_file);
    TEST_ASSERT_EQUAL_UINT(0, entry.bytes_in_file);
    TEST_ASSERT_EQUAL_UINT(NDFS_PTR_CONTIGUOUS, entry.file_pointer.type);

    ndfs_close(fs);
    return 0;
}

/* The property the whole file kind exists for, checked against the bitmap
 * rather than the pointer: a run that is not actually allocated would be handed
 * out again to the next file. */
static int test_contiguous_run_is_one_unbroken_allocated_range(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    ndfs_object_entry_t entry;
    uint32_t start;
    uint32_t i;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/RUN:DATA", 6));
    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "RUN:DATA", "SYSTEM", &entry));

    start = entry.file_pointer.block_id;
    TEST_ASSERT(start > 0);

    /* Page N of the file is block start+N -- there is no index block in
     * between, which is exactly why every one of those blocks must be used. */
    for (i = 0; i < 6; i++) {
        TEST_ASSERT(ndfs_is_block_used(fs, start + i));
    }

    ndfs_close(fs);
    return 0;
}

/* A contiguous file has no structural overhead: the reservation is all it
 * costs, unlike an indexed file which also burns an index block. */
static int test_contiguous_creation_costs_exactly_the_reservation(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    uint32_t free_before = 0, free_after = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_before));
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/COST:DATA", 7));
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_after));

    TEST_ASSERT_EQUAL_UINT(7, free_before - free_after);

    ndfs_close(fs);
    return 0;
}

/* A file with no pages has no run to point at. */
static int test_contiguous_zero_pages_is_rejected(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_EQUAL(NDFS_ERR_INVALID_ARG,
                      ndfs_create_contiguous_file(fs, "SYSTEM/EMPTY:DATA", 0));

    ndfs_close(fs);
    return 0;
}

static int test_contiguous_duplicate_name_is_rejected(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/ONCE:DATA", 2));
    TEST_ASSERT_EQUAL(NDFS_ERR_ALREADY_EXISTS,
                      ndfs_create_contiguous_file(fs, "SYSTEM/ONCE:DATA", 2));

    ndfs_close(fs);
    return 0;
}

/* The allocator must refuse rather than hand back a truncated run. */
static int test_contiguous_run_larger_than_the_pack_is_rejected(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_EQUAL(NDFS_ERR_NO_SPACE,
                      ndfs_create_contiguous_file(fs, "SYSTEM/HUGE:DATA", 10000000));

    ndfs_close(fs);
    return 0;
}

/* ---- Writing ---- */

static int test_contiguous_written_data_reads_back(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    const char *content = "contiguous payload written in place";
    uint8_t *data = NULL;
    size_t size = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/PAYLOAD:DATA", 4));
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/PAYLOAD:DATA",
                                   (const uint8_t *)content, strlen(content)));

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/PAYLOAD:DATA", &data, &size));
    TEST_ASSERT_EQUAL_UINT((uint32_t)strlen(content), (uint32_t)size);
    TEST_ASSERT(memcmp(data, content, size) == 0);
    ndfs_free_data(data);

    ndfs_close(fs);
    return 0;
}

/* A write must not convert the file, move its run, or shrink the reservation. */
static int test_contiguous_write_does_not_convert_or_move_the_file(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    ndfs_object_entry_t before, after;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/STABLE:DATA", 4));
    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "STABLE:DATA", "SYSTEM", &before));

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/STABLE:DATA",
                                   (const uint8_t *)"some data", 9));

    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "STABLE:DATA", "SYSTEM", &after));
    TEST_ASSERT((after.file_type_flags & NDFS_FT_CONTIGUOUS) != 0);
    TEST_ASSERT_EQUAL_UINT(before.file_pointer.block_id, after.file_pointer.block_id);
    TEST_ASSERT_EQUAL_UINT(4, after.pages_in_file);

    ndfs_close(fs);
    return 0;
}

/* Guards the boundary from the side that must always fit. */
static int test_contiguous_write_that_exactly_fills_is_accepted(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    size_t exact_size = 3 * NDFS_PAGE_SIZE;
    uint8_t *exact = (uint8_t *)malloc(exact_size);
    uint8_t *data = NULL;
    size_t size = 0;
    size_t i;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(exact);
    for (i = 0; i < exact_size; i++) exact[i] = (uint8_t)(i & 0xFF);

    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/EXACT:DATA", 3));
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/EXACT:DATA", exact, exact_size));

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/EXACT:DATA", &data, &size));
    TEST_ASSERT_EQUAL_UINT((uint32_t)exact_size, (uint32_t)size);
    TEST_ASSERT(memcmp(data, exact, exact_size) == 0);
    ndfs_free_data(data);

    free(exact);
    ndfs_close(fs);
    return 0;
}

/* The deliberate design choice: fail, never grow and never convert. */
static int test_contiguous_write_beyond_the_reservation_fails(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    size_t too_big_size = 2 * NDFS_PAGE_SIZE + 1;
    uint8_t *too_big = (uint8_t *)calloc(1, too_big_size);
    ndfs_object_entry_t entry;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(too_big);

    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/SMALL:DATA", 2));
    TEST_ASSERT_EQUAL(NDFS_ERR_NO_SPACE,
                      ndfs_write_file(fs, "SYSTEM/SMALL:DATA", too_big, too_big_size));

    /* The refused write must leave the file exactly as it was. */
    TEST_ASSERT_OK(ndfs_get_object_entry(fs, "SMALL:DATA", "SYSTEM", &entry));
    TEST_ASSERT_EQUAL_UINT(0, entry.bytes_in_file);
    TEST_ASSERT_EQUAL_UINT(2, entry.pages_in_file);

    free(too_big);
    ndfs_close(fs);
    return 0;
}

/* The run is not reallocated between writes, so the old bytes are physically
 * still there unless the write path clears the rest of the reservation. */
static int test_contiguous_shorter_rewrite_leaves_no_old_tail(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    size_t long_size = 3 * NDFS_PAGE_SIZE;
    uint8_t *long_data = (uint8_t *)malloc(long_size);
    uint8_t *data = NULL;
    size_t size = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_NOT_NULL(long_data);
    memset(long_data, 0xAB, long_size);

    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/SHRINK:DATA", 3));
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SHRINK:DATA", long_data, long_size));
    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/SHRINK:DATA",
                                   (const uint8_t *)"short", 5));

    TEST_ASSERT_OK(ndfs_read_file(fs, "SYSTEM/SHRINK:DATA", &data, &size));
    TEST_ASSERT_EQUAL_UINT(5, (uint32_t)size);
    TEST_ASSERT(memcmp(data, "short", 5) == 0);
    ndfs_free_data(data);

    free(long_data);
    ndfs_close(fs);
    return 0;
}

/* The pages were charged at creation; a write must not move disk usage at all. */
static int test_contiguous_write_allocates_and_frees_nothing(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    uint32_t free_after_create = 0, free_after_write = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/FIXED:DATA", 5));
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_after_create));

    TEST_ASSERT_OK(ndfs_write_file(fs, "SYSTEM/FIXED:DATA",
                                   (const uint8_t *)"xxxxxxxxxx", 10));
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_after_write));

    TEST_ASSERT_EQUAL_UINT(free_after_create, free_after_write);

    ndfs_close(fs);
    return 0;
}

/* Deleting a contiguous file must give the whole run back. */
static int test_contiguous_delete_frees_the_whole_run(void)
{
    ndfs_filesystem_t *fs = create_writable("CONTIG");
    uint32_t free_before = 0, free_after = 0;

    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_before));

    TEST_ASSERT_OK(ndfs_create_contiguous_file(fs, "SYSTEM/GONE:DATA", 6));
    TEST_ASSERT_OK(ndfs_delete_file(fs, "SYSTEM/GONE:DATA"));

    TEST_ASSERT_OK(ndfs_get_free_pages(fs, &free_after));
    TEST_ASSERT_EQUAL_UINT(free_before, free_after);

    ndfs_close(fs);
    return 0;
}

void run_contiguous_file_tests(void)
{
    TEST_SUITE_BEGIN("Contiguous Files");

    RUN_TEST(test_contiguous_entry_is_flagged_and_sized);
    RUN_TEST(test_contiguous_run_is_one_unbroken_allocated_range);
    RUN_TEST(test_contiguous_creation_costs_exactly_the_reservation);
    RUN_TEST(test_contiguous_zero_pages_is_rejected);
    RUN_TEST(test_contiguous_duplicate_name_is_rejected);
    RUN_TEST(test_contiguous_run_larger_than_the_pack_is_rejected);
    RUN_TEST(test_contiguous_written_data_reads_back);
    RUN_TEST(test_contiguous_write_does_not_convert_or_move_the_file);
    RUN_TEST(test_contiguous_write_that_exactly_fills_is_accepted);
    RUN_TEST(test_contiguous_write_beyond_the_reservation_fails);
    RUN_TEST(test_contiguous_shorter_rewrite_leaves_no_old_tail);
    RUN_TEST(test_contiguous_write_allocates_and_frees_nothing);
    RUN_TEST(test_contiguous_delete_frees_the_whole_run);
}
