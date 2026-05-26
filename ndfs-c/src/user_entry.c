/**
 * NDFS user entry implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/user_entry.h>
#include "endian_util.h"
#include "ndfs_name.h"
#include <string.h>

void ndfs_ue_init(ndfs_user_entry_t *entry)
{
    int i;
    memset(entry, 0, sizeof(*entry));
    entry->default_file_access = 0x04FF;
    for (i = 0; i < NDFS_MAX_FRIENDS; i++) {
        entry->friends[i].bits = 0;
    }
}

ndfs_error_t ndfs_ue_from_bytes(const uint8_t *data, size_t data_len,
                                size_t offset, ndfs_user_entry_t *out)
{
    int i;

    if (!data || !out) return NDFS_ERR_NULL_PTR;
    if (data_len < offset + NDFS_ENTRY_SIZE) return NDFS_ERR_TOO_SMALL;

    /* Check valid user flag */
    if ((data[offset] & NDFS_USER_ENTRY_FLAG) != NDFS_USER_ENTRY_FLAG)
        return NDFS_ERR_NOT_FOUND;

    ndfs_ue_init(out);

    /* Preserve the verbatim 64 bytes so re-serialization never loses fields
     * we do not model (offsets 38-39 and 42-47, incl. byte 47 mxobl/acobl). */
    memcpy(out->raw, data + offset, NDFS_ENTRY_SIZE);
    out->has_raw = true;

    out->enter_count       = data[offset + 1];
    ndfs_read_name(data, offset + 2, NDFS_NAME_MAX, out->user_name);
    out->password          = ndfs_read_u16be(data, offset + 18);
    out->date_created      = ndfs_read_u32be(data, offset + 20);
    out->last_date_entered = ndfs_read_u32be(data, offset + 24);
    out->pages_reserved    = ndfs_read_u32be(data, offset + 28);
    out->pages_used        = ndfs_read_u32be(data, offset + 32);
    out->directory_index   = data[offset + 36];
    out->user_index        = data[offset + 37];
    /* Default file access is at offset 40 (verified against on-disk images
     * and the reference ndfs/RetroCore tools); offset 38-39 is unused. */
    out->default_file_access = ndfs_read_u16be(data, offset + 40);

    /* Parse friends (8 x 2 bytes starting at offset 48) */
    for (i = 0; i < NDFS_MAX_FRIENDS; i++) {
        out->friends[i] = ndfs_uf_from_bytes(data, offset + 48 + (size_t)i * 2);
    }

    return NDFS_OK;
}

void ndfs_ue_to_bytes(const ndfs_user_entry_t *entry, uint8_t *buf)
{
    int i;

    /* Base on the original on-disk bytes when available so unmodelled bytes
     * survive; otherwise start from zero. */
    if (entry->has_raw) {
        memcpy(buf, entry->raw, NDFS_ENTRY_SIZE);
    } else {
        memset(buf, 0, NDFS_ENTRY_SIZE);
    }

    buf[0] = NDFS_USER_ENTRY_FLAG;
    buf[1] = entry->enter_count;

    ndfs_write_name(buf, 2, entry->user_name, NDFS_NAME_MAX);
    ndfs_write_u16be(buf, 18, entry->password);
    ndfs_write_u32be(buf, 20, entry->date_created);
    ndfs_write_u32be(buf, 24, entry->last_date_entered);
    ndfs_write_u32be(buf, 28, entry->pages_reserved);
    ndfs_write_u32be(buf, 32, entry->pages_used);
    buf[36] = entry->directory_index;
    buf[37] = entry->user_index;
    ndfs_write_u16be(buf, 40, entry->default_file_access);

    for (i = 0; i < NDFS_MAX_FRIENDS; i++) {
        ndfs_uf_to_bytes(&entry->friends[i], buf, 48 + (size_t)i * 2);
    }
}
