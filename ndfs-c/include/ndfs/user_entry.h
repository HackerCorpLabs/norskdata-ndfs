/**
 * NDFS user entry: 64-byte record in the user file.
 *
 * Byte offsets:
 *   0:     Flag (0x81 = valid user)
 *   1:     Enter count
 *   2-17:  User name (16 bytes, terminated by 0x27)
 *   18-19: Password (16-bit, big-endian)
 *   20-23: Date created (ND time, big-endian)
 *   24-27: Last date entered (ND time, big-endian)
 *   28-31: Pages reserved (32-bit, big-endian)
 *   32-35: Pages used (32-bit, big-endian)
 *   36:    Directory index
 *   37:    User index
 *   38-39: Default file access (16-bit, big-endian)
 *   40-55: Friends (8 x 2-byte entries, big-endian)
 *   56-63: Reserved
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_USER_ENTRY_H
#define NDFS_USER_ENTRY_H

#include "user_friend.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 64-byte user entry. */
typedef struct {
    uint8_t            user_index;
    char               user_name[NDFS_NAME_MAX + 1];
    uint16_t           password;
    uint8_t            enter_count;
    uint32_t           date_created;
    uint32_t           last_date_entered;
    uint32_t           pages_reserved;
    uint32_t           pages_used;
    uint8_t            directory_index;
    uint16_t           default_file_access;
    ndfs_user_friend_t friends[NDFS_MAX_FRIENDS];
} ndfs_user_entry_t;

/**
 * Parse a user entry from 64 bytes at offset.
 * @param data    Buffer.
 * @param offset  Byte offset into buffer.
 * @param out     Receives the parsed entry.
 * @return NDFS_OK, NDFS_ERR_TOO_SMALL, or NDFS_ERR_NOT_FOUND
 *         (if flag byte != 0x81).
 */
ndfs_error_t ndfs_ue_from_bytes(const uint8_t *data, size_t data_len,
                                size_t offset, ndfs_user_entry_t *out);

/**
 * Serialize a user entry to a 64-byte buffer.
 * @param entry  The entry to serialize.
 * @param buf    Must be at least NDFS_ENTRY_SIZE bytes.
 */
void ndfs_ue_to_bytes(const ndfs_user_entry_t *entry, uint8_t *buf);

/** Check if user has exceeded quota. */
static inline bool ndfs_ue_is_over_quota(const ndfs_user_entry_t *e)
{
    return e->pages_used > e->pages_reserved;
}

/** Get remaining free pages in quota. */
static inline int32_t ndfs_ue_free_pages(const ndfs_user_entry_t *e)
{
    return (int32_t)e->pages_reserved - (int32_t)e->pages_used;
}

/** Initialize a new empty user entry with defaults. */
void ndfs_ue_init(ndfs_user_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_USER_ENTRY_H */
