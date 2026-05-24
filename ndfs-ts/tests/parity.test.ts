import { describe, it, expect } from 'vitest';
import { stripParity, setParity, isTextType } from '../src/parity.js';

describe('parity', () => {
  describe('stripParity', () => {
    it('clears bit 7 on all bytes', () => {
      const input = new Uint8Array([0xc8, 0xe5, 0xec, 0xec, 0xef]);
      const result = stripParity(input);
      expect(result[0]).toBe(0x48); // H
      expect(result[1]).toBe(0x65); // e
      expect(result[2]).toBe(0x6c); // l
      expect(result[3]).toBe(0x6c); // l
      expect(result[4]).toBe(0x6f); // o
    });

    it('does not modify bytes with bit 7 already clear', () => {
      const input = new Uint8Array([0x48, 0x65, 0x6c]);
      const result = stripParity(input);
      expect(result[0]).toBe(0x48);
      expect(result[1]).toBe(0x65);
      expect(result[2]).toBe(0x6c);
    });

    it('handles empty input', () => {
      expect(stripParity(new Uint8Array(0)).length).toBe(0);
    });
  });

  describe('setParity', () => {
    it('sets even parity on each byte', () => {
      const input = new Uint8Array([
        0x48, // H: 01001000 -> 2 ones (even) -> bit7=0 -> 0x48
        0x65, // e: 01100101 -> 4 ones (even) -> bit7=0 -> 0x65
        0x20, // ' ': 00100000 -> 1 one (odd) -> bit7=1 -> 0xA0
        0x57, // W: 01010111 -> 5 ones (odd) -> bit7=1 -> 0xD7
        0x64, // d: 01100100 -> 3 ones (odd) -> bit7=1 -> 0xE4
      ]);
      const result = setParity(input);
      expect(result[0]).toBe(0x48); // even, no change
      expect(result[1]).toBe(0x65); // even, no change
      expect(result[2]).toBe(0xa0); // odd -> set bit 7
      expect(result[3]).toBe(0xd7); // odd -> set bit 7
      expect(result[4]).toBe(0xe4); // odd -> set bit 7
    });

    it('every byte has even total parity', () => {
      const input = new Uint8Array(256);
      for (let i = 0; i < 256; i++) input[i] = i;
      const result = setParity(input);
      for (let i = 0; i < 256; i++) {
        const ones = result[i].toString(2).split('').filter(c => c === '1').length;
        expect(ones % 2).toBe(0);
      }
    });

    it('round-trips with stripParity', () => {
      const original = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64]);
      const withParity = setParity(original);
      const stripped = stripParity(withParity);
      for (let i = 0; i < original.length; i++) {
        expect(stripped[i]).toBe(original[i] & 0x7f);
      }
    });
  });

  describe('isTextType', () => {
    it('identifies text types', () => {
      expect(isTextType('MODE')).toBe(true);
      expect(isTextType('SYMB')).toBe(true);
      expect(isTextType('TEXT')).toBe(true);
      expect(isTextType('C')).toBe(true);
      expect(isTextType('BATC')).toBe(true);
      expect(isTextType('FORT')).toBe(true);
    });

    it('rejects binary types', () => {
      expect(isTextType('PROG')).toBe(false);
      expect(isTextType('BPUN')).toBe(false);
      expect(isTextType('DATA')).toBe(false);
      expect(isTextType('VTM')).toBe(false);
    });

    it('is case-insensitive', () => {
      expect(isTextType('mode')).toBe(true);
      expect(isTextType('Mode')).toBe(true);
    });

    it('handles empty string', () => {
      expect(isTextType('')).toBe(false);
    });
  });
});
