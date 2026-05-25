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

#ifdef _WIN32
#include <direct.h>
#define ndtool_mkdir(path) _mkdir(path)
#else
#define ndtool_mkdir(path) mkdir((path), 0755)
#endif

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ndtool_mkdir(path);
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

    /* Collision handling against the host filesystem. */
    {
        struct stat st;
        bool exists = (stat(out_path, &st) == 0);

        if (ctx->dry_run) {
            const char *act = !exists ? "create"
                : (ctx->on_exist == NDTOOL_ON_EXIST_OVERWRITE ? "overwrite" : "skip");
            printf("  %s -> %s (%zu bytes) [%s]\n",
                   ndfs_path, out_path, data_size, act);
            ndfs_free_data(data);
            return 0;
        }

        if (!ndtool_confirm_overwrite(ctx, out_path, exists)) {
            ndfs_free_data(data);
            return 0;
        }
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

/* Extract one user's files, keeping only those whose full name matches
 * file_pat (a wildcard pattern; NULL means "all files"). */
static int extract_user_files(ndtool_ctx_t *ctx, const char *user_name,
                              const char *file_pat)
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

        /* Case-insensitive wildcard match (NDFS names are uppercase). */
        if (file_pat && !ndfs_wildmatch(file_pat, entries[i].full_name, true)) {
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
    char user_pat_buf[NDFS_NAME_MAX + 1];
    const char *user_pat = NULL;
    const char *file_pat = ctx->filter_file;
    ndfs_file_entry_t *users = NULL;
    size_t user_count = 0;
    size_t i;
    ndfs_error_t err;
    int ret = 0;
    int matched_users = 0;

    /* A -F pattern of the form USER/FILE splits into a user glob and a file
     * glob, e.g. SYSTEM followed by '*:MODE', or a '*' user with a file glob. */
    if (file_pat) {
        const char *slash = strchr(file_pat, '/');
        if (slash) {
            size_t ulen = (size_t)(slash - file_pat);
            if (ulen >= sizeof(user_pat_buf)) ulen = sizeof(user_pat_buf) - 1;
            memcpy(user_pat_buf, file_pat, ulen);
            user_pat_buf[ulen] = '\0';
            user_pat = user_pat_buf;
            file_pat = slash + 1;
        }
    }

    /* An explicit -u USER selects the user when no user glob was embedded. */
    if (!user_pat && ctx->filter_user) user_pat = ctx->filter_user;

    err = ndfs_list_directory(ctx->fs, "/", &users, &user_count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error listing root: %s\n", ndfs_strerror(err));
        return -1;
    }

    for (i = 0; i < user_count; i++) {
        if (!users[i].is_directory) continue;
        if (user_pat && !ndfs_wildmatch(user_pat, users[i].name, true)) continue;
        matched_users++;
        if (extract_user_files(ctx, users[i].name, file_pat) != 0) {
            ret = -1;
        }
    }

    if (user_pat && matched_users == 0) {
        fprintf(stderr, "No users match '%s'\n", user_pat);
        ret = -1;
    }

    ndfs_free_entries(users);
    return ret;
}
