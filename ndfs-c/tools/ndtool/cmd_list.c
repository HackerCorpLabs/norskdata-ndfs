/**
 * ndtool: list files and users.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── List files for one user ──────────────────────────────────────── */

static int list_user_files(ndtool_ctx_t *ctx, const char *user_name,
                           const char *file_pat)
{
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;
    ndfs_error_t err;

    err = ndfs_list_directory(ctx->fs, user_name, &entries, &count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error listing '%s': %s\n", user_name, ndfs_strerror(err));
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (file_pat && !ndfs_wildmatch(file_pat, entries[i].full_name, true)) {
            continue;
        }
        if (ctx->verbose) {
            printf("  %-24s %8u bytes  %4u pages\n",
                   entries[i].full_name,
                   entries[i].size,
                   entries[i].pages);
        } else {
            printf("  %-24s %8u bytes  %4u pages\n",
                   entries[i].full_name,
                   entries[i].size,
                   entries[i].pages);
        }
    }

    ndfs_free_entries(entries);
    return 0;
}

/* ── Volume header ────────────────────────────────────────────────── */

/* Print the volume (directory) name from the master block, if present. */
static void print_volume_header(ndtool_ctx_t *ctx)
{
    const ndfs_master_block_t *mb = NULL;

    if (ndfs_get_master_block(ctx->fs, &mb) != NDFS_OK || !mb) return;
    if (mb->directory_name[0] == '\0') return;

    printf("Volume: %s\n\n", mb->directory_name);
}

/* ── cmd_list ─────────────────────────────────────────────────────── */

int cmd_list(ndtool_ctx_t *ctx)
{
    char user_pat_buf[NDFS_NAME_MAX + 1];
    const char *user_pat = NULL;
    const char *file_pat = ctx->filter_file;
    ndfs_file_entry_t *users = NULL;
    size_t user_count = 0;
    size_t i;
    ndfs_error_t err;

    print_volume_header(ctx);

    /* A -F pattern of the form USER/FILE splits into a user glob and a file
     * glob; otherwise it filters file names within the selected user(s). */
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
    if (!user_pat && ctx->filter_user) user_pat = ctx->filter_user;

    err = ndfs_list_directory(ctx->fs, "/", &users, &user_count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error listing root: %s\n", ndfs_strerror(err));
        return -1;
    }

    for (i = 0; i < user_count; i++) {
        if (!users[i].is_directory) continue;
        if (user_pat && !ndfs_wildmatch(user_pat, users[i].name, true)) continue;
        printf("USER: %s\n", users[i].name);
        list_user_files(ctx, users[i].name, file_pat);
    }

    ndfs_free_entries(users);
    return 0;
}

/* ── cmd_users ────────────────────────────────────────────────────── */

int cmd_users(ndtool_ctx_t *ctx)
{
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;
    size_t i;
    ndfs_error_t err;

    err = ndfs_get_users(ctx->fs, &users, &count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error getting users: %s\n", ndfs_strerror(err));
        return -1;
    }

    printf("Users: %zu\n", count);
    for (i = 0; i < count; i++) {
        int32_t free_pages = ndfs_ue_free_pages(&users[i]);
        /* User index shown in 3-digit octal, matching SINTRAN's
         * file-system-investigator (LIST-USERS) e.g. "011 BUILD". */
        printf("  %03o  %-16s  Reserved: %5u  Used: %5u  Free: %5d\n",
               users[i].user_index,
               users[i].user_name,
               users[i].pages_reserved,
               users[i].pages_used,
               free_pages);

        if (ctx->verbose) {
            int j;
            printf("         Friends:");
            for (j = 0; j < NDFS_MAX_FRIENDS; j++) {
                if (ndfs_uf_is_active(&users[i].friends[j])) {
                    printf(" [%u]", ndfs_uf_friend_index(&users[i].friends[j]));
                }
            }
            printf("\n");
        }
    }

    ndfs_free_users(users);
    return 0;
}
