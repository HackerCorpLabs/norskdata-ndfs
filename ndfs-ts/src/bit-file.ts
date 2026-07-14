/**
 * NDFS bit file (allocation bitmap): one bit per page, 0=free, 1=used.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import { NDFS_PAGE_SIZE, FIRST_ALLOCATABLE_BLOCK } from './constants.js';
import { BlockPointer } from './block-pointer.js';

export class BitFile {
  indexPointer: BlockPointer | null = null;

  /**
   * Pages the bitmap covers. SINTRAN sizes the bitmap to the PHYSICAL DEVICE (e.g. 38400
   * on a 75MB SMD pack), not to the declared capacity.
   */
  totalPages: number = 0;

  /**
   * Highest allocatable page + 1 — the descending allocator's ceiling.
   *
   * SINTRAN bounds the allocatable window by the directory's DECLARED CAPACITY
   * (extPagesAvailable, words 1756B-1757B), not by the device size. On PACK-ONE the
   * capacity is 36945, so the highest allocatable page is 36944; pages 36945..38399 are a
   * deliberate free-but-unreachable gap (the drive's bad-sector spare region) — they stay
   * 0 in the bitmap yet are never handed out.
   *
   * Defaults to totalPages when the capacity is unknown (FLOMON floppies carry no valid
   * extended-info block). Always clamped to totalPages: the real Winchester WD0.img
   * declares a capacity 36 pages LARGER than the file, and allocating there would run off
   * the end.
   */
  allocCeiling: number = 0;

  private bitmap: Uint8Array | null = null;

  /** Initialize a new empty bitmap for the given total page count. */
  initialize(totalPages: number): void {
    this.totalPages = totalPages;
    // Until the caller supplies the declared capacity, the whole device is allocatable.
    this.allocCeiling = totalPages;
    const bitmapBytes = Math.ceil(totalPages / 8);
    this.bitmap = new Uint8Array(bitmapBytes);
  }

  /** Load bitmap data from raw bytes. */
  loadBitmap(data: Uint8Array): void {
    this.bitmap = new Uint8Array(data.length);
    this.bitmap.set(data);
  }

  /** Check if a block is marked as used. */
  isBlockUsed(blockId: number): boolean {
    if (this.bitmap === null || blockId >= this.totalPages) return false;
    const byteIndex = blockId >>> 3;
    const bitIndex = blockId & 7;
    return (this.bitmap[byteIndex] & (1 << bitIndex)) !== 0;
  }

  /** Mark a block as used. */
  markBlockUsed(blockId: number): void {
    if (this.bitmap === null || blockId >= this.totalPages) {
      throw new RangeError(`Block ID ${blockId} out of range`);
    }
    const byteIndex = blockId >>> 3;
    const bitIndex = blockId & 7;
    this.bitmap[byteIndex] |= 1 << bitIndex;
  }

  /** Mark a block as free. */
  markBlockFree(blockId: number): void {
    if (this.bitmap === null || blockId >= this.totalPages) {
      throw new RangeError(`Block ID ${blockId} out of range`);
    }
    const byteIndex = blockId >>> 3;
    const bitIndex = blockId & 7;
    this.bitmap[byteIndex] &= ~(1 << bitIndex);
  }

  /** Count total used pages. */
  calcUsedPages(): number {
    if (this.bitmap === null) return 0;
    let count = 0;
    for (let i = 0; i < this.totalPages; i++) {
      if (this.isBlockUsed(i)) count++;
    }
    return count;
  }

  /** Get number of free pages. */
  getFreePages(): number {
    return this.totalPages - this.calcUsedPages();
  }

  /**
   * Highest page the allocator may consider (inclusive), or -1 if there is no usable
   * window. Bounded by {@link allocCeiling} — the DECLARED CAPACITY, not the device size —
   * and clamped to the bitmap so a stale ceiling can never index past it.
   */
  private topScanIndex(): number {
    let ceiling = this.allocCeiling;
    if (ceiling <= 0 || ceiling > this.totalPages) ceiling = this.totalPages;
    if (ceiling <= FIRST_ALLOCATABLE_BLOCK) return -1;
    return ceiling - 1;
  }

  /**
   * Find a single free block, scanning HIGH -> LOW.
   *
   * KERNEL-CORRECTED: SINTRAN allocates from the TOP of the volume downward. The scanner
   * TESTP (51355B) only ever DECREMENTS its bitmap word index (`AAX -1` at 51372B and
   * 51401B) — it never increments — bounded below by the block-7 floor. This matches the
   * @CREATE-FILE rule "contiguous files are positioned in the highest page addresses".
   * The old upward scan produced the opposite layout to genuine SINTRAN.
   *
   * Blocks 0-6 are system-reserved and are never handed out.
   * Returns the block ID, or -1 if no free block exists.
   */
  findFirstFreeBlock(): number {
    for (let i = this.topScanIndex(); i >= FIRST_ALLOCATABLE_BLOCK; i--) {
      if (!this.isBlockUsed(i)) return i;
    }
    return -1;
  }

  /**
   * Find a contiguous run of free blocks, scanning HIGH -> LOW so the run lands in the
   * highest available addresses (SINTRAN's downward range reserve, RSPAG 51120B -> TESTP).
   * Returns the LOWEST block of the run (its start), or -1.
   */
  findFreeBlockRange(blocksNeeded: number): number {
    if (blocksNeeded === 0 || blocksNeeded > this.totalPages) return -1;

    const top = this.topScanIndex();
    if (top < FIRST_ALLOCATABLE_BLOCK) return -1;

    let consecutiveFree = 0;
    for (let i = top; i >= FIRST_ALLOCATABLE_BLOCK; i--) {
      if (!this.isBlockUsed(i)) {
        consecutiveFree++;
        // i is the lowest block of the run.
        if (consecutiveFree >= blocksNeeded) return i;
      } else {
        consecutiveFree = 0;
      }
    }
    return -1;
  }

  /**
   * Allocate a range of blocks (mark as used).
   * Blocks 0-6 cannot be allocated. Returns false if any block is already used.
   */
  allocateBlocks(startBlock: number, count: number): boolean {
    if (startBlock < FIRST_ALLOCATABLE_BLOCK) return false;
    if (startBlock + count > this.totalPages) return false;

    // Check all blocks are free
    for (let i = startBlock; i < startBlock + count; i++) {
      if (this.isBlockUsed(i)) return false;
    }

    // Mark all blocks as used
    for (let i = startBlock; i < startBlock + count; i++) {
      this.markBlockUsed(i);
    }
    return true;
  }

  /** Free a range of blocks. */
  freeBlocks(startBlock: number, count: number): void {
    for (let i = startBlock; i < startBlock + count && i < this.totalPages; i++) {
      this.markBlockFree(i);
    }
  }

  /** Get a copy of the raw bitmap data. */
  getBitmapData(): Uint8Array {
    if (this.bitmap === null) return new Uint8Array(0);
    const copy = new Uint8Array(this.bitmap.length);
    copy.set(this.bitmap);
    return copy;
  }

  /**
   * Write bitmap data into page-aligned buffers for disk writing.
   * Returns an array of page buffers to write starting at the pointer's block ID.
   */
  toPageBuffers(): Uint8Array[] {
    if (this.bitmap === null) return [];

    const pagesNeeded = Math.ceil(this.bitmap.length / NDFS_PAGE_SIZE);
    const pages: Uint8Array[] = [];

    for (let i = 0; i < pagesNeeded; i++) {
      const page = new Uint8Array(NDFS_PAGE_SIZE);
      const srcOffset = i * NDFS_PAGE_SIZE;
      const bytesToCopy = Math.min(NDFS_PAGE_SIZE, this.bitmap.length - srcOffset);
      if (bytesToCopy > 0) {
        page.set(this.bitmap.subarray(srcOffset, srcOffset + bytesToCopy));
      }
      pages.push(page);
    }
    return pages;
  }
}
