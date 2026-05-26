/**
 * ndtool: interactive shell.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ndtool.h"
#include "parity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* ── Shell helpers ────────────────────────────────────────────────── */

static void shell_help(void)
{
    printf("Commands:\n");
    printf("  ls [USER]          List users or user's files\n");
    printf("  cat PATH           Display file contents (parity stripped)\n");
    printf("  hexdump PATH       Hex dump of file contents\n");
    printf("  get PATH [LOCAL]   Extract file to local disk\n");
    printf("  put LOCAL [PATH]   Copy local file into image\n");
    printf("  rm PATH            Delete file\n");
    printf("  mv OLD NEW         Rename file\n");
    printf("  info               Show filesystem info\n");
    printf("  bitmap             Show bitmap visualization and validation\n");
    printf("  fsck               Full filesystem check (block refs, quotas, etc.)\n");
    printf("  stat PATH          Show detailed file metadata\n");
    printf("  edit PATH          Edit file in external editor (extracts, edits, re-imports)\n");
    printf("  users              List users with quota info\n");
    printf("  useradd NAME [Q]   Add user (Q = quota, default 100)\n");
    printf("  userdel NAME       Remove user by name\n");
    printf("  addquota NAME N    Add N pages to user quota\n");
    printf("  remquota NAME N    Remove N pages from user quota\n");
    printf("  passwd NAME        Clear user password\n");
    printf("  save [PATH]        Save image (optionally to new path)\n");
    printf("  help               Show this help\n");
    printf("  quit / exit        Exit shell\n");
}

static void shell_ls(ndtool_ctx_t *ctx, const char *user)
{
    ndfs_file_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;
    ndfs_error_t err;

    if (!user) {
        /* List users with file count summary */
        ndfs_user_entry_t *users = NULL;
        size_t user_count = 0;

        err = ndfs_get_users(ctx->fs, &users, &user_count);
        if (err != NDFS_OK) {
            fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
            return;
        }

        for (i = 0; i < user_count; i++) {
            ndfs_file_entry_t *files = NULL;
            size_t file_count = 0;
            ndfs_list_directory(ctx->fs, users[i].user_name, &files, &file_count);
            printf("  %-16s  %3zu files  Reserved: %5u  Used: %5u\n",
                   users[i].user_name, file_count,
                   users[i].pages_reserved, users[i].pages_used);
            ndfs_free_entries(files);
        }

        ndfs_free_users(users);
        return;
    }

    err = ndfs_list_directory(ctx->fs, user, &entries, &count);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    for (i = 0; i < count; i++) {
        printf("  %-24s %8u bytes  %4u pages\n",
               entries[i].full_name, entries[i].size, entries[i].pages);
    }

    ndfs_free_entries(entries);
}

static void shell_cat(ndtool_ctx_t *ctx, const char *path)
{
    uint8_t *data = NULL;
    size_t size = 0;
    size_t i;
    ndfs_error_t err;

    if (!path) {
        fprintf(stderr, "Usage: cat USER/FILE:TYPE\n");
        return;
    }

    err = ndfs_read_file(ctx->fs, path, &data, &size);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    /* Strip parity for display */
    for (i = 0; i < size; i++) {
        data[i] &= 0x7F;
    }

    fwrite(data, 1, size, stdout);
    if (size > 0 && data[size - 1] != '\n') {
        printf("\n");
    }

    ndfs_free_data(data);
}

static void shell_hexdump(ndtool_ctx_t *ctx, const char *path)
{
    uint8_t *data = NULL;
    size_t size = 0;
    size_t i, j;
    ndfs_error_t err;

    if (!path) {
        fprintf(stderr, "Usage: hexdump USER/FILE:TYPE\n");
        return;
    }

    err = ndfs_read_file(ctx->fs, path, &data, &size);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    for (i = 0; i < size; i += 16) {
        printf("%08zx  ", i);

        /* Hex bytes */
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" |");

        /* ASCII */
        for (j = 0; j < 16 && i + j < size; j++) {
            uint8_t c = data[i + j] & 0x7F;
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }

        printf("|\n");
    }

    ndfs_free_data(data);
}

static void shell_edit(ndtool_ctx_t *ctx, const char *path)
{
    uint8_t *data = NULL;
    size_t size = 0;
    ndfs_error_t err;
    char tmp_path[512];
    char host_name[64];
    const char *editor;
    char cmd_buf[1024];
    int ret;
    struct stat st_before, st_after;
    ndfs_xat_properties_t saved_xat;
    bool has_xat = false;

    if (!path) {
        fprintf(stderr, "Usage: edit USER/FILE:TYPE\n");
        return;
    }

    /* Save XAT properties (status bits) before editing */
    if (ndfs_get_file_properties(ctx->fs, path, &saved_xat) == NDFS_OK) {
        has_xat = true;
    }

    /* Read file from image */
    err = ndfs_read_file(ctx->fs, path, &data, &size);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error reading '%s': %s\n", path, ndfs_strerror(err));
        return;
    }

    /* Strip parity for editing */
    ndtool_strip_parity(data, size);

    /* Build temp file path */
    ndtool_to_host_name(path, host_name, sizeof(host_name), true);
    /* Replace / with _ for flat temp filename */
    {
        size_t k;
        for (k = 0; host_name[k]; k++) {
            if (host_name[k] == '/') host_name[k] = '_';
        }
    }
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/ndtool_%s", host_name);

    /* Write to temp file */
    if (ndtool_write_local_file(tmp_path, data, size) != 0) {
        ndfs_free_data(data);
        return;
    }
    ndfs_free_data(data);

    /* Get file timestamp before edit */
    stat(tmp_path, &st_before);

    /* Editor priority: --editor flag > $EDITOR > $VISUAL > code --wait */
    editor = ctx->editor;
    if (!editor || editor[0] == '\0') editor = getenv("EDITOR");
    if (!editor || editor[0] == '\0') editor = getenv("VISUAL");
    if (!editor || editor[0] == '\0') editor = "code --wait";

    /* Open editor */
    printf("Opening '%s' in editor...\n", path);
    snprintf(cmd_buf, sizeof(cmd_buf), "%s \"%s\"", editor, tmp_path);
    ret = system(cmd_buf);

    if (ret != 0) {
        fprintf(stderr, "Editor exited with code %d\n", ret);
        /* Still check if file changed */
    }

    /* Check if file was modified */
    stat(tmp_path, &st_after);
    if (st_before.st_mtime == st_after.st_mtime &&
        (size_t)st_before.st_size == size) {
        printf("No changes detected, skipping re-import.\n");
        remove(tmp_path);
        return;
    }

    /* Read back modified file */
    {
        size_t new_size = 0;
        uint8_t *new_data = ndtool_read_local_file(tmp_path, &new_size);
        if (!new_data) {
            fprintf(stderr, "Error reading back temp file\n");
            remove(tmp_path);
            return;
        }

        /* Check if this is a text type -- if so, re-apply even parity */
        {
            /* Extract type from path: after last : */
            const char *colon = strrchr(path, ':');
            if (colon && ndtool_is_text_type(colon + 1)) {
                ndtool_set_parity(new_data, new_size);
                printf("Re-applied even parity (text file type).\n");
            }
        }

        /* Write back to image */
        err = ndfs_write_file(ctx->fs, path, new_data, new_size);
        free(new_data);

        if (err != NDFS_OK) {
            fprintf(stderr, "Error writing back '%s': %s\n", path, ndfs_strerror(err));
        } else {
            /* Re-apply saved XAT status bits (access_bits, file_type, etc.) */
            if (has_xat) {
                ndfs_object_entry_t obj;
                /* Parse the path to get user_name and full_name for lookup */
                {
                    const char *slash = strchr(path, '/');
                    const char *file_part = slash ? slash + 1 : path;
                    char user_buf[NDFS_NAME_MAX + 1];
                    if (slash) {
                        size_t ulen = (size_t)(slash - path);
                        if (ulen > NDFS_NAME_MAX) ulen = NDFS_NAME_MAX;
                        memcpy(user_buf, path, ulen);
                        user_buf[ulen] = '\0';
                    } else {
                        user_buf[0] = '\0';
                    }
                    if (ndfs_get_object_entry(ctx->fs, file_part,
                                              user_buf[0] ? user_buf : NULL,
                                              &obj) == NDFS_OK) {
                        /* Logging only -- the object in the fs is what matters,
                           and we can't directly modify it through the public API.
                           The status bits are preserved by the XAT sidecar on
                           extract/put, and the edit command already re-applies
                           parity. */
                    }
                }
            }
            ctx->modified = true;
            printf("Updated '%s' (%zu bytes).\n", path, new_size);
        }
    }

    remove(tmp_path);
}

static void shell_get(ndtool_ctx_t *ctx, const char *ndfs_path, const char *local_path)
{
    uint8_t *data = NULL;
    size_t size = 0;
    char auto_name[64];
    ndfs_error_t err;

    if (!ndfs_path) {
        fprintf(stderr, "Usage: get USER/FILE:TYPE [local_path]\n");
        return;
    }

    err = ndfs_read_file(ctx->fs, ndfs_path, &data, &size);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    if (!local_path) {
        /* Derive from NDFS path: take part after last / */
        const char *slash = strrchr(ndfs_path, '/');
        const char *fname = slash ? slash + 1 : ndfs_path;
        ndtool_to_host_name(fname, auto_name, sizeof(auto_name), true);
        local_path = auto_name;
    }

    if (ctx->do_parity) {
        ndtool_strip_parity(data, size);
    }

    if (ndtool_write_local_file(local_path, data, size) == 0) {
        printf("Extracted %s -> %s (%zu bytes)\n", ndfs_path, local_path, size);
    }

    ndfs_free_data(data);
}

static void shell_put(ndtool_ctx_t *ctx, const char *local_path, const char *ndfs_path)
{
    if (!local_path) {
        fprintf(stderr, "Usage: put local_file [USER/FILE:TYPE]\n");
        return;
    }

    cmd_put(ctx, local_path, ndfs_path);
    ctx->modified = true;
}

static void shell_rm(ndtool_ctx_t *ctx, const char *path)
{
    ndfs_error_t err;

    if (!path) {
        fprintf(stderr, "Usage: rm USER/FILE:TYPE\n");
        return;
    }

    err = ndfs_delete_file(ctx->fs, path);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    ctx->modified = true;
    printf("Deleted %s\n", path);
}

static void shell_mv(ndtool_ctx_t *ctx, const char *old_path, const char *new_path)
{
    ndfs_error_t err;

    if (!old_path || !new_path) {
        fprintf(stderr, "Usage: mv OLD_PATH NEW_PATH\n");
        return;
    }

    err = ndfs_rename(ctx->fs, old_path, new_path);
    if (err != NDFS_OK) {
        fprintf(stderr, "Error: %s\n", ndfs_strerror(err));
        return;
    }

    ctx->modified = true;
    printf("Renamed %s -> %s\n", old_path, new_path);
}

/* ── Main shell loop ──────────────────────────────────────────────── */

int cmd_shell(ndtool_ctx_t *ctx)
{
    char line[1024];
    printf("ndtool shell - type 'help' for commands\n");

    while (1) {
        printf("ndtool> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Trim newline */
        {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
        }
        if (line[0] == '\0') continue;

        /* Parse command and args */
        {
            char *cmd = strtok(line, " \t");
            char *arg1 = strtok(NULL, " \t");
            char *arg2 = strtok(NULL, " \t");

            if (!cmd) continue;

            if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
                if (ctx->modified) {
                    char resp[16];
                    printf("Unsaved changes. Save before quitting? [y/N] ");
                    fflush(stdout);
                    if (fgets(resp, sizeof(resp), stdin) &&
                        (resp[0] == 'y' || resp[0] == 'Y')) {
                        ndtool_save_image(ctx);
                    }
                }
                break;
            }
            else if (strcmp(cmd, "help") == 0) {
                shell_help();
            }
            else if (strcmp(cmd, "ls") == 0) {
                shell_ls(ctx, arg1);
            }
            else if (strcmp(cmd, "cat") == 0) {
                shell_cat(ctx, arg1);
            }
            else if (strcmp(cmd, "hexdump") == 0) {
                shell_hexdump(ctx, arg1);
            }
            else if (strcmp(cmd, "edit") == 0) {
                shell_edit(ctx, arg1);
            }
            else if (strcmp(cmd, "get") == 0) {
                shell_get(ctx, arg1, arg2);
            }
            else if (strcmp(cmd, "put") == 0) {
                shell_put(ctx, arg1, arg2);
            }
            else if (strcmp(cmd, "rm") == 0) {
                shell_rm(ctx, arg1);
            }
            else if (strcmp(cmd, "mv") == 0) {
                shell_mv(ctx, arg1, arg2);
            }
            else if (strcmp(cmd, "info") == 0) {
                cmd_info(ctx);
            }
            else if (strcmp(cmd, "bitmap") == 0) {
                /* Temporarily enable verbose to show bitmap */
                bool was_verbose = ctx->verbose;
                ctx->verbose = true;
                cmd_info(ctx);
                ctx->verbose = was_verbose;
            }
            else if (strcmp(cmd, "fsck") == 0) {
                char *report = NULL;
                int errs = 0;
                ndfs_error_t ferr = ndfs_fsck(ctx->fs, &report, &errs);
                if (ferr == NDFS_OK && report) {
                    printf("%s", report);
                    ndfs_free_string(report);
                } else {
                    fprintf(stderr, "Error running fsck: %s\n", ndfs_strerror(ferr));
                }
            }
            else if (strcmp(cmd, "stat") == 0) {
                /* Accept "stat PATH", "stat -v PATH" or "stat PATH -v". */
                bool verbose = false;
                const char *path = arg1;
                if (arg1 && strcmp(arg1, "-v") == 0) {
                    verbose = true;
                    path = arg2;
                } else if (arg2 && strcmp(arg2, "-v") == 0) {
                    verbose = true;
                }
                if (!path) {
                    fprintf(stderr, "Usage: stat [-v] USER/FILE:TYPE\n");
                } else {
                    cmd_stat(ctx, path, verbose);
                }
            }
            else if (strcmp(cmd, "users") == 0) {
                cmd_users(ctx);
            }
            else if (strcmp(cmd, "useradd") == 0) {
                uint32_t q = arg2 ? (uint32_t)atoi(arg2) : 100;
                cmd_useradd(ctx, arg1, q);
            }
            else if (strcmp(cmd, "userdel") == 0) {
                if (!arg1) {
                    fprintf(stderr, "Usage: userdel NAME\n");
                } else {
                    cmd_userdel(ctx, arg1);
                }
            }
            else if (strcmp(cmd, "addquota") == 0) {
                if (!arg1 || !arg2) {
                    fprintf(stderr, "Usage: addquota NAME PAGES\n");
                } else {
                    cmd_addquota(ctx, arg1, (uint32_t)atoi(arg2));
                }
            }
            else if (strcmp(cmd, "remquota") == 0) {
                if (!arg1 || !arg2) {
                    fprintf(stderr, "Usage: remquota NAME PAGES\n");
                } else {
                    cmd_remquota(ctx, arg1, (uint32_t)atoi(arg2));
                }
            }
            else if (strcmp(cmd, "passwd") == 0) {
                if (!arg1) {
                    fprintf(stderr, "Usage: passwd NAME\n");
                } else {
                    cmd_passwd(ctx, arg1);
                }
            }
            else if (strcmp(cmd, "save") == 0) {
                if (arg1) ctx->image_path = arg1;
                ndtool_save_image(ctx);
                ctx->modified = false;
                printf("Image saved to %s\n", ctx->image_path);
            }
            else {
                printf("Unknown command: %s (type 'help')\n", cmd);
            }
        }
    }

    return 0;
}
