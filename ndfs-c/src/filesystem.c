/**
 * NDFS filesystem implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/filesystem.h>
#include <ndfs/parity.h>
#include "endian_util.h"
#include "ndfs_name.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ── Internal filesystem struct ──────────────────────────────────── */

#define MAX_INTERNAL_USERS   NDFS_MAX_USERS
#define MAX_INTERNAL_OBJECTS 16384

struct ndfs_filesystem {
    uint8_t            *data;
    size_t              size;
    bool                read_only;
    bool                owns_buffer;

    ndfs_master_block_t master_block;
    ndfs_bit_file_t     bit_file;

    /* User entries */
    ndfs_user_entry_t   users[MAX_INTERNAL_USERS];
    bool                user_valid[MAX_INTERNAL_USERS];
    size_t              user_count;

    /* Object entries */
    ndfs_object_entry_t *objects;
    size_t              object_count;
    size_t              object_capacity;
};

/* ── Forward declarations ────────────────────────────────────────── */

static const uint8_t *read_page(const struct ndfs_filesystem *fs, uint32_t block_id);
static uint8_t *write_page_ptr(struct ndfs_filesystem *fs, uint32_t block_id);
static ndfs_error_t load_structures(struct ndfs_filesystem *fs);
static ndfs_error_t persist_all(struct ndfs_filesystem *fs);
static void parse_path(const char *path,
                       char *user_name, size_t user_sz,
                       char *obj_name, size_t obj_sz,
                       char *file_type, size_t type_sz);
static int find_user_by_name(const struct ndfs_filesystem *fs, const char *name);
static int find_user_by_index(const struct ndfs_filesystem *fs, uint8_t idx);
static int find_object(const struct ndfs_filesystem *fs, const char *path);
static void str_toupper(char *s);
static ndfs_error_t read_object_data(const struct ndfs_filesystem *fs,
                                     const ndfs_object_entry_t *obj,
                                     uint8_t **out_data, size_t *out_size);
static void free_file_blocks(struct ndfs_filesystem *fs,
                             const ndfs_object_entry_t *obj);
static ndfs_error_t create_new_file(struct ndfs_filesystem *fs,
                                    const char *obj_name, const char *file_type,
                                    int user_idx,
                                    const uint8_t *file_data, size_t file_size);
static ndfs_error_t update_existing_file(struct ndfs_filesystem *fs,
                                         int obj_idx, int user_idx,
                                         const uint8_t *file_data, size_t file_size);
static void add_object(struct ndfs_filesystem *fs, const ndfs_object_entry_t *obj);
static int next_object_index(const struct ndfs_filesystem *fs);
static size_t objects_for_user_by_index(const struct ndfs_filesystem *fs, uint8_t idx);

/* ── Helpers ─────────────────────────────────────────────────────── */

static const uint8_t *read_page(const struct ndfs_filesystem *fs, uint32_t block_id)
{
    size_t offset = (size_t)block_id * NDFS_PAGE_SIZE;
    if (offset + NDFS_PAGE_SIZE > fs->size) return NULL;
    return fs->data + offset;
}

static uint8_t *write_page_ptr(struct ndfs_filesystem *fs, uint32_t block_id)
{
    size_t offset = (size_t)block_id * NDFS_PAGE_SIZE;
    if (offset + NDFS_PAGE_SIZE > fs->size) return NULL;
    return fs->data + offset;
}

static void str_toupper(char *s)
{
    while (*s) {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
}

static int strcasecmp_port(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static void parse_path(const char *path,
                       char *user_name, size_t user_sz,
                       char *obj_name, size_t obj_sz,
                       char *file_type, size_t type_sz)
{
    const char *p = path;
    const char *slash;
    const char *colon;
    const char *dot;
    const char *file_part;

    /* Skip leading slashes */
    while (*p == '/') p++;

    user_name[0] = '\0';
    obj_name[0]  = '\0';
    file_type[0] = '\0';

    /* Find slash separator */
    slash = strchr(p, '/');
    if (slash) {
        size_t ulen = (size_t)(slash - p);
        if (ulen >= user_sz) ulen = user_sz - 1;
        memcpy(user_name, p, ulen);
        user_name[ulen] = '\0';
        file_part = slash + 1;
        /* Skip trailing slashes */
        while (*file_part == '/') file_part++;
    } else {
        file_part = p;
    }

    /* Split filename into name and type */
    colon = strchr(file_part, ':');
    if (colon) {
        size_t nlen = (size_t)(colon - file_part);
        size_t tlen = strlen(colon + 1);
        if (nlen >= obj_sz) nlen = obj_sz - 1;
        memcpy(obj_name, file_part, nlen);
        obj_name[nlen] = '\0';
        if (tlen >= type_sz) tlen = type_sz - 1;
        memcpy(file_type, colon + 1, tlen);
        file_type[tlen] = '\0';
    } else {
        dot = strrchr(file_part, '.');
        if (dot) {
            size_t nlen = (size_t)(dot - file_part);
            size_t tlen = strlen(dot + 1);
            if (nlen >= obj_sz) nlen = obj_sz - 1;
            memcpy(obj_name, file_part, nlen);
            obj_name[nlen] = '\0';
            if (tlen >= type_sz) tlen = type_sz - 1;
            memcpy(file_type, dot + 1, tlen);
            file_type[tlen] = '\0';
        } else {
            size_t nlen = strlen(file_part);
            if (nlen >= obj_sz) nlen = obj_sz - 1;
            memcpy(obj_name, file_part, nlen);
            obj_name[nlen] = '\0';
        }
    }
}

static int find_user_by_name(const struct ndfs_filesystem *fs, const char *name)
{
    size_t i;
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (fs->user_valid[i] &&
            strcasecmp_port(fs->users[i].user_name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_user_by_index(const struct ndfs_filesystem *fs, uint8_t idx)
{
    size_t i;
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (fs->user_valid[i] && fs->users[i].user_index == idx) {
            return (int)i;
        }
    }
    return -1;
}

static int find_object(const struct ndfs_filesystem *fs, const char *path)
{
    char user_name[NDFS_NAME_MAX + 1];
    char obj_name[NDFS_NAME_MAX + 1];
    char file_type[NDFS_TYPE_MAX + 1];
    char full_name[NDFS_NAME_MAX + NDFS_TYPE_MAX + 2];
    char entry_full[NDFS_NAME_MAX + NDFS_TYPE_MAX + 2];
    size_t i;

    parse_path(path, user_name, sizeof(user_name),
               obj_name, sizeof(obj_name),
               file_type, sizeof(file_type));

    str_toupper(user_name);
    str_toupper(obj_name);
    str_toupper(file_type);

    if (file_type[0] != '\0') {
        snprintf(full_name, sizeof(full_name), "%s:%s", obj_name, file_type);
    } else {
        snprintf(full_name, sizeof(full_name), "%s", obj_name);
    }

    for (i = 0; i < fs->object_count; i++) {
        const ndfs_object_entry_t *o = &fs->objects[i];

        /* Filter by user if specified */
        if (user_name[0] != '\0' &&
            strcasecmp_port(o->user_name, user_name) != 0) {
            continue;
        }

        /* Build entry full name */
        if (o->type[0] != '\0') {
            snprintf(entry_full, sizeof(entry_full), "%s:%s",
                     o->object_name, o->type);
        } else {
            snprintf(entry_full, sizeof(entry_full), "%s", o->object_name);
        }

        if (strcasecmp_port(entry_full, full_name) == 0) return (int)i;

        /* Also try matching object name only (without type) */
        if (file_type[0] == '\0' &&
            strcasecmp_port(o->object_name, obj_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int next_object_index(const struct ndfs_filesystem *fs)
{
    uint32_t max_idx = 0;
    size_t i;
    uint32_t candidate;
    bool found;

    for (i = 0; i < fs->object_count; i++) {
        if (fs->objects[i].object_index >= max_idx)
            max_idx = fs->objects[i].object_index + 1;
    }

    /* Find first gap */
    for (candidate = 0; candidate < max_idx + 1; candidate++) {
        found = false;
        for (i = 0; i < fs->object_count; i++) {
            if (fs->objects[i].object_index == candidate) {
                found = true;
                break;
            }
        }
        if (!found) return (int)candidate;
    }
    return (int)max_idx;
}

static size_t objects_for_user_by_index(const struct ndfs_filesystem *fs, uint8_t idx)
{
    size_t count = 0;
    size_t i;
    for (i = 0; i < fs->object_count; i++) {
        if (fs->objects[i].user_index == idx) count++;
    }
    return count;
}

static void add_object(struct ndfs_filesystem *fs, const ndfs_object_entry_t *obj)
{
    if (fs->object_count >= fs->object_capacity) {
        size_t new_cap = fs->object_capacity == 0 ? 64 : fs->object_capacity * 2;
        ndfs_object_entry_t *new_arr = (ndfs_object_entry_t *)realloc(
            fs->objects, new_cap * sizeof(ndfs_object_entry_t));
        if (!new_arr) return;
        fs->objects = new_arr;
        fs->object_capacity = new_cap;
    }
    fs->objects[fs->object_count] = *obj;
    fs->object_count++;
}

/* ── Structure loading ───────────────────────────────────────────── */

static ndfs_error_t load_structures(struct ndfs_filesystem *fs)
{
    const ndfs_master_block_t *mb = &fs->master_block;
    size_t i, j;
    int ui;

    /* Load user file */
    if (ndfs_bp_is_valid(&mb->user_file_ptr)) {
        const uint8_t *index_page = read_page(fs, mb->user_file_ptr.block_id);
        if (!index_page) return NDFS_ERR_CORRUPT;

        /* Read up to 8 pointers from the index block */
        for (i = 0; i < NDFS_MAX_USER_FILE_PTRS; i++) {
            ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(index_page, i * 4);
            const uint8_t *data_page;
            if (!ndfs_bp_is_valid(&ptr)) continue;

            data_page = read_page(fs, ptr.block_id);
            if (!data_page) continue;

            for (j = 0; j < NDFS_ENTRIES_PER_PAGE; j++) {
                ndfs_user_entry_t ue;
                ndfs_error_t err = ndfs_ue_from_bytes(data_page, NDFS_PAGE_SIZE,
                                                       j * NDFS_ENTRY_SIZE, &ue);
                if (err == NDFS_OK) {
                    size_t slot = ue.user_index;
                    if (slot < MAX_INTERNAL_USERS) {
                        fs->users[slot] = ue;
                        fs->user_valid[slot] = true;
                        fs->user_count++;
                    }
                }
            }
        }
    }

    /* Load object file */
    if (ndfs_bp_is_valid(&mb->object_file_ptr)) {
        if (mb->object_file_ptr.type == NDFS_PTR_INDEXED) {
            const uint8_t *idx_page = read_page(fs, mb->object_file_ptr.block_id);
            uint32_t global_idx = 0;
            if (!idx_page) return NDFS_ERR_CORRUPT;

            for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS; i++) {
                ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(idx_page, i * 4);
                const uint8_t *dp;
                if (!ndfs_bp_is_valid(&ptr)) {
                    global_idx += NDFS_ENTRIES_PER_PAGE;
                    continue;
                }
                dp = read_page(fs, ptr.block_id);
                if (!dp) { global_idx += NDFS_ENTRIES_PER_PAGE; continue; }

                for (j = 0; j < NDFS_ENTRIES_PER_PAGE; j++) {
                    ndfs_object_entry_t oe;
                    ndfs_error_t err = ndfs_oe_from_bytes(dp, NDFS_PAGE_SIZE,
                                                          j * NDFS_ENTRY_SIZE, &oe);
                    if (err == NDFS_OK) {
                        oe.object_index = global_idx + (uint32_t)j;
                        /* Resolve user name */
                        ui = find_user_by_index(fs, oe.user_index);
                        if (ui >= 0) {
                            memcpy(oe.user_name, fs->users[ui].user_name,
                                   NDFS_NAME_MAX + 1);
                        }
                        add_object(fs, &oe);
                    }
                }
                global_idx += NDFS_ENTRIES_PER_PAGE;
            }
        } else if (mb->object_file_ptr.type == NDFS_PTR_SUBINDEXED) {
            const uint8_t *sub_idx_page = read_page(fs, mb->object_file_ptr.block_id);
            uint32_t global_idx = 0;
            if (!sub_idx_page) return NDFS_ERR_CORRUPT;

            for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS; i++) {
                ndfs_block_pointer_t idx_ptr = ndfs_bp_from_bytes(sub_idx_page, i * 4);
                const uint8_t *idx_page;
                size_t k;
                if (!ndfs_bp_is_valid(&idx_ptr)) continue;

                idx_page = read_page(fs, idx_ptr.block_id);
                if (!idx_page) continue;

                for (k = 0; k < NDFS_MAX_OBJECT_FILE_PTRS; k++) {
                    ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(idx_page, k * 4);
                    const uint8_t *dp;
                    if (!ndfs_bp_is_valid(&ptr)) {
                        global_idx += NDFS_ENTRIES_PER_PAGE;
                        continue;
                    }
                    dp = read_page(fs, ptr.block_id);
                    if (!dp) { global_idx += NDFS_ENTRIES_PER_PAGE; continue; }

                    for (j = 0; j < NDFS_ENTRIES_PER_PAGE; j++) {
                        ndfs_object_entry_t oe;
                        ndfs_error_t err = ndfs_oe_from_bytes(dp, NDFS_PAGE_SIZE,
                                                              j * NDFS_ENTRY_SIZE, &oe);
                        if (err == NDFS_OK) {
                            oe.object_index = global_idx + (uint32_t)j;
                            ui = find_user_by_index(fs, oe.user_index);
                            if (ui >= 0) {
                                memcpy(oe.user_name, fs->users[ui].user_name,
                                       NDFS_NAME_MAX + 1);
                            }
                            add_object(fs, &oe);
                        }
                    }
                    global_idx += NDFS_ENTRIES_PER_PAGE;
                }
            }
        }
    }

    /* Load bit file */
    if (ndfs_bp_is_valid(&mb->bit_file_ptr)) {
        uint32_t total_pages = (uint32_t)(fs->size / NDFS_PAGE_SIZE);
        uint32_t bitmap_bytes = (total_pages + 7) / 8;
        uint32_t bitmap_pages = (bitmap_bytes + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE;
        ndfs_error_t err;

        err = ndfs_bf_init(&fs->bit_file, total_pages);
        if (err != NDFS_OK) return err;

        /* Read contiguous bitmap pages */
        {
            uint8_t *tmp = (uint8_t *)malloc((size_t)bitmap_pages * NDFS_PAGE_SIZE);
            if (!tmp) return NDFS_ERR_ALLOC;

            for (i = 0; i < bitmap_pages; i++) {
                const uint8_t *page = read_page(fs, mb->bit_file_ptr.block_id + (uint32_t)i);
                if (page) {
                    memcpy(tmp + i * NDFS_PAGE_SIZE, page, NDFS_PAGE_SIZE);
                }
            }
            ndfs_bf_load(&fs->bit_file, tmp, bitmap_bytes);
            free(tmp);
        }
    }

    return NDFS_OK;
}

/* ── File data reading ───────────────────────────────────────────── */

static ndfs_error_t read_indexed_data(const struct ndfs_filesystem *fs,
                                      uint32_t index_block_id,
                                      uint32_t bytes_in_file,
                                      uint8_t *result)
{
    const uint8_t *index_page = read_page(fs, index_block_id);
    size_t bytes_read = 0;
    size_t i;

    if (!index_page) return NDFS_ERR_CORRUPT;

    for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS && bytes_read < bytes_in_file; i++) {
        ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(index_page, i * 4);
        size_t to_copy = NDFS_PAGE_SIZE;
        if (to_copy > bytes_in_file - bytes_read) to_copy = bytes_in_file - bytes_read;

        if (ptr.block_id == 0) {
            /* Sparse hole: result is already zeroed */
            bytes_read += to_copy;
        } else {
            const uint8_t *page = read_page(fs, ptr.block_id);
            if (!page) return NDFS_ERR_CORRUPT;
            memcpy(result + bytes_read, page, to_copy);
            bytes_read += to_copy;
        }
    }
    return NDFS_OK;
}

static ndfs_error_t read_subindexed_data(const struct ndfs_filesystem *fs,
                                         uint32_t sub_index_block_id,
                                         uint32_t bytes_in_file,
                                         uint8_t *result)
{
    const uint8_t *sub_page = read_page(fs, sub_index_block_id);
    size_t bytes_read = 0;
    size_t si, i;

    if (!sub_page) return NDFS_ERR_CORRUPT;

    for (si = 0; si < NDFS_MAX_OBJECT_FILE_PTRS && bytes_read < bytes_in_file; si++) {
        ndfs_block_pointer_t idx_ptr = ndfs_bp_from_bytes(sub_page, si * 4);
        const uint8_t *idx_page;
        if (!ndfs_bp_is_valid(&idx_ptr)) continue;

        idx_page = read_page(fs, idx_ptr.block_id);
        if (!idx_page) continue;

        for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS && bytes_read < bytes_in_file; i++) {
            ndfs_block_pointer_t data_ptr = ndfs_bp_from_bytes(idx_page, i * 4);
            size_t to_copy = NDFS_PAGE_SIZE;
            if (to_copy > bytes_in_file - bytes_read) to_copy = bytes_in_file - bytes_read;

            if (data_ptr.block_id == 0) {
                bytes_read += to_copy;
            } else {
                const uint8_t *page = read_page(fs, data_ptr.block_id);
                if (!page) return NDFS_ERR_CORRUPT;
                memcpy(result + bytes_read, page, to_copy);
                bytes_read += to_copy;
            }
        }
    }
    return NDFS_OK;
}

static ndfs_error_t read_object_data(const struct ndfs_filesystem *fs,
                                     const ndfs_object_entry_t *obj,
                                     uint8_t **out_data, size_t *out_size)
{
    uint8_t *result;

    if (!obj->file_pointer.block_id) {
        *out_data = NULL;
        *out_size = 0;
        return NDFS_OK;
    }

    result = (uint8_t *)calloc(obj->bytes_in_file, 1);
    if (!result) return NDFS_ERR_ALLOC;

    if (obj->file_pointer.type == NDFS_PTR_CONTIGUOUS) {
        size_t bytes_read = 0;
        uint32_t i;
        for (i = 0; i < obj->pages_in_file && bytes_read < obj->bytes_in_file; i++) {
            const uint8_t *page = read_page(fs, obj->file_pointer.block_id + i);
            size_t to_copy = NDFS_PAGE_SIZE;
            if (to_copy > obj->bytes_in_file - bytes_read) to_copy = obj->bytes_in_file - bytes_read;
            if (!page) { free(result); return NDFS_ERR_CORRUPT; }
            memcpy(result + bytes_read, page, to_copy);
            bytes_read += to_copy;
        }
    } else if (obj->file_pointer.type == NDFS_PTR_INDEXED) {
        ndfs_error_t err = read_indexed_data(fs, obj->file_pointer.block_id,
                                             obj->bytes_in_file, result);
        if (err != NDFS_OK) { free(result); return err; }
    } else if (obj->file_pointer.type == NDFS_PTR_SUBINDEXED) {
        ndfs_error_t err = read_subindexed_data(fs, obj->file_pointer.block_id,
                                                obj->bytes_in_file, result);
        if (err != NDFS_OK) { free(result); return err; }
    }

    *out_data = result;
    *out_size = obj->bytes_in_file;
    return NDFS_OK;
}

/* ── Block freeing ───────────────────────────────────────────────── */

static void free_file_blocks(struct ndfs_filesystem *fs,
                             const ndfs_object_entry_t *obj)
{
    size_t i;

    if (!obj->file_pointer.block_id) return;

    if (obj->file_pointer.type == NDFS_PTR_INDEXED) {
        const uint8_t *index_page = read_page(fs, obj->file_pointer.block_id);
        if (index_page) {
            for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS; i++) {
                ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(index_page, i * 4);
                if (ptr.block_id > 0) {
                    ndfs_bf_mark_free(&fs->bit_file, ptr.block_id);
                }
            }
        }
        ndfs_bf_mark_free(&fs->bit_file, obj->file_pointer.block_id);
    } else if (obj->file_pointer.type == NDFS_PTR_CONTIGUOUS) {
        ndfs_bf_free_range(&fs->bit_file, obj->file_pointer.block_id,
                           obj->pages_in_file);
    } else if (obj->file_pointer.type == NDFS_PTR_SUBINDEXED) {
        const uint8_t *sub_page = read_page(fs, obj->file_pointer.block_id);
        size_t si;
        if (sub_page) {
            for (si = 0; si < NDFS_MAX_OBJECT_FILE_PTRS; si++) {
                ndfs_block_pointer_t idx_ptr = ndfs_bp_from_bytes(sub_page, si * 4);
                const uint8_t *idx_page;
                if (!ndfs_bp_is_valid(&idx_ptr)) continue;
                idx_page = read_page(fs, idx_ptr.block_id);
                if (idx_page) {
                    for (i = 0; i < NDFS_MAX_OBJECT_FILE_PTRS; i++) {
                        ndfs_block_pointer_t dp = ndfs_bp_from_bytes(idx_page, i * 4);
                        if (dp.block_id > 0) ndfs_bf_mark_free(&fs->bit_file, dp.block_id);
                    }
                }
                ndfs_bf_mark_free(&fs->bit_file, idx_ptr.block_id);
            }
        }
        ndfs_bf_mark_free(&fs->bit_file, obj->file_pointer.block_id);
    }
}

/* ── File creation/update ────────────────────────────────────────── */

static ndfs_error_t create_new_file(struct ndfs_filesystem *fs,
                                    const char *obj_name, const char *file_type,
                                    int user_slot,
                                    const uint8_t *file_data, size_t file_size)
{
    uint32_t data_pages = (uint32_t)((file_size + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE);
    uint32_t index_block_id;
    uint8_t *index_page_buf;
    uint32_t i;
    ndfs_object_entry_t entry;
    ndfs_error_t err;

    if (data_pages == 0) data_pages = 1;

    /* Allocate index block */
    err = ndfs_bf_find_free(&fs->bit_file, &index_block_id);
    if (err != NDFS_OK) return NDFS_ERR_NO_SPACE;
    ndfs_bf_mark_used(&fs->bit_file, index_block_id);

    /* Write index + data blocks */
    index_page_buf = write_page_ptr(fs, index_block_id);
    if (!index_page_buf) return NDFS_ERR_CORRUPT;
    memset(index_page_buf, 0, NDFS_PAGE_SIZE);

    for (i = 0; i < data_pages; i++) {
        size_t page_offset = (size_t)i * NDFS_PAGE_SIZE;
        size_t page_end = page_offset + NDFS_PAGE_SIZE;
        size_t slice_len;
        bool all_zeros = true;
        size_t b;
        uint32_t data_block_id;
        uint8_t *data_page_buf;
        ndfs_block_pointer_t data_ptr;

        if (page_end > file_size) page_end = file_size;
        slice_len = page_end > page_offset ? page_end - page_offset : 0;

        /* Check for sparse (all zeros) */
        for (b = 0; b < slice_len; b++) {
            if (file_data[page_offset + b] != 0) {
                all_zeros = false;
                break;
            }
        }

        if (all_zeros && slice_len == NDFS_PAGE_SIZE) {
            /* Sparse hole */
            ndfs_write_u32be(index_page_buf, i * 4, 0);
        } else {
            err = ndfs_bf_find_free(&fs->bit_file, &data_block_id);
            if (err != NDFS_OK) return NDFS_ERR_NO_SPACE;
            ndfs_bf_mark_used(&fs->bit_file, data_block_id);

            data_page_buf = write_page_ptr(fs, data_block_id);
            if (!data_page_buf) return NDFS_ERR_CORRUPT;
            memset(data_page_buf, 0, NDFS_PAGE_SIZE);
            if (slice_len > 0) memcpy(data_page_buf, file_data + page_offset, slice_len);

            data_ptr.block_id = data_block_id;
            data_ptr.type = NDFS_PTR_CONTIGUOUS;
            ndfs_bp_to_bytes(&data_ptr, index_page_buf, i * 4);
        }
    }

    /* Create object entry */
    ndfs_oe_init(&entry);
    entry.object_index = (uint32_t)next_object_index(fs);
    {
        size_t nlen = strlen(obj_name);
        if (nlen > NDFS_NAME_MAX) nlen = NDFS_NAME_MAX;
        memcpy(entry.object_name, obj_name, nlen);
        entry.object_name[nlen] = '\0';
        str_toupper(entry.object_name);
    }
    {
        size_t tlen = strlen(file_type);
        if (tlen > NDFS_TYPE_MAX) tlen = NDFS_TYPE_MAX;
        memcpy(entry.type, file_type, tlen);
        entry.type[tlen] = '\0';
        str_toupper(entry.type);
    }
    entry.user_index = fs->users[user_slot].user_index;
    memcpy(entry.user_name, fs->users[user_slot].user_name, NDFS_NAME_MAX + 1);
    entry.pages_in_file = data_pages;
    entry.bytes_in_file = file_size > 0 ? (uint32_t)file_size : 1;
    entry.file_pointer.block_id = index_block_id;
    entry.file_pointer.type = NDFS_PTR_INDEXED;
    /* Sensible defaults for a freshly-created file: owner (and friends) get
     * full rights, and the allocation type flag reflects the indexed layout
     * used here. */
    entry.access_bits = NDFS_ACCESS_DEFAULT;
    entry.file_type_flags = NDFS_FT_INDEXED;

    add_object(fs, &entry);

    /* Update user pages used */
    fs->users[user_slot].pages_used += data_pages + 1;

    return NDFS_OK;
}

static ndfs_error_t update_existing_file(struct ndfs_filesystem *fs,
                                         int obj_idx, int user_slot,
                                         const uint8_t *file_data, size_t file_size)
{
    ndfs_object_entry_t *existing = &fs->objects[obj_idx];
    uint32_t old_total = existing->pages_in_file + 1;
    uint32_t data_pages, index_block_id, i;
    uint8_t *index_page_buf;
    ndfs_error_t err;

    /* Free old blocks */
    free_file_blocks(fs, existing);
    fs->users[user_slot].pages_used =
        fs->users[user_slot].pages_used >= old_total
            ? fs->users[user_slot].pages_used - old_total : 0;

    /* Allocate new */
    data_pages = (uint32_t)((file_size + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE);
    if (data_pages == 0) data_pages = 1;

    err = ndfs_bf_find_free(&fs->bit_file, &index_block_id);
    if (err != NDFS_OK) return NDFS_ERR_NO_SPACE;
    ndfs_bf_mark_used(&fs->bit_file, index_block_id);

    index_page_buf = write_page_ptr(fs, index_block_id);
    if (!index_page_buf) return NDFS_ERR_CORRUPT;
    memset(index_page_buf, 0, NDFS_PAGE_SIZE);

    for (i = 0; i < data_pages; i++) {
        size_t page_offset = (size_t)i * NDFS_PAGE_SIZE;
        size_t page_end = page_offset + NDFS_PAGE_SIZE;
        size_t slice_len;
        bool all_zeros = true;
        size_t b;

        if (page_end > file_size) page_end = file_size;
        slice_len = page_end > page_offset ? page_end - page_offset : 0;

        for (b = 0; b < slice_len; b++) {
            if (file_data[page_offset + b] != 0) {
                all_zeros = false;
                break;
            }
        }

        if (all_zeros && slice_len == NDFS_PAGE_SIZE) {
            ndfs_write_u32be(index_page_buf, i * 4, 0);
        } else {
            uint32_t data_block_id;
            uint8_t *data_page_buf;
            ndfs_block_pointer_t data_ptr;

            err = ndfs_bf_find_free(&fs->bit_file, &data_block_id);
            if (err != NDFS_OK) return NDFS_ERR_NO_SPACE;
            ndfs_bf_mark_used(&fs->bit_file, data_block_id);

            data_page_buf = write_page_ptr(fs, data_block_id);
            if (!data_page_buf) return NDFS_ERR_CORRUPT;
            memset(data_page_buf, 0, NDFS_PAGE_SIZE);
            if (slice_len > 0) memcpy(data_page_buf, file_data + page_offset, slice_len);

            data_ptr.block_id = data_block_id;
            data_ptr.type = NDFS_PTR_CONTIGUOUS;
            ndfs_bp_to_bytes(&data_ptr, index_page_buf, i * 4);
        }
    }

    /* Update existing entry */
    existing->pages_in_file = data_pages;
    existing->bytes_in_file = file_size > 0 ? (uint32_t)file_size : 1;
    existing->file_pointer.block_id = index_block_id;
    existing->file_pointer.type = NDFS_PTR_INDEXED;
    /* Rewritten as an indexed file; keep the allocation flag consistent.
     * Access bits, dates and versioning are intentionally preserved. */
    existing->file_type_flags = NDFS_FT_INDEXED;

    fs->users[user_slot].pages_used += data_pages + 1;

    return NDFS_OK;
}

/* ── Persist all structures to disk ──────────────────────────────── */

static ndfs_error_t persist_all(struct ndfs_filesystem *fs)
{
    const ndfs_master_block_t *mb = &fs->master_block;
    size_t i, j;

    /* BitFile: write contiguous pages */
    if (ndfs_bp_is_valid(&mb->bit_file_ptr)) {
        const uint8_t *bm_data;
        size_t bm_len;
        uint32_t bm_pages;

        ndfs_bf_get_data(&fs->bit_file, &bm_data, &bm_len);
        bm_pages = (uint32_t)((bm_len + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE);

        for (i = 0; i < bm_pages; i++) {
            uint8_t *page = write_page_ptr(fs, mb->bit_file_ptr.block_id + (uint32_t)i);
            size_t src_off = i * NDFS_PAGE_SIZE;
            size_t copy_len = NDFS_PAGE_SIZE;
            if (!page) continue;
            memset(page, 0, NDFS_PAGE_SIZE);
            if (src_off + copy_len > bm_len) copy_len = bm_len - src_off;
            if (copy_len > 0 && src_off < bm_len) memcpy(page, bm_data + src_off, copy_len);
        }
    }

    /* UserFile: write data pages via existing index pointers */
    if (ndfs_bp_is_valid(&mb->user_file_ptr)) {
        const uint8_t *existing_idx = read_page(fs, mb->user_file_ptr.block_id);
        if (existing_idx) {
            for (i = 0; i < NDFS_MAX_USER_FILE_PTRS; i++) {
                ndfs_block_pointer_t ptr = ndfs_bp_from_bytes(existing_idx, i * 4);
                uint8_t *page;
                if (!ndfs_bp_is_valid(&ptr)) continue;

                page = write_page_ptr(fs, ptr.block_id);
                if (!page) continue;
                memset(page, 0, NDFS_PAGE_SIZE);

                /* Write user entries for this page */
                for (j = 0; j < NDFS_ENTRIES_PER_PAGE; j++) {
                    uint32_t user_idx = (uint32_t)(i * NDFS_ENTRIES_PER_PAGE + j);
                    if (user_idx < MAX_INTERNAL_USERS && fs->user_valid[user_idx]) {
                        ndfs_ue_to_bytes(&fs->users[user_idx],
                                         page + j * NDFS_ENTRY_SIZE);
                    }
                }
            }
        }
    }

    /* ObjectFile: write data pages */
    if (ndfs_bp_is_valid(&mb->object_file_ptr)) {
        if (mb->object_file_ptr.type == NDFS_PTR_INDEXED) {
            const uint8_t *idx_page = read_page(fs, mb->object_file_ptr.block_id);
            if (idx_page) {
                /* Determine which data pages have entries */
                for (i = 0; i < fs->object_count; i++) {
                    uint32_t page_idx = fs->objects[i].object_index / NDFS_ENTRIES_PER_PAGE;
                    uint32_t slot = fs->objects[i].object_index % NDFS_ENTRIES_PER_PAGE;
                    ndfs_block_pointer_t ptr;
                    uint8_t *page;

                    if (page_idx >= NDFS_MAX_OBJECT_FILE_PTRS) continue;
                    ptr = ndfs_bp_from_bytes(idx_page, page_idx * 4);
                    if (!ndfs_bp_is_valid(&ptr)) continue;

                    page = write_page_ptr(fs, ptr.block_id);
                    if (!page) continue;

                    ndfs_oe_to_bytes(&fs->objects[i], page, NDFS_PAGE_SIZE,
                                     slot * NDFS_ENTRY_SIZE);
                }
            }
        } else if (mb->object_file_ptr.type == NDFS_PTR_SUBINDEXED) {
            const uint8_t *sub_page = read_page(fs, mb->object_file_ptr.block_id);
            if (sub_page) {
                for (i = 0; i < fs->object_count; i++) {
                    uint32_t page_idx = fs->objects[i].object_index / NDFS_ENTRIES_PER_PAGE;
                    uint32_t slot = fs->objects[i].object_index % NDFS_ENTRIES_PER_PAGE;
                    uint32_t sub_idx = page_idx / NDFS_MAX_OBJECT_FILE_PTRS;
                    uint32_t inner_idx = page_idx % NDFS_MAX_OBJECT_FILE_PTRS;
                    ndfs_block_pointer_t sub_ptr, data_ptr;
                    const uint8_t *inner_idx_page;
                    uint8_t *page;

                    sub_ptr = ndfs_bp_from_bytes(sub_page, sub_idx * 4);
                    if (!ndfs_bp_is_valid(&sub_ptr)) continue;
                    inner_idx_page = read_page(fs, sub_ptr.block_id);
                    if (!inner_idx_page) continue;
                    data_ptr = ndfs_bp_from_bytes(inner_idx_page, inner_idx * 4);
                    if (!ndfs_bp_is_valid(&data_ptr)) continue;

                    page = write_page_ptr(fs, data_ptr.block_id);
                    if (!page) continue;
                    ndfs_oe_to_bytes(&fs->objects[i], page, NDFS_PAGE_SIZE,
                                     slot * NDFS_ENTRY_SIZE);
                }
            }
        }
    }

    return NDFS_OK;
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */

static ndfs_error_t open_internal(uint8_t *data, size_t size,
                                  bool read_only, bool owns_buffer,
                                  ndfs_filesystem_t **out_fs)
{
    struct ndfs_filesystem *fs;
    const uint8_t *page0;
    ndfs_error_t err;

    if (!data || !out_fs) return NDFS_ERR_NULL_PTR;
    if (size < NDFS_PAGE_SIZE) return NDFS_ERR_TOO_SMALL;
    if (size % NDFS_PAGE_SIZE != 0) return NDFS_ERR_NOT_ALIGNED;

    fs = (struct ndfs_filesystem *)calloc(1, sizeof(*fs));
    if (!fs) return NDFS_ERR_ALLOC;

    fs->data        = data;
    fs->size        = size;
    fs->read_only   = read_only;
    fs->owns_buffer = owns_buffer;

    /* Parse master block */
    page0 = read_page(fs, 0);
    err = ndfs_mb_parse(page0, &fs->master_block);
    if (err != NDFS_OK) { free(fs); return err; }

    if (!ndfs_mb_is_valid(&fs->master_block)) {
        free(fs);
        return NDFS_ERR_INVALID_IMAGE;
    }
    fs->master_block.image_size = (uint32_t)(size / NDFS_PAGE_SIZE);

    /* Load structures */
    err = load_structures(fs);
    if (err != NDFS_OK) {
        ndfs_bf_destroy(&fs->bit_file);
        free(fs->objects);
        free(fs);
        return err;
    }

    *out_fs = fs;
    return NDFS_OK;
}

ndfs_error_t ndfs_open_buffer(const uint8_t *data, size_t size,
                              bool read_only,
                              ndfs_filesystem_t **out_fs)
{
    /* Cast away const: caller guarantees buffer stays alive.
       We mark read_only or trust the caller for writes. */
    return open_internal((uint8_t *)(uintptr_t)data, size,
                         read_only, false, out_fs);
}

ndfs_error_t ndfs_open_buffer_copy(const uint8_t *data, size_t size,
                                   bool read_only,
                                   ndfs_filesystem_t **out_fs)
{
    uint8_t *copy;

    if (!data) return NDFS_ERR_NULL_PTR;

    copy = (uint8_t *)malloc(size);
    if (!copy) return NDFS_ERR_ALLOC;
    memcpy(copy, data, size);

    {
        ndfs_error_t err = open_internal(copy, size, read_only, true, out_fs);
        if (err != NDFS_OK) {
            free(copy);
        }
        return err;
    }
}

void ndfs_close(ndfs_filesystem_t *fs)
{
    if (!fs) return;
    ndfs_bf_destroy(&fs->bit_file);
    free(fs->objects);
    if (fs->owns_buffer) free(fs->data);
    free(fs);
}

/* ── Read operations ─────────────────────────────────────────────── */

ndfs_error_t ndfs_get_master_block(const ndfs_filesystem_t *fs,
                                   const ndfs_master_block_t **out)
{
    if (!fs || !out) return NDFS_ERR_NULL_PTR;
    *out = &fs->master_block;
    return NDFS_OK;
}

ndfs_error_t ndfs_get_directory_name(const ndfs_filesystem_t *fs,
                                     char *buf, size_t buf_len)
{
    size_t len;

    if (!fs || !buf) return NDFS_ERR_NULL_PTR;
    if (buf_len < NDFS_NAME_MAX + 1) return NDFS_ERR_TOO_SMALL;

    len = strlen(fs->master_block.directory_name);
    memcpy(buf, fs->master_block.directory_name, len + 1);
    return NDFS_OK;
}

ndfs_error_t ndfs_list_directory(const ndfs_filesystem_t *fs,
                                 const char *path,
                                 ndfs_file_entry_t **out_entries,
                                 size_t *out_count)
{
    const char *normalized;
    char norm_buf[256];
    ndfs_file_entry_t *entries;
    size_t count = 0;
    size_t i;

    if (!fs || !out_entries || !out_count) return NDFS_ERR_NULL_PTR;

    /* Normalize: skip leading/trailing slashes */
    if (!path) path = "";
    normalized = path;
    while (*normalized == '/') normalized++;
    {
        size_t len = strlen(normalized);
        if (len >= sizeof(norm_buf)) len = sizeof(norm_buf) - 1;
        memcpy(norm_buf, normalized, len);
        norm_buf[len] = '\0';
        while (len > 0 && norm_buf[len - 1] == '/') { norm_buf[--len] = '\0'; }
    }

    if (norm_buf[0] == '\0') {
        /* Root: list users as directories */
        size_t valid_count = 0;
        for (i = 0; i < MAX_INTERNAL_USERS; i++) {
            if (fs->user_valid[i]) valid_count++;
        }
        if (valid_count == 0) {
            *out_entries = NULL;
            *out_count = 0;
            return NDFS_OK;
        }
        entries = (ndfs_file_entry_t *)calloc(valid_count, sizeof(ndfs_file_entry_t));
        if (!entries) return NDFS_ERR_ALLOC;

        for (i = 0; i < MAX_INTERNAL_USERS; i++) {
            if (!fs->user_valid[i]) continue;
            memcpy(entries[count].name, fs->users[i].user_name, NDFS_NAME_MAX + 1);
            entries[count].type[0] = '\0';
            memcpy(entries[count].full_name, fs->users[i].user_name, NDFS_NAME_MAX + 1);
            memcpy(entries[count].user_name, fs->users[i].user_name, NDFS_NAME_MAX + 1);
            entries[count].size = 0;
            entries[count].pages = 0;
            entries[count].is_directory = true;
            count++;
        }
        *out_entries = entries;
        *out_count = count;
    } else {
        /* User directory: list files */
        char user_upper[NDFS_NAME_MAX + 1];
        size_t file_count = 0;
        size_t nlen = strlen(norm_buf);
        if (nlen > NDFS_NAME_MAX) nlen = NDFS_NAME_MAX;
        memcpy(user_upper, norm_buf, nlen);
        user_upper[nlen] = '\0';
        str_toupper(user_upper);

        /* Count matches */
        for (i = 0; i < fs->object_count; i++) {
            if (strcasecmp_port(fs->objects[i].user_name, user_upper) == 0)
                file_count++;
        }

        if (file_count == 0) {
            *out_entries = NULL;
            *out_count = 0;
            return NDFS_OK;
        }

        entries = (ndfs_file_entry_t *)calloc(file_count, sizeof(ndfs_file_entry_t));
        if (!entries) return NDFS_ERR_ALLOC;

        for (i = 0; i < fs->object_count; i++) {
            const ndfs_object_entry_t *obj = &fs->objects[i];
            if (strcasecmp_port(obj->user_name, user_upper) != 0) continue;

            memcpy(entries[count].name, obj->object_name, NDFS_NAME_MAX + 1);
            memcpy(entries[count].type, obj->type, NDFS_TYPE_MAX + 1);
            ndfs_oe_full_name(obj, entries[count].full_name,
                              sizeof(entries[count].full_name));
            memcpy(entries[count].user_name, obj->user_name, NDFS_NAME_MAX + 1);
            entries[count].size = obj->bytes_in_file;
            entries[count].pages = obj->pages_in_file;
            entries[count].is_directory = false;
            count++;
        }
        *out_entries = entries;
        *out_count = count;
    }

    return NDFS_OK;
}

void ndfs_free_entries(ndfs_file_entry_t *entries)
{
    free(entries);
}

ndfs_error_t ndfs_read_file(const ndfs_filesystem_t *fs,
                            const char *path,
                            uint8_t **out_data,
                            size_t *out_size)
{
    int idx;

    if (!fs || !path || !out_data || !out_size) return NDFS_ERR_NULL_PTR;

    idx = find_object(fs, path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;

    return read_object_data(fs, &fs->objects[idx], out_data, out_size);
}

ndfs_error_t ndfs_read_file_parity(const ndfs_filesystem_t *fs,
                                   const char *path,
                                   ndfs_parity_mode_t parity,
                                   uint8_t **out_data,
                                   size_t *out_size)
{
    ndfs_error_t err = ndfs_read_file(fs, path, out_data, out_size);
    if (err != NDFS_OK) return err;
    if (parity == NDFS_PARITY_STRIP && *out_data && *out_size > 0) {
        ndfs_strip_parity(*out_data, *out_size);
    } else if (parity == NDFS_PARITY_SET && *out_data && *out_size > 0) {
        ndfs_set_parity(*out_data, *out_size);
    }
    return NDFS_OK;
}

void ndfs_free_data(uint8_t *data)
{
    free(data);
}

/* ── Write operations ────────────────────────────────────────────── */

ndfs_error_t ndfs_write_file_parity(ndfs_filesystem_t *fs,
                                    const char *path,
                                    const uint8_t *file_data,
                                    size_t file_size,
                                    ndfs_parity_mode_t parity)
{
    if (parity == NDFS_PARITY_SET && file_data && file_size > 0) {
        uint8_t *copy = (uint8_t *)malloc(file_size);
        ndfs_error_t err;
        if (!copy) return NDFS_ERR_ALLOC;
        memcpy(copy, file_data, file_size);
        ndfs_set_parity(copy, file_size);
        err = ndfs_write_file(fs, path, copy, file_size);
        free(copy);
        return err;
    }
    return ndfs_write_file(fs, path, file_data, file_size);
}

ndfs_error_t ndfs_write_file(ndfs_filesystem_t *fs,
                             const char *path,
                             const uint8_t *file_data,
                             size_t file_size)
{
    char user_name[NDFS_NAME_MAX + 1];
    char obj_name[NDFS_NAME_MAX + 1];
    char file_type[NDFS_TYPE_MAX + 1];
    int user_slot;
    int existing_idx;
    uint32_t data_pages, total_required, additional_needed;
    ndfs_user_entry_t *user;
    ndfs_error_t err;

    if (!fs || !path) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    parse_path(path, user_name, sizeof(user_name),
               obj_name, sizeof(obj_name),
               file_type, sizeof(file_type));

    if (obj_name[0] == '\0') return NDFS_ERR_INVALID_ARG;

    /* Find user */
    if (user_name[0] != '\0') {
        user_slot = find_user_by_name(fs, user_name);
    } else {
        /* Default to first user */
        user_slot = -1;
        {
            size_t i;
            for (i = 0; i < MAX_INTERNAL_USERS; i++) {
                if (fs->user_valid[i]) { user_slot = (int)i; break; }
            }
        }
    }
    if (user_slot < 0) return NDFS_ERR_NOT_FOUND;
    user = &fs->users[user_slot];

    /* Calculate required pages */
    data_pages = (uint32_t)((file_size + NDFS_PAGE_SIZE - 1) / NDFS_PAGE_SIZE);
    if (data_pages == 0) data_pages = 1;
    total_required = data_pages + 1;

    /* Check for existing file */
    existing_idx = find_object(fs, path);
    additional_needed = total_required;

    if (existing_idx >= 0) {
        uint32_t existing_total = fs->objects[existing_idx].pages_in_file + 1;
        additional_needed = total_required > existing_total
            ? total_required - existing_total : 0;
    }

    /* Check and expand quota if needed */
    if (additional_needed > 0) {
        int32_t avail = (int32_t)user->pages_reserved - (int32_t)user->pages_used;
        if (avail < (int32_t)additional_needed) {
            uint32_t expansion = additional_needed - (avail > 0 ? (uint32_t)avail : 0);
            uint32_t free_on_disk = ndfs_bf_count_free(&fs->bit_file);
            if (free_on_disk < expansion) return NDFS_ERR_NO_SPACE;
            user->pages_reserved += expansion;
        }
    }

    if (existing_idx >= 0) {
        err = update_existing_file(fs, existing_idx, user_slot, file_data, file_size);
    } else {
        err = create_new_file(fs, obj_name, file_type, user_slot, file_data, file_size);
    }

    if (err != NDFS_OK) return err;
    return persist_all(fs);
}

ndfs_error_t ndfs_delete_file(ndfs_filesystem_t *fs, const char *path)
{
    int idx, user_slot;
    ndfs_object_entry_t *obj;
    uint32_t total_blocks;

    if (!fs || !path) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    idx = find_object(fs, path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;
    obj = &fs->objects[idx];

    /* Free blocks */
    if (obj->file_pointer.block_id > 0) {
        free_file_blocks(fs, obj);
    }

    /* Update user pages used */
    user_slot = find_user_by_index(fs, obj->user_index);
    if (user_slot >= 0) {
        total_blocks = obj->pages_in_file;
        if (obj->file_pointer.type == NDFS_PTR_INDEXED ||
            obj->file_pointer.type == NDFS_PTR_SUBINDEXED) {
            total_blocks += 1;
        }
        if (fs->users[user_slot].pages_used >= total_blocks)
            fs->users[user_slot].pages_used -= total_blocks;
        else
            fs->users[user_slot].pages_used = 0;
    }

    /* Remove from object array */
    if ((size_t)idx < fs->object_count - 1) {
        memmove(&fs->objects[idx], &fs->objects[idx + 1],
                (fs->object_count - (size_t)idx - 1) * sizeof(ndfs_object_entry_t));
    }
    fs->object_count--;

    return persist_all(fs);
}

ndfs_error_t ndfs_rename(ndfs_filesystem_t *fs,
                         const char *old_path,
                         const char *new_path)
{
    int idx;
    char user_name[NDFS_NAME_MAX + 1];
    char obj_name[NDFS_NAME_MAX + 1];
    char file_type[NDFS_TYPE_MAX + 1];
    size_t nlen, tlen;

    if (!fs || !old_path || !new_path) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    idx = find_object(fs, old_path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;

    parse_path(new_path, user_name, sizeof(user_name),
               obj_name, sizeof(obj_name),
               file_type, sizeof(file_type));

    if (obj_name[0] == '\0') return NDFS_ERR_INVALID_ARG;

    nlen = strlen(obj_name);
    if (nlen > NDFS_NAME_MAX) nlen = NDFS_NAME_MAX;
    memcpy(fs->objects[idx].object_name, obj_name, nlen);
    fs->objects[idx].object_name[nlen] = '\0';
    str_toupper(fs->objects[idx].object_name);

    tlen = strlen(file_type);
    if (tlen > NDFS_TYPE_MAX) tlen = NDFS_TYPE_MAX;
    memcpy(fs->objects[idx].type, file_type, tlen);
    fs->objects[idx].type[tlen] = '\0';
    str_toupper(fs->objects[idx].type);

    return persist_all(fs);
}

/* ── User management ─────────────────────────────────────────────── */

ndfs_error_t ndfs_get_users(const ndfs_filesystem_t *fs,
                            ndfs_user_entry_t **out_users,
                            size_t *out_count)
{
    ndfs_user_entry_t *arr;
    size_t count = 0;
    size_t i;

    if (!fs || !out_users || !out_count) return NDFS_ERR_NULL_PTR;

    /* Count valid users */
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (fs->user_valid[i]) count++;
    }

    if (count == 0) {
        *out_users = NULL;
        *out_count = 0;
        return NDFS_OK;
    }

    arr = (ndfs_user_entry_t *)calloc(count, sizeof(ndfs_user_entry_t));
    if (!arr) return NDFS_ERR_ALLOC;

    {
        size_t idx = 0;
        for (i = 0; i < MAX_INTERNAL_USERS; i++) {
            if (fs->user_valid[i]) {
                arr[idx++] = fs->users[i];
            }
        }
    }

    *out_users = arr;
    *out_count = count;
    return NDFS_OK;
}

void ndfs_free_users(ndfs_user_entry_t *users)
{
    free(users);
}

ndfs_error_t ndfs_add_user(ndfs_filesystem_t *fs,
                           const char *name,
                           uint32_t reserved_pages)
{
    int slot;
    ndfs_user_entry_t user;
    size_t i, nlen;

    if (!fs || !name) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    /* Check if already exists */
    if (find_user_by_name(fs, name) >= 0) return NDFS_ERR_ALREADY_EXISTS;

    /* Find next available index */
    slot = -1;
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (!fs->user_valid[i]) { slot = (int)i; break; }
    }
    if (slot < 0) return NDFS_ERR_NO_SLOTS;

    ndfs_ue_init(&user);
    nlen = strlen(name);
    if (nlen > NDFS_NAME_MAX) nlen = NDFS_NAME_MAX;
    memcpy(user.user_name, name, nlen);
    user.user_name[nlen] = '\0';
    str_toupper(user.user_name);
    user.user_index = (uint8_t)slot;
    user.pages_reserved = reserved_pages;

    fs->users[slot] = user;
    fs->user_valid[slot] = true;
    fs->user_count++;

    return persist_all(fs);
}

ndfs_error_t ndfs_remove_user(ndfs_filesystem_t *fs, uint8_t index)
{
    int slot;

    if (!fs) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    slot = find_user_by_index(fs, index);
    if (slot < 0) return NDFS_ERR_NOT_FOUND;

    /* Check if user has files */
    if (objects_for_user_by_index(fs, index) > 0) return NDFS_ERR_HAS_FILES;

    fs->user_valid[slot] = false;
    fs->user_count--;

    return persist_all(fs);
}

ndfs_error_t ndfs_update_user_quota(ndfs_filesystem_t *fs,
                                    uint8_t index,
                                    uint32_t new_pages)
{
    int slot;

    if (!fs) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    slot = find_user_by_index(fs, index);
    if (slot < 0) return NDFS_ERR_NOT_FOUND;

    fs->users[slot].pages_reserved = new_pages;

    return persist_all(fs);
}

ndfs_error_t ndfs_clear_user_password(ndfs_filesystem_t *fs, const char *name)
{
    int slot;

    if (!fs || !name) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    slot = find_user_by_name(fs, name);
    if (slot < 0) return NDFS_ERR_NOT_FOUND;

    fs->users[slot].password = 0;

    return persist_all(fs);
}

ndfs_error_t ndfs_clear_user_password_by_index(ndfs_filesystem_t *fs,
                                               uint8_t index)
{
    int slot;

    if (!fs) return NDFS_ERR_NULL_PTR;
    if (fs->read_only) return NDFS_ERR_READ_ONLY;

    slot = find_user_by_index(fs, index);
    if (slot < 0) return NDFS_ERR_NOT_FOUND;

    fs->users[slot].password = 0;

    return persist_all(fs);
}

/* ── Read operations (extended) ──────────────────────────────────── */

ndfs_error_t ndfs_get_file_blocks(const ndfs_filesystem_t *fs, const char *path,
                                  uint32_t **out_blocks, size_t *out_count)
{
    int idx;
    const ndfs_object_entry_t *obj;
    uint32_t *blocks;
    size_t cap, n = 0;

    if (!fs || !path || !out_blocks || !out_count) return NDFS_ERR_NULL_PTR;
    *out_blocks = NULL;
    *out_count = 0;

    idx = find_object(fs, path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;
    obj = &fs->objects[idx];

    cap = obj->pages_in_file ? obj->pages_in_file : 1;
    blocks = (uint32_t *)malloc(cap * sizeof(uint32_t));
    if (!blocks) return NDFS_ERR_ALLOC;

    if (obj->file_pointer.type == NDFS_PTR_CONTIGUOUS) {
        uint32_t i;
        for (i = 0; i < obj->pages_in_file; i++) {
            blocks[n++] = obj->file_pointer.block_id + i;
        }
    } else if (obj->file_pointer.type == NDFS_PTR_INDEXED) {
        const uint8_t *ib = read_page(fs, obj->file_pointer.block_id);
        uint32_t i;
        if (!ib) { free(blocks); return NDFS_ERR_CORRUPT; }
        for (i = 0; i < obj->pages_in_file && i < 512; i++) {
            ndfs_block_pointer_t p = ndfs_bp_from_bytes(ib, i * 4);
            blocks[n++] = p.block_id;
        }
    } else if (obj->file_pointer.type == NDFS_PTR_SUBINDEXED) {
        const uint8_t *sib = read_page(fs, obj->file_pointer.block_id);
        uint32_t remaining = obj->pages_in_file;
        uint32_t si;
        if (!sib) { free(blocks); return NDFS_ERR_CORRUPT; }
        for (si = 0; si < 512 && remaining > 0; si++) {
            ndfs_block_pointer_t ip = ndfs_bp_from_bytes(sib, si * 4);
            const uint8_t *ib;
            uint32_t j;
            if (ip.block_id == 0) break;
            ib = read_page(fs, ip.block_id);
            if (!ib) { free(blocks); return NDFS_ERR_CORRUPT; }
            for (j = 0; j < 512 && remaining > 0; j++) {
                ndfs_block_pointer_t dp = ndfs_bp_from_bytes(ib, j * 4);
                blocks[n++] = dp.block_id;
                remaining--;
            }
        }
    }

    *out_blocks = blocks;
    *out_count = n;
    return NDFS_OK;
}

ndfs_error_t ndfs_file_exists(const ndfs_filesystem_t *fs, const char *path,
                              bool *out_exists)
{
    int idx;

    if (!fs || !path || !out_exists) return NDFS_ERR_NULL_PTR;

    idx = find_object(fs, path);
    *out_exists = (idx >= 0);
    return NDFS_OK;
}

ndfs_error_t ndfs_get_metadata(const ndfs_filesystem_t *fs, const char *path,
                               ndfs_file_entry_t *out_entry)
{
    int idx;
    const ndfs_object_entry_t *obj;

    if (!fs || !path || !out_entry) return NDFS_ERR_NULL_PTR;

    idx = find_object(fs, path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;

    obj = &fs->objects[idx];

    memset(out_entry, 0, sizeof(*out_entry));
    memcpy(out_entry->name, obj->object_name, NDFS_NAME_MAX + 1);
    memcpy(out_entry->type, obj->type, NDFS_TYPE_MAX + 1);
    ndfs_oe_full_name(obj, out_entry->full_name, sizeof(out_entry->full_name));
    memcpy(out_entry->user_name, obj->user_name, NDFS_NAME_MAX + 1);
    out_entry->size = obj->bytes_in_file;
    out_entry->pages = obj->pages_in_file;
    out_entry->is_directory = false;

    return NDFS_OK;
}

/* ── User query (extended) ──────────────────────────────────────── */

ndfs_error_t ndfs_get_user(const ndfs_filesystem_t *fs, uint8_t index,
                           ndfs_user_entry_t *out_user)
{
    int slot;

    if (!fs || !out_user) return NDFS_ERR_NULL_PTR;

    slot = find_user_by_index(fs, index);
    if (slot < 0) return NDFS_ERR_NOT_FOUND;

    *out_user = fs->users[slot];
    return NDFS_OK;
}

/* ── Bitmap queries ──────────────────────────────────────────────── */

bool ndfs_is_block_used(const ndfs_filesystem_t *fs, uint32_t block_id)
{
    if (!fs) return false;
    return ndfs_bf_is_used(&fs->bit_file, block_id);
}

ndfs_error_t ndfs_get_free_pages(const ndfs_filesystem_t *fs, uint32_t *out)
{
    if (!fs || !out) return NDFS_ERR_NULL_PTR;
    *out = ndfs_bf_count_free(&fs->bit_file);
    return NDFS_OK;
}

ndfs_error_t ndfs_get_used_pages(const ndfs_filesystem_t *fs, uint32_t *out)
{
    if (!fs || !out) return NDFS_ERR_NULL_PTR;
    *out = ndfs_bf_count_used(&fs->bit_file);
    return NDFS_OK;
}

/* ── Low-level object access ────────────────────────────────────── */

ndfs_error_t ndfs_get_object_entries(const ndfs_filesystem_t *fs,
                                     ndfs_object_entry_t **out_entries,
                                     size_t *out_count)
{
    ndfs_object_entry_t *arr;

    if (!fs || !out_entries || !out_count) return NDFS_ERR_NULL_PTR;

    if (fs->object_count == 0) {
        *out_entries = NULL;
        *out_count = 0;
        return NDFS_OK;
    }

    arr = (ndfs_object_entry_t *)malloc(
        fs->object_count * sizeof(ndfs_object_entry_t));
    if (!arr) return NDFS_ERR_ALLOC;

    memcpy(arr, fs->objects,
           fs->object_count * sizeof(ndfs_object_entry_t));

    *out_entries = arr;
    *out_count = fs->object_count;
    return NDFS_OK;
}

ndfs_error_t ndfs_get_object_entry(const ndfs_filesystem_t *fs,
                                   const char *name,
                                   const char *user_name,
                                   ndfs_object_entry_t *out_entry)
{
    char search_path[256];

    if (!fs || !name || !out_entry) return NDFS_ERR_NULL_PTR;

    /* Build a path string that find_object can parse */
    if (user_name && user_name[0] != '\0') {
        snprintf(search_path, sizeof(search_path), "%s/%s", user_name, name);
    } else {
        snprintf(search_path, sizeof(search_path), "%s", name);
    }

    {
        int idx = find_object(fs, search_path);
        if (idx < 0) return NDFS_ERR_NOT_FOUND;
        *out_entry = fs->objects[idx];
    }

    return NDFS_OK;
}

void ndfs_free_object_entries(ndfs_object_entry_t *entries)
{
    free(entries);
}

/* ── Diagnostics ─────────────────────────────────────────────────── */

ndfs_error_t ndfs_verify_integrity(const ndfs_filesystem_t *fs, bool *out_ok)
{
    size_t i;

    if (!fs || !out_ok) return NDFS_ERR_NULL_PTR;

    if (!ndfs_mb_is_valid(&fs->master_block)) {
        *out_ok = false;
        return NDFS_OK;
    }

    for (i = 0; i < fs->object_count; i++) {
        const ndfs_object_entry_t *obj = &fs->objects[i];
        if (!ndfs_bp_is_valid(&obj->file_pointer)) continue;
        if (!ndfs_bf_is_used(&fs->bit_file, obj->file_pointer.block_id)) {
            *out_ok = false;
            return NDFS_OK;
        }
    }

    *out_ok = true;
    return NDFS_OK;
}

ndfs_error_t ndfs_fsck(const ndfs_filesystem_t *fs, char **out_report,
                       int *out_errors)
{
    char *report;
    size_t cap = 8192;
    size_t len = 0;
    int errors = 0;
    int warnings = 0;
    int n;
    uint32_t total_pages;
    uint32_t block;
    size_t i, j;

    if (!fs || !out_report || !out_errors) return NDFS_ERR_NULL_PTR;

    report = (char *)malloc(cap);
    if (!report) return NDFS_ERR_ALLOC;

    total_pages = (uint32_t)(fs->size / NDFS_PAGE_SIZE);

#define APPEND(...) do { \
    n = snprintf(report + len, cap - len, __VA_ARGS__); \
    if (n > 0) len += (size_t)n; \
    if (len + 512 > cap) { \
        cap *= 2; \
        report = (char *)realloc(report, cap); \
        if (!report) return NDFS_ERR_ALLOC; \
    } \
} while(0)

    APPEND("NDFS Filesystem Check\n");
    APPEND("=====================\n");
    APPEND("Volume: %s\n", fs->master_block.directory_name);
    APPEND("Total pages: %u\n\n", total_pages);

    /* --- Phase 1: Master block validation --- */
    APPEND("Phase 1: Master block\n");
    if (!ndfs_mb_is_valid(&fs->master_block)) {
        APPEND("  ERROR: Master block is invalid\n");
        errors++;
    } else {
        APPEND("  OK: Master block valid, directory name '%s'\n",
               fs->master_block.directory_name);
    }
    if (!ndfs_bp_is_valid(&fs->master_block.object_file_ptr)) {
        APPEND("  ERROR: Object file pointer invalid\n");
        errors++;
    }
    if (!ndfs_bp_is_valid(&fs->master_block.user_file_ptr)) {
        APPEND("  ERROR: User file pointer invalid\n");
        errors++;
    }
    if (!ndfs_bp_is_valid(&fs->master_block.bit_file_ptr)) {
        APPEND("  ERROR: Bit file pointer invalid\n");
        errors++;
    }

    /* --- Phase 2: Build reference count map --- */
    APPEND("\nPhase 2: Block reference analysis\n");

    /* Allocate a reference count array: how many times each block is referenced */
    uint8_t *refcount = (uint8_t *)calloc(total_pages, sizeof(uint8_t));
    if (!refcount) {
        free(report);
        return NDFS_ERR_ALLOC;
    }

    /* Mark system blocks (master block pointer targets) */
    if (ndfs_bp_is_valid(&fs->master_block.object_file_ptr))
        refcount[fs->master_block.object_file_ptr.block_id]++;
    if (ndfs_bp_is_valid(&fs->master_block.user_file_ptr))
        refcount[fs->master_block.user_file_ptr.block_id]++;
    if (ndfs_bp_is_valid(&fs->master_block.bit_file_ptr))
        refcount[fs->master_block.bit_file_ptr.block_id]++;

    /* Walk all file pointers */
    for (i = 0; i < fs->object_count; i++) {
        const ndfs_object_entry_t *obj = &fs->objects[i];
        if (!ndfs_bp_is_valid(&obj->file_pointer)) continue;

        uint32_t fp_block = obj->file_pointer.block_id;
        if (fp_block >= total_pages) {
            APPEND("  ERROR: File '%s:%s' (user %s) pointer block %u out of range\n",
                   obj->object_name, obj->type, obj->user_name, fp_block);
            errors++;
            continue;
        }

        refcount[fp_block]++; /* index/sub-index block */

        if (obj->file_pointer.type == NDFS_PTR_INDEXED) {
            /* Walk index block entries */
            const uint8_t *idx_page = read_page(fs, fp_block);
            if (idx_page) {
                for (j = 0; j < NDFS_MAX_OBJECT_FILE_PTRS && j < obj->pages_in_file; j++) {
                    ndfs_block_pointer_t dp = ndfs_bp_from_bytes(idx_page, j * 4);
                    if (dp.block_id > 0 && dp.block_id < total_pages) {
                        refcount[dp.block_id]++;
                    } else if (dp.block_id >= total_pages) {
                        APPEND("  ERROR: File '%s:%s' data block %u out of range\n",
                               obj->object_name, obj->type, dp.block_id);
                        errors++;
                    }
                    /* block_id == 0 is sparse hole, that's OK */
                }
            }
        } else if (obj->file_pointer.type == NDFS_PTR_CONTIGUOUS) {
            for (j = 0; j < obj->pages_in_file; j++) {
                uint32_t db = fp_block + (uint32_t)j;
                if (db < total_pages) {
                    refcount[db]++;
                }
            }
        }
        /* SubIndexed: would need deeper walk, skip for now */
    }

    /* --- Phase 3: Bitmap vs reference check --- */
    APPEND("\nPhase 3: Bitmap consistency\n");
    {
        uint32_t orphaned = 0;
        uint32_t referenced_free = 0;
        uint32_t multiply_referenced = 0;

        for (block = 0; block < total_pages; block++) {
            bool in_bitmap = ndfs_bf_is_used(&fs->bit_file, block);
            bool referenced = (refcount[block] > 0);

            if (in_bitmap && !referenced && block >= NDFS_FIRST_ALLOC_BLOCK) {
                orphaned++;
            }
            if (!in_bitmap && referenced) {
                referenced_free++;
            }
            if (refcount[block] > 1) {
                multiply_referenced++;
            }
        }

        if (orphaned > 0) {
            APPEND("  WARNING: %u orphaned blocks (marked used but not referenced by any file)\n",
                   orphaned);
            warnings++;
        } else {
            APPEND("  OK: No orphaned blocks\n");
        }

        if (referenced_free > 0) {
            APPEND("  ERROR: %u blocks referenced by files but marked FREE in bitmap!\n",
                   referenced_free);
            errors++;
        } else {
            APPEND("  OK: All referenced blocks marked used in bitmap\n");
        }

        if (multiply_referenced > 0) {
            APPEND("  ERROR: %u blocks referenced by multiple files (cross-linked)\n",
                   multiply_referenced);
            errors++;
        } else {
            APPEND("  OK: No cross-linked blocks\n");
        }
    }

    /* --- Phase 4: User quota verification --- */
    APPEND("\nPhase 4: User quota verification\n");
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (!fs->user_valid[i]) continue;
        const ndfs_user_entry_t *user = &fs->users[i];

        /* Count actual pages used by this user's files */
        uint32_t actual_pages = 0;
        for (j = 0; j < fs->object_count; j++) {
            if (fs->objects[j].user_index == user->user_index) {
                actual_pages += fs->objects[j].pages_in_file;
                /* Add index block */
                if (ndfs_bp_is_valid(&fs->objects[j].file_pointer) &&
                    fs->objects[j].file_pointer.type == NDFS_PTR_INDEXED) {
                    actual_pages++;
                }
            }
        }

        if (actual_pages != user->pages_used) {
            APPEND("  WARNING: User '%s' pages_used=%u but actual file pages=%u\n",
                   user->user_name, user->pages_used, actual_pages);
            warnings++;
        }

        if (user->pages_used > user->pages_reserved && user->pages_reserved > 0) {
            APPEND("  WARNING: User '%s' over quota: using %u, reserved %u\n",
                   user->user_name, user->pages_used, user->pages_reserved);
            warnings++;
        }
    }

    /* --- Phase 5: File structure validation --- */
    APPEND("\nPhase 5: File structure validation\n");
    {
        uint32_t zero_byte_files = 0;

        for (i = 0; i < fs->object_count; i++) {
            const ndfs_object_entry_t *obj = &fs->objects[i];

            if (obj->bytes_in_file == 0 && obj->pages_in_file > 0) {
                zero_byte_files++;
            }
            if (!ndfs_bp_is_valid(&obj->file_pointer) && obj->pages_in_file > 0) {
                APPEND("  ERROR: File '%s:%s' has %u pages but invalid pointer\n",
                       obj->object_name, obj->type, obj->pages_in_file);
                errors++;
            }
        }

        if (zero_byte_files > 0) {
            APPEND("  INFO: %u files with 0 bytes but allocated pages (device/system files)\n",
                   zero_byte_files);
        }
    }

    /* --- Summary --- */
    APPEND("\n");
    APPEND("========================================\n");
    if (errors == 0 && warnings == 0) {
        APPEND("Filesystem is CLEAN. No errors, no warnings.\n");
    } else {
        APPEND("Found %d error(s) and %d warning(s).\n", errors, warnings);
    }

    free(refcount);

#undef APPEND

    *out_report = report;
    *out_errors = errors;
    return NDFS_OK;
}

ndfs_error_t ndfs_generate_report(const ndfs_filesystem_t *fs, char **out_report)
{
    char *report;
    size_t cap = 4096;
    size_t len = 0;
    uint32_t total_pages, used_pages, free_pages;
    size_t user_count = 0;
    size_t i;
    int n;

    if (!fs || !out_report) return NDFS_ERR_NULL_PTR;

    report = (char *)malloc(cap);
    if (!report) return NDFS_ERR_ALLOC;

    total_pages = (uint32_t)(fs->size / NDFS_PAGE_SIZE);
    used_pages  = ndfs_bf_count_used(&fs->bit_file);
    free_pages  = ndfs_bf_count_free(&fs->bit_file);

    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (fs->user_valid[i]) user_count++;
    }

#define APPEND(...) do { \
    n = snprintf(report + len, cap - len, __VA_ARGS__); \
    if (n > 0) len += (size_t)n; \
    if (len + 256 > cap) { \
        cap *= 2; \
        report = (char *)realloc(report, cap); \
        if (!report) return NDFS_ERR_ALLOC; \
    } \
} while(0)

    APPEND("NDFS Filesystem Report\n");
    APPEND("======================\n");
    APPEND("Volume: %s\n", fs->master_block.directory_name);
    APPEND("Total pages: %u\n", total_pages);
    APPEND("Used pages: %u\n", used_pages);
    APPEND("Free pages: %u\n", free_pages);
    APPEND("Users: %u\n", (unsigned)user_count);
    APPEND("Files: %u\n\n", (unsigned)fs->object_count);

    APPEND("Users:\n");
    for (i = 0; i < MAX_INTERNAL_USERS; i++) {
        if (!fs->user_valid[i]) continue;
        APPEND("  [%u] %s - Reserved: %u, Used: %u\n",
               fs->users[i].user_index,
               fs->users[i].user_name,
               fs->users[i].pages_reserved,
               fs->users[i].pages_used);
    }

    APPEND("\nFiles:\n");
    for (i = 0; i < fs->object_count; i++) {
        const ndfs_object_entry_t *o = &fs->objects[i];
        APPEND("  %s/%s:%s - %u bytes (%u pages)\n",
               o->user_name, o->object_name, o->type,
               o->bytes_in_file, o->pages_in_file);
    }

#undef APPEND

    *out_report = report;
    return NDFS_OK;
}

void ndfs_free_string(char *str)
{
    free(str);
}

ndfs_error_t ndfs_to_buffer(const ndfs_filesystem_t *fs,
                            uint8_t **out_data,
                            size_t *out_size)
{
    uint8_t *copy;

    if (!fs || !out_data || !out_size) return NDFS_ERR_NULL_PTR;

    copy = (uint8_t *)malloc(fs->size);
    if (!copy) return NDFS_ERR_ALLOC;
    memcpy(copy, fs->data, fs->size);

    *out_data = copy;
    *out_size = fs->size;
    return NDFS_OK;
}

/* ── XAT support ────────────────────────────────────────────────── */

ndfs_error_t ndfs_get_file_properties(const ndfs_filesystem_t *fs,
                                      const char *path,
                                      ndfs_xat_properties_t *out)
{
    int idx;

    if (!fs || !path || !out) return NDFS_ERR_NULL_PTR;

    idx = find_object(fs, path);
    if (idx < 0) return NDFS_ERR_NOT_FOUND;

    return ndfs_xat_from_object(&fs->objects[idx], out);
}

const char *ndfs_strerror(ndfs_error_t err)
{
    switch (err) {
        case NDFS_OK:               return "Success";
        case NDFS_ERR_NULL_PTR:     return "Null pointer";
        case NDFS_ERR_INVALID_ARG:  return "Invalid argument";
        case NDFS_ERR_TOO_SMALL:    return "Buffer too small";
        case NDFS_ERR_NOT_ALIGNED:  return "Size not aligned to page boundary";
        case NDFS_ERR_INVALID_IMAGE:return "Invalid NDFS image";
        case NDFS_ERR_OUT_OF_RANGE: return "Block ID out of range";
        case NDFS_ERR_NOT_FOUND:    return "Not found";
        case NDFS_ERR_ALREADY_EXISTS:return "Already exists";
        case NDFS_ERR_NO_SPACE:     return "No free space";
        case NDFS_ERR_READ_ONLY:    return "Filesystem is read-only";
        case NDFS_ERR_NO_SLOTS:     return "No available slots";
        case NDFS_ERR_HAS_FILES:    return "User has files";
        case NDFS_ERR_ALLOC:        return "Memory allocation failed";
        case NDFS_ERR_CORRUPT:      return "Filesystem data corrupt";
        default:                    return "Unknown error";
    }
}
