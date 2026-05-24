import { describe, it, expect } from 'vitest';
import { MasterBlock } from '../src/master-block.js';
import { BlockPointer } from '../src/block-pointer.js';
import { PointerType, ChecksumValidation } from '../src/types.js';
import {
  MASTER_BLOCK_OFFSET,
  NDFS_PAGE_SIZE,
  NDFS_NAME_TERMINATOR,
} from '../src/constants.js';
import { writeUint32BE, writeUint16BE } from '../src/endian.js';
import * as fs from 'fs';
import * as path from 'path';

/** Helper: build a minimal valid page 0 buffer with a master block. */
function buildPage0(
  name: string,
  objPtr: number,
  usrPtr: number,
  bitPtr: number,
  unreserved: number,
): Uint8Array {
  const page = new Uint8Array(NDFS_PAGE_SIZE);
  const off = MASTER_BLOCK_OFFSET;

  // Write name terminated by 0x27
  for (let i = 0; i < name.length && i < 16; i++) {
    page[off + i] = name.charCodeAt(i);
  }
  if (name.length < 16) {
    page[off + name.length] = NDFS_NAME_TERMINATOR;
  }

  writeUint32BE(page, off + 0x10, objPtr);
  writeUint32BE(page, off + 0x14, usrPtr);
  writeUint32BE(page, off + 0x18, bitPtr);
  writeUint32BE(page, off + 0x1c, unreserved);

  return page;
}

describe('MasterBlock', () => {
  describe('fromBytes', () => {
    it('parses directory name', () => {
      const page = buildPage0('TESTDISK', 0x40000002, 0x40000005, 0x00000008, 100);
      const mb = MasterBlock.fromBytes(page);
      expect(mb.directoryName).toBe('TESTDISK');
    });

    it('parses block pointers', () => {
      const page = buildPage0('DISK', 0x40000002, 0x40000005, 0x00000008, 50);
      const mb = MasterBlock.fromBytes(page);

      expect(mb.objectFilePointer!.blockId).toBe(2);
      expect(mb.objectFilePointer!.type).toBe(PointerType.Indexed);

      expect(mb.userFilePointer!.blockId).toBe(5);
      expect(mb.userFilePointer!.type).toBe(PointerType.Indexed);

      expect(mb.bitFilePointer!.blockId).toBe(8);
      expect(mb.bitFilePointer!.type).toBe(PointerType.Contiguous);
    });

    it('parses unreserved pages', () => {
      const page = buildPage0('DISK', 0x40000002, 0x40000005, 0x00000008, 12345);
      const mb = MasterBlock.fromBytes(page);
      expect(mb.unreservedPages).toBe(12345);
    });

    it('throws on too-small buffer', () => {
      expect(() => MasterBlock.fromBytes(new Uint8Array(100))).toThrow();
    });
  });

  describe('isValid', () => {
    it('returns true for valid master block', () => {
      const page = buildPage0('VALID', 0x40000002, 0x40000005, 0x00000008, 100);
      const mb = MasterBlock.fromBytes(page);
      expect(mb.isValid()).toBe(true);
    });

    it('returns true for name-only (no valid pointers)', () => {
      const page = buildPage0('NAMEONLY', 0, 0, 0, 0);
      const mb = MasterBlock.fromBytes(page);
      expect(mb.isValid()).toBe(true);
    });

    it('returns false for non-printable name and no pointers', () => {
      const page = new Uint8Array(NDFS_PAGE_SIZE);
      // Put a control char in the name
      page[MASTER_BLOCK_OFFSET] = 0x01;
      page[MASTER_BLOCK_OFFSET + 1] = NDFS_NAME_TERMINATOR;
      const mb = MasterBlock.fromBytes(page);
      expect(mb.isValid()).toBe(false);
    });
  });

  describe('writeToBytes round-trip', () => {
    it('round-trips name and pointers', () => {
      const page = buildPage0('ROUNDTRIP', 0x40000010, 0x40000020, 0x00000030, 999);
      const mb = MasterBlock.fromBytes(page);

      const page2 = new Uint8Array(NDFS_PAGE_SIZE);
      mb.writeToBytes(page2);

      const mb2 = MasterBlock.fromBytes(page2);
      expect(mb2.directoryName).toBe('ROUNDTRIP');
      expect(mb2.objectFilePointer!.blockId).toBe(0x10);
      expect(mb2.userFilePointer!.blockId).toBe(0x20);
      expect(mb2.bitFilePointer!.blockId).toBe(0x30);
      expect(mb2.unreservedPages).toBe(999);
    });
  });

  describe('extended info', () => {
    it('calculates checksum correctly', () => {
      const page = buildPage0('HARDDISK', 0x40000002, 0x40000005, 0x00000008, 100);

      // Write extended info at offset 2000
      const ext = 2000;
      const systemNumber = 100;
      const flagWord = 0x0051;
      const pagesAvailable = 38400;

      const pagesLo = pagesAvailable & 0xffff;
      const pagesHi = (pagesAvailable >>> 16) & 0xffff;
      const checksum = ((pagesLo ^ pagesHi ^ flagWord ^ 0 ^ 0 ^ 0) + systemNumber) & 0xffff;

      writeUint16BE(page, ext, checksum); // checksum
      writeUint16BE(page, ext + 2, 0); // reserved1
      writeUint16BE(page, ext + 4, 0); // reserved2
      writeUint16BE(page, ext + 6, 0); // reserved3
      writeUint16BE(page, ext + 8, flagWord); // flag word
      writeUint16BE(page, ext + 10, systemNumber); // system number
      writeUint32BE(page, ext + 12, pagesAvailable); // pages available

      const mb = MasterBlock.fromBytes(page);
      expect(mb.checksumState).toBe(ChecksumValidation.Valid);
      expect(mb.extValid).toBe(true);
      expect(mb.extPagesAvailable).toBe(38400);
      expect(mb.extLastSystemNumber).toBe(100);
    });

    it('detects invalid checksum', () => {
      const page = buildPage0('BADCRC', 0x40000002, 0x40000005, 0x00000008, 100);

      const ext = 2000;
      writeUint16BE(page, ext, 0xdead); // bad checksum
      writeUint16BE(page, ext + 8, 0x0051);
      writeUint16BE(page, ext + 10, 100);
      writeUint32BE(page, ext + 12, 38400);

      const mb = MasterBlock.fromBytes(page);
      expect(mb.checksumState).toBe(ChecksumValidation.Invalid);
      expect(mb.extValid).toBe(false);
    });
  });

  describe('real fixture files', () => {
    const fixtureDir = path.join(__dirname, 'fixtures');

    it('parses empty.ndfs', () => {
      const data = new Uint8Array(fs.readFileSync(path.join(fixtureDir, 'empty.ndfs')));
      const page0 = data.subarray(0, NDFS_PAGE_SIZE);
      const mb = MasterBlock.fromBytes(page0);
      expect(mb.isValid()).toBe(true);
      expect(mb.directoryName.length).toBeGreaterThan(0);
    });

    it('parses withfiles.ndfs', () => {
      const data = new Uint8Array(fs.readFileSync(path.join(fixtureDir, 'withfiles.ndfs')));
      const page0 = data.subarray(0, NDFS_PAGE_SIZE);
      const mb = MasterBlock.fromBytes(page0);
      expect(mb.isValid()).toBe(true);
      expect(mb.directoryName.length).toBeGreaterThan(0);
      expect(mb.objectFilePointer).not.toBeNull();
      expect(mb.objectFilePointer!.isValid()).toBe(true);
      expect(mb.userFilePointer).not.toBeNull();
      expect(mb.userFilePointer!.isValid()).toBe(true);
      expect(mb.bitFilePointer).not.toBeNull();
      expect(mb.bitFilePointer!.isValid()).toBe(true);
    });
  });
});
