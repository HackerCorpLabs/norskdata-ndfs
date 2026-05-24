/**
 * NDFS object entry implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/object_entry.h>
#include "endian_util.h"
#include "ndfs_name.h"
#include <string.h>
#include <stdio.h>

void ndfs_oe_init(ndfs_object_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->header = NDFS_OBJECT_IN_USE;
    entry->type[0] = 'D'; entry->type[1] = 'A';
    entry->type[2] = 'T'; entry->type[3] = 'A';
    entry->type[4] = '\0';
}

ndfs_error_t ndfs_oe_from_bytes(const uint8_t *data, size_t data_len,
                                size_t offset, ndfs_object_entry_t *out)
{
    uint32_t bytes_minus_one;

    if (!data || !out) return NDFS_ERR_NULL_PTR;
    if (data_len < offset + NDFS_ENTRY_SIZE) return NDFS_ERR_TOO_SMALL;

    /* Check in-use bit */
    if ((data[offset] & NDFS_OBJECT_IN_USE) == 0)
        return NDFS_ERR_NOT_FOUND;

    ndfs_oe_init(out);

    out->header = data[offset];

    /* Object name (16 bytes at offset+2) */
    ndfs_read_name(data, offset + 2, NDFS_NAME_MAX, out->object_name);

    /* File type (4 bytes at offset+18) */
    ndfs_read_name(data, offset + 18, NDFS_TYPE_MAX, out->type);
    if (out->type[0] == '\0') {
        out->type[0] = 'D'; out->type[1] = 'A';
        out->type[2] = 'T'; out->type[3] = 'A';
        out->type[4] = '\0';
    }

    /* File type code (byte 32) */
    out->file_type = data[offset + 32];

    /* User index (byte 34) */
    out->user_index = data[offset + 34];

    /* Pages in file (bytes 52-55, big-endian) */
    out->pages_in_file = ndfs_read_u32be(data, offset + 52);

    /* Bytes in file (bytes 56-59, big-endian) + 1 */
    bytes_minus_one = ndfs_read_u32be(data, offset + 56);
    out->bytes_in_file = bytes_minus_one + 1;

    /* File pointer (bytes 60-63) */
    out->file_pointer = ndfs_bp_from_bytes(data, offset + 60);

    out->user_name[0] = '\0';

    return NDFS_OK;
}

ndfs_error_t ndfs_oe_to_bytes(const ndfs_object_entry_t *entry,
                              uint8_t *buffer, size_t buf_len,
                              size_t offset)
{
    uint32_t bytes_minus_one;

    if (!entry || !buffer) return NDFS_ERR_NULL_PTR;
    if (buf_len < offset + NDFS_ENTRY_SIZE) return NDFS_ERR_TOO_SMALL;

    /* Clear the entry area */
    memset(buffer + offset, 0, NDFS_ENTRY_SIZE);

    /* Header (0x80 = in use) */
    buffer[offset] = NDFS_OBJECT_IN_USE;

    /* Object name */
    ndfs_write_name(buffer, offset + 2, entry->object_name, NDFS_NAME_MAX);

    /* File type string */
    ndfs_write_name(buffer, offset + 18, entry->type, NDFS_TYPE_MAX);

    /* File type code */
    buffer[offset + 32] = entry->file_type;

    /* User index */
    buffer[offset + 34] = entry->user_index;

    /* Pages in file */
    ndfs_write_u32be(buffer, offset + 52, entry->pages_in_file);

    /* Bytes in file - 1 */
    bytes_minus_one = entry->bytes_in_file > 0 ? entry->bytes_in_file - 1 : 0;
    ndfs_write_u32be(buffer, offset + 56, bytes_minus_one);

    /* File pointer */
    ndfs_bp_to_bytes(&entry->file_pointer, buffer, offset + 60);

    return NDFS_OK;
}

void ndfs_oe_full_name(const ndfs_object_entry_t *entry, char *buf, size_t buf_len)
{
    if (entry->type[0] != '\0') {
        snprintf(buf, buf_len, "%s:%s", entry->object_name, entry->type);
    } else {
        snprintf(buf, buf_len, "%s", entry->object_name);
    }
}
