import { describe, it, expect } from 'vitest';
import { UserEntry } from '../src/user-entry.js';
import { ENTRY_SIZE, USER_ENTRY_FLAG, NDFS_NAME_TERMINATOR } from '../src/constants.js';
import { writeUint16BE, writeUint32BE } from '../src/endian.js';

function buildUserBytes(
  name: string,
  userIndex: number,
  pagesReserved: number,
  pagesUsed: number,
  password: number = 0,
): Uint8Array {
  const buf = new Uint8Array(ENTRY_SIZE);
  buf[0] = USER_ENTRY_FLAG; // 0x81
  // Name at offset 2
  for (let i = 0; i < name.length && i < 16; i++) {
    buf[2 + i] = name.charCodeAt(i);
  }
  if (name.length < 16) buf[2 + name.length] = NDFS_NAME_TERMINATOR;
  writeUint16BE(buf, 18, password);
  writeUint32BE(buf, 28, pagesReserved);
  writeUint32BE(buf, 32, pagesUsed);
  buf[37] = userIndex;
  return buf;
}

describe('UserEntry', () => {
  describe('fromBytes', () => {
    it('parses valid user entry', () => {
      const data = buildUserBytes('SYSTEM', 0, 1000, 50, 0x1234);
      const user = UserEntry.fromBytes(data, 0);
      expect(user).not.toBeNull();
      expect(user!.userName).toBe('SYSTEM');
      expect(user!.userIndex).toBe(0);
      expect(user!.pagesReserved).toBe(1000);
      expect(user!.pagesUsed).toBe(50);
      expect(user!.password).toBe(0x1234);
    });

    it('returns null for invalid flag', () => {
      const data = new Uint8Array(ENTRY_SIZE);
      data[0] = 0x00; // not 0x81
      expect(UserEntry.fromBytes(data, 0)).toBeNull();
    });

    it('parses at offset', () => {
      const data = new Uint8Array(ENTRY_SIZE * 2);
      const userBytes = buildUserBytes('USER1', 5, 200, 10);
      data.set(userBytes, ENTRY_SIZE);
      const user = UserEntry.fromBytes(data, ENTRY_SIZE);
      expect(user).not.toBeNull();
      expect(user!.userName).toBe('USER1');
      expect(user!.userIndex).toBe(5);
    });
  });

  describe('toBytes round-trip', () => {
    it('round-trips all fields', () => {
      const user = new UserEntry();
      user.userName = 'TESTUSER';
      user.userIndex = 3;
      user.pagesReserved = 500;
      user.pagesUsed = 100;
      user.password = 0xabcd;

      const bytes = user.toBytes();
      const parsed = UserEntry.fromBytes(bytes, 0);

      expect(parsed).not.toBeNull();
      expect(parsed!.userName).toBe('TESTUSER');
      expect(parsed!.userIndex).toBe(3);
      expect(parsed!.pagesReserved).toBe(500);
      expect(parsed!.pagesUsed).toBe(100);
      expect(parsed!.password).toBe(0xabcd);
    });
  });

  describe('quota', () => {
    it('detects over-quota', () => {
      const user = new UserEntry();
      user.pagesReserved = 100;
      user.pagesUsed = 150;
      expect(user.isOverQuota()).toBe(true);
    });

    it('detects under-quota', () => {
      const user = new UserEntry();
      user.pagesReserved = 100;
      user.pagesUsed = 50;
      expect(user.isOverQuota()).toBe(false);
      expect(user.getFreePages()).toBe(50);
    });
  });

  describe('setName', () => {
    it('uppercases name', () => {
      const user = new UserEntry();
      user.setName('lowercase');
      expect(user.userName).toBe('LOWERCASE');
    });

    it('truncates to 16 chars', () => {
      const user = new UserEntry();
      user.setName('VERYLONGNAMETHATEXCEEDS');
      expect(user.userName.length).toBe(16);
    });

    it('throws on empty name', () => {
      const user = new UserEntry();
      expect(() => user.setName('')).toThrow();
    });
  });

  describe('friends', () => {
    it('adds and finds friend', () => {
      const user = new UserEntry();
      expect(user.addFriend(5, 0x03)).toBe(true); // read + write
      expect(user.isFriend(5)).toBe(true);
      expect(user.isFriend(6)).toBe(false);
    });

    it('removes friend', () => {
      const user = new UserEntry();
      user.addFriend(5, 0x01);
      expect(user.removeFriend(5)).toBe(true);
      expect(user.isFriend(5)).toBe(false);
    });

    it('returns false when friend slots full', () => {
      const user = new UserEntry();
      for (let i = 0; i < 8; i++) user.addFriend(i, 0x01);
      expect(user.addFriend(99, 0x01)).toBe(false);
    });

    it('getFriend returns entry with permissions', () => {
      const user = new UserEntry();
      user.addFriend(10, 0x03); // read + write
      const f = user.getFriend(10);
      expect(f).not.toBeNull();
      expect(f!.readAccess).toBe(true);
      expect(f!.writeAccess).toBe(true);
    });
  });
});
