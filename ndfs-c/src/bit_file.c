/**
 * NDFS bit file (allocation bitmap) implementation.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#include <ndfs/bit_file.h>
#include <string.h>

ndfs_error_t ndfs_bf_init(ndfs_bit_file_t *bf, uint32_t total_pages)
{
    size_t bytes;

    if (!bf) return NDFS_ERR_NULL_PTR;

    ndfs_bf_destroy(bf);

    bf->total_pages = total_pages;
    bytes = (total_pages + 7) / 8;
    bf->bitmap = (uint8_t *)calloc(bytes, 1);
    if (!bf->bitmap) return NDFS_ERR_ALLOC;
    bf->bitmap_size = bytes;

    memset(&bf->index_pointer, 0, sizeof(bf->index_pointer));
    return NDFS_OK;
}

void ndfs_bf_destroy(ndfs_bit_file_t *bf)
{
    if (!bf) return;
    if (bf->bitmap) {
        free(bf->bitmap);
        bf->bitmap = NULL;
    }
    bf->bitmap_size = 0;
    bf->total_pages = 0;
}

ndfs_error_t ndfs_bf_load(ndfs_bit_file_t *bf, const uint8_t *data, size_t len)
{
    if (!bf || !data) return NDFS_ERR_NULL_PTR;
    if (!bf->bitmap) return NDFS_ERR_INVALID_ARG;

    {
        size_t copy_len = len < bf->bitmap_size ? len : bf->bitmap_size;
        memcpy(bf->bitmap, data, copy_len);
    }
    return NDFS_OK;
}

bool ndfs_bf_is_used(const ndfs_bit_file_t *bf, uint32_t block_id)
{
    size_t byte_idx;
    uint8_t bit_idx;

    if (!bf || !bf->bitmap || block_id >= bf->total_pages) return false;
    byte_idx = block_id >> 3;
    bit_idx  = (uint8_t)(block_id & 7);
    return (bf->bitmap[byte_idx] & (1u << bit_idx)) != 0;
}

ndfs_error_t ndfs_bf_mark_used(ndfs_bit_file_t *bf, uint32_t block_id)
{
    size_t byte_idx;
    uint8_t bit_idx;

    if (!bf) return NDFS_ERR_NULL_PTR;
    if (!bf->bitmap || block_id >= bf->total_pages) return NDFS_ERR_OUT_OF_RANGE;

    byte_idx = block_id >> 3;
    bit_idx  = (uint8_t)(block_id & 7);
    bf->bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
    return NDFS_OK;
}

ndfs_error_t ndfs_bf_mark_free(ndfs_bit_file_t *bf, uint32_t block_id)
{
    size_t byte_idx;
    uint8_t bit_idx;

    if (!bf) return NDFS_ERR_NULL_PTR;
    if (!bf->bitmap || block_id >= bf->total_pages) return NDFS_ERR_OUT_OF_RANGE;

    byte_idx = block_id >> 3;
    bit_idx  = (uint8_t)(block_id & 7);
    bf->bitmap[byte_idx] &= (uint8_t)~(1u << bit_idx);
    return NDFS_OK;
}

uint32_t ndfs_bf_count_used(const ndfs_bit_file_t *bf)
{
    uint32_t count = 0;
    uint32_t i;

    if (!bf || !bf->bitmap) return 0;
    for (i = 0; i < bf->total_pages; i++) {
        if (ndfs_bf_is_used(bf, i)) count++;
    }
    return count;
}

uint32_t ndfs_bf_count_free(const ndfs_bit_file_t *bf)
{
    if (!bf) return 0;
    return bf->total_pages - ndfs_bf_count_used(bf);
}

ndfs_error_t ndfs_bf_find_free(const ndfs_bit_file_t *bf, uint32_t *out_block)
{
    uint32_t i;

    if (!bf || !out_block) return NDFS_ERR_NULL_PTR;

    for (i = NDFS_FIRST_ALLOC_BLOCK; i < bf->total_pages; i++) {
        if (!ndfs_bf_is_used(bf, i)) {
            *out_block = i;
            return NDFS_OK;
        }
    }
    return NDFS_ERR_NO_SPACE;
}

ndfs_error_t ndfs_bf_find_free_range(const ndfs_bit_file_t *bf,
                                     uint32_t count,
                                     uint32_t *out_start)
{
    uint32_t consecutive = 0;
    uint32_t range_start = 0;
    uint32_t i;

    if (!bf || !out_start) return NDFS_ERR_NULL_PTR;
    if (count == 0 || count > bf->total_pages) return NDFS_ERR_INVALID_ARG;

    for (i = 0; i < bf->total_pages; i++) {
        if (!ndfs_bf_is_used(bf, i)) {
            if (consecutive == 0) range_start = i;
            consecutive++;
            if (consecutive >= count) {
                *out_start = range_start;
                return NDFS_OK;
            }
        } else {
            consecutive = 0;
        }
    }
    return NDFS_ERR_NO_SPACE;
}

ndfs_error_t ndfs_bf_allocate(ndfs_bit_file_t *bf,
                              uint32_t start_block,
                              uint32_t count)
{
    uint32_t i;

    if (!bf) return NDFS_ERR_NULL_PTR;
    if (start_block < NDFS_FIRST_ALLOC_BLOCK) return NDFS_ERR_INVALID_ARG;
    if (start_block + count > bf->total_pages) return NDFS_ERR_OUT_OF_RANGE;

    /* Check all blocks are free */
    for (i = start_block; i < start_block + count; i++) {
        if (ndfs_bf_is_used(bf, i)) return NDFS_ERR_ALREADY_EXISTS;
    }

    /* Mark all blocks as used */
    for (i = start_block; i < start_block + count; i++) {
        ndfs_bf_mark_used(bf, i);
    }
    return NDFS_OK;
}

void ndfs_bf_free_range(ndfs_bit_file_t *bf,
                        uint32_t start_block,
                        uint32_t count)
{
    uint32_t i;

    if (!bf) return;
    for (i = start_block; i < start_block + count && i < bf->total_pages; i++) {
        ndfs_bf_mark_free(bf, i);
    }
}

ndfs_error_t ndfs_bf_get_data(const ndfs_bit_file_t *bf,
                              const uint8_t **out_data,
                              size_t *out_len)
{
    if (!bf || !out_data || !out_len) return NDFS_ERR_NULL_PTR;
    *out_data = bf->bitmap;
    *out_len  = bf->bitmap_size;
    return NDFS_OK;
}
