/**
 * NDFS boot loader detection implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/boot_loader.h>
#include "endian_util.h"
#include <string.h>
#include <stdlib.h>

/* ---- Internal: access page 0 data from filesystem -------------------- */

/* We need access to the raw data pointer.  The filesystem struct is opaque,
   but ndfs_to_buffer() gives us a copy.  For efficiency we read page 0
   via ndfs_to_buffer and only copy 1 page.  Alternatively, since the
   master block is at a known offset, we use ndfs_to_buffer. */

static ndfs_error_t get_page0(ndfs_filesystem_t *fs, uint8_t *buf)
{
    uint8_t *full;
    size_t full_size;
    ndfs_error_t err;

    err = ndfs_to_buffer(fs, &full, &full_size);
    if (err != NDFS_OK) return err;

    if (full_size < NDFS_PAGE_SIZE) {
        free(full);
        return NDFS_ERR_TOO_SMALL;
    }
    memcpy(buf, full, NDFS_PAGE_SIZE);
    free(full);
    return NDFS_OK;
}

/* ---- BPUN/FLOMON detection ------------------------------------------- */

/**
 * Try to parse BPUN binary section starting after a '!' delimiter.
 * Returns true if valid BPUN or FLOMON was found.
 */
static bool try_parse_bpun(const uint8_t *data, size_t excl_pos,
                           ndfs_boot_code_t *code)
{
    size_t pos = excl_pos + 1;
    uint16_t address, count, file_checksum, calc_checksum;
    size_t data_bytes;
    size_t total_needed;
    size_t i;
    uint8_t *code_data;

    if (pos + 4 > NDFS_PAGE_SIZE) return false;

    address = ndfs_read_u16be(data, pos);
    pos += 2;
    count = ndfs_read_u16be(data, pos);
    pos += 2;

    data_bytes = (size_t)count * 2;
    total_needed = pos + data_bytes + 4; /* +4 for checksum and action */
    if (total_needed > NDFS_PAGE_SIZE) return false;

    /* Read code data and calculate checksum */
    code_data = NULL;
    calc_checksum = 0;
    if (data_bytes > 0) {
        code_data = (uint8_t *)malloc(data_bytes);
        if (!code_data) return false;

        for (i = 0; i < data_bytes; i += 2) {
            uint16_t word;
            code_data[i]     = data[pos + i];
            code_data[i + 1] = data[pos + i + 1];
            word = ndfs_read_u16be(data, pos + i);
            calc_checksum = (uint16_t)(calc_checksum + word);
        }
    }
    pos += data_bytes;

    /* Read file checksum */
    file_checksum = ndfs_read_u16be(data, pos);
    pos += 2;

    /* Check for FLOMON marker: address=0, count=0, checksum=0 */
    if (address == 0 && count == 0 && file_checksum == 0) {
        uint8_t word_count;
        uint8_t *flomon_data;
        size_t flomon_idx;

        free(code_data);

        if (pos >= NDFS_PAGE_SIZE) {
            code->format = NDFS_BOOT_FLOMON;
            code->data = NULL;
            code->data_size = 0;
            code->word_count = 0;
            code->checksum_valid = true;
            return true;
        }

        word_count = data[pos];
        pos++;

        code->format = NDFS_BOOT_FLOMON;
        code->word_count = word_count;
        code->start_address = 0;
        code->load_address = 0;
        code->checksum_valid = true;

        if (word_count == 0) {
            code->data = NULL;
            code->data_size = 0;
            return true;
        }

        /* FLOMON: each word stored as 00 HI 00 LO (4 bytes per word) */
        flomon_data = (uint8_t *)malloc((size_t)word_count * 2);
        if (!flomon_data) {
            code->data = NULL;
            code->data_size = 0;
            return true;
        }

        flomon_idx = 0;
        for (i = 0; i < (size_t)word_count; i++) {
            if (pos + 4 > NDFS_PAGE_SIZE) break;
            if (data[pos] != 0) break;     /* pad1 */
            flomon_data[flomon_idx++] = data[pos + 1]; /* HI */
            if (data[pos + 2] != 0) break; /* pad2 */
            flomon_data[flomon_idx++] = data[pos + 3]; /* LO */
            pos += 4;
        }

        code->data = flomon_data;
        code->data_size = flomon_idx;
        return true;
    }

    /* Standard BPUN format */
    code->format = NDFS_BOOT_BPUN;
    code->load_address = address;
    code->word_count = count;
    code->data = code_data;
    code->data_size = data_bytes;
    code->checksum_valid = (calc_checksum == file_checksum);

    /* Read action field (optional) */
    if (pos + 2 <= NDFS_PAGE_SIZE) {
        code->start_address = ndfs_read_u16be(data, pos);
    }

    return true;
}

/**
 * Check if page 0 contains raw binary boot code (heuristic).
 */
static bool is_valid_binary(const uint8_t *data)
{
    bool all_zero = true;
    bool all_ff = true;
    bool all_f6 = true;
    int i;
    int limit = 256;

    for (i = 0; i < limit; i++) {
        if (data[i] != 0x00) all_zero = false;
        if (data[i] != 0xFF) all_ff = false;
        if (data[i] != 0xF6) all_f6 = false;
        if (!all_zero && !all_ff && !all_f6) break;
    }

    if (all_zero || all_ff || all_f6) return false;

    /* If a '!' exists in first 512 bytes, it might be BPUN, not raw binary */
    for (i = 0; i < 512 && i < (int)NDFS_PAGE_SIZE; i++) {
        if (data[i] == 0x21) return false;
    }

    return true;
}

/* ---- Internal load from raw page 0 ---------------------------------- */

static ndfs_error_t load_from_page0(const uint8_t *page0, ndfs_boot_code_t *code)
{
    int i;
    int limit = 512;

    memset(code, 0, sizeof(*code));
    code->format = NDFS_BOOT_NONE;

    /* Try BPUN/FLOMON: find '!' delimiter */
    for (i = 0; i < limit && i < (int)NDFS_PAGE_SIZE; i++) {
        if (page0[i] == 0x21) {
            if (try_parse_bpun(page0, (size_t)i, code)) {
                return NDFS_OK;
            }
        }
    }

    /* Try raw binary */
    if (is_valid_binary(page0)) {
        code->format = NDFS_BOOT_BINARY;
        code->load_address = 0;
        code->start_address = 0;
        /* Copy entire page 0 as code data (up to master block area) */
        code->data_size = NDFS_MASTER_BLOCK_OFFSET;
        code->data = (uint8_t *)malloc(code->data_size);
        if (code->data) {
            memcpy(code->data, page0, code->data_size);
        } else {
            code->data_size = 0;
        }
        code->checksum_valid = false;
        return NDFS_OK;
    }

    return NDFS_OK; /* NDFS_BOOT_NONE */
}

/* ---- Public API ------------------------------------------------------ */

ndfs_error_t ndfs_detect_boot_format(ndfs_filesystem_t *fs,
                                     ndfs_boot_format_t *format)
{
    uint8_t page0[NDFS_PAGE_SIZE];
    ndfs_boot_code_t code;
    ndfs_error_t err;

    if (!fs || !format) return NDFS_ERR_NULL_PTR;

    err = get_page0(fs, page0);
    if (err != NDFS_OK) return err;

    err = load_from_page0(page0, &code);
    *format = code.format;
    ndfs_boot_code_destroy(&code);
    return err;
}

ndfs_error_t ndfs_load_boot_code(ndfs_filesystem_t *fs,
                                 ndfs_boot_code_t *code)
{
    uint8_t page0[NDFS_PAGE_SIZE];
    ndfs_error_t err;

    if (!fs || !code) return NDFS_ERR_NULL_PTR;

    err = get_page0(fs, page0);
    if (err != NDFS_OK) return err;

    return load_from_page0(page0, code);
}

ndfs_error_t ndfs_is_bootable(ndfs_filesystem_t *fs, bool *bootable)
{
    ndfs_boot_format_t fmt;
    ndfs_error_t err;

    if (!fs || !bootable) return NDFS_ERR_NULL_PTR;

    err = ndfs_detect_boot_format(fs, &fmt);
    if (err != NDFS_OK) return err;

    *bootable = (fmt != NDFS_BOOT_NONE);
    return NDFS_OK;
}

void ndfs_boot_code_destroy(ndfs_boot_code_t *bc)
{
    if (!bc) return;
    free(bc->data);
    bc->data = NULL;
    bc->data_size = 0;
}
