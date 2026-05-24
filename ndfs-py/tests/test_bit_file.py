"""
Tests for ndfs.bit_file -- BitFile allocation bitmap.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
import pytest
from ndfs.bit_file import BitFile
from ndfs.constants import FIRST_ALLOCATABLE_BLOCK


class TestBitFileInitialize:
    def test_creates_bitmap_of_correct_size(self):
        bf = BitFile()
        bf.initialize(100)
        assert bf.total_pages == 100
        assert len(bf.get_bitmap_data()) == 13  # ceil(100/8)

    def test_starts_with_all_blocks_free(self):
        bf = BitFile()
        bf.initialize(64)
        assert bf.calc_used_pages() == 0
        assert bf.get_free_pages() == 64


class TestBitFileMarkCheck:
    def test_marks_a_block_as_used(self):
        bf = BitFile()
        bf.initialize(64)
        bf.mark_block_used(10)
        assert bf.is_block_used(10) is True
        assert bf.is_block_used(9) is False
        assert bf.is_block_used(11) is False

    def test_marks_a_block_as_free(self):
        bf = BitFile()
        bf.initialize(64)
        bf.mark_block_used(10)
        bf.mark_block_free(10)
        assert bf.is_block_used(10) is False

    def test_handles_bit_boundaries_correctly(self):
        bf = BitFile()
        bf.initialize(32)
        bf.mark_block_used(0)
        bf.mark_block_used(7)
        bf.mark_block_used(8)
        bf.mark_block_used(15)
        assert bf.is_block_used(0) is True
        assert bf.is_block_used(7) is True
        assert bf.is_block_used(8) is True
        assert bf.is_block_used(15) is True
        assert bf.is_block_used(1) is False
        assert bf.is_block_used(6) is False
        assert bf.calc_used_pages() == 4

    def test_throws_on_out_of_range_mark_block_used(self):
        bf = BitFile()
        bf.initialize(16)
        with pytest.raises(IndexError):
            bf.mark_block_used(16)

    def test_returns_false_for_out_of_range_is_block_used(self):
        bf = BitFile()
        bf.initialize(16)
        assert bf.is_block_used(100) is False


class TestBitFileFindFirstFreeBlock:
    def test_skips_system_blocks_0_to_6(self):
        bf = BitFile()
        bf.initialize(32)
        for i in range(7):
            bf.mark_block_used(i)
        assert bf.find_first_free_block() == 7

    def test_finds_first_free_after_some_used_blocks(self):
        bf = BitFile()
        bf.initialize(32)
        for i in range(12):
            bf.mark_block_used(i)
        assert bf.find_first_free_block() == 12

    def test_returns_minus_1_when_all_blocks_used(self):
        bf = BitFile()
        bf.initialize(16)
        for i in range(16):
            bf.mark_block_used(i)
        assert bf.find_first_free_block() == -1


class TestBitFileFindFreeBlockRange:
    def test_finds_contiguous_range(self):
        bf = BitFile()
        bf.initialize(32)
        bf.mark_block_used(10)
        start = bf.find_free_block_range(5)
        assert start >= 0

    def test_returns_minus_1_when_no_range_exists(self):
        bf = BitFile()
        bf.initialize(16)
        for i in range(0, 16, 2):
            bf.mark_block_used(i)
        assert bf.find_free_block_range(2) == -1

    def test_returns_minus_1_for_zero_blocks_needed(self):
        bf = BitFile()
        bf.initialize(16)
        assert bf.find_free_block_range(0) == -1


class TestBitFileAllocateBlocks:
    def test_allocates_range_and_marks_as_used(self):
        bf = BitFile()
        bf.initialize(32)
        ok = bf.allocate_blocks(10, 3)
        assert ok is True
        assert bf.is_block_used(10) is True
        assert bf.is_block_used(11) is True
        assert bf.is_block_used(12) is True
        assert bf.is_block_used(9) is False
        assert bf.is_block_used(13) is False

    def test_refuses_to_allocate_system_blocks(self):
        bf = BitFile()
        bf.initialize(32)
        assert bf.allocate_blocks(0, 3) is False
        assert bf.allocate_blocks(5, 1) is False

    def test_refuses_if_any_block_already_used(self):
        bf = BitFile()
        bf.initialize(32)
        bf.mark_block_used(11)
        assert bf.allocate_blocks(10, 3) is False

    def test_refuses_if_range_exceeds_total_pages(self):
        bf = BitFile()
        bf.initialize(16)
        assert bf.allocate_blocks(10, 10) is False


class TestBitFileFreeBlocks:
    def test_frees_a_range(self):
        bf = BitFile()
        bf.initialize(32)
        bf.allocate_blocks(10, 5)
        bf.free_blocks(10, 5)
        for i in range(10, 15):
            assert bf.is_block_used(i) is False


class TestBitFileLoadBitmap:
    def test_loads_raw_bitmap_data(self):
        bf = BitFile()
        bf.initialize(16)
        raw = bytes([0xFF, 0x00])  # first 8 used, next 8 free
        bf.load_bitmap(raw)
        for i in range(8):
            assert bf.is_block_used(i) is True
        for i in range(8, 16):
            assert bf.is_block_used(i) is False


class TestBitFileToPageBuffers:
    def test_produces_page_aligned_output(self):
        bf = BitFile()
        bf.initialize(100)
        bf.mark_block_used(10)
        pages = bf.to_page_buffers()
        assert len(pages) == 1
        assert len(pages[0]) == 2048
