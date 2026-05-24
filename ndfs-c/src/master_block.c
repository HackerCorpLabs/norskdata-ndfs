/**
 * NDFS master block parse/write implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/master_block.h>
#include "endian_util.h"
#include "ndfs_name.h"
#include <string.h>

/* ── Detect FLOMON boot format ───────────────────────────────────── */

static bool detect_flomon(const uint8_t *page_data)
{
    int excl_pos = -1;
    int i;
    int limit = 256;

    for (i = 0; i < limit; i++) {
        if (page_data[i] == 0x21) {
            excl_pos = i;
            break;
        }
    }
    if (excl_pos < 0) return false;

    {
        int after = excl_pos + 1;
        uint16_t addr, count;
        if (after + 4 > (int)NDFS_PAGE_SIZE) return false;
        addr  = ndfs_read_u16be(page_data, (size_t)after);
        count = ndfs_read_u16be(page_data, (size_t)(after + 2));
        return addr == 0 && count == 0;
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

ndfs_error_t ndfs_mb_parse(const uint8_t *page_data, ndfs_master_block_t *out)
{
    size_t off, ext;
    uint16_t pages_lo, pages_hi, calculated;
    bool checksum_non_zero, checksum_ok;

    if (!page_data || !out) return NDFS_ERR_NULL_PTR;

    memset(out, 0, sizeof(*out));
    off = NDFS_MASTER_BLOCK_OFFSET;

    /* Directory name (16 bytes, terminated by 0x27) */
    ndfs_read_name(page_data, off, NDFS_NAME_MAX, out->directory_name);

    /* Block pointers */
    out->object_file_ptr = ndfs_bp_from_bytes(page_data, off + 0x10);
    out->user_file_ptr   = ndfs_bp_from_bytes(page_data, off + 0x14);
    out->bit_file_ptr    = ndfs_bp_from_bytes(page_data, off + 0x18);

    /* Unreserved pages */
    out->unreserved_pages = ndfs_read_u32be(page_data, off + 0x1C);

    /* ── Extended info (bytes 2000-2015) ── */
    ext = NDFS_EXTENDED_INFO_OFFSET;
    out->ext_checksum           = ndfs_read_u16be(page_data, ext);
    out->ext_reserved1          = ndfs_read_u16be(page_data, ext + 2);
    out->ext_reserved2          = ndfs_read_u16be(page_data, ext + 4);
    out->ext_reserved3          = ndfs_read_u16be(page_data, ext + 6);
    out->ext_flag_word          = ndfs_read_u16be(page_data, ext + 8);
    out->ext_last_system_number = ndfs_read_u16be(page_data, ext + 10);
    out->ext_pages_available    = ndfs_read_u32be(page_data, ext + 12);

    /* Calculate checksum */
    pages_lo = (uint16_t)(out->ext_pages_available & 0xFFFF);
    pages_hi = (uint16_t)((out->ext_pages_available >> 16) & 0xFFFF);
    calculated = (uint16_t)(
        (pages_lo ^ pages_hi ^ out->ext_flag_word ^
         out->ext_reserved1 ^ out->ext_reserved2 ^ out->ext_reserved3)
        + out->ext_last_system_number);
    out->ext_calculated_checksum = calculated;

    /* Determine checksum validation state */
    if (out->ext_checksum == calculated) {
        out->checksum_state = NDFS_CHECKSUM_VALID;
    } else if ((out->ext_checksum & 0xFF) == (calculated & 0xFF) &&
               (out->ext_checksum & 0xFF00) == 0) {
        out->checksum_state = NDFS_CHECKSUM_VALID_LOW_BYTE;
    } else {
        out->checksum_state = NDFS_CHECKSUM_INVALID;
    }

    /* Detect FLOMON */
    out->has_flomon = detect_flomon(page_data);

    /* Extended info validity */
    if (out->has_flomon) {
        out->ext_valid = false;
    } else {
        checksum_non_zero = out->ext_checksum != 0;
        checksum_ok = (out->checksum_state == NDFS_CHECKSUM_VALID ||
                       out->checksum_state == NDFS_CHECKSUM_VALID_LOW_BYTE);
        out->ext_valid = checksum_non_zero && checksum_ok;
    }

    return NDFS_OK;
}

bool ndfs_mb_is_valid(const ndfs_master_block_t *mb)
{
    size_t i;
    bool has_valid_ptr;

    if (!mb) return false;

    /* Check directory name is printable ASCII */
    if (mb->directory_name[0] != '\0') {
        for (i = 0; mb->directory_name[i] != '\0'; i++) {
            unsigned char c = (unsigned char)mb->directory_name[i];
            if (c < 0x20 || c > 0x7E) return false;
        }
    }

    /* At least one pointer must be valid, or a directory name must exist */
    has_valid_ptr = false;
    if (ndfs_bp_is_valid(&mb->object_file_ptr)) has_valid_ptr = true;
    if (ndfs_bp_is_valid(&mb->user_file_ptr))   has_valid_ptr = true;
    if (ndfs_bp_is_valid(&mb->bit_file_ptr))     has_valid_ptr = true;

    return has_valid_ptr || mb->directory_name[0] != '\0';
}

ndfs_error_t ndfs_mb_write(const ndfs_master_block_t *mb, uint8_t *page_data)
{
    size_t off;

    if (!mb || !page_data) return NDFS_ERR_NULL_PTR;

    off = NDFS_MASTER_BLOCK_OFFSET;

    /* Clear the master block area */
    memset(page_data + off, 0, NDFS_MASTER_BLOCK_SIZE);

    /* Directory name */
    ndfs_write_name(page_data, off, mb->directory_name, NDFS_NAME_MAX);

    /* Block pointers */
    ndfs_bp_to_bytes(&mb->object_file_ptr, page_data, off + 0x10);
    ndfs_bp_to_bytes(&mb->user_file_ptr,   page_data, off + 0x14);
    ndfs_bp_to_bytes(&mb->bit_file_ptr,    page_data, off + 0x18);

    /* Unreserved pages */
    ndfs_write_u32be(page_data, off + 0x1C, mb->unreserved_pages);

    return NDFS_OK;
}

ndfs_error_t ndfs_mb_write_extended(const ndfs_master_block_t *mb, uint8_t *page_data)
{
    size_t ext;
    uint16_t pages_lo, pages_hi, checksum;

    if (!mb || !page_data) return NDFS_ERR_NULL_PTR;

    ext = NDFS_EXTENDED_INFO_OFFSET;

    /* Calculate checksum */
    pages_lo = (uint16_t)(mb->ext_pages_available & 0xFFFF);
    pages_hi = (uint16_t)((mb->ext_pages_available >> 16) & 0xFFFF);
    checksum = (uint16_t)(
        (pages_lo ^ pages_hi ^ mb->ext_flag_word ^
         mb->ext_reserved1 ^ mb->ext_reserved2 ^ mb->ext_reserved3)
        + mb->ext_last_system_number);

    ndfs_write_u16be(page_data, ext,      checksum);
    ndfs_write_u16be(page_data, ext + 2,  mb->ext_reserved1);
    ndfs_write_u16be(page_data, ext + 4,  mb->ext_reserved2);
    ndfs_write_u16be(page_data, ext + 6,  mb->ext_reserved3);
    ndfs_write_u16be(page_data, ext + 8,  mb->ext_flag_word);
    ndfs_write_u16be(page_data, ext + 10, mb->ext_last_system_number);
    ndfs_write_u32be(page_data, ext + 12, mb->ext_pages_available);

    return NDFS_OK;
}
