/**
 * NDFS object (file) entry: 64-byte record in the object file.
 *
 * Byte offsets:
 *   0:     Header (bit 7 = in use, i.e. 0x80)
 *   1:     Reserved
 *   2-17:  Object name (16 bytes, terminated by 0x27)
 *   18-21: File type (4 bytes, terminated by 0x27)
 *   22-31: Reserved / versioning / access
 *   32:    File type code (0=DATA, 1=PROG, 2=SYMB, 3=TEXT)
 *   33:    Reserved
 *   34:    User index (owner)
 *   35-51: Reserved / tracking
 *   52-55: Pages in file (32-bit, big-endian)
 *   56-59: Bytes in file - 1 (32-bit, big-endian; actual = stored + 1)
 *   60-63: File pointer (BlockPointer, big-endian)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import {
  ENTRY_SIZE,
  OBJECT_ENTRY_IN_USE,
  NDFS_NAME_MAX,
  NDFS_TYPE_MAX,
  NDFS_NAME_TERMINATOR,
} from './constants.js';
import { readUint32BE, writeUint32BE } from './endian.js';
import { readNdfsName, writeNdfsName } from './ndfs-name.js';
import { BlockPointer } from './block-pointer.js';
import { PointerType } from './types.js';

export class ObjectEntry {
  header: number = OBJECT_ENTRY_IN_USE;
  objectIndex: number = 0;
  objectName: string = '';
  type: string = 'DATA';
  userName: string = '';
  userIndex: number = 0;
  fileType: number = 0; // 0=DATA, 1=PROG, 2=SYMB, 3=TEXT
  pagesInFile: number = 0;
  bytesInFile: number = 0;
  filePointer: BlockPointer | null = null;
  accessBits: number = 0;
  dateCreated: number = 0;
  lastDateRead: number = 0;
  lastDateWritten: number = 0;

  /** Full name in "NAME:TYPE" format. */
  get fullName(): string {
    return this.type ? `${this.objectName}:${this.type}` : this.objectName;
  }

  /** File type as text string. */
  get fileTypeAsText(): string {
    switch (this.fileType) {
      case 0:
        return 'DATA';
      case 1:
        return 'PROG';
      case 2:
        return 'SYMB';
      case 3:
        return 'TEXT';
      default:
        return `TYPE${this.fileType}`;
    }
  }

  /**
   * Parse an object entry from 64 bytes at offset.
   * Returns null if the entry is not in use (bit 7 of byte 0 not set).
   */
  static fromBytes(data: Uint8Array, offset: number): ObjectEntry | null {
    if (data.length < offset + ENTRY_SIZE) {
      throw new Error('Insufficient data for object entry');
    }

    // Check in-use bit
    if ((data[offset] & OBJECT_ENTRY_IN_USE) === 0) return null;

    const entry = new ObjectEntry();
    entry.header = data[offset];

    // Object name (16 bytes at offset+2)
    entry.objectName = readNdfsName(data, offset + 2, NDFS_NAME_MAX);

    // File type (4 bytes at offset+18)
    const typeStr = readNdfsName(data, offset + 18, NDFS_TYPE_MAX);
    entry.type = typeStr.length > 0 ? typeStr : 'DATA';

    // File type code (byte 32)
    entry.fileType = data[offset + 32];

    // User index (byte 34)
    entry.userIndex = data[offset + 34];

    // Pages in file (bytes 52-55, big-endian)
    entry.pagesInFile = readUint32BE(data, offset + 52);

    // Bytes in file (bytes 56-59, big-endian) + 1
    entry.bytesInFile = readUint32BE(data, offset + 56) + 1;

    // File pointer (bytes 60-63)
    entry.filePointer = BlockPointer.fromBytes(data, offset + 60);

    return entry;
  }

  /** Serialize to a 64-byte region in a buffer. */
  toBytes(buffer: Uint8Array, offset: number): void {
    if (buffer.length < offset + ENTRY_SIZE) {
      throw new Error('Insufficient buffer for object entry');
    }

    // Clear the entry area
    buffer.fill(0, offset, offset + ENTRY_SIZE);

    // Header (0x80 = in use)
    buffer[offset] = OBJECT_ENTRY_IN_USE;

    // Object name
    writeNdfsName(buffer, offset + 2, this.objectName, NDFS_NAME_MAX);

    // File type string
    writeNdfsName(buffer, offset + 18, this.type, NDFS_TYPE_MAX);

    // File type code
    buffer[offset + 32] = this.fileType & 0xff;

    // User index
    buffer[offset + 34] = this.userIndex & 0xff;

    // Pages in file
    writeUint32BE(buffer, offset + 52, this.pagesInFile);

    // Bytes in file - 1
    const bytesMinusOne = this.bytesInFile > 0 ? this.bytesInFile - 1 : 0;
    writeUint32BE(buffer, offset + 56, bytesMinusOne);

    // File pointer
    if (this.filePointer) {
      this.filePointer.toBytes(buffer, offset + 60);
    }
  }
}
