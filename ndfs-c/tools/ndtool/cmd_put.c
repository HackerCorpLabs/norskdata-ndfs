/**
 * ndtool: copy file(s) into NDFS image.
 *
 * Supports three forms (dispatched from cmd_put):
 *   --put FILE NDFS_PATH        single file to an explicit NDFS path
 *   --put FILE --dest USER      single file, auto-named, into USER
 *   --put 'GLOB' --dest USER    every host file matching GLOB, into USER
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"
#include "parity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <dirent.h>

/* Host glob matching is case-insensitive on Windows (case-insensitive FS,
 * and the shell does not pre-expand), case-sensitive elsewhere (matching the
 * Unix shell). */
#ifdef _WIN32
#define HOST_GLOB_CI true
#else
#define HOST_GLOB_CI false
#endif

/* Build the NDFS destination path "USER/NAME:TYPE" for a host file, deriving
 * the NDFS name from the file's basename. */
static void build_dest_path(const char *dest_user, const char *local_path,
                            char *out, size_t out_len)
{
    char name_buf[256];
    char ndfs_name[64];
    const char *base;

    strncpy(name_buf, local_path, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    base = basename(name_buf);

    ndtool_to_ndfs_name(base, ndfs_name, sizeof(ndfs_name));
    snprintf(out, out_len, "%s/%s", dest_user, ndfs_name);
}

/* Copy a single host file to an explicit NDFS path. Does NOT save the image;
 * the caller saves once after the batch. Returns 0 on success/skip, -1 on
 * error. */
static int put_one_file(ndtool_ctx_t *ctx, const char *local_path,
                        const char *ndfs_path)
{
    uint8_t *data = NULL;
    size_t data_size = 0;
    bool exists = false;
    ndfs_error_t err;

    data = ndtool_read_local_file(local_path, &data_size);
    if (!data) {
        fprintf(stderr, "Error: cannot read '%s'\n", local_path);
        return -1;
    }

    /* Collision handling against the image. */
    ndfs_file_exists(ctx->fs, ndfs_path, &exists);

    if (ctx->dry_run) {
        const char *act = !exists ? "create"
            : (ctx->on_exist == NDTOOL_ON_EXIST_OVERWRITE ? "overwrite" : "skip");
        printf("  %s -> %s (%zu bytes) [%s]\n",
               local_path, ndfs_path, data_size, act);
        free(data);
        return 0;
    }

    if (!ndtool_confirm_overwrite(ctx, ndfs_path, exists)) {
        free(data);
        return 0;
    }

    /* Set parity if requested. */
    if (ctx->do_parity) {
        ndtool_set_parity(data, data_size);
    }

    err = ndfs_write_file(ctx->fs, ndfs_path, data, data_size);
    free(data);

    if (err != NDFS_OK) {
        fprintf(stderr, "Error writing '%s': %s\n", ndfs_path, ndfs_strerror(err));
        return -1;
    }

    ctx->modified = true;
    if (ctx->verbose) {
        printf("  copied %s -> %s (%zu bytes)\n", local_path, ndfs_path, data_size);
    }
    return 0;
}

/* Expand a host glob such as 'src' + slash + '*.NPL' and put each match into
 * --dest USER. Returns 0 on success, -1 if any file errored or none matched. */
static int put_glob(ndtool_ctx_t *ctx, const char *pattern)
{
    char dir_buf[512];
    const char *dir;
    const char *file_pat;
    const char *slash;
    DIR *d;
    struct dirent *ent;
    int ret = 0;
    int matched = 0;

    if (!ctx->dest_user) {
        fprintf(stderr, "Error: --put with a wildcard requires --dest USER\n");
        return -1;
    }

    /* Split "dir/filepat" -- accept both separators on Windows. */
    slash = strrchr(pattern, '/');
#ifdef _WIN32
    {
        const char *bslash = strrchr(pattern, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
    }
#endif
    if (slash) {
        size_t dlen = (size_t)(slash - pattern);
        if (dlen == 0) dlen = 1; /* pattern like "/foo" -> root */
        if (dlen >= sizeof(dir_buf)) dlen = sizeof(dir_buf) - 1;
        memcpy(dir_buf, pattern, dlen);
        dir_buf[dlen] = '\0';
        dir = dir_buf;
        file_pat = slash + 1;
    } else {
        dir = ".";
        file_pat = pattern;
    }

    d = opendir(dir);
    if (!d) {
        fprintf(stderr, "Error: cannot open directory '%s'\n", dir);
        return -1;
    }

    while ((ent = readdir(d)) != NULL) {
        char local_path[768];
        char ndfs_path[128];

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!ndfs_wildmatch(file_pat, ent->d_name, HOST_GLOB_CI)) continue;

        matched++;
        snprintf(local_path, sizeof(local_path), "%.500s/%.255s", dir, ent->d_name);
        build_dest_path(ctx->dest_user, local_path, ndfs_path, sizeof(ndfs_path));

        if (put_one_file(ctx, local_path, ndfs_path) != 0) ret = -1;
    }

    closedir(d);

    if (matched == 0) {
        fprintf(stderr, "No files match '%s'\n", pattern);
        return -1;
    }
    return ret;
}

int cmd_put(ndtool_ctx_t *ctx, const char *local_path, const char *ndfs_path)
{
    int ret;

    /* Batch mode: the local path is a wildcard. */
    if (ndtool_has_wildcard(local_path)) {
        ret = put_glob(ctx, local_path);
    } else {
        char auto_path[256];
        const char *target = ndfs_path;

        /* No explicit NDFS path: derive name from the file. Prefer --dest
         * USER; otherwise fall back to the first user in the image. */
        if (!target) {
            if (ctx->dest_user) {
                build_dest_path(ctx->dest_user, local_path,
                                auto_path, sizeof(auto_path));
            } else {
                char name_buf[256];
                char ndfs_name[64];
                const char *base;
                ndfs_user_entry_t *users = NULL;
                size_t user_count = 0;

                if (ndfs_get_users(ctx->fs, &users, &user_count) != NDFS_OK ||
                    user_count == 0) {
                    fprintf(stderr, "Error: no users in image\n");
                    if (users) ndfs_free_users(users);
                    return -1;
                }
                strncpy(name_buf, local_path, sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                base = basename(name_buf);
                ndtool_to_ndfs_name(base, ndfs_name, sizeof(ndfs_name));
                snprintf(auto_path, sizeof(auto_path), "%s/%s",
                         users[0].user_name, ndfs_name);
                ndfs_free_users(users);
            }
            target = auto_path;
        } else if (ctx->dest_user && !strchr(target, '/')) {
            /* An explicit name without a USER/ prefix, plus --dest USER:
             * place it under that user (e.g. --put f.txt --dest BUILD T:TXT). */
            snprintf(auto_path, sizeof(auto_path), "%s/%s", ctx->dest_user, target);
            target = auto_path;
        }

        ret = put_one_file(ctx, local_path, target);
    }

    if (ret != 0) return ret;

    /* Save once for the whole operation. */
    if (ctx->modified) {
        if (ndtool_save_image(ctx) != 0) return -1;
        ctx->modified = false;
    }
    return 0;
}
