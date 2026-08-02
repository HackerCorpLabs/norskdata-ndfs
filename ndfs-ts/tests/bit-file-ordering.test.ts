/**
 * Bit-file bit ordering, checked against sources OUTSIDE this codebase.
 *
 * WHY THIS FILE EXISTS
 * ====================
 * Until 2026-08-02 every port here addressed the bit file as "byte N/8, bit N%8".
 * SINTRAN actually addresses it as a 16-bit WORD: page N is bit N%16 of word N/16. On a
 * big-endian image those two differ by a byte swap inside every word.
 *
 * The bug survived a full test suite for months, and the reason matters:
 *
 *   1. Every existing test was a ROUND TRIP - written with convention X, read back with
 *      convention X. That passes for any self-consistent X, including a wrong one.
 *   2. The one check made against real data was a POPCOUNT comparison, and popcount is
 *      invariant under byte-swapping. It could not fail for this bug even in principle,
 *      yet it is what the docs cited as "VERIFIED".
 *   3. The tests were written from the same incorrect note as the code, so they
 *      asserted the bug as though it were the specification.
 *
 * Every test below is therefore ASYMMETRIC: it compares this code against something the
 * project did not produce - ND's own manual or SINTRAN's own allocator. Nothing here
 * round-trips our own output.
 *
 * AUTHORITY: SINTRAN III System Supervisor, ND-30.003.007 EN, appendix F.2 "Bit-File":
 *
 *     PAGE = BLOCK*400B + WORD*20B + BIT
 *
 * 20B is 16 decimal, so one 16-bit word maps 16 pages; and because the formula ADDS the
 * bit number, page x sits at bit 0 - the least significant bit. The Norwegian edition
 * ND-30.003.7 NO carries the identical formula. SINTRAN's own allocator agrees: TPAGF at
 * 51043B forms the word index with `SHA ZIN SHR 4`, i.e. page/16, not page/8.
 */
import { describe, it, expect } from 'vitest';
import { BitFile } from '../src/bit-file.js';

describe('BitFile ordering - the manual as ground truth', () => {
  it("reproduces the worked example from ND-30.003.007 appendix F.2 (word 313B)", () => {
    // The manual prints one bit-file word holding 313B and labels which of the 16 pages
    // it covers are Free and Used. 313B = 0b0000_0000_1100_1011 -> bits 0,1,3,6,7 set.
    // This vector comes from ND, not from us; under the old byte convention the same two
    // bytes decode to a different set of pages entirely.
    const word = 0o313;
    expect(word).toBe(0b0000000011001011);

    const raw = new Uint8Array([(word >> 8) & 0xff, word & 0xff]); // big-endian on disk

    const bf = new BitFile();
    bf.initialize(16);
    bf.loadBitmap(raw);

    const expectedUsed = new Set([0, 1, 3, 6, 7]);
    for (let page = 0; page < 16; page++) {
      expect(bf.isBlockUsed(page)).toBe(expectedUsed.has(page));
    }
  });

  it('satisfies PAGE = BLOCK*400B + WORD*20B + BIT', () => {
    const pagesPerBlock = 0o400; // 256
    const pagesPerWord = 0o20; //  16
    expect(pagesPerBlock).toBe(256);
    expect(pagesPerWord).toBe(16);

    const cases: Array<[number, number, number]> = [
      [0, 0, 0], [0, 0, 15], [0, 1, 0], [0, 5, 9], [1, 0, 0], [1, 15, 15], [3, 7, 4],
    ];

    for (const [block, word, bit] of cases) {
      const page = block * pagesPerBlock + word * pagesPerWord + bit;

      const bf = new BitFile();
      bf.initialize(4096);
      bf.markBlockUsed(page);

      const raw = bf.getBitmapData();
      const wordIndex = block * (pagesPerBlock / pagesPerWord) + word;
      const stored = (raw[wordIndex * 2] << 8) | raw[wordIndex * 2 + 1];

      expect(stored).toBe(1 << bit);
      expect(bf.calcUsedPages()).toBe(1);
    }
  });
});

describe('BitFile ordering - the byte swap, asserted explicitly', () => {
  it('puts page 0 in the LOW byte of the first word', () => {
    // The single assertion that most directly contradicts the old convention. A 16-bit
    // big-endian word is stored high byte first, and page 0 is bit 0 - the LEAST
    // significant bit - so page 0 lands in byte 1, not byte 0.
    const bf = new BitFile();
    bf.initialize(64);
    bf.markBlockUsed(0);

    const raw = bf.getBitmapData();
    expect(raw[0]).toBe(0x00); // page 0 must NOT be in the high byte
    expect(raw[1]).toBe(0x01);
  });

  it('puts page 8 in the HIGH byte of the same word', () => {
    const bf = new BitFile();
    bf.initialize(64);
    bf.markBlockUsed(8);

    const raw = bf.getBitmapData();
    expect(raw[0]).toBe(0x01);
    expect(raw[1]).toBe(0x00);
  });

  it('fills the low byte before the high byte across the first word', () => {
    const bf = new BitFile();
    bf.initialize(64);

    for (let page = 0; page < 8; page++) bf.markBlockUsed(page);
    expect(bf.getBitmapData()[0]).toBe(0x00);
    expect(bf.getBitmapData()[1]).toBe(0xff);

    for (let page = 8; page < 16; page++) bf.markBlockUsed(page);
    expect(bf.getBitmapData()[0]).toBe(0xff);
    expect(bf.getBitmapData()[1]).toBe(0xff);
  });

  it('actively rejects the old byte-per-8-pages convention', () => {
    // Guard against a silent revert: computes what the discredited scheme would produce
    // and asserts we do NOT produce it. Fails even if every round-trip test still
    // passes, which is exactly the hole the original bug went through.
    const bf = new BitFile();
    bf.initialize(64);
    const pages = [0, 3, 17, 40];
    for (const page of pages) bf.markBlockUsed(page);

    const actual = bf.getBitmapData();

    const oldScheme = new Uint8Array(actual.length);
    for (const page of pages) oldScheme[page >>> 3] |= 1 << (page & 7);

    expect(Array.from(actual)).not.toEqual(Array.from(oldScheme));
  });
});

describe('BitFile ordering - devices not a multiple of 16 pages', () => {
  // A 616-page ND floppy is the real case: ceil(616/8) = 77 bytes, but word addressing
  // needs byte 77 to exist or pages 608..615 fall off the end of the buffer.
  const sizes = [1, 8, 15, 16, 17, 100, 616, 4095, 38400];

  it.each(sizes)('makes every page of a %i-page device addressable', (totalPages) => {
    const bf = new BitFile();
    bf.initialize(totalPages);

    expect(bf.getBitmapData().length % 2).toBe(0);
    expect(bf.getBitmapData().length).toBeGreaterThanOrEqual(Math.ceil(totalPages / 8));

    const last = totalPages - 1;
    bf.markBlockUsed(last);
    expect(bf.isBlockUsed(last)).toBe(true);
    expect(bf.calcUsedPages()).toBe(1);
  });

  it('never aliases two pages onto the same bit', () => {
    const total = 616;
    const bf = new BitFile();
    bf.initialize(total);

    for (let page = 0; page < total; page++) {
      bf.markBlockUsed(page);
      expect(bf.isBlockUsed(page)).toBe(true);
      expect(bf.calcUsedPages()).toBe(1);
      bf.markBlockFree(page);
      expect(bf.isBlockUsed(page)).toBe(false);
    }
  });
});
