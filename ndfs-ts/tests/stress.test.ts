import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ImageTemplate } from '../src/types.js';
import { NDFS_PAGE_SIZE } from '../src/constants.js';

describe('Stress', () => {
  it('create image, add 50 users', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 500,
      directoryName: 'STRESS50',
    });

    for (let i = 0; i < 50; i++) {
      const name = `USR${String(i).padStart(3, '0')}`;
      expect(fs.addUser(name, 10)).toBe(true);
    }
    expect(fs.getUsers().length).toBe(51); // SYSTEM + 50
  });

  it('write 200 files across multiple users', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 2000,
      directoryName: 'STRESS200',
    });

    // Add 5 users
    for (let i = 0; i < 5; i++) {
      fs.addUser(`USER${i}`, 2000);
    }

    // Write 200 files: 40 per user
    for (let u = 0; u < 5; u++) {
      const userName = `USER${u}`;
      for (let f = 0; f < 40; f++) {
        const data = new TextEncoder().encode(`file-${u}-${f}-content`);
        fs.writeFile(`${userName}/FILE${f}:DATA`, data);
      }
    }

    // Verify all files
    for (let u = 0; u < 5; u++) {
      const files = fs.listDirectory(`USER${u}`);
      expect(files.length).toBe(40);
    }
  });

  it('write/read/delete cycle 50 iterations', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 200,
      directoryName: 'CYCLE',
    });

    for (let i = 0; i < 50; i++) {
      const data = new TextEncoder().encode(`iteration-${i}`);
      fs.writeFile('SYSTEM/CYCLE:DATA', data);
      const read = new TextDecoder().decode(fs.readFile('SYSTEM/CYCLE:DATA'));
      expect(read).toBe(`iteration-${i}`);
      fs.deleteFile('SYSTEM/CYCLE:DATA');
      expect(fs.fileExists('SYSTEM/CYCLE:DATA')).toBe(false);
    }
  });

  it('verify data integrity with random content', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 500,
      directoryName: 'INTEGRITY',
    });

    // Simple pseudo-random generator
    let seed = 42;
    const pseudoRandom = (): number => {
      seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
      return seed & 0xFF;
    };

    const fileData: Map<string, Uint8Array> = new Map();

    // Write 20 files with random content
    for (let i = 0; i < 20; i++) {
      seed = i * 1000; // Reset seed per file for reproducibility
      const size = 100 + (i * 50);
      const data = new Uint8Array(size);
      for (let j = 0; j < size; j++) data[j] = pseudoRandom();
      const name = `FILE${String(i).padStart(3, '0')}`;
      fs.writeFile(`SYSTEM/${name}:DATA`, data);
      fileData.set(name, data);
    }

    // Verify all files
    fileData.forEach((expectedData, name) => {
      const read = fs.readFile(`SYSTEM/${name}:DATA`);
      expect(read.length).toBe(expectedData.length);
      for (let i = 0; i < expectedData.length; i++) {
        expect(read[i]).toBe(expectedData[i]);
      }
    });
  });

  it('verify free page count is consistent after operations', () => {
    const fs = NdfsFileSystem.createImage({
      template: ImageTemplate.Custom,
      customPages: 200,
      directoryName: 'CONSIST',
    });

    const initialFree = fs.getFreePages();

    // Write some files
    for (let i = 0; i < 10; i++) {
      fs.writeFile(`SYSTEM/FILE${i}:DATA`, new TextEncoder().encode(`content-${i}`));
    }

    // Delete all files
    for (let i = 0; i < 10; i++) {
      fs.deleteFile(`SYSTEM/FILE${i}:DATA`);
    }

    // Free pages should be back to initial
    expect(fs.getFreePages()).toBe(initialFree);

    // Used + free should equal total
    const total = 200;
    expect(fs.getUsedPages() + fs.getFreePages()).toBe(total);
  });
});
