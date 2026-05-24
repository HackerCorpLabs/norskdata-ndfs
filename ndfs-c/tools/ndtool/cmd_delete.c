/**
 * ndtool: delete file from NDFS image.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"

#include <stdio.h>
#include <string.h>

int cmd_delete(ndtool_ctx_t *ctx, const char *ndfs_path)
{
    ndfs_error_t err;

    if (!ctx->force) {
        char resp[16];
        printf("Delete %s? [y/N] ", ndfs_path);
        fflush(stdout);
        if (!fgets(resp, sizeof(resp), stdin) ||
            (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Aborted.\n");
            return 0;
        }
    }

    if (ctx->dry_run) {
        printf("Would delete %s [dry run]\n", ndfs_path);
        return 0;
    }

    err = ndfs_delete_file(ctx->fs, ndfs_path);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error deleting '%s': %s\n", ndfs_path, ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("Deleted %s\n", ndfs_path);
    ctx->modified = false;
    return 0;
}
