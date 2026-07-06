import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ImageTemplate } from '../src/types.js';
import { ENTRIES_PER_PAGE } from '../src/constants.js';

function createFS(pages: number = 200): NdfsFileSystem {
  return NdfsFileSystem.createImage({
    template: ImageTemplate.Custom,
    customPages: pages,
    directoryName: 'USERMGMT',
  });
}

describe('UserManagement', () => {
  it('add user with quota, verify in user list', () => {
    const fs = createFS();
    expect(fs.addUser('ALICE', 500)).toBe(true);
    const users = fs.getUsers();
    expect(users.length).toBe(2);
    const alice = users.find((u) => u.userName === 'ALICE');
    expect(alice).toBeDefined();
    expect(alice!.pagesReserved).toBe(500);
  });

  it('remove user with no files succeeds', () => {
    const fs = createFS();
    fs.addUser('TEMP', 100);
    const temp = fs.getUsers().find((u) => u.userName === 'TEMP');
    expect(fs.removeUser(temp!.userIndex)).toBe(true);
    expect(fs.getUsers().length).toBe(1);
  });

  it('remove user with files fails', () => {
    const fs = createFS();
    fs.writeFile('SYSTEM/FILE:DATA', new TextEncoder().encode('data'));
    expect(fs.removeUser(0)).toBe(false);
  });

  it('update user quota', () => {
    const fs = createFS();
    expect(fs.updateUserQuota(0, 5000)).toBe(true);
    expect(fs.getUser(0)!.pagesReserved).toBe(5000);
  });

  it('clear user password', () => {
    const fs = createFS();
    // Set password manually would require internal access, so just test clear
    expect(fs.clearUserPassword(0)).toBe(true);
    expect(fs.getUser(0)!.password).toBe(0);
  });

  it('clear user password by name', () => {
    const fs = createFS();
    expect(fs.clearUserPassword('SYSTEM')).toBe(true);
  });

  it('add many users (up to limit)', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 500,
      directoryName: 'MANYUSERS',
    });
    // Add users up to some reasonable count
    let addedCount = 0;
    for (let i = 0; i < 50; i++) {
      const name = `USER${String(i).padStart(3, '0')}`;
      if (fs.addUser(name, 10)) {
        addedCount++;
      }
    }
    // Should have added at least 50 users (1 SYSTEM + 50 new)
    expect(fs.getUsers().length).toBe(51);
  });

  it('duplicate user name rejected', () => {
    const fs = createFS();
    expect(fs.addUser('SYSTEM', 500)).toBe(false);
    expect(fs.getUsers().length).toBe(1);
  });

  it('user name max 16 chars', () => {
    const fs = createFS();
    const longName = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'; // 26 chars
    expect(fs.addUser(longName, 100)).toBe(true);
    const users = fs.getUsers();
    const added = users.find((u) => u.userName !== 'SYSTEM');
    expect(added).toBeDefined();
    expect(added!.userName.length).toBeLessThanOrEqual(16);
  });

  it('user name is uppercased', () => {
    const fs = createFS();
    expect(fs.addUser('lowercase', 100)).toBe(true);
    const users = fs.getUsers();
    const added = users.find((u) => u.userName !== 'SYSTEM');
    expect(added).toBeDefined();
    expect(added!.userName).toBe('LOWERCASE');
  });

  it('users added past the first UserFile page survive a round-trip through disk', () => {
    // A freshly created small image only pre-allocates the first UserFile
    // directory page (32 entries incl. SYSTEM). Adding enough users to spill
    // into the second page (userIndex >= ENTRIES_PER_PAGE) used to hit a
    // silent no-op in writeUserPage() when that page's disk block wasn't yet
    // allocated -- the user existed only in memory and vanished on remount.
    // This test forces that spill and reopens the image from a fresh buffer
    // (not the same in-memory instance) so an in-memory-only bug would be
    // caught.
    const fs1 = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 500,
      directoryName: 'SPILLUSR',
    });

    const total = 40; // > ENTRIES_PER_PAGE (32), forces a second UserFile page
    for (let i = 0; i < total; i++) {
      const name = `USR${String(i).padStart(3, '0')}`;
      expect(fs1.addUser(name, 10 + i)).toBe(true);
    }
    // SYSTEM (index 0) + 40 new users.
    expect(fs1.getUsers().length).toBe(total + 1);

    // Round-trip through a brand-new buffer/instance, simulating a remount.
    const fs2 = new NdfsFileSystem(fs1.toBuffer());
    const users2 = fs2.getUsers();
    expect(users2.length).toBe(total + 1);

    for (let i = 0; i < total; i++) {
      const name = `USR${String(i).padStart(3, '0')}`;
      const u = users2.find((u) => u.userName === name);
      expect(u, `user ${name} missing after reopen`).toBeDefined();
    }

    // Spot-check a user whose slot definitely lives in the newly allocated
    // second page (userIndex >= ENTRIES_PER_PAGE), and verify its fields
    // (not just presence) survived the round-trip correctly.
    const secondPageUser = users2.find((u) => u.userIndex >= ENTRIES_PER_PAGE);
    expect(secondPageUser, 'expected at least one user in the second UserFile page').toBeDefined();
    const expectedIndex = secondPageUser!.userIndex - 1; // SYSTEM occupies index 0
    expect(secondPageUser!.userName).toBe(`USR${String(expectedIndex).padStart(3, '0')}`);
    expect(secondPageUser!.pagesReserved).toBe(10 + expectedIndex);
  });
});
