"""
NDFS bit file (allocation bitmap): one bit per page, 0=free, 1=used.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

import math
from typing import List, Optional, Union

from ndfs.constants import NDFS_PAGE_SIZE, FIRST_ALLOCATABLE_BLOCK
from ndfs.block_pointer import BlockPointer

_BufType = Union[bytes, bytearray, memoryview]


class BitFile:
    """NDFS allocation bitmap manager."""

    __slots__ = ("index_pointer", "total_pages", "_bitmap")

    def __init__(self) -> None:
        self.index_pointer: Optional[BlockPointer] = None
        self.total_pages: int = 0
        self._bitmap: Optional[bytearray] = None

    def initialize(self, total_pages: int) -> None:
        """Initialize a new empty bitmap for the given total page count."""
        self.total_pages = total_pages
        bitmap_bytes = math.ceil(total_pages / 8)
        self._bitmap = bytearray(bitmap_bytes)

    def load_bitmap(self, data: _BufType) -> None:
        """Load bitmap data from raw bytes."""
        self._bitmap = bytearray(data)

    def is_block_used(self, block_id: int) -> bool:
        """Check if a block is marked as used."""
        if self._bitmap is None or block_id >= self.total_pages:
            return False
        byte_index = block_id >> 3
        bit_index = block_id & 7
        return (self._bitmap[byte_index] & (1 << bit_index)) != 0

    def mark_block_used(self, block_id: int) -> None:
        """Mark a block as used."""
        if self._bitmap is None or block_id >= self.total_pages:
            raise IndexError(f"Block ID {block_id} out of range")
        byte_index = block_id >> 3
        bit_index = block_id & 7
        self._bitmap[byte_index] |= 1 << bit_index

    def mark_block_free(self, block_id: int) -> None:
        """Mark a block as free."""
        if self._bitmap is None or block_id >= self.total_pages:
            raise IndexError(f"Block ID {block_id} out of range")
        byte_index = block_id >> 3
        bit_index = block_id & 7
        self._bitmap[byte_index] &= ~(1 << bit_index)

    def calc_used_pages(self) -> int:
        """Count total used pages."""
        if self._bitmap is None:
            return 0
        count = 0
        for i in range(self.total_pages):
            if self.is_block_used(i):
                count += 1
        return count

    def get_free_pages(self) -> int:
        """Get number of free pages."""
        return self.total_pages - self.calc_used_pages()

    def find_first_free_block(self) -> int:
        """Find the first free block, starting from block 7 (blocks 0-6 are system).

        Returns the block ID or -1 if no free block exists.
        """
        for i in range(FIRST_ALLOCATABLE_BLOCK, self.total_pages):
            if not self.is_block_used(i):
                return i
        return -1

    def find_free_block_range(self, blocks_needed: int) -> int:
        """Find a contiguous range of free blocks.

        Returns the starting block ID or -1 if no range found.
        """
        if blocks_needed == 0 or blocks_needed > self.total_pages:
            return -1

        consecutive_free = 0
        range_start = 0

        for i in range(self.total_pages):
            if not self.is_block_used(i):
                if consecutive_free == 0:
                    range_start = i
                consecutive_free += 1
                if consecutive_free >= blocks_needed:
                    return range_start
            else:
                consecutive_free = 0
        return -1

    def allocate_blocks(self, start_block: int, count: int) -> bool:
        """Allocate a range of blocks (mark as used).

        Blocks 0-6 cannot be allocated. Returns False if any block is already used.
        """
        if start_block < FIRST_ALLOCATABLE_BLOCK:
            return False
        if start_block + count > self.total_pages:
            return False

        # Check all blocks are free
        for i in range(start_block, start_block + count):
            if self.is_block_used(i):
                return False

        # Mark all blocks as used
        for i in range(start_block, start_block + count):
            self.mark_block_used(i)
        return True

    def free_blocks(self, start_block: int, count: int) -> None:
        """Free a range of blocks."""
        for i in range(start_block, min(start_block + count, self.total_pages)):
            self.mark_block_free(i)

    def get_bitmap_data(self) -> bytes:
        """Get a copy of the raw bitmap data."""
        if self._bitmap is None:
            return b""
        return bytes(self._bitmap)

    def to_page_buffers(self) -> List[bytearray]:
        """Write bitmap data into page-aligned buffers for disk writing.

        Returns a list of page buffers to write starting at the pointer's block ID.
        """
        if self._bitmap is None:
            return []

        pages_needed = math.ceil(len(self._bitmap) / NDFS_PAGE_SIZE)
        pages: List[bytearray] = []

        for i in range(pages_needed):
            page = bytearray(NDFS_PAGE_SIZE)
            src_offset = i * NDFS_PAGE_SIZE
            bytes_to_copy = min(NDFS_PAGE_SIZE, len(self._bitmap) - src_offset)
            if bytes_to_copy > 0:
                page[0:bytes_to_copy] = self._bitmap[src_offset:src_offset + bytes_to_copy]
            pages.append(page)
        return pages
