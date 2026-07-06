/**
 * NDFS image creation implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/image_creator.h>
#include "endian_util.h"
#include "ndfs_name.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ---- Template specifications ----------------------------------------- */

typedef struct {
    uint32_t ndfs_pages;
    uint32_t file_blocks;
    uint32_t object_file_block;
    uint32_t user_file_block;
    uint32_t bit_file_block;
    uint32_t unreserved_pages;
    bool     has_flomon;
    bool     ext_valid;
} ndfs_template_spec_t;

static const ndfs_template_spec_t spec_floppy_360kb = {
    154, 154, 149, 151, 153, 1, true, false
};
static const ndfs_template_spec_t spec_floppy_12mb = {
    616, 616, 611, 613, 615, 1, true, false
};
static const ndfs_template_spec_t spec_smd_75mb = {
    36945, 38400, 18684, 18686, 18472, 36945, false, true
};
static const ndfs_template_spec_t spec_winchester_74mb = {
    36396, 36360, 32771, 32769, 18198, 36396, false, true
};

static const ndfs_template_spec_t *get_spec(ndfs_image_template_t tmpl)
{
    switch (tmpl) {
        case NDFS_TMPL_FLOPPY_360KB:    return &spec_floppy_360kb;
        case NDFS_TMPL_FLOPPY_12MB:     return &spec_floppy_12mb;
        case NDFS_TMPL_SMD_75MB:        return &spec_smd_75mb;
        case NDFS_TMPL_WINCHESTER_74MB: return &spec_winchester_74mb;
        default:                        return NULL;
    }
}

/* ---- Build custom spec from page count ------------------------------- */

static void build_custom_spec(uint32_t pages, ndfs_template_spec_t *out)
{
    uint32_t bf_block;

    memset(out, 0, sizeof(*out));
    out->ndfs_pages  = pages;
    out->file_blocks = pages;
    out->has_flomon  = false;
    out->ext_valid   = (pages > 1000);

    /* Layout: object file ~50%, user file after it, bit file (BitFile
     * bitmap) before both -- or near the end for small disks. */
    if (pages > 1000) {
        /* The BitFile bitmap is 1 bit per page, so it needs
         * ceil(ceil(pages/8)/NDFS_PAGE_SIZE) contiguous pages -- for small
         * page counts that is always 1, but for large custom images (tens
         * of thousands of pages, e.g. a 30,000+ file stress image) it can
         * span several pages. The object-file and user-file index blocks
         * used to be placed at a fixed bf_block+2/+4 offset, which only
         * left room for a 1-page bitmap; on any custom image needing more
         * than that, the bitmap's own later pages silently overlapped and
         * overwrote the object/user file index blocks the very first time
         * write_bit_file() ran, corrupting the object directory. Compute
         * the real bitmap page span here and place object/user file blocks
         * strictly after it. */
        uint32_t bitmap_bytes = (pages + 7) / 8;
        uint32_t bitmap_pages = (bitmap_bytes + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE;

        bf_block = pages / 2;
        out->bit_file_block    = bf_block;
        out->object_file_block = bf_block + bitmap_pages;     /* +index +data page */
        out->user_file_block   = out->object_file_block + 2;  /* +index +data page */
    } else {
        /* Small disks: put system structures near end */
        out->object_file_block = pages - 5;
        out->user_file_block   = pages - 3;
        out->bit_file_block    = pages - 1;
    }
    out->unreserved_pages = pages;
}

/* ---- Helpers --------------------------------------------------------- */

static void str_toupper_ic(char *s)
{
    while (*s) {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
}

/* ---- Public API ------------------------------------------------------ */

void ndfs_image_options_init(ndfs_image_options_t *opts)
{
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->template_type = NDFS_TMPL_FLOPPY_360KB;
    strcpy(opts->directory_name, "NDFS-DISK");
    opts->include_extended_info = false;
    opts->system_number = 0;
    opts->flag_word = 0;
}

ndfs_error_t ndfs_create_image(ndfs_filesystem_t **out_fs,
                               const ndfs_image_options_t *options)
{
    const ndfs_template_spec_t *spec;
    ndfs_template_spec_t custom_spec;
    uint8_t *img;
    size_t img_size;
    ndfs_master_block_t mb;
    ndfs_user_entry_t ue;
    ndfs_block_pointer_t bp;
    uint32_t bitmap_bytes, bitmap_pages;
    uint32_t i;
    size_t b;
    ndfs_error_t err;

    if (!out_fs || !options) return NDFS_ERR_NULL_PTR;

    /* Get template spec */
    if (options->template_type == NDFS_TMPL_CUSTOM) {
        if (options->custom_pages < 32) return NDFS_ERR_TOO_SMALL;
        build_custom_spec(options->custom_pages, &custom_spec);
        spec = &custom_spec;
    } else {
        spec = get_spec(options->template_type);
        if (!spec) return NDFS_ERR_INVALID_ARG;
    }

    /* Allocate image buffer */
    img_size = (size_t)spec->file_blocks * NDFS_PAGE_SIZE;
    img = (uint8_t *)calloc(img_size, 1);
    if (!img) return NDFS_ERR_ALLOC;

    /* ---- Page 0: Master block ---- */
    memset(&mb, 0, sizeof(mb));
    {
        size_t nlen = strlen(options->directory_name);
        if (nlen > NDFS_NAME_MAX) nlen = NDFS_NAME_MAX;
        memcpy(mb.directory_name, options->directory_name, nlen);
        mb.directory_name[nlen] = '\0';
        str_toupper_ic(mb.directory_name);
    }

    mb.object_file_ptr.block_id = spec->object_file_block;
    mb.object_file_ptr.type     = NDFS_PTR_INDEXED;
    mb.user_file_ptr.block_id   = spec->user_file_block;
    mb.user_file_ptr.type       = NDFS_PTR_INDEXED;
    mb.bit_file_ptr.block_id    = spec->bit_file_block;
    mb.bit_file_ptr.type        = NDFS_PTR_CONTIGUOUS;
    mb.unreserved_pages         = spec->unreserved_pages;
    mb.image_size               = spec->ndfs_pages;
    mb.has_flomon               = spec->has_flomon;

    ndfs_mb_write(&mb, img);

    /* Write extended info if requested or if spec says so */
    if (options->include_extended_info || spec->ext_valid) {
        mb.ext_valid = true;
        mb.ext_flag_word = options->flag_word;
        mb.ext_last_system_number = options->system_number;
        mb.ext_pages_available = spec->ndfs_pages;
        ndfs_mb_write_extended(&mb, img);
    }

    /* ---- User file index block ---- */
    /* The user file index block contains up to 8 pointers.
       First pointer -> user data page (user_file_block + 1) */
    {
        uint32_t user_data_page = spec->user_file_block + 1;
        bp.block_id = user_data_page;
        bp.type = NDFS_PTR_CONTIGUOUS;
        ndfs_bp_to_bytes(&bp, img + (size_t)spec->user_file_block * NDFS_PAGE_SIZE, 0);

        /* ---- User data page: SYSTEM user ---- */
        ndfs_ue_init(&ue);
        strcpy(ue.user_name, "SYSTEM");
        ue.user_index = 0;
        ue.pages_reserved = spec->ndfs_pages > 1000 ? spec->ndfs_pages / 2 : 500;
        ue.pages_used = 0;
        ndfs_ue_to_bytes(&ue, img + (size_t)user_data_page * NDFS_PAGE_SIZE);
    }

    /* ---- Object file index block ---- */
    /* First pointer -> object data page (object_file_block + 1) */
    {
        uint32_t obj_data_page = spec->object_file_block + 1;
        bp.block_id = obj_data_page;
        bp.type = NDFS_PTR_CONTIGUOUS;
        ndfs_bp_to_bytes(&bp, img + (size_t)spec->object_file_block * NDFS_PAGE_SIZE, 0);
        /* Object data page is all zeros (no files yet) - already calloc'd */
    }

    /* ---- Bitmap ---- */
    bitmap_bytes = (spec->ndfs_pages + 7) / 8;
    bitmap_pages = (bitmap_bytes + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE;

    /* Mark system pages as used in the bitmap.
       System pages: 0 (master), user file index, user data page,
       object file index, object data page, bitmap pages */
    {
        uint8_t *bm = img + (size_t)spec->bit_file_block * NDFS_PAGE_SIZE;

        /* Mark page 0 */
        bm[0] |= (uint8_t)(1u << 0);

        /* Mark user file index block and its data page */
        b = spec->user_file_block;
        bm[b / 8] |= (uint8_t)(1u << (b % 8));
        b = spec->user_file_block + 1;
        if (b < spec->ndfs_pages) {
            bm[b / 8] |= (uint8_t)(1u << (b % 8));
        }

        /* Mark object file index block and its data page */
        b = spec->object_file_block;
        bm[b / 8] |= (uint8_t)(1u << (b % 8));
        b = spec->object_file_block + 1;
        if (b < spec->ndfs_pages) {
            bm[b / 8] |= (uint8_t)(1u << (b % 8));
        }

        /* Mark bitmap pages */
        for (i = 0; i < bitmap_pages; i++) {
            b = spec->bit_file_block + i;
            if (b < spec->ndfs_pages) {
                bm[b / 8] |= (uint8_t)(1u << (b % 8));
            }
        }
    }

    /* ---- Open the image as a filesystem ---- */
    err = ndfs_open_buffer_copy(img, img_size, false, out_fs);
    free(img);

    return err;
}
