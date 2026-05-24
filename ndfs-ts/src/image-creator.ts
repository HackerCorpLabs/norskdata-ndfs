/**
 * NDFS image creation: builds new disk images from templates.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import {
  NDFS_PAGE_SIZE,
  MASTER_BLOCK_OFFSET,
  EXTENDED_INFO_OFFSET,
  NDFS_NAME_TERMINATOR,
  NDFS_NAME_MAX,
  ENTRIES_PER_PAGE,
  ENTRY_SIZE,
  USER_ENTRY_FLAG,
} from './constants.js';
import { writeUint16BE, writeUint32BE } from './endian.js';
import { writeNdfsName } from './ndfs-name.js';
import { BlockPointer } from './block-pointer.js';
import { PointerType, ImageTemplate, ImageCreationOptions } from './types.js';

/** Internal specification for a disk template. */
interface TemplateSpec {
  ndfsPages: number;
  fileBlocks: number;
  objectFileBlock: number;
  userFileBlock: number;
  bitFileBlock: number;
  unreservedPages: number;
  isFloppy: boolean;
  includeExtendedInfo: boolean;
}

function getTemplateSpec(template: ImageTemplate, customPages?: number): TemplateSpec {
  switch (template) {
    case ImageTemplate.Floppy360KB:
      return {
        ndfsPages: 154,
        fileBlocks: 154,
        objectFileBlock: 149,
        userFileBlock: 151,
        bitFileBlock: 153,
        unreservedPages: 1,
        isFloppy: true,
        includeExtendedInfo: false,
      };
    case ImageTemplate.Floppy12MB:
      return {
        ndfsPages: 616,
        fileBlocks: 616,
        objectFileBlock: 611,
        userFileBlock: 613,
        bitFileBlock: 615,
        unreservedPages: 1,
        isFloppy: true,
        includeExtendedInfo: false,
      };
    case ImageTemplate.Smd75MB:
      return {
        ndfsPages: 36945,
        fileBlocks: 38400,
        objectFileBlock: 18684,
        userFileBlock: 18686,
        bitFileBlock: 18472,
        unreservedPages: 36945,
        isFloppy: false,
        includeExtendedInfo: true,
      };
    case ImageTemplate.Winchester74MB:
      return {
        ndfsPages: 36396,
        fileBlocks: 36360,
        objectFileBlock: 32771,
        userFileBlock: 32769,
        bitFileBlock: 18198,
        unreservedPages: 36396,
        isFloppy: false,
        includeExtendedInfo: true,
      };
    case ImageTemplate.Custom: {
      if (!customPages || customPages < 20) {
        throw new Error('Custom template requires at least 20 pages');
      }
      const pages = customPages;
      const isFloppy = pages <= 1000;

      let objBlock: number;
      let usrBlock: number;
      let bitBlock: number;

      if (isFloppy) {
        objBlock = pages - 5;
        usrBlock = pages - 3;
        bitBlock = pages - 1;
      } else {
        bitBlock = Math.floor(pages / 2);
        objBlock = Math.floor(pages * 0.85);
        usrBlock = objBlock + 2;
      }

      return {
        ndfsPages: pages,
        fileBlocks: pages,
        objectFileBlock: objBlock,
        userFileBlock: usrBlock,
        bitFileBlock: bitBlock,
        unreservedPages: isFloppy ? 1 : pages,
        isFloppy,
        includeExtendedInfo: !isFloppy,
      };
    }
    default:
      throw new Error(`Unknown template: ${template}`);
  }
}

/**
 * Calculate spare blocks for a given template.
 */
function getSpareBlocks(template: ImageTemplate, pages: number): number {
  switch (template) {
    case ImageTemplate.Smd75MB:
      return Math.floor(pages * 0.0394);
    default:
      return 0;
  }
}

/**
 * Create a new NDFS disk image from the given options.
 * Returns the raw image buffer.
 */
export function createNdfsImage(options: ImageCreationOptions): Uint8Array {
  const spec = getTemplateSpec(options.template, options.customPages);
  const dirName = (options.directoryName || 'NDFS').toUpperCase().substring(0, NDFS_NAME_MAX);
  const includeExt = options.includeExtendedInfo !== undefined
    ? options.includeExtendedInfo
    : spec.includeExtendedInfo;

  // Allocate zero-filled buffer
  const imageSize = spec.fileBlocks * NDFS_PAGE_SIZE;
  const image = new Uint8Array(imageSize);

  // ---- Page 0: Master block at offset 2016 ----
  const mbOff = MASTER_BLOCK_OFFSET;

  // Directory name
  writeNdfsName(image, mbOff, dirName, NDFS_NAME_MAX);

  // Object file pointer (Indexed)
  const objPtr = new BlockPointer(spec.objectFileBlock, PointerType.Indexed);
  objPtr.toBytes(image, mbOff + 0x10);

  // User file pointer (Indexed)
  const usrPtr = new BlockPointer(spec.userFileBlock, PointerType.Indexed);
  usrPtr.toBytes(image, mbOff + 0x14);

  // Bit file pointer (Contiguous)
  const bitPtr = new BlockPointer(spec.bitFileBlock, PointerType.Contiguous);
  bitPtr.toBytes(image, mbOff + 0x18);

  // Unreserved pages
  writeUint32BE(image, mbOff + 0x1c, spec.unreservedPages);

  // ---- Extended info at offset 2000 (hard disks only) ----
  if (includeExt) {
    const ext = EXTENDED_INFO_OFFSET;
    const sysNum = options.systemNumber || 0;
    const flagWord = options.flagWord || 0;
    const pagesAvailable = spec.ndfsPages;

    writeUint16BE(image, ext + 2, 0); // reserved1
    writeUint16BE(image, ext + 4, 0); // reserved2
    writeUint16BE(image, ext + 6, 0); // reserved3
    writeUint16BE(image, ext + 8, flagWord);
    writeUint16BE(image, ext + 10, sysNum);
    writeUint32BE(image, ext + 12, pagesAvailable);

    // Calculate checksum
    const pagesLo = pagesAvailable & 0xffff;
    const pagesHi = (pagesAvailable >>> 16) & 0xffff;
    const checksum = ((pagesLo ^ pagesHi ^ flagWord ^ 0 ^ 0 ^ 0) + sysNum) & 0xffff;
    writeUint16BE(image, ext, checksum);
  }

  // ---- Bitmap: mark system blocks as used ----
  const bitmapOff = spec.bitFileBlock * NDFS_PAGE_SIZE;

  // Helper to mark a block used in bitmap
  const markUsed = (blockId: number): void => {
    if (blockId >= spec.fileBlocks) return;
    const byteIdx = blockId >>> 3;
    const bitIdx = blockId & 7;
    image[bitmapOff + byteIdx] |= (1 << bitIdx);
  };

  // Mark page 0 (master block)
  markUsed(0);

  // Object file: index block + 1 data block
  markUsed(spec.objectFileBlock);
  const objDataBlock = spec.objectFileBlock + 1;
  markUsed(objDataBlock);

  // User file: index block + 1 data block
  markUsed(spec.userFileBlock);
  const usrDataBlock = spec.userFileBlock + 1;
  markUsed(usrDataBlock);

  // Bit file block
  markUsed(spec.bitFileBlock);

  // ---- Object file index block: one pointer to data block ----
  const objIdxOff = spec.objectFileBlock * NDFS_PAGE_SIZE;
  const objDataPtr = new BlockPointer(objDataBlock, PointerType.Contiguous);
  objDataPtr.toBytes(image, objIdxOff);

  // Object file data block is already zero (no files)

  // ---- User file index block: one pointer to data block ----
  const usrIdxOff = spec.userFileBlock * NDFS_PAGE_SIZE;
  const usrDataPtr = new BlockPointer(usrDataBlock, PointerType.Contiguous);
  usrDataPtr.toBytes(image, usrIdxOff);

  // ---- User file data block: create SYSTEM user (index 0) ----
  const usrDataOff = usrDataBlock * NDFS_PAGE_SIZE;
  image[usrDataOff + 0] = USER_ENTRY_FLAG; // 0x81 = valid user
  image[usrDataOff + 1] = 0; // enter count

  // User name "SYSTEM"
  writeNdfsName(image, usrDataOff + 2, 'SYSTEM', NDFS_NAME_MAX);

  // Password = 0
  writeUint16BE(image, usrDataOff + 18, 0);

  // Default quota: pages reserved
  const defaultQuota = Math.min(spec.ndfsPages, 1000);
  writeUint32BE(image, usrDataOff + 28, defaultQuota);

  // Pages used = 0
  writeUint32BE(image, usrDataOff + 32, 0);

  // Directory index = 0
  image[usrDataOff + 36] = 0;

  // User index = 0
  image[usrDataOff + 37] = 0;

  // Default file access = 0x04FF
  writeUint16BE(image, usrDataOff + 38, 0x04ff);

  // Add optional extra users
  if (options.users) {
    for (let u = 0; u < options.users.length; u++) {
      const userOpt = options.users[u];
      const slotIndex = u + 1; // 0 is SYSTEM
      if (slotIndex >= ENTRIES_PER_PAGE) break;

      const entryOff = usrDataOff + slotIndex * ENTRY_SIZE;
      image[entryOff] = USER_ENTRY_FLAG;
      writeNdfsName(image, entryOff + 2, userOpt.name.toUpperCase(), NDFS_NAME_MAX);
      writeUint16BE(image, entryOff + 18, 0); // no password
      writeUint32BE(image, entryOff + 28, userOpt.reservedPages);
      writeUint32BE(image, entryOff + 32, 0);
      image[entryOff + 36] = 0;
      image[entryOff + 37] = slotIndex;
      writeUint16BE(image, entryOff + 38, 0x04ff);
    }
  }

  return image;
}
