/**
 * NDFS filesystem: opaque handle for reading and writing NDFS disk images.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_FILESYSTEM_H
#define NDFS_FILESYSTEM_H

#include "master_block.h"
#include "bit_file.h"
#include "user_entry.h"
#include "object_entry.h"
#include "xat.h"
#include "block_io.h"   /* ndfs_block_io, ndfs_open_block, ndfs_open_file */

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque filesystem handle.
 *  (Guard matches block_io.h so the shared typedef appears once under C99.) */
#ifndef NDFS_FILESYSTEM_T_DEFINED
#define NDFS_FILESYSTEM_T_DEFINED
typedef struct ndfs_filesystem ndfs_filesystem_t;
#endif

/* ── Lifecycle ───────────────────────────────────────────────────── */

/**
 * Open an NDFS image from a caller-owned buffer.
 * The caller must keep the buffer alive and valid for the lifetime
 * of the filesystem handle.  The library does NOT copy the buffer.
 *
 * @param data       Pointer to disk image bytes.
 * @param size       Size in bytes (must be a multiple of NDFS_PAGE_SIZE).
 * @param read_only  If true, write operations return NDFS_ERR_READ_ONLY.
 * @param out_fs     Receives the filesystem handle.
 */
ndfs_error_t ndfs_open_buffer(const uint8_t *data, size_t size,
                              bool read_only,
                              ndfs_filesystem_t **out_fs);

/**
 * Open an NDFS image by copying the buffer.
 * The library owns the copy; caller may free the original immediately.
 */
ndfs_error_t ndfs_open_buffer_copy(const uint8_t *data, size_t size,
                                   bool read_only,
                                   ndfs_filesystem_t **out_fs);

/**
 * Close a filesystem and free all associated resources.
 * Safe to call with NULL.
 */
void ndfs_close(ndfs_filesystem_t *fs);

/* ── Read operations ─────────────────────────────────────────────── */

/**
 * Get the parsed master block (read-only pointer; valid while fs is open).
 */
ndfs_error_t ndfs_get_master_block(const ndfs_filesystem_t *fs,
                                   const ndfs_master_block_t **out);

/**
 * Get the volume/directory name.
 * @param buf      Destination buffer.
 * @param buf_len  Must be >= NDFS_NAME_MAX + 1.
 */
ndfs_error_t ndfs_get_directory_name(const ndfs_filesystem_t *fs,
                                     char *buf, size_t buf_len);

/**
 * List directory contents.
 * - path="" or "/": lists users as directories.
 * - path="USERNAME": lists that user's files.
 *
 * @param out_entries   Receives malloc'd array of entries.
 * @param out_count     Receives number of entries.
 */
ndfs_error_t ndfs_list_directory(const ndfs_filesystem_t *fs,
                                 const char *path,
                                 ndfs_file_entry_t **out_entries,
                                 size_t *out_count);

/** Free an entries array returned by ndfs_list_directory(). */
void ndfs_free_entries(ndfs_file_entry_t *entries);

/** Parity mode for read/write operations. */
typedef enum {
    NDFS_PARITY_NONE  = 0,  /**< No parity handling (raw bytes). */
    NDFS_PARITY_STRIP = 1,  /**< Strip parity: clear bit 7 (for reading ND text as ASCII). */
    NDFS_PARITY_SET   = 2   /**< Set even parity: bit 7 set so total 1-bits is even. */
} ndfs_parity_mode_t;

/**
 * Read a file's contents.
 * @param path      "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param out_data  Receives malloc'd data buffer.
 * @param out_size  Receives data size in bytes.
 */
ndfs_error_t ndfs_read_file(const ndfs_filesystem_t *fs,
                            const char *path,
                            uint8_t **out_data,
                            size_t *out_size);

/**
 * Read a file's contents with parity handling.
 * @param path      "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param parity    NDFS_PARITY_NONE, NDFS_PARITY_STRIP, or NDFS_PARITY_SET.
 * @param out_data  Receives malloc'd data buffer.
 * @param out_size  Receives data size in bytes.
 */
ndfs_error_t ndfs_read_file_parity(const ndfs_filesystem_t *fs,
                                   const char *path,
                                   ndfs_parity_mode_t parity,
                                   uint8_t **out_data,
                                   size_t *out_size);

/** Free data returned by ndfs_read_file(). */
void ndfs_free_data(uint8_t *data);

/* ── Write operations ────────────────────────────────────────────── */

/**
 * Write (create or overwrite) a file.
 * @param path       "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param file_data  The raw bytes to write.
 * @param file_size  Number of bytes.
 */
ndfs_error_t ndfs_write_file(ndfs_filesystem_t *fs,
                             const char *path,
                             const uint8_t *file_data,
                             size_t file_size);

/**
 * Write (create or overwrite) a file with parity handling.
 * @param path       "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param file_data  The raw bytes to write.
 * @param file_size  Number of bytes.
 * @param parity     NDFS_PARITY_NONE, NDFS_PARITY_STRIP, or NDFS_PARITY_SET.
 */
ndfs_error_t ndfs_write_file_parity(ndfs_filesystem_t *fs,
                                    const char *path,
                                    const uint8_t *file_data,
                                    size_t file_size,
                                    ndfs_parity_mode_t parity);

/** Delete a file. */
ndfs_error_t ndfs_delete_file(ndfs_filesystem_t *fs, const char *path);

/** Rename a file. */
ndfs_error_t ndfs_rename(ndfs_filesystem_t *fs,
                         const char *old_path,
                         const char *new_path);

/* ── User management ─────────────────────────────────────────────── */

/**
 * Get all users.
 * @param out_users  Receives malloc'd array of user entries.
 * @param out_count  Receives number of entries.
 */
ndfs_error_t ndfs_get_users(const ndfs_filesystem_t *fs,
                            ndfs_user_entry_t **out_users,
                            size_t *out_count);

/** Free users array returned by ndfs_get_users(). */
void ndfs_free_users(ndfs_user_entry_t *users);

/** Add a new user. Returns NDFS_ERR_ALREADY_EXISTS or NDFS_ERR_NO_SLOTS. */
ndfs_error_t ndfs_add_user(ndfs_filesystem_t *fs,
                           const char *name,
                           uint32_t reserved_pages);

/** Remove a user (fails with NDFS_ERR_HAS_FILES if user has files). */
ndfs_error_t ndfs_remove_user(ndfs_filesystem_t *fs, uint8_t index);

/** Update a user's page quota. */
ndfs_error_t ndfs_update_user_quota(ndfs_filesystem_t *fs,
                                    uint8_t index,
                                    uint32_t new_pages);

/** Clear a user's password (set to 0). Can pass index or name. */
ndfs_error_t ndfs_clear_user_password(ndfs_filesystem_t *fs, const char *name);

ndfs_error_t ndfs_clear_user_password_by_index(ndfs_filesystem_t *fs,
                                               uint8_t index);

/* ── Friends ─────────────────────────────────────────────────────────
 *
 * A user has 0..NDFS_MAX_FRIENDS friends, stored in that user's own entry.
 * A friend grants a specific other user a set of rights (RWACD) to this
 * user's files. The owner and friend may be given by name OR by a decimal
 * user index (0-255); a numeric string is treated as an index. */

/** One entry returned by ndfs_list_friends. */
typedef struct {
    uint8_t  index;                  /* friend's user index */
    char     name[NDFS_NAME_MAX + 1];/* friend's user name, or "" if no such user */
    uint16_t bits;                   /* raw 16-bit friend entry */
    char     perms[6];               /* "RWACD" / "-----" */
} ndfs_friend_info_t;

/**
 * List a user's friends. *out_friends is a malloc'd array of *out_count
 * entries (free with ndfs_free_friends); both are set to NULL/0 if the user
 * has no friends. Returns NDFS_ERR_NOT_FOUND if the user does not exist.
 */
ndfs_error_t ndfs_list_friends(const ndfs_filesystem_t *fs, const char *user_ref,
                               ndfs_friend_info_t **out_friends, size_t *out_count);

/** Free the array returned by ndfs_list_friends. */
void ndfs_free_friends(ndfs_friend_info_t *friends);

/**
 * Add a friend to a user with the given permission letters (e.g. "RWA";
 * NULL/empty defaults to "RWA"). Errors: NDFS_ERR_NOT_FOUND (owner/friend
 * unknown), NDFS_ERR_ALREADY_EXISTS (already a friend), NDFS_ERR_NO_SLOTS
 * (all 8 slots used), NDFS_ERR_INVALID_ARG (bad permission letters).
 * Persists by writing only the owner's user page.
 */
ndfs_error_t ndfs_add_friend(ndfs_filesystem_t *fs, const char *user_ref,
                             const char *friend_ref, const char *perms);

/**
 * Remove a friend from a user. Returns NDFS_ERR_NOT_FOUND if the owner does
 * not exist or the friend is not in the list. Persists the owner's page.
 */
ndfs_error_t ndfs_remove_friend(ndfs_filesystem_t *fs, const char *user_ref,
                                const char *friend_ref);

/* ── Read operations (extended) ──────────────────────────────────── */

/**
 * Check if a file exists at the given path.
 * @param path       "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param out_exists Receives true if the file exists.
 */
ndfs_error_t ndfs_file_exists(const ndfs_filesystem_t *fs, const char *path,
                              bool *out_exists);

/**
 * Get metadata for a file (without reading its contents).
 * @param path      "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param out_entry Receives the file entry metadata.
 */
ndfs_error_t ndfs_get_metadata(const ndfs_filesystem_t *fs, const char *path,
                               ndfs_file_entry_t *out_entry);

/* ── User query (extended) ──────────────────────────────────────── */

/**
 * Get a single user by index.
 * @param index    The user index to look up.
 * @param out_user Receives the user entry.
 */
ndfs_error_t ndfs_get_user(const ndfs_filesystem_t *fs, uint8_t index,
                           ndfs_user_entry_t *out_user);

/**
 * Get the list of data-block IDs that make up a file, in order.
 * Walks contiguous / indexed / sub-indexed allocation. A block ID of 0 marks
 * a sparse hole. The caller must free(*out_blocks).
 *
 * @param path        File path "USER/NAME:TYPE".
 * @param out_blocks  Receives a malloc'd array of block IDs.
 * @param out_count   Receives the number of entries.
 */
ndfs_error_t ndfs_get_file_blocks(const ndfs_filesystem_t *fs, const char *path,
                                  uint32_t **out_blocks, size_t *out_count);

/**
 * Overwrite @p len bytes at file-relative byte offset @p file_offset within an
 * existing file, IN PLACE, touching only the page(s) that overlap the region.
 * The file's allocation is never changed, so this is safe on a large contiguous
 * system file (e.g. SEGFIL0) where a full rewrite could reallocate. The region
 * may straddle a page boundary.
 *
 * Errors: NDFS_ERR_NOT_FOUND (no such file), NDFS_ERR_READ_ONLY,
 * NDFS_ERR_OUT_OF_RANGE (region extends past the file), NDFS_ERR_CORRUPT
 * (the region overlaps a sparse hole).
 */
ndfs_error_t ndfs_patch_file_region(ndfs_filesystem_t *fs, const char *path,
                                    size_t file_offset,
                                    const uint8_t *data, size_t len);

/**
 * Set the 15-bit access word for a file and persist the change.
 * @param path         File path "USER/NAME:TYPE".
 * @param access_bits  New access word (3x5-bit OWN/FRIEND/PUBLIC tiers).
 */
ndfs_error_t ndfs_set_file_access(ndfs_filesystem_t *fs, const char *path,
                                  uint16_t access_bits);

/* ── Bitmap queries ──────────────────────────────────────────────── */

bool ndfs_is_block_used(const ndfs_filesystem_t *fs, uint32_t block_id);

/**
 * True if the image opened with a byte length that was not a whole multiple of
 * NDFS_PAGE_SIZE.  The trailing partial page was dropped and the filesystem was
 * forced read-only.  False for cleanly page-aligned images.
 */
bool ndfs_is_unaligned(const ndfs_filesystem_t *fs);

ndfs_error_t ndfs_get_free_pages(const ndfs_filesystem_t *fs, uint32_t *out);

/**
 * Get the number of used pages.
 * @param out  Receives the count of used pages.
 */
ndfs_error_t ndfs_get_used_pages(const ndfs_filesystem_t *fs, uint32_t *out);

/* ── Low-level object access ────────────────────────────────────── */

/**
 * Get all active object entries.
 * @param out_entries  Receives malloc'd array of object entries.
 * @param out_count    Receives number of entries.
 */
ndfs_error_t ndfs_get_object_entries(const ndfs_filesystem_t *fs,
                                     ndfs_object_entry_t **out_entries,
                                     size_t *out_count);

/**
 * Get a single object entry by name and user name.
 * @param name       Object name (e.g. "HELLO:DATA" or "HELLO").
 * @param user_name  User name (e.g. "SYSTEM"). May be NULL for any user.
 * @param out_entry  Receives the object entry.
 */
ndfs_error_t ndfs_get_object_entry(const ndfs_filesystem_t *fs,
                                   const char *name,
                                   const char *user_name,
                                   ndfs_object_entry_t *out_entry);

/** Free an object entries array returned by ndfs_get_object_entries(). */
void ndfs_free_object_entries(ndfs_object_entry_t *entries);

/* ── Diagnostics ─────────────────────────────────────────────────── */

/**
 * Basic integrity verification.
 * @param out_ok  Receives true if integrity check passes.
 */
ndfs_error_t ndfs_verify_integrity(const ndfs_filesystem_t *fs, bool *out_ok);

/**
 * Full filesystem check (fsck).
 * Checks bitmap consistency, orphaned blocks, multiply-referenced blocks,
 * user quota accuracy, and structural integrity.
 *
 * @param out_report  Receives a malloc'd detailed report string.
 * @param out_errors  Receives the number of errors found.
 */
ndfs_error_t ndfs_fsck(const ndfs_filesystem_t *fs, char **out_report,
                       int *out_errors);

/**
 * Generate a text report about the filesystem.
 * @param out_report  Receives a malloc'd null-terminated string.
 */
ndfs_error_t ndfs_generate_report(const ndfs_filesystem_t *fs, char **out_report);

/** Free a string returned by ndfs_generate_report(). */
void ndfs_free_string(char *str);

/** Get a human-readable description of an error code. */
const char *ndfs_strerror(ndfs_error_t err);

/**
 * Export the current image as a new malloc'd buffer.
 * @param out_data  Receives malloc'd copy of the image.
 * @param out_size  Receives size in bytes.
 */
ndfs_error_t ndfs_to_buffer(const ndfs_filesystem_t *fs,
                            uint8_t **out_data,
                            size_t *out_size);

/* ── XAT (Extended Attribute) support ───────────────────────────── */

/**
 * Get XAT properties for a file.
 * @param path  "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
 * @param out   Receives the XAT properties.
 */
ndfs_error_t ndfs_get_file_properties(const ndfs_filesystem_t *fs,
                                      const char *path,
                                      ndfs_xat_properties_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_FILESYSTEM_H */
