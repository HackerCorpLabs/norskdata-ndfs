/**
 * NDFS object (file) entry: 64-byte record in the object file.
 *
 * Byte offsets:
 *   0:     Header (bit 7 = in use, i.e. 0x80)
 *   1:     Reserved
 *   2-17:  Object name (16 bytes, terminated by 0x27)
 *   18-21: File type (4 bytes, terminated by 0x27)
 *   22-31: Reserved / versioning / access
 *   32:    File type code (0=DATA, 1=PROG, 2=SYMB, 3=TEXT)
 *   33:    Reserved
 *   34:    User index (owner)
 *   35-51: Reserved / tracking
 *   52-55: Pages in file (32-bit, big-endian)
 *   56-59: Bytes in file - 1 (32-bit, big-endian; actual = stored + 1)
 *   60-63: File pointer (BlockPointer, big-endian)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_OBJECT_ENTRY_H
#define NDFS_OBJECT_ENTRY_H

#include "block_pointer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 64-byte object (file) entry. */
typedef struct {
    uint8_t              header;
    uint32_t             object_index;
    char                 object_name[NDFS_NAME_MAX + 1];
    char                 type[NDFS_TYPE_MAX + 1];
    char                 user_name[NDFS_NAME_MAX + 1]; /**< Resolved at load time. */
    uint8_t              user_index;
    uint8_t              file_type;    /**< 0=DATA, 1=PROG, 2=SYMB, 3=TEXT */
    uint32_t             pages_in_file;
    uint32_t             bytes_in_file;
    ndfs_block_pointer_t file_pointer;
    uint16_t             access_bits;
} ndfs_object_entry_t;

/**
 * Parse an object entry from 64 bytes at offset.
 * @return NDFS_OK, NDFS_ERR_TOO_SMALL, or NDFS_ERR_NOT_FOUND
 *         (if the in-use bit is not set).
 */
ndfs_error_t ndfs_oe_from_bytes(const uint8_t *data, size_t data_len,
                                size_t offset, ndfs_object_entry_t *out);

/**
 * Serialize an object entry to 64 bytes at offset in buffer.
 * @param entry   The entry to serialize.
 * @param buffer  Destination buffer.
 * @param offset  Byte offset into buffer.
 */
ndfs_error_t ndfs_oe_to_bytes(const ndfs_object_entry_t *entry,
                              uint8_t *buffer, size_t buf_len,
                              size_t offset);

/**
 * Get full name in "NAME:TYPE" format.
 * @param buf  Must be at least NDFS_NAME_MAX + NDFS_TYPE_MAX + 2 bytes.
 */
void ndfs_oe_full_name(const ndfs_object_entry_t *entry, char *buf, size_t buf_len);

/** Initialize a new empty object entry. */
void ndfs_oe_init(ndfs_object_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_OBJECT_ENTRY_H */
