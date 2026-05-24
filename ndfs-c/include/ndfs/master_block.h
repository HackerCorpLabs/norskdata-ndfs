/**
 * NDFS master block: the 32-byte structure at offset 2016 of page 0.
 * Also handles the 16-byte extended info block at offset 2000.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_MASTER_BLOCK_H
#define NDFS_MASTER_BLOCK_H

#include "block_pointer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parsed master block + extended info from page 0. */
typedef struct {
    char                       directory_name[NDFS_NAME_MAX + 1];
    ndfs_block_pointer_t       object_file_ptr;
    ndfs_block_pointer_t       user_file_ptr;
    ndfs_block_pointer_t       bit_file_ptr;
    uint32_t                   unreserved_pages;
    uint32_t                   image_size; /**< total pages */

    /* Extended info fields */
    uint16_t                   ext_checksum;
    uint16_t                   ext_reserved1;
    uint16_t                   ext_reserved2;
    uint16_t                   ext_reserved3;
    uint16_t                   ext_flag_word;
    uint16_t                   ext_last_system_number;
    uint32_t                   ext_pages_available;
    uint16_t                   ext_calculated_checksum;
    bool                       ext_valid;
    ndfs_checksum_validation_t checksum_state;
    bool                       has_flomon;
} ndfs_master_block_t;

/**
 * Parse a master block (and extended info) from a full page 0 buffer.
 * @param page_data  Buffer of at least NDFS_PAGE_SIZE bytes.
 * @param out        Receives the parsed master block.
 * @return NDFS_OK or error code.
 */
ndfs_error_t ndfs_mb_parse(const uint8_t *page_data, ndfs_master_block_t *out);

/**
 * Write master block fields into a page 0 buffer at the standard offset.
 * @param mb         The master block to serialize.
 * @param page_data  Buffer of at least NDFS_PAGE_SIZE bytes.
 * @return NDFS_OK or error code.
 */
ndfs_error_t ndfs_mb_write(const ndfs_master_block_t *mb, uint8_t *page_data);

/**
 * Write extended info into a page 0 buffer.
 * Recalculates the checksum.
 */
ndfs_error_t ndfs_mb_write_extended(const ndfs_master_block_t *mb, uint8_t *page_data);

/**
 * Check if the master block is valid.
 */
bool ndfs_mb_is_valid(const ndfs_master_block_t *mb);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_MASTER_BLOCK_H */
