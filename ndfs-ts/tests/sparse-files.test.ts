import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ImageTemplate } from '../src/types.js';
import { NDFS_PAGE_SIZE } from '../src/constants.js';

function createFS(pages: number = 200): NdfsFileSystem {
  return NdfsFileSystem.createImage({
    template: ImageTemplate.Custom,
    customPages: pages,
    directoryName: 'SPARSE',
  });
}

describe('SparseFiles', () => {
  it('write file with all zeros should use sparse allocation', () => {
    const fs = createFS();
    const freeBefore = fs.getFreePages();

    // Write a multi-page all-zero file
    const data = new Uint8Array(NDFS_PAGE_SIZE * 5); // all zeros
    fs.writeFile('SYSTEM/ZEROS:DATA', data);

    const freeAfter = fs.getFreePages();
    // With sparse, only 1 index block should be allocated (data pages are sparse holes)
    // So we should lose only 1 page (the index block)
    expect(freeAfter).toBe(freeBefore - 1);
  });

  it('write file with mixed zero/data pages', () => {
    const fs = createFS();
    const freeBefore = fs.getFreePages();

    // 4 pages: 0=zeros, 1=data, 2=zeros, 3=data
    const data = new Uint8Array(NDFS_PAGE_SIZE * 4);
    // Page 1 (offset 2048-4095): non-zero
    for (let i = NDFS_PAGE_SIZE; i < NDFS_PAGE_SIZE * 2; i++) data[i] = 0xAA;
    // Page 3 (offset 6144-8191): non-zero
    for (let i = NDFS_PAGE_SIZE * 3; i < NDFS_PAGE_SIZE * 4; i++) data[i] = 0xBB;

    fs.writeFile('SYSTEM/MIXED:DATA', data);

    const freeAfter = fs.getFreePages();
    // 1 index block + 2 data blocks (pages 0 and 2 are sparse)
    expect(freeAfter).toBe(freeBefore - 3);
  });

  it('read sparse file returns correct zeros', () => {
    const fs = createFS();

    const data = new Uint8Array(NDFS_PAGE_SIZE * 3);
    // Only page 1 has data
    for (let i = NDFS_PAGE_SIZE; i < NDFS_PAGE_SIZE * 2; i++) data[i] = 0xCC;

    fs.writeFile('SYSTEM/SPARSEREAD:DATA', data);
    const read = fs.readFile('SYSTEM/SPARSEREAD:DATA');

    expect(read.length).toBe(data.length);

    // Page 0 should be zeros
    for (let i = 0; i < NDFS_PAGE_SIZE; i++) {
      expect(read[i]).toBe(0);
    }
    // Page 1 should have data
    for (let i = NDFS_PAGE_SIZE; i < NDFS_PAGE_SIZE * 2; i++) {
      expect(read[i]).toBe(0xCC);
    }
    // Page 2 should be zeros
    for (let i = NDFS_PAGE_SIZE * 2; i < NDFS_PAGE_SIZE * 3; i++) {
      expect(read[i]).toBe(0);
    }
  });

  it('sparse file uses fewer disk blocks than non-sparse', () => {
    const fs1 = createFS();
    const fs2 = createFS();

    // Sparse: all zeros
    const sparseData = new Uint8Array(NDFS_PAGE_SIZE * 5);
    fs1.writeFile('SYSTEM/SPARSE:DATA', sparseData);

    // Non-sparse: all non-zero
    const denseData = new Uint8Array(NDFS_PAGE_SIZE * 5);
    denseData.fill(0xFF);
    fs2.writeFile('SYSTEM/DENSE:DATA', denseData);

    // Sparse should have more free pages
    expect(fs1.getFreePages()).toBeGreaterThan(fs2.getFreePages());
  });
});

/**
 * Regression tests for NDFS user-quota (pagesUsed) accounting on sparse files.
 * Previously pagesUsed was incremented by the file's LOGICAL page count
 * (including sparse holes, which allocate no real disk block) plus a flat
 * structural-block addition — overcharging every sparse file and every file
 * needing a SubIndexed structure. Also, updateExistingFile never adjusted
 * pagesUsed at all on overwrite. Fixed to track only real allocated data pages.
 */
describe('SparseFileQuotaAccounting', () => {
  // Find a user's pagesUsed by name (case-insensitive), matching how the
  // C# reference test (NdfsSparseFileQuotaAccountingTests) looks users up.
  function pagesUsedFor(fs: NdfsFileSystem, userName: string): number {
    const users = fs.getUsers();
    for (let i = 0; i < users.length; i++) {
      if (users[i].userName.toUpperCase() === userName.toUpperCase()) {
        return users[i].pagesUsed;
      }
    }
    throw new Error(`User not found: ${userName}`);
  }

  it('fully-sparse file charges zero pagesUsed', () => {
    const fs = createFS();
    const before = pagesUsedFor(fs, 'SYSTEM');

    const data = new Uint8Array(NDFS_PAGE_SIZE * 5); // all zero — fully sparse
    fs.writeFile('SYSTEM/ALLZERO:DAT', data);

    const after = pagesUsedFor(fs, 'SYSTEM');
    expect(after).toBe(before);
  });

  it('mixed sparse/real file charges only the real pages', () => {
    const fs = createFS();
    const before = pagesUsedFor(fs, 'SYSTEM');

    // 10 pages: pages 0-2 real (non-zero), pages 3-9 stay zero (7 sparse holes)
    const data = new Uint8Array(NDFS_PAGE_SIZE * 10);
    for (let i = 0; i < NDFS_PAGE_SIZE * 3; i++) data[i] = (i % 251) + 1; // never zero
    fs.writeFile('SYSTEM/MIXEDQUOTA:DAT', data);

    const after = pagesUsedFor(fs, 'SYSTEM');
    expect(after - before).toBe(3);
  });

  it('fully-real file charges exactly its page count, no structural overhead', () => {
    const fs = createFS();
    const before = pagesUsedFor(fs, 'SYSTEM');

    const data = new Uint8Array(NDFS_PAGE_SIZE * 6);
    for (let i = 0; i < data.length; i++) data[i] = (i % 251) + 1; // no zero pages at all
    fs.writeFile('SYSTEM/FULLREAL:DAT', data);

    const after = pagesUsedFor(fs, 'SYSTEM');
    expect(after - before).toBe(6);
  });

  it('deleting a file refunds exactly what creating it charged', () => {
    const fs = createFS();
    const before = pagesUsedFor(fs, 'SYSTEM');

    const data = new Uint8Array(NDFS_PAGE_SIZE * 10);
    for (let i = 0; i < NDFS_PAGE_SIZE * 4; i++) data[i] = (i % 251) + 1; // 4 real, 6 sparse
    fs.writeFile('SYSTEM/DELETEME:DAT', data);
    fs.deleteFile('SYSTEM/DELETEME:DAT');

    const after = pagesUsedFor(fs, 'SYSTEM');
    expect(after).toBe(before);
  });

  it('overwriting with a larger then smaller real file tracks the new real page count each time', () => {
    const fs = createFS();
    const beforeFirstWrite = pagesUsedFor(fs, 'SYSTEM');

    const grow = (pages: number): Uint8Array => {
      const buf = new Uint8Array(NDFS_PAGE_SIZE * pages);
      for (let i = 0; i < buf.length; i++) buf[i] = (i % 251) + 1; // no zero pages
      return buf;
    };

    fs.writeFile('SYSTEM/GROW:DAT', grow(2));
    expect(pagesUsedFor(fs, 'SYSTEM') - beforeFirstWrite).toBe(2);

    // Overwrite with a much larger real file: quota must track the NEW real
    // page count, replacing the old one (exercises the updateExistingFile fix).
    fs.writeFile('SYSTEM/GROW:DAT', grow(8));
    expect(pagesUsedFor(fs, 'SYSTEM') - beforeFirstWrite).toBe(8);

    // Overwrite with a smaller real file: quota must shrink back down.
    fs.writeFile('SYSTEM/GROW:DAT', grow(1));
    expect(pagesUsedFor(fs, 'SYSTEM') - beforeFirstWrite).toBe(1);
  });

  it('a mostly-sparse SubIndexed file (>512 pages) charges only its real pages, not structural blocks', () => {
    // >512 pages forces the SubIndexed layout (group-index blocks + a
    // top-level sub-index block) — none of that structural overhead may be
    // charged to the user's quota.
    const fs = createFS(2000);
    const before = pagesUsedFor(fs, 'SYSTEM');

    const data = new Uint8Array(NDFS_PAGE_SIZE * 600);
    // Only 2 real pages; the remaining 598 pages stay zero (sparse holes).
    for (let i = 0; i < NDFS_PAGE_SIZE * 2; i++) data[i] = (i % 251) + 1;
    fs.writeFile('SYSTEM/BIGSPARSE:DAT', data);

    const after = pagesUsedFor(fs, 'SYSTEM');
    expect(after - before).toBe(2);
  });
});
