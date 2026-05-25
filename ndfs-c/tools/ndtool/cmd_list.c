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

static int list_user_files(ndtool_ctx_t *ctx, const char *user_name)
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
        if (ctx->filter_file && strcmp(entries[i].full_name, ctx->filter_file) != 0) {
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
    print_volume_header(ctx);

    if (ctx->filter_user) {
        printf("USER: %s\n", ctx->filter_user);
        return list_user_files(ctx, ctx->filter_user);
    }

    /* List all users and their files */
    {
        ndfs_file_entry_t *users = NULL;
        size_t user_count = 0;
        size_t i;
        ndfs_error_t err;

        err = ndfs_list_directory(ctx->fs, "/", &users, &user_count);
        if (err != NDFS_OK) {
            fprintf(stderr, "Error listing root: %s\n", ndfs_strerror(err));
            return -1;
        }

        for (i = 0; i < user_count; i++) {
            if (!users[i].is_directory) continue;
            printf("USER: %s\n", users[i].name);
            list_user_files(ctx, users[i].name);
        }

        ndfs_free_entries(users);
    }

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
        printf("  [%3u]  %-16s  Reserved: %5u  Used: %5u  Free: %5d\n",
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
