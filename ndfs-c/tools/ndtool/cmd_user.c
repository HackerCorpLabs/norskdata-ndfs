/**
 * ndtool: user management commands.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Find a user by name (case-insensitive) and return their entry.
 * Returns NDFS_OK and fills out_user, or NDFS_ERR_NOT_FOUND.
 */
static ndfs_error_t find_user_by_name(ndtool_ctx_t *ctx, const char *name,
                                       ndfs_user_entry_t *out_user)
{
    ndfs_user_entry_t *users = NULL;
    size_t count = 0;
    ndfs_error_t err = ndfs_get_users(ctx->fs, &users, &count);
    if (err != NDFS_OK) return err;

    /* Uppercase the search name */
    char upper[NDFS_NAME_MAX + 1];
    size_t i;
    for (i = 0; i < NDFS_NAME_MAX && name[i]; i++) {
        upper[i] = (char)toupper((unsigned char)name[i]);
    }
    upper[i] = '\0';

    for (i = 0; i < count; i++) {
        if (strcmp(users[i].user_name, upper) == 0) {
            if (out_user) *out_user = users[i];
            ndfs_free_users(users);
            return NDFS_OK;
        }
    }

    ndfs_free_users(users);
    return NDFS_ERR_NOT_FOUND;
}

int cmd_useradd(ndtool_ctx_t *ctx, const char *name, uint32_t quota)
{
    ndfs_error_t err;

    if (ctx->dry_run) {
        printf("Would add user '%s' with quota %u [dry run]\n", name, quota);
        return 0;
    }

    err = ndfs_add_user(ctx->fs, name, quota);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error adding user '%s': %s\n", name, ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("Added user '%s' with quota %u pages\n", name, quota);
    ctx->modified = false;
    return 0;
}

int cmd_userdel(ndtool_ctx_t *ctx, const char *name)
{
    ndfs_error_t err;
    ndfs_user_entry_t user;

    /* Find user by name */
    err = find_user_by_name(ctx, name, &user);
    if (err != NDFS_OK) {
        fprintf(stderr, "User '%s' not found\n", name);
        return -1;
    }

    if (ctx->dry_run) {
        printf("Would remove user '%s' (index %u) [dry run]\n",
               user.user_name, user.user_index);
        return 0;
    }

    err = ndfs_remove_user(ctx->fs, user.user_index);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error removing user '%s': %s\n", name, ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("Removed user '%s' (index %u)\n", user.user_name, user.user_index);
    ctx->modified = false;
    return 0;
}

int cmd_addquota(ndtool_ctx_t *ctx, const char *name, uint32_t pages)
{
    ndfs_error_t err;
    ndfs_user_entry_t user;
    uint32_t free_on_disk = 0;

    /* Find user by name */
    err = find_user_by_name(ctx, name, &user);
    if (err != NDFS_OK) {
        fprintf(stderr, "User '%s' not found\n", name);
        return -1;
    }

    /* Check disk space */
    ndfs_get_free_pages(ctx->fs, &free_on_disk);
    if (pages > free_on_disk) {
        fprintf(stderr, "Cannot add %u pages: only %u free on disk\n",
                pages, free_on_disk);
        return -1;
    }

    uint32_t new_quota = user.pages_reserved + pages;

    if (ctx->dry_run) {
        printf("Would add %u pages to '%s' quota (%u -> %u) [dry run]\n",
               pages, user.user_name, user.pages_reserved, new_quota);
        return 0;
    }

    err = ndfs_update_user_quota(ctx->fs, user.user_index, new_quota);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error updating quota: %s\n", ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("User '%s' quota: %u -> %u pages (+%u)\n",
           user.user_name, user.pages_reserved, new_quota, pages);
    ctx->modified = false;
    return 0;
}

int cmd_remquota(ndtool_ctx_t *ctx, const char *name, uint32_t pages)
{
    ndfs_error_t err;
    ndfs_user_entry_t user;

    /* Find user by name */
    err = find_user_by_name(ctx, name, &user);
    if (err != NDFS_OK) {
        fprintf(stderr, "User '%s' not found\n", name);
        return -1;
    }

    /* Check we're not going below used pages */
    if (pages > user.pages_reserved) {
        fprintf(stderr, "Cannot remove %u pages: user only has %u reserved\n",
                pages, user.pages_reserved);
        return -1;
    }

    uint32_t new_quota = user.pages_reserved - pages;
    if (new_quota < user.pages_used) {
        fprintf(stderr, "Cannot reduce quota to %u: user is using %u pages\n",
                new_quota, user.pages_used);
        return -1;
    }

    if (ctx->dry_run) {
        printf("Would remove %u pages from '%s' quota (%u -> %u) [dry run]\n",
               pages, user.user_name, user.pages_reserved, new_quota);
        return 0;
    }

    err = ndfs_update_user_quota(ctx->fs, user.user_index, new_quota);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error updating quota: %s\n", ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("User '%s' quota: %u -> %u pages (-%u)\n",
           user.user_name, user.pages_reserved, new_quota, pages);
    ctx->modified = false;
    return 0;
}

int cmd_passwd(ndtool_ctx_t *ctx, const char *name)
{
    ndfs_error_t err;

    if (ctx->dry_run) {
        printf("Would clear password for '%s' [dry run]\n", name);
        return 0;
    }

    err = ndfs_clear_user_password(ctx->fs, name);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error clearing password for '%s': %s\n", name, ndfs_strerror(err));
        return -1;
    }

    if (ndtool_save_image(ctx) != 0) return -1;

    printf("Password cleared for '%s'\n", name);
    ctx->modified = false;
    return 0;
}
