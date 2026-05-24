import { describe, it, expect } from 'vitest';
import { AccessPermissions, PERM_READ, PERM_WRITE, PERM_APPEND, PERM_EXECUTE, PERM_DELETE } from '../src/access-permissions.js';
import { FileAccessType } from '../src/types.js';

describe('AccessPermissions', () => {
  describe('default', () => {
    it('creates 0x4FF default permissions', () => {
      const p = AccessPermissions.default();
      expect(p.rawBits).toBe(0x4ff);
    });
  });

  describe('fromTiers', () => {
    it('encodes separate tier values', () => {
      // Own=all(0x1f), Friend=RW(0x03), Public=R(0x01)
      const p = AccessPermissions.fromTiers(0x1f, 0x03, 0x01);
      expect(p.canRead(FileAccessType.Own)).toBe(true);
      expect(p.canDelete(FileAccessType.Own)).toBe(true);
      expect(p.canRead(FileAccessType.Friend)).toBe(true);
      expect(p.canWrite(FileAccessType.Friend)).toBe(true);
      expect(p.canAppend(FileAccessType.Friend)).toBe(false);
      expect(p.canRead(FileAccessType.Public)).toBe(true);
      expect(p.canWrite(FileAccessType.Public)).toBe(false);
    });
  });

  describe('permission checks', () => {
    it('checks individual permissions', () => {
      const p = new AccessPermissions(0);
      p.setPermission(FileAccessType.Own, PERM_READ, true);
      p.setPermission(FileAccessType.Own, PERM_WRITE, true);
      expect(p.canRead(FileAccessType.Own)).toBe(true);
      expect(p.canWrite(FileAccessType.Own)).toBe(true);
      expect(p.canDelete(FileAccessType.Own)).toBe(false);
    });
  });

  describe('getPermissionString', () => {
    it('formats all permissions', () => {
      const p = AccessPermissions.fromTiers(0x1f, 0, 0);
      expect(p.getPermissionString(FileAccessType.Own)).toBe('DXAWR');
    });

    it('formats no permissions', () => {
      const p = new AccessPermissions(0);
      expect(p.getPermissionString(FileAccessType.Own)).toBe('-----');
    });
  });

  describe('ownerOnly', () => {
    it('gives full access to owner, none to others', () => {
      const p = AccessPermissions.ownerOnly();
      expect(p.canRead(FileAccessType.Own)).toBe(true);
      expect(p.canDelete(FileAccessType.Own)).toBe(true);
      expect(p.canRead(FileAccessType.Friend)).toBe(false);
      expect(p.canRead(FileAccessType.Public)).toBe(false);
    });
  });
});
