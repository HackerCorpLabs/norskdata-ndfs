import { describe, it, expect } from 'vitest';
import { readNdfsName, writeNdfsName } from '../src/ndfs-name.js';
import { NDFS_NAME_TERMINATOR } from '../src/constants.js';

describe('NDFS name encoding', () => {
  describe('readNdfsName', () => {
    it('reads name terminated by 0x27', () => {
      const data = new Uint8Array([0x54, 0x45, 0x53, 0x54, 0x27, 0x27, 0x27]);
      expect(readNdfsName(data, 0, 7)).toBe('TEST');
    });

    it('reads name terminated by null', () => {
      const data = new Uint8Array([0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x00]);
      expect(readNdfsName(data, 0, 6)).toBe('HELLO');
    });

    it('reads full-length name (no terminator within maxLen)', () => {
      const data = new Uint8Array(16);
      for (let i = 0; i < 16; i++) data[i] = 0x41 + i; // A-P
      expect(readNdfsName(data, 0, 16)).toBe('ABCDEFGHIJKLMNOP');
    });

    it('reads at offset', () => {
      const data = new Uint8Array([0x00, 0x00, 0x41, 0x42, 0x27]);
      expect(readNdfsName(data, 2, 3)).toBe('AB');
    });

    it('returns empty string for immediate terminator', () => {
      const data = new Uint8Array([0x27, 0x27]);
      expect(readNdfsName(data, 0, 2)).toBe('');
    });
  });

  describe('writeNdfsName', () => {
    it('writes name and pads with 0x27', () => {
      const buf = new Uint8Array(8);
      writeNdfsName(buf, 0, 'TEST', 8);
      expect(buf[0]).toBe(0x54); // T
      expect(buf[1]).toBe(0x45); // E
      expect(buf[2]).toBe(0x53); // S
      expect(buf[3]).toBe(0x54); // T
      expect(buf[4]).toBe(NDFS_NAME_TERMINATOR);
      expect(buf[5]).toBe(NDFS_NAME_TERMINATOR);
    });

    it('uppercases the name', () => {
      const buf = new Uint8Array(8);
      writeNdfsName(buf, 0, 'hello', 8);
      expect(buf[0]).toBe(0x48); // H
      expect(buf[1]).toBe(0x45); // E
    });

    it('truncates to maxLen', () => {
      const buf = new Uint8Array(4);
      writeNdfsName(buf, 0, 'TOOLONGNAME', 4);
      expect(readNdfsName(buf, 0, 4)).toBe('TOOL');
    });

    it('round-trips with readNdfsName', () => {
      const buf = new Uint8Array(16);
      writeNdfsName(buf, 0, 'NORDISK', 16);
      expect(readNdfsName(buf, 0, 16)).toBe('NORDISK');
    });
  });
});
