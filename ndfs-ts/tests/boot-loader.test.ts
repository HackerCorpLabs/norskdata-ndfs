import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { detectBootFormat, loadBootCode } from '../src/boot-loader.js';
import { BootFormat, ImageTemplate } from '../src/types.js';
import { NDFS_PAGE_SIZE, MASTER_BLOCK_OFFSET } from '../src/constants.js';
import { writeUint16BE } from '../src/endian.js';
import * as fs from 'fs';
import * as path from 'path';

const fixtureDir = path.join(__dirname, 'fixtures');

/**
 * Helper: create a page 0 with specific boot data injected.
 */
function createPage0WithBoot(inject: (page: Uint8Array) => void): Uint8Array {
  // Create a minimal NDFS image so we can test via NdfsFileSystem too
  const ndfs = NdfsFileSystem.createImage({
    template: ImageTemplate.Custom,
    customPages: 50,
    directoryName: 'BOOTTEST',
  });
  const buf = ndfs.toBuffer();
  inject(buf); // modify page 0
  return buf;
}

describe('BootLoader', () => {
  describe('detectBootFormat', () => {
    it('detects None on empty image', () => {
      const ndfs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 50,
        directoryName: 'EMPTY',
      });
      expect(ndfs.detectBootFormat()).toBe(BootFormat.None);
      expect(ndfs.isBootable()).toBe(false);
    });

    it('detects FloMon when address=0 count=0 after !', () => {
      const buf = createPage0WithBoot((page) => {
        // Write some preamble data then '!' then address=0 count=0
        page[0] = 0x4E; // some data
        page[1] = 0x44; // some data
        page[2] = 0x21; // '!'
        writeUint16BE(page, 3, 0); // address = 0
        writeUint16BE(page, 5, 0); // count = 0
      });
      const page0 = buf.subarray(0, NDFS_PAGE_SIZE);
      expect(detectBootFormat(page0)).toBe(BootFormat.FloMon);
    });

    it('detects BPUN with valid address and count', () => {
      const buf = createPage0WithBoot((page) => {
        page[0] = 0x21; // '!' at start
        writeUint16BE(page, 1, 0x1000); // address
        writeUint16BE(page, 3, 4); // count = 4 words
        // Write 4 words of data (8 bytes)
        for (let i = 0; i < 8; i++) page[5 + i] = 0xAA;
        // Checksum at offset 13
        const checksum = (0x1000 + 4 + 0xAAAA + 0xAAAA + 0xAAAA + 0xAAAA) & 0xFFFF;
        writeUint16BE(page, 13, checksum);
      });
      const page0 = buf.subarray(0, NDFS_PAGE_SIZE);
      expect(detectBootFormat(page0)).toBe(BootFormat.BPUN);
    });

    it('detects Binary when non-zero data but no !', () => {
      const buf = createPage0WithBoot((page) => {
        // Write random-looking non-zero data (no '!' character 0x21)
        for (let i = 0; i < 100; i++) {
          page[i] = (i * 7 + 3) & 0xFF;
          // Make sure no 0x21 accidentally appears
          if (page[i] === 0x21) page[i] = 0x22;
        }
      });
      const page0 = buf.subarray(0, NDFS_PAGE_SIZE);
      expect(detectBootFormat(page0)).toBe(BootFormat.Binary);
    });

    it('returns None for all-zero page', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      expect(detectBootFormat(page0)).toBe(BootFormat.None);
    });

    it('returns None for uniform (all same byte) page', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      page0.fill(0xFF);
      expect(detectBootFormat(page0)).toBe(BootFormat.None);
    });
  });

  describe('loadBootCode', () => {
    it('returns null for no boot format', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      expect(loadBootCode(page0)).toBeNull();
    });

    it('loads FLOMON boot code', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      page0[0] = 0x4E;
      page0[1] = 0x44;
      page0[2] = 0x21; // '!'
      writeUint16BE(page0, 3, 0);
      writeUint16BE(page0, 5, 0);

      const boot = loadBootCode(page0);
      expect(boot).not.toBeNull();
      expect(boot!.format).toBe(BootFormat.FloMon);
      expect(boot!.startAddress).toBe(0);
      expect(boot!.wordCount).toBe(0);
      expect(boot!.data.length).toBe(2); // data before '!'
      expect(boot!.checksumValid).toBe(true);
    });

    it('loads BPUN boot code with checksum validation', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      page0[0] = 0x21; // '!'
      writeUint16BE(page0, 1, 0x0100); // address
      writeUint16BE(page0, 3, 2); // count = 2 words

      // Data: 2 words
      writeUint16BE(page0, 5, 0x1234);
      writeUint16BE(page0, 7, 0x5678);

      // Checksum
      const checksum = (0x0100 + 2 + 0x1234 + 0x5678) & 0xFFFF;
      writeUint16BE(page0, 9, checksum);

      const boot = loadBootCode(page0);
      expect(boot).not.toBeNull();
      expect(boot!.format).toBe(BootFormat.BPUN);
      expect(boot!.startAddress).toBe(0x0100);
      expect(boot!.wordCount).toBe(2);
      expect(boot!.data.length).toBe(4); // 2 words = 4 bytes
      expect(boot!.checksumValid).toBe(true);
    });

    it('loads Binary boot code', () => {
      const page0 = new Uint8Array(NDFS_PAGE_SIZE);
      for (let i = 0; i < 100; i++) {
        page0[i] = (i * 13 + 5) & 0xFF;
        if (page0[i] === 0x21) page0[i] = 0x22;
      }

      const boot = loadBootCode(page0);
      expect(boot).not.toBeNull();
      expect(boot!.format).toBe(BootFormat.Binary);
      expect(boot!.data.length).toBe(1024);
    });
  });

  describe('NdfsFileSystem boot methods', () => {
    it('isBootable returns false for non-bootable image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 50,
      });
      expect(fs.isBootable()).toBe(false);
    });

    it('detectBootFormat returns None for clean image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 50,
      });
      expect(fs.detectBootFormat()).toBe(BootFormat.None);
    });

    it('loadBootCode returns null for clean image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 50,
      });
      expect(fs.loadBootCode()).toBeNull();
    });
  });

  describe('fixture files', () => {
    it('detects boot format on empty.ndfs', () => {
      const data = new Uint8Array(fs.readFileSync(path.join(fixtureDir, 'empty.ndfs')));
      const ndfs = new NdfsFileSystem(data, true);
      const format = ndfs.detectBootFormat();
      // Just verify it returns a valid enum value without crashing
      expect([BootFormat.None, BootFormat.Binary, BootFormat.BPUN, BootFormat.FloMon]).toContain(format);
    });

    it('detects boot format on withfiles.ndfs', () => {
      const data = new Uint8Array(fs.readFileSync(path.join(fixtureDir, 'withfiles.ndfs')));
      const ndfs = new NdfsFileSystem(data, true);
      const format = ndfs.detectBootFormat();
      expect([BootFormat.None, BootFormat.Binary, BootFormat.BPUN, BootFormat.FloMon]).toContain(format);
    });
  });
});
