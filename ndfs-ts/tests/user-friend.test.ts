import { describe, it, expect } from 'vitest';
import { UserFriend } from '../src/user-friend.js';

describe('UserFriend', () => {
  describe('constructor', () => {
    it('defaults to unused', () => {
      const f = new UserFriend();
      expect(f.entryUsed).toBe(false);
      expect(f.bits).toBe(0);
    });

    it('parses packed bits', () => {
      // bit15=1 (used), bit9=1 (write), bit8=1 (read), index=42
      const bits = (1 << 15) | (1 << 9) | (1 << 8) | 42;
      const f = new UserFriend(bits);
      expect(f.entryUsed).toBe(true);
      expect(f.readAccess).toBe(true);
      expect(f.writeAccess).toBe(true);
      expect(f.appendAccess).toBe(false);
      expect(f.friendUserIndex).toBe(42);
    });
  });

  describe('create', () => {
    it('creates with all permissions', () => {
      const f = UserFriend.create(10, true, true, true, true, true);
      expect(f.entryUsed).toBe(true);
      expect(f.friendUserIndex).toBe(10);
      expect(f.readAccess).toBe(true);
      expect(f.writeAccess).toBe(true);
      expect(f.appendAccess).toBe(true);
      expect(f.commonAccess).toBe(true);
      expect(f.directoryAccess).toBe(true);
    });

    it('creates with read-only', () => {
      const f = UserFriend.create(5, true);
      expect(f.readAccess).toBe(true);
      expect(f.writeAccess).toBe(false);
    });
  });

  describe('setFriend / clear', () => {
    it('sets friend with permissions', () => {
      const f = new UserFriend();
      f.setFriend(7, 0x03); // bits 0-1 = read+write
      expect(f.entryUsed).toBe(true);
      expect(f.friendUserIndex).toBe(7);
      expect(f.readAccess).toBe(true);
      expect(f.writeAccess).toBe(true);
      expect(f.appendAccess).toBe(false);
    });

    it('clears entry', () => {
      const f = UserFriend.create(5, true, true);
      f.clear();
      expect(f.entryUsed).toBe(false);
      expect(f.bits).toBe(0);
    });
  });

  describe('fromBytes / toBytes', () => {
    it('round-trips through bytes', () => {
      const f = UserFriend.create(42, true, false, true, false, true);
      const buf = new Uint8Array(4);
      f.toBytes(buf, 1);
      const f2 = UserFriend.fromBytes(buf, 1);
      expect(f2.entryUsed).toBe(true);
      expect(f2.friendUserIndex).toBe(42);
      expect(f2.readAccess).toBe(true);
      expect(f2.writeAccess).toBe(false);
      expect(f2.appendAccess).toBe(true);
      expect(f2.directoryAccess).toBe(true);
    });
  });

  describe('getPermissionString', () => {
    it('returns ----- for unused', () => {
      expect(new UserFriend().getPermissionString()).toBe('-----');
    });

    it('returns RWACD for all permissions', () => {
      const f = UserFriend.create(0, true, true, true, true, true);
      expect(f.getPermissionString()).toBe('RWACD');
    });

    it('returns R---- for read-only', () => {
      const f = UserFriend.create(0, true);
      expect(f.getPermissionString()).toBe('R----');
    });
  });
});
