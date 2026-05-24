import { describe, it, expect } from 'vitest';
import { ndTimeToDate, dateToNdTime } from '../src/nd-time.js';

describe('ND timestamp', () => {
  describe('ndTimeToDate', () => {
    it('returns null for value 0', () => {
      expect(ndTimeToDate(0)).toBeNull();
    });

    it('decodes a known timestamp', () => {
      // Year 1985 (35 years from 1950 epoch), month 6, day 15, hour 10, min 30, sec 45
      // Bits: year=35(6) month=6(4) day=15(5) hour=10(5) min=30(6) sec=45(6)
      const value =
        ((35 & 0x3f) << 26) |
        ((6 & 0x0f) << 22) |
        ((15 & 0x1f) << 17) |
        ((10 & 0x1f) << 12) |
        ((30 & 0x3f) << 6) |
        (45 & 0x3f);
      const date = ndTimeToDate(value);
      expect(date).not.toBeNull();
      expect(date!.getFullYear()).toBe(1985);
      expect(date!.getMonth()).toBe(5); // 0-indexed
      expect(date!.getDate()).toBe(15);
      expect(date!.getHours()).toBe(10);
      expect(date!.getMinutes()).toBe(30);
      expect(date!.getSeconds()).toBe(45);
    });
  });

  describe('dateToNdTime', () => {
    it('returns 0 for null', () => {
      expect(dateToNdTime(null)).toBe(0);
    });

    it('returns 0 for year before epoch', () => {
      expect(dateToNdTime(new Date(1940, 0, 1))).toBe(0);
    });

    it('returns 0 for year beyond range', () => {
      expect(dateToNdTime(new Date(2020, 0, 1))).toBe(0); // 2020-1950=70 > 63
    });
  });

  describe('round-trip', () => {
    it('round-trips 1985-06-15 10:30:45', () => {
      const original = new Date(1985, 5, 15, 10, 30, 45);
      const packed = dateToNdTime(original);
      const unpacked = ndTimeToDate(packed);
      expect(unpacked).not.toBeNull();
      expect(unpacked!.getFullYear()).toBe(1985);
      expect(unpacked!.getMonth()).toBe(5);
      expect(unpacked!.getDate()).toBe(15);
      expect(unpacked!.getHours()).toBe(10);
      expect(unpacked!.getMinutes()).toBe(30);
      expect(unpacked!.getSeconds()).toBe(45);
    });

    it('round-trips 1950-01-01 00:00:01', () => {
      const original = new Date(1950, 0, 1, 0, 0, 1);
      const packed = dateToNdTime(original);
      const unpacked = ndTimeToDate(packed);
      expect(unpacked!.getFullYear()).toBe(1950);
      expect(unpacked!.getMonth()).toBe(0);
      expect(unpacked!.getDate()).toBe(1);
      expect(unpacked!.getSeconds()).toBe(1);
    });

    it('round-trips 2013-12-31 23:59:59', () => {
      const original = new Date(2013, 11, 31, 23, 59, 59);
      const packed = dateToNdTime(original);
      const unpacked = ndTimeToDate(packed);
      expect(unpacked!.getFullYear()).toBe(2013);
      expect(unpacked!.getMonth()).toBe(11);
      expect(unpacked!.getDate()).toBe(31);
      expect(unpacked!.getHours()).toBe(23);
      expect(unpacked!.getMinutes()).toBe(59);
      expect(unpacked!.getSeconds()).toBe(59);
    });
  });
});
