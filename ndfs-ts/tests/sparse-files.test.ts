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
