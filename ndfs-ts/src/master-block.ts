/**
 * NDFS master block: the 32-byte structure at offset 2016 of page 0.
 * Also handles the 16-byte extended info block at offset 2000.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import { BlockPointer } from './block-pointer.js';
import {
  MASTER_BLOCK_OFFSET,
  EXTENDED_INFO_OFFSET,
  NDFS_PAGE_SIZE,
  NDFS_NAME_MAX,
} from './constants.js';
import { ChecksumValidation } from './types.js';
import { readUint16BE, readUint32BE, writeUint16BE, writeUint32BE } from './endian.js';
import { readNdfsName, writeNdfsName } from './ndfs-name.js';

export class MasterBlock {
  directoryName: string = '';
  objectFilePointer: BlockPointer | null = null;
  userFilePointer: BlockPointer | null = null;
  bitFilePointer: BlockPointer | null = null;
  unreservedPages: number = 0;
  imageSize: number = 0;

  // Extended info fields
  extChecksum: number = 0;
  extReserved1: number = 0;
  extReserved2: number = 0;
  extReserved3: number = 0;
  extFlagWord: number = 0;
  extLastSystemNumber: number = 0;
  extPagesAvailable: number = 0;
  extCalculatedChecksum: number = 0;
  extValid: boolean = false;
  checksumState: ChecksumValidation = ChecksumValidation.Invalid;
  hasFlomon: boolean = false;

  /**
   * Parse a master block (and extended info) from a full page 0 buffer.
   * The buffer must be at least NDFS_PAGE_SIZE bytes.
   */
  static fromBytes(pageData: Uint8Array): MasterBlock {
    if (pageData.length < NDFS_PAGE_SIZE) {
      throw new Error('Page data too small for master block');
    }

    const mb = new MasterBlock();
    const off = MASTER_BLOCK_OFFSET;

    // Directory name (16 bytes, terminated by 0x27)
    mb.directoryName = readNdfsName(pageData, off, NDFS_NAME_MAX);

    // Block pointers
    mb.objectFilePointer = BlockPointer.fromBytes(pageData, off + 0x10);
    mb.userFilePointer = BlockPointer.fromBytes(pageData, off + 0x14);
    mb.bitFilePointer = BlockPointer.fromBytes(pageData, off + 0x18);

    // Unreserved pages
    mb.unreservedPages = readUint32BE(pageData, off + 0x1c);

    // --- Extended info (bytes 2000-2015) ---
    const ext = EXTENDED_INFO_OFFSET;
    mb.extChecksum = readUint16BE(pageData, ext);
    mb.extReserved1 = readUint16BE(pageData, ext + 2);
    mb.extReserved2 = readUint16BE(pageData, ext + 4);
    mb.extReserved3 = readUint16BE(pageData, ext + 6);
    mb.extFlagWord = readUint16BE(pageData, ext + 8);
    mb.extLastSystemNumber = readUint16BE(pageData, ext + 10);
    mb.extPagesAvailable = readUint32BE(pageData, ext + 12);

    // Calculate checksum
    const pagesLo = mb.extPagesAvailable & 0xffff;
    const pagesHi = (mb.extPagesAvailable >>> 16) & 0xffff;
    const calculated =
      ((pagesLo ^ pagesHi ^ mb.extFlagWord ^ mb.extReserved1 ^ mb.extReserved2 ^ mb.extReserved3) +
        mb.extLastSystemNumber) &
      0xffff;
    mb.extCalculatedChecksum = calculated;

    // Determine checksum validation state
    if (mb.extChecksum === calculated) {
      mb.checksumState = ChecksumValidation.Valid;
    } else if (
      (mb.extChecksum & 0xff) === (calculated & 0xff) &&
      (mb.extChecksum & 0xff00) === 0
    ) {
      mb.checksumState = ChecksumValidation.ValidLowByteOnly;
    } else {
      mb.checksumState = ChecksumValidation.Invalid;
    }

    // Detect FLOMON: all extended fields are zero (floppy)
    // FLOMON signature: address(2) + count(2) + checksum(2) all zero in boot area
    // Simplified: check if the first 6 bytes after boot offset look like FLOMON
    mb.hasFlomon = MasterBlock.detectFlomon(pageData);

    // Extended info validity
    if (mb.hasFlomon) {
      mb.extValid = false;
    } else {
      const checksumNonZero = mb.extChecksum !== 0;
      const checksumOk =
        mb.checksumState === ChecksumValidation.Valid ||
        mb.checksumState === ChecksumValidation.ValidLowByteOnly;
      mb.extValid = checksumNonZero && checksumOk;
    }

    return mb;
  }

  /**
   * Detect FLOMON boot format by checking for the zero-address/count/checksum pattern.
   * FLOMON disks have a simplified boot loader where the BPUN binary section
   * has address=0, count=0, and checksum=0.
   */
  private static detectFlomon(pageData: Uint8Array): boolean {
    // Look for '!' marker in the first part of the page
    let exclamationPos = -1;
    for (let i = 0; i < Math.min(pageData.length, 256); i++) {
      if (pageData[i] === 0x21) {
        exclamationPos = i;
        break;
      }
    }
    if (exclamationPos < 0) return false;

    // After '!', BPUN has: address(2) + count(2) bytes
    // FLOMON has address=0, count=0
    const afterExcl = exclamationPos + 1;
    if (afterExcl + 4 > pageData.length) return false;

    const addr = readUint16BE(pageData, afterExcl);
    const count = readUint16BE(pageData, afterExcl + 2);

    return addr === 0 && count === 0;
  }

  /** Check if the master block is valid. */
  isValid(): boolean {
    // Check directory name is printable ASCII
    if (this.directoryName.length > 0) {
      for (let i = 0; i < this.directoryName.length; i++) {
        const c = this.directoryName.charCodeAt(i);
        if (c < 0x20 || c > 0x7e) return false;
      }
    }

    // At least one pointer must be valid, or a directory name must exist
    let hasValidPointer = false;
    if (this.objectFilePointer !== null && this.objectFilePointer.isValid()) hasValidPointer = true;
    if (this.userFilePointer !== null && this.userFilePointer.isValid()) hasValidPointer = true;
    if (this.bitFilePointer !== null && this.bitFilePointer.isValid()) hasValidPointer = true;

    return hasValidPointer || this.directoryName.length > 0;
  }

  /** Write the master block to page data at the standard offset. */
  writeToBytes(pageData: Uint8Array): void {
    if (pageData.length < NDFS_PAGE_SIZE) {
      throw new Error('Page buffer too small for master block');
    }

    const off = MASTER_BLOCK_OFFSET;

    // Clear the master block area
    pageData.fill(0, off, off + 32);

    // Directory name
    writeNdfsName(pageData, off, this.directoryName, NDFS_NAME_MAX);

    // Block pointers
    if (this.objectFilePointer) this.objectFilePointer.toBytes(pageData, off + 0x10);
    if (this.userFilePointer) this.userFilePointer.toBytes(pageData, off + 0x14);
    if (this.bitFilePointer) this.bitFilePointer.toBytes(pageData, off + 0x18);

    // Unreserved pages
    writeUint32BE(pageData, off + 0x1c, this.unreservedPages);
  }

  /** Write extended info to page data. */
  writeExtendedInfo(pageData: Uint8Array): void {
    if (pageData.length < NDFS_PAGE_SIZE) {
      throw new Error('Page buffer too small for extended info');
    }

    const ext = EXTENDED_INFO_OFFSET;

    // Calculate checksum
    const pagesLo = this.extPagesAvailable & 0xffff;
    const pagesHi = (this.extPagesAvailable >>> 16) & 0xffff;
    const checksum =
      ((pagesLo ^ pagesHi ^ this.extFlagWord ^ this.extReserved1 ^ this.extReserved2 ^ this.extReserved3) +
        this.extLastSystemNumber) &
      0xffff;

    writeUint16BE(pageData, ext, checksum);
    writeUint16BE(pageData, ext + 2, this.extReserved1);
    writeUint16BE(pageData, ext + 4, this.extReserved2);
    writeUint16BE(pageData, ext + 6, this.extReserved3);
    writeUint16BE(pageData, ext + 8, this.extFlagWord);
    writeUint16BE(pageData, ext + 10, this.extLastSystemNumber);
    writeUint32BE(pageData, ext + 12, this.extPagesAvailable);
  }
}
