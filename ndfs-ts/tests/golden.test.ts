import { describe, it, expect } from 'vitest';
import { ObjectEntry } from '../src/object-entry.js';
import { UserEntry } from '../src/user-entry.js';

// Real 64-byte entries from a genuine SINTRAN image (tor-disk.img). These pin
// the on-disk format to ground truth and prove lossless round-trips.
function hex(s: string): Uint8Array {
  const b = new Uint8Array(s.length / 2);
  for (let i = 0; i < b.length; i++) b[i] = parseInt(s.substr(i * 2, 2), 16);
  return b;
}

const GOLDEN_OBJ = hex(
  '900053494e5452414e270000000000000000444154410000000007ff0020' +
    '00000000000000000028962696859a08a18b9a08a18b0000003f0001e7ff00000001',
);
const GOLDEN_USR = hex(
  '810353595354454d27000000000000000000575796269683b22b73ec0000717a' +
    '00004cc20000000004ff00000000000087018707811800000000000000000000',
);

describe('Golden byte-vectors (real SINTRAN entries)', () => {
  it('object entry parses to the expected fields', () => {
    const e = ObjectEntry.fromBytes(GOLDEN_OBJ, 0)!;
    expect(e.objectName).toBe('SINTRAN');
    expect(e.type).toBe('DATA');
    expect(e.nextVersion).toBe(0);
    expect(e.prevVersion).toBe(0);
    expect(e.accessBits).toBe(0x07ff);
    expect(e.fileTypeFlags).toBe(0x0020);
    expect(e.userIndex).toBe(0);
    expect(e.totalOpenCount).toBe(40);
    expect(e.dateCreated >>> 0).toBe(0x96269685);
    expect(e.lastDateWritten >>> 0).toBe(0x9a08a18b);
    expect(e.pagesInFile).toBe(63);
    expect(e.bytesInFile).toBe(124928);
  });

  it('object entry round-trips byte-for-byte', () => {
    const e = ObjectEntry.fromBytes(GOLDEN_OBJ, 0)!;
    const out = new Uint8Array(64);
    e.toBytes(out, 0);
    expect(Array.from(out)).toEqual(Array.from(GOLDEN_OBJ));
  });

  it('user entry parses to the expected fields', () => {
    const u = UserEntry.fromBytes(GOLDEN_USR, 0)!;
    expect(u.userName).toBe('SYSTEM');
    expect(u.enterCount).toBe(3);
    expect(u.pagesReserved).toBe(29050);
    expect(u.pagesUsed).toBe(19650);
    expect(u.userIndex).toBe(0);
    expect(u.defaultFileAccess).toBe(0x04ff);
    expect(u.friends[0].entryUsed).toBe(true);
    expect(u.friends[1].entryUsed).toBe(true);
    expect(u.friends[2].entryUsed).toBe(true);
    expect(u.friends[3].entryUsed).toBe(false);
  });

  it('user entry round-trips byte-for-byte', () => {
    const u = UserEntry.fromBytes(GOLDEN_USR, 0)!;
    expect(Array.from(u.toBytes())).toEqual(Array.from(GOLDEN_USR));
  });

  // Object entry whose type field is intentionally empty on disk: offset 18 =
  // 0x27 (terminator) + NULs. SINTRAN writes such entries (e.g. TERMINAL).
  // Parsing must NOT default the empty type to 'DATA', or the round-trip
  // clobbers 27 00 00 00 with 44 41 54 41. Regression for Bug 1.
  const GOLDEN_OBJ_EMPTY_TYPE = hex(
    '90005445524d494e414c270000000000' +
      '0000270000000000000007ff00200000' +
      '0000000000000028962696859a08a18b' +
      '9a08a18b0000003f0001e7ff00000001',
  );

  it('preserves an empty type field (does not default to DATA)', () => {
    const e = ObjectEntry.fromBytes(GOLDEN_OBJ_EMPTY_TYPE, 0)!;
    expect(e.objectName).toBe('TERMINAL');
    expect(e.type).toBe('');
  });

  it('round-trips an empty type field byte-for-byte', () => {
    const e = ObjectEntry.fromBytes(GOLDEN_OBJ_EMPTY_TYPE, 0)!;
    const out = new Uint8Array(64);
    e.toBytes(out, 0);
    expect(Array.from(out)).toEqual(Array.from(GOLDEN_OBJ_EMPTY_TYPE));
  });
});
