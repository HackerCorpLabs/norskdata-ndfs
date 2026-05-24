/**
 * ndtool: create new NDFS disk image.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"

#include <stdio.h>
#include <string.h>

int cmd_create(ndtool_ctx_t *ctx, const char *template_name,
               uint32_t custom_pages, const char *dir_name)
{
    ndfs_image_options_t opts;
    ndfs_image_template_t tmpl;
    ndfs_error_t err;

    /* Map template name to enum */
    if (strcmp(template_name, "floppy360") == 0) {
        tmpl = NDFS_TMPL_FLOPPY_360KB;
    } else if (strcmp(template_name, "floppy12") == 0) {
        tmpl = NDFS_TMPL_FLOPPY_12MB;
    } else if (strcmp(template_name, "smd75") == 0) {
        tmpl = NDFS_TMPL_SMD_75MB;
    } else if (strcmp(template_name, "winchester74") == 0) {
        tmpl = NDFS_TMPL_WINCHESTER_74MB;
    } else if (strcmp(template_name, "custom") == 0) {
        tmpl = NDFS_TMPL_CUSTOM;
        if (custom_pages == 0) {
            fprintf(stderr, "Error: --pages N required for custom template\n");
            return -1;
        }
    } else {
        fprintf(stderr, "Error: unknown template '%s'\n", template_name);
        fprintf(stderr, "Valid templates: floppy360, floppy12, smd75, winchester74, custom\n");
        return -1;
    }

    ndfs_image_options_init(&opts);
    opts.template_type = tmpl;

    if (dir_name) {
        strncpy(opts.directory_name, dir_name, NDFS_NAME_MAX);
        opts.directory_name[NDFS_NAME_MAX] = '\0';
    }

    if (custom_pages) {
        opts.custom_pages = custom_pages;
    }

    /* Floppy images typically don't have extended info */
    if (tmpl == NDFS_TMPL_FLOPPY_360KB || tmpl == NDFS_TMPL_FLOPPY_12MB) {
        opts.include_extended_info = false;
    } else {
        opts.include_extended_info = true;
    }

    if (ctx->dry_run) {
        printf("Would create %s image: %s [dry run]\n", template_name, ctx->image_path);
        return 0;
    }

    err = ndfs_create_image(&ctx->fs, &opts);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error creating image: %s\n", ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) {
        ndfs_close(ctx->fs);
        ctx->fs = NULL;
        return -1;
    }

    printf("Created %s image: %s\n", template_name, ctx->image_path);
    ndfs_close(ctx->fs);
    ctx->fs = NULL;
    return 0;
}
