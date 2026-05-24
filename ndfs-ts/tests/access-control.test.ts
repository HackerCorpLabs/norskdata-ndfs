import { describe, it, expect } from 'vitest';
import { getAccessLevel, checkAccess, getFriendPermissions } from '../src/access-control.js';
import { AccessPermissions } from '../src/access-permissions.js';
import { UserEntry } from '../src/user-entry.js';
import { ObjectEntry } from '../src/object-entry.js';
import { UserFriend } from '../src/user-friend.js';
import { FileAccessType, FileOperationType } from '../src/types.js';

function makeUser(index: number, name: string): UserEntry {
  const u = new UserEntry();
  u.userIndex = index;
  u.userName = name;
  return u;
}

function makeFile(ownerIndex: number, accessBits: number = 0x4FF): ObjectEntry {
  const o = new ObjectEntry();
  o.userIndex = ownerIndex;
  o.accessBits = accessBits;
  return o;
}

describe('AccessControl', () => {
  describe('getAccessLevel', () => {
    it('owner gets Own access', () => {
      const owner = makeUser(0, 'OWNER');
      const file = makeFile(0);
      expect(getAccessLevel(file, owner, owner)).toBe(FileAccessType.Own);
    });

    it('friend gets Friend access', () => {
      const owner = makeUser(0, 'OWNER');
      const friend = makeUser(1, 'FRIEND');
      // Add friend to owner's friend list
      owner.addFriend(1, 0x1F); // all permissions
      const file = makeFile(0);
      expect(getAccessLevel(file, friend, owner)).toBe(FileAccessType.Friend);
    });

    it('stranger gets Public access', () => {
      const owner = makeUser(0, 'OWNER');
      const stranger = makeUser(2, 'STRANGER');
      const file = makeFile(0);
      expect(getAccessLevel(file, stranger, owner)).toBe(FileAccessType.Public);
    });
  });

  describe('checkAccess', () => {
    it('owner can read with default permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const file = makeFile(0, 0x4FF); // default: own=DXAWR, friend=DXAWR, public=R
      expect(checkAccess(file, owner, owner, FileOperationType.Read)).toBe(true);
    });

    it('owner can write with default permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const file = makeFile(0, 0x4FF);
      expect(checkAccess(file, owner, owner, FileOperationType.Write)).toBe(true);
    });

    it('owner can delete with default permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const file = makeFile(0, 0x4FF);
      expect(checkAccess(file, owner, owner, FileOperationType.Delete)).toBe(true);
    });

    it('public can read with default permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const stranger = makeUser(2, 'STRANGER');
      // 0x4FF = 000 0100 11111 11111
      // public bits: (0x4FF >>> 10) & 0x1F = 1 = read only
      const file = makeFile(0, 0x4FF);
      expect(checkAccess(file, stranger, owner, FileOperationType.Read)).toBe(true);
    });

    it('public cannot write with default permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const stranger = makeUser(2, 'STRANGER');
      const file = makeFile(0, 0x4FF);
      expect(checkAccess(file, stranger, owner, FileOperationType.Write)).toBe(false);
    });

    it('friend with specific permissions overrides file-level', () => {
      const owner = makeUser(0, 'OWNER');
      const friend = makeUser(1, 'FRIEND');
      // Friend has read+write in the owner's friend list
      owner.addFriend(1, 0x03); // read + write

      const file = makeFile(0, 0x001F); // own=DXAWR, friend=0, public=0
      // Even though file says friends have no access, the friend-specific perms should apply
      expect(checkAccess(file, friend, owner, FileOperationType.Read)).toBe(true);
      expect(checkAccess(file, friend, owner, FileOperationType.Write)).toBe(true);
    });
  });

  describe('permission string formatting', () => {
    it('formats default permissions correctly', () => {
      const perms = AccessPermissions.default();
      expect(perms.getPermissionString(FileAccessType.Own)).toBe('DXAWR');
    });

    it('formats owner-only permissions', () => {
      const perms = AccessPermissions.ownerOnly();
      expect(perms.getPermissionString(FileAccessType.Own)).toBe('DXAWR');
      expect(perms.getPermissionString(FileAccessType.Friend)).toBe('-----');
      expect(perms.getPermissionString(FileAccessType.Public)).toBe('-----');
    });

    it('formats read-only permissions', () => {
      const perms = new AccessPermissions(0x0001); // own=R only
      expect(perms.getPermissionString(FileAccessType.Own)).toBe('----R');
    });
  });

  describe('friend override', () => {
    it('friend-specific permissions take precedence over file permissions', () => {
      const owner = makeUser(0, 'OWNER');
      const friend = makeUser(1, 'FRIEND');
      // Give friend read+directory access
      const friendEntry = UserFriend.create(1, true, false, false, false, true);
      owner.friends[0] = friendEntry;

      const file = makeFile(0, 0x7FFF); // all permissions for all tiers

      // Friend should be able to read (friend-specific says yes)
      expect(checkAccess(file, friend, owner, FileOperationType.Read)).toBe(true);
      // Friend should NOT be able to write (friend-specific says no)
      expect(checkAccess(file, friend, owner, FileOperationType.Write)).toBe(false);
    });
  });

  describe('getFriendPermissions', () => {
    it('returns friend entry when user is a friend', () => {
      const owner = makeUser(0, 'OWNER');
      const friend = makeUser(1, 'FRIEND');
      owner.addFriend(1, 0x1F);

      const entry = getFriendPermissions(friend, owner);
      expect(entry).not.toBeNull();
      expect(entry!.friendUserIndex).toBe(1);
    });

    it('returns null when user is not a friend', () => {
      const owner = makeUser(0, 'OWNER');
      const stranger = makeUser(2, 'STRANGER');

      const entry = getFriendPermissions(stranger, owner);
      expect(entry).toBeNull();
    });
  });
});
