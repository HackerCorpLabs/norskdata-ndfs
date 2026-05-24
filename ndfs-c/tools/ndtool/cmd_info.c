/**
 * ndtool: filesystem info and bitmap visualization.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"
#include <stdio.h>
#include <string.h>

static const char *ptr_type_str(ndfs_pointer_type_t t)
{
    switch (t) {
    case NDFS_PTR_CONTIGUOUS: return "Contiguous";
    case NDFS_PTR_INDEXED:    return "Indexed";
    case NDFS_PTR_SUBINDEXED: return "SubIndexed";
    default:                  return "Reserved";
    }
}

static const char *boot_format_str(ndfs_boot_format_t f)
{
    switch (f) {
    case NDFS_BOOT_NONE:   return "None";
    case NDFS_BOOT_BINARY: return "Binary";
    case NDFS_BOOT_BPUN:   return "BPUN";
    case NDFS_BOOT_FLOMON: return "FLOMON";
    default:               return "Unknown";
    }
}

static const char *checksum_str(ndfs_checksum_validation_t c)
{
    switch (c) {
    case NDFS_CHECKSUM_VALID:          return "Valid";
    case NDFS_CHECKSUM_VALID_LOW_BYTE: return "Valid (low byte)";
    case NDFS_CHECKSUM_INVALID:        return "Invalid";
    default:                           return "Unknown";
    }
}

/**
 * Print bitmap visualization using Unicode block characters.
 *
 * Legend:
 *   Block chars represent groups of pages:
 *   - Full block  = all pages in group used
 *   - Dark shade  = 75%+ used
 *   - Medium shade = 25-75% used
 *   - Light shade = <25% used
 *   - Middle dot  = all free
 *
 * Layout: 64 chars per row, each char = group of pages.
 * Total bitmap fits in a compact visual.
 */
static void print_bitmap(ndtool_ctx_t *ctx)
{
    const ndfs_master_block_t *mb = NULL;
    uint32_t total = 0;
    uint32_t free_p = 0;
    uint32_t used_p = 0;

    ndfs_get_master_block(ctx->fs, &mb);
    if (!mb) return;
    total = mb->image_size;
    ndfs_get_free_pages(ctx->fs, &free_p);
    ndfs_get_used_pages(ctx->fs, &used_p);

    /* Determine grouping: fit into ~64 columns */
    uint32_t cols = 64;
    uint32_t group_size = (total + cols - 1) / cols;
    if (group_size < 1) group_size = 1;
    uint32_t rows = (total + (cols * group_size) - 1) / (cols * group_size);
    if (rows < 1) rows = 1;

    printf("\nBitmap (%u pages, %u used, %u free):\n", total, used_p, free_p);

    /* Legend */
    /* Using UTF-8 encoded Unicode block characters */
    printf("  Legend: \xe2\x96\x88=full  \xe2\x96\x93=75%%+  "
           "\xe2\x96\x92=25-75%%  \xe2\x96\x91=<25%%  "
           "\xc2\xb7=free\n\n");

    /* Row header width */
    printf("  Block ");

    /* Column headers (every 8th) */
    {
        uint32_t c;
        for (c = 0; c < cols; c++) {
            if (c % 8 == 0) {
                printf("%-8u", c * group_size);
            }
        }
    }
    printf("\n");

    uint32_t page = 0;
    uint32_t r, c;
    for (r = 0; r < rows && page < total; r++) {
        printf("  %5u ", page);

        for (c = 0; c < cols && page < total; c++) {
            uint32_t group_start = page;
            uint32_t group_end = page + group_size;
            if (group_end > total) group_end = total;
            uint32_t group_total = group_end - group_start;

            /* Count used pages in this group */
            uint32_t group_used = 0;
            uint32_t p;
            for (p = group_start; p < group_end; p++) {
                if (ndfs_is_block_used(ctx->fs, p)) {
                    group_used++;
                }
            }

            /* Select character based on usage ratio */
            if (group_total == 0) {
                printf("\xc2\xb7");  /* middle dot */
            } else {
                double ratio = (double)group_used / (double)group_total;
                if (ratio >= 1.0) {
                    printf("\xe2\x96\x88");  /* U+2588 full block */
                } else if (ratio >= 0.75) {
                    printf("\xe2\x96\x93");  /* U+2593 dark shade */
                } else if (ratio >= 0.25) {
                    printf("\xe2\x96\x92");  /* U+2592 medium shade */
                } else if (ratio > 0.0) {
                    printf("\xe2\x96\x91");  /* U+2591 light shade */
                } else {
                    printf("\xc2\xb7");      /* U+00B7 middle dot */
                }
            }

            page = group_end;
        }
        printf("\n");
    }
    printf("\n");
}

/**
 * Validate bitmap consistency.
 * Checks that every block referenced by file pointers is marked as used
 * in the bitmap, and that no orphaned blocks exist.
 */
static void validate_bitmap(ndtool_ctx_t *ctx)
{
    bool ok = false;
    ndfs_error_t err = ndfs_verify_integrity(ctx->fs, &ok);

    printf("\nBitmap Validation:\n");
    if (err != NDFS_OK) {
        printf("  Error running validation: %s\n", ndfs_strerror(err));
        return;
    }

    if (ok) {
        printf("  \xe2\x9c\x93 Bitmap is consistent with file allocation\n");
    } else {
        printf("  \xe2\x9c\x97 Bitmap inconsistency detected!\n");
        printf("    Run a full filesystem check for details.\n");
    }

    /* Additional checks: count blocks referenced by files vs bitmap */
    uint32_t free_p = 0, used_p = 0;
    ndfs_get_free_pages(ctx->fs, &free_p);
    ndfs_get_used_pages(ctx->fs, &used_p);

    ndfs_object_entry_t *objects = NULL;
    size_t obj_count = 0;
    ndfs_get_object_entries(ctx->fs, &objects, &obj_count);

    uint32_t total_file_pages = 0;
    size_t i;
    for (i = 0; i < obj_count; i++) {
        total_file_pages += objects[i].pages_in_file;
    }

    printf("  Bitmap used:       %u pages\n", used_p);
    printf("  Bitmap free:       %u pages\n", free_p);
    printf("  File data pages:   %u pages (across %zu files)\n",
           total_file_pages, obj_count);
    printf("  Overhead:          %u pages (system + index blocks)\n",
           used_p > total_file_pages ? used_p - total_file_pages : 0);

    ndfs_free_object_entries(objects);
}

int cmd_info(ndtool_ctx_t *ctx)
{
    const ndfs_master_block_t *mb = NULL;
    ndfs_error_t err;
    uint32_t free_pages = 0;
    uint32_t used_pages = 0;
    ndfs_boot_format_t boot_fmt = NDFS_BOOT_NONE;

    ndfs_user_entry_t *users = NULL;
    size_t user_count = 0;

    ndfs_object_entry_t *objects = NULL;
    size_t obj_count = 0;

    err = ndfs_get_master_block(ctx->fs, &mb);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error getting master block: %s\n", ndfs_strerror(err));
        return -1;
    }

    ndfs_get_free_pages(ctx->fs, &free_pages);
    ndfs_get_used_pages(ctx->fs, &used_pages);
    ndfs_detect_boot_format(ctx->fs, &boot_fmt);
    ndfs_get_users(ctx->fs, &users, &user_count);
    ndfs_get_object_entries(ctx->fs, &objects, &obj_count);

    printf("Volume:        %s\n", mb->directory_name);
    printf("Total pages:   %u\n", mb->image_size);
    printf("Used pages:    %u\n", used_pages);
    printf("Free pages:    %u\n", free_pages);
    printf("Users:         %zu\n", user_count);
    printf("Files:         %zu\n", obj_count);
    printf("Boot format:   %s\n", boot_format_str(boot_fmt));

    if (ctx->verbose) {
        printf("\n");
        printf("Object file:   Block %u (%s)\n",
               mb->object_file_ptr.block_id,
               ptr_type_str(mb->object_file_ptr.type));
        printf("User file:     Block %u (%s)\n",
               mb->user_file_ptr.block_id,
               ptr_type_str(mb->user_file_ptr.type));
        printf("Bit file:      Block %u (%s)\n",
               mb->bit_file_ptr.block_id,
               ptr_type_str(mb->bit_file_ptr.type));

        if (mb->ext_valid) {
            printf("Extended info: Valid (System %u, Checksum %s)\n",
                   mb->ext_last_system_number,
                   checksum_str(mb->checksum_state));
            printf("  Pages avail: %u\n", mb->ext_pages_available);
            printf("  Flag word:   0x%04X\n", mb->ext_flag_word);
        } else if (mb->has_flomon) {
            printf("Extended info: N/A (FLOMON floppy)\n");
        } else {
            printf("Extended info: Not present\n");
        }

        /* Bitmap visualization */
        print_bitmap(ctx);

        /* Bitmap validation */
        validate_bitmap(ctx);
    }

    ndfs_free_users(users);
    ndfs_free_object_entries(objects);
    return 0;
}
