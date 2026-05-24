/**
 * NDFS bit file (allocation bitmap): one bit per page, 0=free, 1=used.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_BIT_FILE_H
#define NDFS_BIT_FILE_H

#include "block_pointer.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Allocation bitmap. */
typedef struct {
    ndfs_block_pointer_t index_pointer;
    uint32_t             total_pages;
    uint8_t             *bitmap;      /**< Owned by the struct. */
    size_t               bitmap_size; /**< Bytes in bitmap.     */
} ndfs_bit_file_t;

/** Initialize an empty bitmap for the given total page count. */
ndfs_error_t ndfs_bf_init(ndfs_bit_file_t *bf, uint32_t total_pages);

/** Release bitmap memory. Safe to call on zeroed or already-destroyed struct. */
void ndfs_bf_destroy(ndfs_bit_file_t *bf);

/** Load bitmap data from raw bytes (copies the data). */
ndfs_error_t ndfs_bf_load(ndfs_bit_file_t *bf, const uint8_t *data, size_t len);

/** Check if a block is marked as used. */
bool ndfs_bf_is_used(const ndfs_bit_file_t *bf, uint32_t block_id);

/** Mark a block as used. */
ndfs_error_t ndfs_bf_mark_used(ndfs_bit_file_t *bf, uint32_t block_id);

/** Mark a block as free. */
ndfs_error_t ndfs_bf_mark_free(ndfs_bit_file_t *bf, uint32_t block_id);

/** Count total used pages. */
uint32_t ndfs_bf_count_used(const ndfs_bit_file_t *bf);

/** Get number of free pages. */
uint32_t ndfs_bf_count_free(const ndfs_bit_file_t *bf);

/**
 * Find the first free block (starting from block 7).
 * @param out_block  Receives the block ID.
 * @return NDFS_OK or NDFS_ERR_NO_SPACE.
 */
ndfs_error_t ndfs_bf_find_free(const ndfs_bit_file_t *bf, uint32_t *out_block);

/**
 * Find a contiguous range of free blocks.
 * @param count      Number of contiguous blocks needed.
 * @param out_start  Receives the starting block ID.
 * @return NDFS_OK or NDFS_ERR_NO_SPACE.
 */
ndfs_error_t ndfs_bf_find_free_range(const ndfs_bit_file_t *bf,
                                     uint32_t count,
                                     uint32_t *out_start);

/**
 * Allocate (mark as used) a range of blocks.
 * Blocks 0-6 cannot be allocated. Fails if any block is already used.
 */
ndfs_error_t ndfs_bf_allocate(ndfs_bit_file_t *bf,
                              uint32_t start_block,
                              uint32_t count);

/** Free a range of blocks. */
void ndfs_bf_free_range(ndfs_bit_file_t *bf,
                        uint32_t start_block,
                        uint32_t count);

/**
 * Get a pointer to the raw bitmap data (not a copy).
 * @param out_data  Receives pointer to internal bitmap.
 * @param out_len   Receives bitmap length in bytes.
 */
ndfs_error_t ndfs_bf_get_data(const ndfs_bit_file_t *bf,
                              const uint8_t **out_data,
                              size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_BIT_FILE_H */
