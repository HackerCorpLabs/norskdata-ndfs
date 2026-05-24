import { describe, it, expect } from 'vitest';
import { readUint16BE, readUint32BE, writeUint16BE, writeUint32BE } from '../src/endian.js';

describe('endian helpers', () => {
  describe('readUint16BE', () => {
    it('reads 0x0000', () => {
      expect(readUint16BE(new Uint8Array([0x00, 0x00]), 0)).toBe(0);
    });

    it('reads 0xFFFF', () => {
      expect(readUint16BE(new Uint8Array([0xff, 0xff]), 0)).toBe(0xffff);
    });

    it('reads 0x1234', () => {
      expect(readUint16BE(new Uint8Array([0x12, 0x34]), 0)).toBe(0x1234);
    });

    it('reads at offset', () => {
      expect(readUint16BE(new Uint8Array([0x00, 0x00, 0xab, 0xcd]), 2)).toBe(0xabcd);
    });
  });

  describe('readUint32BE', () => {
    it('reads 0x00000000', () => {
      expect(readUint32BE(new Uint8Array([0, 0, 0, 0]), 0)).toBe(0);
    });

    it('reads 0xFFFFFFFF', () => {
      expect(readUint32BE(new Uint8Array([0xff, 0xff, 0xff, 0xff]), 0)).toBe(0xffffffff);
    });

    it('reads 0x12345678', () => {
      expect(readUint32BE(new Uint8Array([0x12, 0x34, 0x56, 0x78]), 0)).toBe(0x12345678);
    });

    it('reads at offset', () => {
      const data = new Uint8Array([0x00, 0x00, 0xde, 0xad, 0xbe, 0xef]);
      expect(readUint32BE(data, 2)).toBe(0xdeadbeef);
    });
  });

  describe('writeUint16BE', () => {
    it('writes 0x1234', () => {
      const buf = new Uint8Array(2);
      writeUint16BE(buf, 0, 0x1234);
      expect(buf[0]).toBe(0x12);
      expect(buf[1]).toBe(0x34);
    });

    it('round-trips with read', () => {
      const buf = new Uint8Array(4);
      writeUint16BE(buf, 1, 0xabcd);
      expect(readUint16BE(buf, 1)).toBe(0xabcd);
    });
  });

  describe('writeUint32BE', () => {
    it('writes 0xDEADBEEF', () => {
      const buf = new Uint8Array(4);
      writeUint32BE(buf, 0, 0xdeadbeef);
      expect(buf[0]).toBe(0xde);
      expect(buf[1]).toBe(0xad);
      expect(buf[2]).toBe(0xbe);
      expect(buf[3]).toBe(0xef);
    });

    it('round-trips with read', () => {
      const buf = new Uint8Array(8);
      writeUint32BE(buf, 2, 0x12345678);
      expect(readUint32BE(buf, 2)).toBe(0x12345678);
    });

    it('handles high bit correctly (unsigned)', () => {
      const buf = new Uint8Array(4);
      writeUint32BE(buf, 0, 0x80000000);
      expect(readUint32BE(buf, 0)).toBe(0x80000000);
    });
  });
});
