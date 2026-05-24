import { describe, it, expect } from 'vitest';
import { BlockPointer } from '../src/block-pointer.js';
import { PointerType } from '../src/types.js';

describe('BlockPointer', () => {
  describe('constructor', () => {
    it('defaults to blockId=0, Contiguous', () => {
      const bp = new BlockPointer();
      expect(bp.blockId).toBe(0);
      expect(bp.type).toBe(PointerType.Contiguous);
    });

    it('creates with explicit values', () => {
      const bp = new BlockPointer(100, PointerType.Indexed);
      expect(bp.blockId).toBe(100);
      expect(bp.type).toBe(PointerType.Indexed);
    });

    it('masks blockId to 30 bits', () => {
      const bp = new BlockPointer(0xffffffff, PointerType.Contiguous);
      expect(bp.blockId).toBe(0x3fffffff);
    });
  });

  describe('fromNative', () => {
    it('decodes Contiguous pointer', () => {
      const bp = BlockPointer.fromNative(0x00000005);
      expect(bp.blockId).toBe(5);
      expect(bp.type).toBe(PointerType.Contiguous);
    });

    it('decodes Indexed pointer', () => {
      const bp = BlockPointer.fromNative(0x40000005);
      expect(bp.blockId).toBe(5);
      expect(bp.type).toBe(PointerType.Indexed);
    });

    it('decodes SubIndexed pointer', () => {
      const bp = BlockPointer.fromNative(0x80000005);
      expect(bp.blockId).toBe(5);
      expect(bp.type).toBe(PointerType.SubIndexed);
    });

    it('decodes Reserved pointer', () => {
      const bp = BlockPointer.fromNative(0xc0000005);
      expect(bp.blockId).toBe(5);
      expect(bp.type).toBe(PointerType.Reserved);
    });
  });

  describe('native property', () => {
    it('encodes Contiguous', () => {
      expect(new BlockPointer(5, PointerType.Contiguous).native).toBe(0x00000005);
    });

    it('encodes Indexed', () => {
      expect(new BlockPointer(5, PointerType.Indexed).native).toBe(0x40000005);
    });

    it('encodes SubIndexed', () => {
      expect(new BlockPointer(5, PointerType.SubIndexed).native).toBe(0x80000005);
    });

    it('encodes large blockId with Indexed', () => {
      expect(new BlockPointer(100, PointerType.Indexed).native).toBe(0x40000064);
    });

    it('round-trips through fromNative', () => {
      const values = [0x00000005, 0x40000064, 0x80001000, 0xc0ffffff];
      for (const v of values) {
        expect(BlockPointer.fromNative(v).native).toBe(v);
      }
    });
  });

  describe('isValid', () => {
    it('returns false for blockId=0', () => {
      expect(new BlockPointer(0, PointerType.Indexed).isValid()).toBe(false);
    });

    it('returns false for Reserved type', () => {
      expect(new BlockPointer(5, PointerType.Reserved).isValid()).toBe(false);
    });

    it('returns true for valid Contiguous', () => {
      expect(new BlockPointer(5, PointerType.Contiguous).isValid()).toBe(true);
    });

    it('returns true for valid Indexed', () => {
      expect(new BlockPointer(100, PointerType.Indexed).isValid()).toBe(true);
    });
  });

  describe('fromBytes / toBytes', () => {
    it('parses big-endian bytes', () => {
      const data = new Uint8Array([0x40, 0x00, 0x00, 0x64]);
      const bp = BlockPointer.fromBytes(data, 0);
      expect(bp.blockId).toBe(100);
      expect(bp.type).toBe(PointerType.Indexed);
    });

    it('parses at offset', () => {
      const data = new Uint8Array([0xff, 0xff, 0x80, 0x00, 0x10, 0x00]);
      const bp = BlockPointer.fromBytes(data, 2);
      expect(bp.blockId).toBe(0x1000);
      expect(bp.type).toBe(PointerType.SubIndexed);
    });

    it('round-trips through bytes', () => {
      const bp = new BlockPointer(12345, PointerType.Indexed);
      const bytes = bp.toBytesArray();
      const bp2 = BlockPointer.fromBytes(bytes, 0);
      expect(bp2.blockId).toBe(bp.blockId);
      expect(bp2.type).toBe(bp.type);
    });

    it('writes to buffer at offset', () => {
      const buf = new Uint8Array(8);
      const bp = new BlockPointer(5, PointerType.Contiguous);
      bp.toBytes(buf, 4);
      expect(buf[4]).toBe(0x00);
      expect(buf[5]).toBe(0x00);
      expect(buf[6]).toBe(0x00);
      expect(buf[7]).toBe(0x05);
    });
  });
});
