import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ImageTemplate } from '../src/types.js';
import { NDFS_PAGE_SIZE } from '../src/constants.js';

describe('ImageCreator', () => {
  describe('Floppy360KB', () => {
    it('creates a valid 360KB floppy image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
        directoryName: 'FLOPPY360',
      });
      expect(fs.getDirectoryName()).toBe('FLOPPY360');
    });

    it('has correct master block pointers', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
        directoryName: 'TEST360',
      });
      const mb = fs.getMasterBlock();
      expect(mb.objectFilePointer!.blockId).toBe(149);
      expect(mb.userFilePointer!.blockId).toBe(151);
      expect(mb.bitFilePointer!.blockId).toBe(153);
      expect(mb.unreservedPages).toBe(1);
    });

    it('has SYSTEM user after creation', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
      });
      const users = fs.getUsers();
      expect(users.length).toBe(1);
      expect(users[0].userName).toBe('SYSTEM');
    });

    it('marks system blocks as used in bitmap', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
      });
      expect(fs.isBlockUsed(0)).toBe(true);       // master block
      expect(fs.isBlockUsed(149)).toBe(true);      // obj index
      expect(fs.isBlockUsed(150)).toBe(true);      // obj data
      expect(fs.isBlockUsed(151)).toBe(true);      // usr index
      expect(fs.isBlockUsed(152)).toBe(true);      // usr data
      expect(fs.isBlockUsed(153)).toBe(true);      // bit file
    });

    it('pointers are near end of disk', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
      });
      const mb = fs.getMasterBlock();
      // Floppy pointers should be near the end (page 154 total)
      expect(mb.objectFilePointer!.blockId).toBeGreaterThan(140);
      expect(mb.userFilePointer!.blockId).toBeGreaterThan(140);
      expect(mb.bitFilePointer!.blockId).toBeGreaterThan(140);
    });

    it('can write files to the created image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy360KB,
        directoryName: 'WRITABLE',
      });
      const data = new TextEncoder().encode('Hello from a new floppy!');
      fs.writeFile('SYSTEM/HELLO:DATA', data);
      const read = fs.readFile('SYSTEM/HELLO:DATA');
      expect(new TextDecoder().decode(read)).toBe('Hello from a new floppy!');
    });
  });

  describe('Floppy12MB', () => {
    it('creates a valid 1.2MB floppy image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy12MB,
        directoryName: 'FLOPPY12',
      });
      expect(fs.getDirectoryName()).toBe('FLOPPY12');
      const mb = fs.getMasterBlock();
      expect(mb.objectFilePointer!.blockId).toBe(611);
      expect(mb.userFilePointer!.blockId).toBe(613);
      expect(mb.bitFilePointer!.blockId).toBe(615);
    });

    it('has correct free page count', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Floppy12MB,
      });
      const usedPages = fs.getUsedPages();
      // 6 system blocks used: 0, 611, 612, 613, 614, 615
      expect(usedPages).toBe(6);
      expect(fs.getFreePages()).toBe(616 - 6);
    });
  });

  describe('Smd75MB', () => {
    it('creates a valid 75MB SMD image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Smd75MB,
        directoryName: 'SMD75',
      });
      expect(fs.getDirectoryName()).toBe('SMD75');
    });

    it('has extended info with valid checksum', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Smd75MB,
        directoryName: 'SMDEXT',
        systemNumber: 42,
        flagWord: 0x0001,
      });
      const mb = fs.getMasterBlock();
      expect(mb.extValid).toBe(true);
      expect(mb.extLastSystemNumber).toBe(42);
      expect(mb.extFlagWord).toBe(0x0001);
    });

    it('has correct pointers', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Smd75MB,
      });
      const mb = fs.getMasterBlock();
      expect(mb.objectFilePointer!.blockId).toBe(18684);
      expect(mb.userFilePointer!.blockId).toBe(18686);
      expect(mb.bitFilePointer!.blockId).toBe(18472);
      expect(mb.unreservedPages).toBe(36945);
    });

    it('pointers are spread out on hard disk', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Smd75MB,
      });
      const mb = fs.getMasterBlock();
      // Hard disk pointers should be in the middle-to-upper range
      expect(mb.bitFilePointer!.blockId).toBeLessThan(mb.objectFilePointer!.blockId);
    });
  });

  describe('Winchester74MB', () => {
    it('creates a valid 74MB Winchester image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Winchester74MB,
        directoryName: 'WINCH74',
      });
      expect(fs.getDirectoryName()).toBe('WINCH74');
      const mb = fs.getMasterBlock();
      expect(mb.objectFilePointer!.blockId).toBe(32771);
      expect(mb.userFilePointer!.blockId).toBe(32769);
      expect(mb.bitFilePointer!.blockId).toBe(18198);
    });
  });

  describe('Custom', () => {
    it('creates a custom-sized floppy image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 200,
        directoryName: 'CUSTOM',
      });
      expect(fs.getDirectoryName()).toBe('CUSTOM');
      const mb = fs.getMasterBlock();
      // Floppy formula: obj=pages-5, usr=pages-3, bit=pages-1
      expect(mb.objectFilePointer!.blockId).toBe(195);
      expect(mb.userFilePointer!.blockId).toBe(197);
      expect(mb.bitFilePointer!.blockId).toBe(199);
    });

    it('creates a custom-sized hard disk image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 5000,
        directoryName: 'BIGDISK',
      });
      expect(fs.getDirectoryName()).toBe('BIGDISK');
      const mb = fs.getMasterBlock();
      // Hard disk formula: bit=pages/2, obj=floor(pages*0.85), usr=obj+2
      expect(mb.bitFilePointer!.blockId).toBe(2500);
      expect(mb.objectFilePointer!.blockId).toBe(4250);
      expect(mb.userFilePointer!.blockId).toBe(4252);
    });

    it('verifies all fields on custom image', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 300,
        directoryName: 'ALLFIELDS',
      });
      expect(fs.getDirectoryName()).toBe('ALLFIELDS');
      expect(fs.getUsers().length).toBe(1);
      expect(fs.getUsers()[0].userName).toBe('SYSTEM');
      expect(fs.verifyIntegrity()).toBe(true);
    });

    it('throws on too-small custom size', () => {
      expect(() =>
        NdfsFileSystem.createImage({
          template: ImageTemplate.Custom,
          customPages: 5,
        }),
      ).toThrow();
    });

    it('supports extra users during creation', () => {
      const fs = NdfsFileSystem.createImage({
        template: ImageTemplate.Custom,
        customPages: 200,
        directoryName: 'MULTIUSER',
        users: [
          { name: 'ALICE', reservedPages: 50 },
          { name: 'BOB', reservedPages: 30 },
        ],
      });
      const users = fs.getUsers();
      expect(users.length).toBe(3); // SYSTEM + ALICE + BOB
    });
  });
});
