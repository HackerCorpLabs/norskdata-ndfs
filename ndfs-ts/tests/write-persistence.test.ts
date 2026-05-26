import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ImageTemplate } from '../src/types.js';
import { NDFS_PAGE_SIZE } from '../src/constants.js';

function createFS(pages: number = 200): NdfsFileSystem {
  return NdfsFileSystem.createImage({
    template: ImageTemplate.Custom,
    customPages: pages,
    directoryName: 'PERSIST',
  });
}

describe('WritePersistence', () => {
  it('write file, export to buffer, re-open, verify data byte-for-byte', () => {
    const fs1 = createFS();
    const data = new Uint8Array(500);
    for (let i = 0; i < data.length; i++) data[i] = (i * 7 + 13) & 0xFF;
    fs1.writeFile('SYSTEM/PERSIST:DATA', data);

    const buf = fs1.toBuffer();
    const fs2 = new NdfsFileSystem(buf);
    const read = fs2.readFile('SYSTEM/PERSIST:DATA');
    expect(read.length).toBe(data.length);
    for (let i = 0; i < data.length; i++) {
      expect(read[i]).toBe(data[i]);
    }
  });

  it('write multiple files, re-mount, verify all present', () => {
    const fs1 = createFS();
    for (let i = 0; i < 5; i++) {
      fs1.writeFile(`SYSTEM/FILE${i}:DATA`, new TextEncoder().encode(`content-${i}`));
    }

    const fs2 = new NdfsFileSystem(fs1.toBuffer());
    const entries = fs2.listDirectory('SYSTEM');
    expect(entries.length).toBe(5);

    for (let i = 0; i < 5; i++) {
      const content = new TextDecoder().decode(fs2.readFile(`SYSTEM/FILE${i}:DATA`));
      expect(content).toBe(`content-${i}`);
    }
  });

  it('add user, write to buffer, re-mount, verify user exists', () => {
    const fs1 = createFS();
    fs1.addUser('TESTUSER', 100);

    const fs2 = new NdfsFileSystem(fs1.toBuffer());
    const users = fs2.getUsers();
    expect(users.length).toBe(2);
    const found = users.find((u) => u.userName === 'TESTUSER');
    expect(found).toBeDefined();
    expect(found!.pagesReserved).toBe(100);
  });

  it('delete file, re-mount, verify deleted', () => {
    const fs1 = createFS();
    fs1.writeFile('SYSTEM/TEMP:DATA', new TextEncoder().encode('temporary'));
    fs1.deleteFile('SYSTEM/TEMP:DATA');

    const fs2 = new NdfsFileSystem(fs1.toBuffer());
    expect(fs2.fileExists('SYSTEM/TEMP:DATA')).toBe(false);
    expect(fs2.listDirectory('SYSTEM').length).toBe(0);
  });

  it('modify user quota, re-mount, verify', () => {
    const fs1 = createFS();
    fs1.updateUserQuota(0, 9999);

    const fs2 = new NdfsFileSystem(fs1.toBuffer());
    expect(fs2.getUser(0)!.pagesReserved).toBe(9999);
  });

  it('new files get a self-referential version chain and [user|slot] index', () => {
    const fs = createFS();
    fs.writeFile('SYSTEM/A:TEXT', new Uint8Array([1, 2, 3]));
    fs.writeFile('SYSTEM/B:TEXT', new Uint8Array([4, 5, 6]));
    for (const e of fs.getObjectEntries()) {
      expect(e.nextVersion).toBe(e.diskObjectIndex);
      expect(e.prevVersion).toBe(e.diskObjectIndex);
      expect((e.diskObjectIndex >> 8) & 0xff).toBe(e.userIndex);
    }
  });

  it('new file lands in the owning user region (object index high byte = user)', () => {
    const fs = createFS();
    fs.addUser('GUEST', 200);
    const guest = fs.listDirectory('').find((e) => e.name === 'GUEST');
    expect(guest).toBeDefined();
    fs.writeFile('GUEST/HELLO:TEXT', new Uint8Array([1, 2]));
    const e = fs.getObjectEntry('HELLO', 'GUEST')!;
    expect(e.userIndex).not.toBe(0);
    expect((e.diskObjectIndex >> 8) & 0xff).toBe(e.userIndex);
    expect(e.objectIndex).toBeGreaterThanOrEqual(e.userIndex * 256);
    expect(e.objectIndex).toBeLessThan((e.userIndex + 1) * 256);
    expect(e.nextVersion).toBe(e.diskObjectIndex);
    // survives a round-trip
    const fs2 = new NdfsFileSystem(fs.toBuffer());
    const e2 = fs2.getObjectEntry('HELLO', 'GUEST')!;
    expect((e2.diskObjectIndex >> 8) & 0xff).toBe(e2.userIndex);
  });

  it('deleted file stays deleted after export + re-open', () => {
    const fs = createFS();
    fs.writeFile('SYSTEM/A:TEXT', new Uint8Array([1]));
    fs.writeFile('SYSTEM/B:TEXT', new Uint8Array([2]));
    fs.deleteFile('SYSTEM/A:TEXT');
    const fs2 = new NdfsFileSystem(fs.toBuffer());
    expect(fs2.fileExists('SYSTEM/A:TEXT')).toBe(false);
    expect(fs2.fileExists('SYSTEM/B:TEXT')).toBe(true);
  });
});
