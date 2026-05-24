import { describe, it, expect } from 'vitest';
import { BitFile } from '../src/bit-file.js';
import { FIRST_ALLOCATABLE_BLOCK } from '../src/constants.js';

describe('BitFile', () => {
  describe('initialize', () => {
    it('creates bitmap of correct size', () => {
      const bf = new BitFile();
      bf.initialize(100);
      expect(bf.totalPages).toBe(100);
      expect(bf.getBitmapData().length).toBe(13); // ceil(100/8)
    });

    it('starts with all blocks free', () => {
      const bf = new BitFile();
      bf.initialize(64);
      expect(bf.calcUsedPages()).toBe(0);
      expect(bf.getFreePages()).toBe(64);
    });
  });

  describe('mark/check operations', () => {
    it('marks a block as used', () => {
      const bf = new BitFile();
      bf.initialize(64);
      bf.markBlockUsed(10);
      expect(bf.isBlockUsed(10)).toBe(true);
      expect(bf.isBlockUsed(9)).toBe(false);
      expect(bf.isBlockUsed(11)).toBe(false);
    });

    it('marks a block as free', () => {
      const bf = new BitFile();
      bf.initialize(64);
      bf.markBlockUsed(10);
      bf.markBlockFree(10);
      expect(bf.isBlockUsed(10)).toBe(false);
    });

    it('handles bit boundaries correctly', () => {
      const bf = new BitFile();
      bf.initialize(32);
      // Mark bits at byte boundaries
      bf.markBlockUsed(0);
      bf.markBlockUsed(7);
      bf.markBlockUsed(8);
      bf.markBlockUsed(15);
      expect(bf.isBlockUsed(0)).toBe(true);
      expect(bf.isBlockUsed(7)).toBe(true);
      expect(bf.isBlockUsed(8)).toBe(true);
      expect(bf.isBlockUsed(15)).toBe(true);
      expect(bf.isBlockUsed(1)).toBe(false);
      expect(bf.isBlockUsed(6)).toBe(false);
      expect(bf.calcUsedPages()).toBe(4);
    });

    it('throws on out-of-range markBlockUsed', () => {
      const bf = new BitFile();
      bf.initialize(16);
      expect(() => bf.markBlockUsed(16)).toThrow();
    });

    it('returns false for out-of-range isBlockUsed', () => {
      const bf = new BitFile();
      bf.initialize(16);
      expect(bf.isBlockUsed(100)).toBe(false);
    });
  });

  describe('findFirstFreeBlock', () => {
    it('skips system blocks 0-6', () => {
      const bf = new BitFile();
      bf.initialize(32);
      // Mark blocks 0-6 as used (system)
      for (let i = 0; i < 7; i++) bf.markBlockUsed(i);
      expect(bf.findFirstFreeBlock()).toBe(7);
    });

    it('finds first free after some used blocks', () => {
      const bf = new BitFile();
      bf.initialize(32);
      for (let i = 0; i < 12; i++) bf.markBlockUsed(i);
      expect(bf.findFirstFreeBlock()).toBe(12);
    });

    it('returns -1 when all blocks used', () => {
      const bf = new BitFile();
      bf.initialize(16);
      for (let i = 0; i < 16; i++) bf.markBlockUsed(i);
      expect(bf.findFirstFreeBlock()).toBe(-1);
    });
  });

  describe('findFreeBlockRange', () => {
    it('finds contiguous range', () => {
      const bf = new BitFile();
      bf.initialize(32);
      bf.markBlockUsed(10);
      // Should find range before block 10 or after
      const start = bf.findFreeBlockRange(5);
      expect(start).toBeGreaterThanOrEqual(0);
    });

    it('returns -1 when no range exists', () => {
      const bf = new BitFile();
      bf.initialize(16);
      // Fragment the bitmap
      for (let i = 0; i < 16; i += 2) bf.markBlockUsed(i);
      expect(bf.findFreeBlockRange(2)).toBe(-1);
    });

    it('returns -1 for zero blocks needed', () => {
      const bf = new BitFile();
      bf.initialize(16);
      expect(bf.findFreeBlockRange(0)).toBe(-1);
    });
  });

  describe('allocateBlocks', () => {
    it('allocates range and marks as used', () => {
      const bf = new BitFile();
      bf.initialize(32);
      const ok = bf.allocateBlocks(10, 3);
      expect(ok).toBe(true);
      expect(bf.isBlockUsed(10)).toBe(true);
      expect(bf.isBlockUsed(11)).toBe(true);
      expect(bf.isBlockUsed(12)).toBe(true);
      expect(bf.isBlockUsed(9)).toBe(false);
      expect(bf.isBlockUsed(13)).toBe(false);
    });

    it('refuses to allocate system blocks (0-6)', () => {
      const bf = new BitFile();
      bf.initialize(32);
      expect(bf.allocateBlocks(0, 3)).toBe(false);
      expect(bf.allocateBlocks(5, 1)).toBe(false);
    });

    it('refuses if any block already used', () => {
      const bf = new BitFile();
      bf.initialize(32);
      bf.markBlockUsed(11);
      expect(bf.allocateBlocks(10, 3)).toBe(false);
    });

    it('refuses if range exceeds total pages', () => {
      const bf = new BitFile();
      bf.initialize(16);
      expect(bf.allocateBlocks(10, 10)).toBe(false);
    });
  });

  describe('freeBlocks', () => {
    it('frees a range', () => {
      const bf = new BitFile();
      bf.initialize(32);
      bf.allocateBlocks(10, 5);
      bf.freeBlocks(10, 5);
      for (let i = 10; i < 15; i++) {
        expect(bf.isBlockUsed(i)).toBe(false);
      }
    });
  });

  describe('loadBitmap', () => {
    it('loads raw bitmap data', () => {
      const bf = new BitFile();
      bf.initialize(16);
      const raw = new Uint8Array([0xff, 0x00]); // first 8 used, next 8 free
      bf.loadBitmap(raw);
      for (let i = 0; i < 8; i++) expect(bf.isBlockUsed(i)).toBe(true);
      for (let i = 8; i < 16; i++) expect(bf.isBlockUsed(i)).toBe(false);
    });
  });

  describe('toPageBuffers', () => {
    it('produces page-aligned output', () => {
      const bf = new BitFile();
      bf.initialize(100);
      bf.markBlockUsed(10);
      const pages = bf.toPageBuffers();
      expect(pages.length).toBe(1);
      expect(pages[0].length).toBe(2048);
    });
  });
});
