/**
 * ndtool: CLI tool for working with NDFS disk images.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NDTOOL_H
#define NDTOOL_H

#include <ndfs/ndfs.h>
#include <stdbool.h>

#define NDTOOL_VERSION "1.0.0"

typedef struct {
    const char *image_path;
    ndfs_filesystem_t *fs;
    bool verbose;
    bool lowercase;
    bool do_parity;     /* strip on extract, set on put */
    bool dry_run;
    bool force;
    bool with_dirs;     /* -d flag */
    const char *output_dir;
    const char *filter_user;  /* -u USER filter */
    const char *filter_file;  /* -f FILE filter */
    bool modified;
    bool use_xat;       /* --xat: write/read .xat sidecar files */
    const char *editor;     /* custom editor command, NULL = auto-detect */
} ndtool_ctx_t;

/* Helper: convert NDFS name to host filename (: -> ., optional lowercase) */
void ndtool_to_host_name(const char *ndfs_name, char *out, size_t out_len, bool lowercase);

/* Helper: convert host filename to NDFS name (. -> :, uppercase) */
void ndtool_to_ndfs_name(const char *host_name, char *out, size_t out_len);

/* Helper: read entire local file into malloc'd buffer */
uint8_t *ndtool_read_local_file(const char *path, size_t *out_size);

/* Helper: write buffer to local file */
int ndtool_write_local_file(const char *path, const uint8_t *data, size_t size);

/* Helper: save modified image back to disk */
int ndtool_save_image(ndtool_ctx_t *ctx);

/* Commands */
int cmd_list(ndtool_ctx_t *ctx);
int cmd_users(ndtool_ctx_t *ctx);
int cmd_info(ndtool_ctx_t *ctx);
int cmd_extract(ndtool_ctx_t *ctx);
int cmd_put(ndtool_ctx_t *ctx, const char *local_path, const char *ndfs_path);
int cmd_delete(ndtool_ctx_t *ctx, const char *ndfs_path);
int cmd_useradd(ndtool_ctx_t *ctx, const char *name, uint32_t quota);
int cmd_userdel(ndtool_ctx_t *ctx, const char *name);
int cmd_addquota(ndtool_ctx_t *ctx, const char *name, uint32_t pages);
int cmd_remquota(ndtool_ctx_t *ctx, const char *name, uint32_t pages);
int cmd_passwd(ndtool_ctx_t *ctx, const char *name);
int cmd_create(ndtool_ctx_t *ctx, const char *template_name, uint32_t custom_pages, const char *dir_name);
int cmd_shell(ndtool_ctx_t *ctx);

#endif /* NDTOOL_H */
