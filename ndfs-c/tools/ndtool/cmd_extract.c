/**
 * ndtool: extract files from NDFS image.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"
#include "parity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static int extract_one_file(ndtool_ctx_t *ctx, const char *user_name,
                            const ndfs_file_entry_t *entry)
{
    char ndfs_path[256];
    char host_name[64];
    char out_path[512];
    uint8_t *data = NULL;
    size_t data_size = 0;
    ndfs_error_t err;
    int ret;

    /* Build NDFS path */
    snprintf(ndfs_path, sizeof(ndfs_path), "%s/%s", user_name, entry->full_name);

    /* Read file data */
    err = ndfs_read_file(ctx->fs, ndfs_path, &data, &data_size);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error reading '%s': %s\n", ndfs_path, ndfs_strerror(err));
        return -1;
    }

    /* Strip parity if requested */
    if (ctx->do_parity) {
        ndtool_strip_parity(data, data_size);
    }

    /* Build host filename */
    ndtool_to_host_name(entry->full_name, host_name, sizeof(host_name), ctx->lowercase);

    /* Build output path */
    out_path[0] = '\0';
    if (ctx->output_dir) {
        ensure_dir(ctx->output_dir);
        snprintf(out_path, sizeof(out_path), "%s/", ctx->output_dir);
    }

    if (ctx->with_dirs) {
        char user_dir[256];
        if (ctx->lowercase) {
            ndtool_to_host_name(user_name, user_dir, sizeof(user_dir), true);
        } else {
            strncpy(user_dir, user_name, sizeof(user_dir) - 1);
            user_dir[sizeof(user_dir) - 1] = '\0';
        }

        {
            char full_dir[768];
            snprintf(full_dir, sizeof(full_dir), "%.511s%.255s", out_path, user_dir);
            ensure_dir(full_dir);
            snprintf(out_path + strlen(out_path),
                     sizeof(out_path) - strlen(out_path),
                     "%s/", user_dir);
        }
    }

    strncat(out_path, host_name, sizeof(out_path) - strlen(out_path) - 1);

    if (ctx->dry_run) {
        printf("  %s -> %s (%zu bytes) [dry run]\n", ndfs_path, out_path, data_size);
        ndfs_free_data(data);
        return 0;
    }

    /* Write to local file */
    ret = ndtool_write_local_file(out_path, data, data_size);
    if (ret == 0 && ctx->verbose) {
        printf("  Extracted %s -> %s (%zu bytes)\n", ndfs_path, out_path, data_size);
    }

    /* Write .xat sidecar file if requested */
    if (ret == 0 && ctx->use_xat) {
        ndfs_xat_properties_t xat;
        ndfs_error_t xerr = ndfs_get_file_properties(ctx->fs, ndfs_path, &xat);
        if (xerr == NDFS_OK) {
            char *xat_json = NULL;
            xerr = ndfs_xat_serialize(&xat, &xat_json);
            if (xerr == NDFS_OK && xat_json) {
                char xat_path[520];
                ndfs_xat_filename(out_path, xat_path, sizeof(xat_path));
                ndtool_write_local_file(xat_path,
                    (const uint8_t *)xat_json, strlen(xat_json));
                if (ctx->verbose) {
                    printf("  Wrote XAT %s\n", xat_path);
                }
                free(xat_json);
            }
        }
    }

    ndfs_free_data(data);
    return ret;
}

static int extract_user_files(ndtool_ctx_t *ctx, const char *user_name)
{
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;
    ndfs_error_t err;
    int ret = 0;

    err = ndfs_list_directory(ctx->fs, user_name, &entries, &count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error listing '%s': %s\n", user_name, ndfs_strerror(err));
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (entries[i].is_directory) continue;

        if (ctx->filter_file && strcmp(entries[i].full_name, ctx->filter_file) != 0) {
            continue;
        }

        if (extract_one_file(ctx, user_name, &entries[i]) != 0) {
            ret = -1;
        }
    }

    ndfs_free_entries(entries);
    return ret;
}

int cmd_extract(ndtool_ctx_t *ctx)
{
    if (ctx->filter_user) {
        return extract_user_files(ctx, ctx->filter_user);
    }

    /* Extract from all users */
    {
        ndfs_file_entry_t *users = NULL;
        size_t user_count = 0;
        size_t i;
        ndfs_error_t err;
        int ret = 0;

        err = ndfs_list_directory(ctx->fs, "/", &users, &user_count);
        if (err != NDFS_OK) {
            fprintf(stderr, "Error listing root: %s\n", ndfs_strerror(err));
            return -1;
        }

        for (i = 0; i < user_count; i++) {
            if (!users[i].is_directory) continue;
            if (extract_user_files(ctx, users[i].name) != 0) {
                ret = -1;
            }
        }

        ndfs_free_entries(users);
        return ret;
    }
}
