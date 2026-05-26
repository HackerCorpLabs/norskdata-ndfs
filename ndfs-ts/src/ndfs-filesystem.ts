/**
 * NdfsFileSystem: main class for reading and writing NDFS disk images.
 *
 * Operates on a Uint8Array buffer representing the entire disk image.
 * Supports contiguous, indexed, and sub-indexed file allocation,
 * sparse files, user management, and quota tracking.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import {
  NDFS_PAGE_SIZE,
  ENTRIES_PER_PAGE,
  ENTRY_SIZE,
  MAX_OBJECT_FILE_POINTERS,
  MAX_USER_FILE_POINTERS,
  FIRST_ALLOCATABLE_BLOCK,
  MAX_USERS,
  NDFS_NAME_MAX,
  NDFS_TYPE_MAX,
} from './constants.js';
import { readUint32BE, writeUint32BE } from './endian.js';
import { BlockPointer } from './block-pointer.js';
import { MasterBlock } from './master-block.js';
import { BitFile } from './bit-file.js';
import { UserFile } from './user-file.js';
import { ObjectFile } from './object-file.js';
import { UserEntry } from './user-entry.js';
import {
  ObjectEntry,
  ACCESS_DEFAULT,
  FT_INDEXED,
  FT_CONTIGUOUS,
} from './object-entry.js';
import { PointerType, FileEntry, BootFormat, BootCode, ImageTemplate, ImageCreationOptions } from './types.js';
import { createNdfsImage } from './image-creator.js';
import { detectBootFormat, loadBootCode as loadBootCodeFromPage } from './boot-loader.js';
import { stripParity, setParity } from './parity.js';

/** Parity mode for read/write operations. */
export type ParityMode = 'none' | 'strip' | 'set';
import { XatProperties, objectEntryToXat, xatToObjectEntry } from './xat.js';

export class NdfsFileSystem {
  private data: Uint8Array;
  private readOnly: boolean;
  private masterBlock: MasterBlock;
  private bitFile: BitFile = new BitFile();
  private userFile: UserFile = new UserFile();
  private objectFile: ObjectFile = new ObjectFile();

  /**
   * Open an NDFS disk image from a buffer.
   * @param data - The raw disk image bytes (must be a multiple of 2048).
   * @param readOnly - If true, write operations will throw.
   */
  constructor(data: Uint8Array | ArrayBuffer, readOnly: boolean = false) {
    if (data instanceof ArrayBuffer) {
      this.data = new Uint8Array(data);
    } else {
      // Make a copy to avoid external mutation
      this.data = new Uint8Array(data.length);
      this.data.set(data);
    }
    this.readOnly = readOnly;

    if (this.data.length < NDFS_PAGE_SIZE) {
      throw new Error('Image too small: must be at least one NDFS page (2048 bytes)');
    }
    if (this.data.length % NDFS_PAGE_SIZE !== 0) {
      throw new Error('Image size must be a multiple of NDFS page size (2048 bytes)');
    }

    // Parse master block
    const page0 = this.readPage(0);
    this.masterBlock = MasterBlock.fromBytes(page0);
    if (!this.masterBlock.isValid()) {
      throw new Error('Invalid NDFS master block');
    }
    this.masterBlock.imageSize = this.data.length / NDFS_PAGE_SIZE;

    // Load filesystem structures
    this.loadStructures();
  }

  /**
   * Create a new NDFS disk image from options.
   * Returns a fully initialized NdfsFileSystem ready for use.
   */
  static createImage(options: ImageCreationOptions): NdfsFileSystem {
    const imageData = createNdfsImage(options);
    return new NdfsFileSystem(imageData, false);
  }

  // ── Lifecycle ──────────────────────────────────────────────────────

  /** Export the current image as a new Uint8Array. */
  toBuffer(): Uint8Array {
    const copy = new Uint8Array(this.data.length);
    copy.set(this.data);
    return copy;
  }

  // ── Read operations ────────────────────────────────────────────────

  /** Get the parsed master block. */
  getMasterBlock(): MasterBlock {
    return this.masterBlock;
  }

  /** Get the volume/directory name. */
  getDirectoryName(): string {
    return this.masterBlock.directoryName;
  }

  /**
   * List directory contents.
   * - path="" or "/": lists users as directories.
   * - path="USERNAME": lists that user's files.
   */
  listDirectory(path: string = ''): FileEntry[] {
    const normalized = path.replace(/^\/+|\/+$/g, '');
    const entries: FileEntry[] = [];

    if (normalized === '') {
      // Root: list users as directories
      const users = this.userFile.getUsers();
      for (let i = 0; i < users.length; i++) {
        const u = users[i];
        entries.push({
          name: u.userName,
          type: '',
          fullName: u.userName,
          userName: u.userName,
          size: 0,
          pages: 0,
          isDirectory: true,
          lastModified: null,
        });
      }
    } else {
      const parts = normalized.split('/');
      if (parts.length > 1) {
        throw new Error('NDFS does not support subdirectories');
      }
      const userName = parts[0];
      const objects = this.objectFile.getUserObjects(userName);
      for (let i = 0; i < objects.length; i++) {
        const obj = objects[i];
        const fullName = obj.type ? `${obj.objectName}:${obj.type}` : obj.objectName;
        entries.push({
          name: obj.objectName,
          type: obj.type,
          fullName,
          userName: obj.userName,
          size: obj.bytesInFile,
          pages: obj.pagesInFile,
          isDirectory: false,
          lastModified: null,
        });
      }
    }
    return entries;
  }

  /**
   * Read a file's contents.
   * @param path - "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
   * @param parity - Parity handling: 'none' (default, raw bytes),
   *   'strip' (clear bit 7, for reading ND text as ASCII).
   */
  readFile(path: string, parity: ParityMode = 'none'): Uint8Array {
    const obj = this.findObject(path);
    if (!obj) throw new Error(`File not found: ${path}`);
    const data = this.readObjectData(obj);
    if (parity === 'strip') return stripParity(data);
    return data;
  }

  /** Get file metadata, or null if not found. */
  getMetadata(path: string): FileEntry | null {
    const obj = this.findObject(path);
    if (!obj) return null;
    const fullName = obj.type ? `${obj.objectName}:${obj.type}` : obj.objectName;
    return {
      name: obj.objectName,
      type: obj.type,
      fullName,
      userName: obj.userName,
      size: obj.bytesInFile,
      pages: obj.pagesInFile,
      isDirectory: false,
      lastModified: null,
    };
  }

  /** Check if a file exists. */
  fileExists(path: string): boolean {
    return this.findObject(path) !== null;
  }

  // ── Write operations ───────────────────────────────────────────────

  /**
   * Write (create or overwrite) a file.
   * @param path - "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
   * @param fileData - The raw bytes to write.
   * @param parity - Parity handling: 'none' (default, write raw bytes),
   *   'set' (apply ND-100 even parity before writing, for text files).
   */
  writeFile(path: string, fileData: Uint8Array, parity: ParityMode = 'none'): void {
    this.ensureWritable();

    if (parity === 'set') {
      fileData = setParity(fileData);
    }

    const { userName, objectName, fileType } = this.parsePath(path);
    if (!objectName) throw new Error('Invalid path: filename required');

    // Find user
    const user = this.resolveUser(userName);
    if (!user) throw new Error(`User not found: ${userName || '(default)'}`);

    // Calculate required pages (data pages + index blocks)
    let dataPages = Math.ceil(fileData.length / NDFS_PAGE_SIZE);
    if (dataPages === 0) dataPages = 1;
    const indexBlocks = dataPages > MAX_OBJECT_FILE_POINTERS
      ? 1 + Math.ceil(dataPages / MAX_OBJECT_FILE_POINTERS)
      : 1;
    const totalRequired = dataPages + indexBlocks;

    // Check for existing file
    const existing = this.objectFile.findObject(objectName, user.userName);

    // Determine additional pages needed
    let additionalNeeded = totalRequired;
    if (existing) {
      const existingIndexBlocks =
        existing.filePointer &&
        existing.filePointer.type === PointerType.SubIndexed
          ? 1 + Math.ceil(existing.pagesInFile / MAX_OBJECT_FILE_POINTERS)
          : 1;
      const existingTotal = existing.pagesInFile + existingIndexBlocks;
      additionalNeeded = totalRequired > existingTotal ? totalRequired - existingTotal : 0;
    }

    // Check and expand quota if needed
    if (additionalNeeded > 0) {
      const availableToUser = user.pagesReserved - user.pagesUsed;
      if (availableToUser < additionalNeeded) {
        const expansion = additionalNeeded - availableToUser;
        const freeOnDisk = this.bitFile.getFreePages();
        if (freeOnDisk < expansion) {
          throw new Error(`Insufficient disk space: need ${expansion} pages, only ${freeOnDisk} available`);
        }
        user.pagesReserved += expansion;
      }
    }

    if (existing) {
      this.updateExistingFile(existing, user, fileData);
    } else {
      this.createNewFile(objectName, fileType, user, fileData);
    }
    // updateExistingFile / createNewFile perform their own surgical metadata
    // writes (object page, owner user page, bitmap).
  }

  /** Delete a file. */
  deleteFile(path: string): void {
    this.ensureWritable();

    const obj = this.findObject(path);
    if (!obj) throw new Error(`File not found: ${path}`);

    // Free blocks
    if (obj.filePointer && obj.filePointer.blockId > 0) {
      this.freeFileBlocks(obj);
    }

    // Update user pages used
    const user = this.userFile.getUser(obj.userIndex);
    if (user) {
      let indexBlocks = 0;
      if (obj.filePointer) {
        if (obj.filePointer.type === PointerType.Indexed) {
          indexBlocks = 1;
        } else if (obj.filePointer.type === PointerType.SubIndexed) {
          indexBlocks =
            1 + Math.ceil(obj.pagesInFile / MAX_OBJECT_FILE_POINTERS);
        }
      }
      const totalBlocks = obj.pagesInFile + indexBlocks;
      user.pagesUsed = user.pagesUsed >= totalBlocks ? user.pagesUsed - totalBlocks : 0;
    }

    // Surgical writes: capture index/owner before removal, then rewrite the
    // freed object's page (zero-fill clears the slot), the owner's user page,
    // and the allocation bitmap.
    const freedIndex = obj.objectIndex;
    const owner = obj.userIndex;
    this.objectFile.removeObject(obj.objectIndex);
    this.writeObjectPage(freedIndex);
    this.writeUserPage(owner);
    this.writeBitFile();
  }

  /** Rename a file. */
  rename(oldPath: string, newPath: string): void {
    this.ensureWritable();

    const obj = this.findObject(oldPath);
    if (!obj) throw new Error(`File not found: ${oldPath}`);

    const { objectName, fileType } = this.parsePath(newPath);
    if (!objectName) throw new Error('Invalid new path');

    obj.objectName = objectName.toUpperCase().substring(0, NDFS_NAME_MAX);
    obj.type = fileType.toUpperCase().substring(0, NDFS_TYPE_MAX);
    // Rename touches only this file's object entry.
    this.writeObjectPage(obj.objectIndex);
  }

  // ── User management ────────────────────────────────────────────────

  /** Get all users. */
  getUsers(): UserEntry[] {
    return this.userFile.getUsers();
  }

  /** Get a user by index. */
  getUser(index: number): UserEntry | null {
    return this.userFile.getUser(index);
  }

  /** Add a new user. */
  addUser(name: string, reservedPages: number): boolean {
    this.ensureWritable();

    if (this.userFile.findUser(name)) return false; // already exists
    const idx = this.userFile.getNextAvailableIndex();
    if (idx < 0) return false; // no slots

    const user = new UserEntry();
    user.setName(name);
    user.userIndex = idx;
    user.pagesReserved = reservedPages;
    this.userFile.addUser(user);
    // Add-user touches only the UserFile page holding this new user.
    this.writeUserPage(user.userIndex);
    return true;
  }

  /** Remove a user (only if they have no files). */
  removeUser(index: number): boolean {
    this.ensureWritable();

    const files = this.objectFile.getUserObjects(index);
    if (files.length > 0) return false; // user has files

    const ok = this.userFile.removeUser(index);
    // Removed user's UserFile page (slot zeroed by rebuild).
    if (ok) this.writeUserPage(index);
    return ok;
  }

  /** Update a user's page quota. */
  updateUserQuota(index: number, newPages: number): boolean {
    this.ensureWritable();
    const ok = this.userFile.updateUserQuota(index, newPages);
    if (ok) this.writeUserPage(index);
    return ok;
  }

  /** Clear a user's password (set to 0). */
  clearUserPassword(indexOrName: number | string): boolean {
    this.ensureWritable();

    let user: UserEntry | null;
    if (typeof indexOrName === 'number') {
      user = this.userFile.getUser(indexOrName);
    } else {
      user = this.userFile.findUser(indexOrName);
    }
    if (!user) return false;

    user.password = 0;
    this.writeUserPage(user.userIndex);
    return true;
  }

  // ── Bitmap queries ─────────────────────────────────────────────────

  isBlockUsed(blockId: number): boolean {
    return this.bitFile.isBlockUsed(blockId);
  }

  getFreePages(): number {
    return this.bitFile.getFreePages();
  }

  getUsedPages(): number {
    return this.bitFile.calcUsedPages();
  }

  // ── Low-level access ───────────────────────────────────────────────

  getObjectEntries(): ObjectEntry[] {
    return this.objectFile.getObjects();
  }

  getObjectEntry(name: string, userName: string): ObjectEntry | null {
    return this.objectFile.findObject(name, userName);
  }

  // ── Boot loader ────────────────────────────────────────────────────

  /** Detect the boot format of this image. */
  detectBootFormat(): BootFormat {
    const page0 = this.readPage(0);
    return detectBootFormat(page0);
  }

  /** Load boot code from this image, or null if no boot format detected. */
  loadBootCode(): BootCode | null {
    const page0 = this.readPage(0);
    return loadBootCodeFromPage(page0);
  }

  /** Check if this image contains bootable code. */
  isBootable(): boolean {
    return this.detectBootFormat() !== BootFormat.None;
  }

  // ── Diagnostics ────────────────────────────────────────────────────

  /** Basic integrity verification. */
  verifyIntegrity(): boolean {
    if (!this.masterBlock.isValid()) return false;

    // Check all file blocks are marked in bitmap
    const objects = this.objectFile.getObjects();
    for (let i = 0; i < objects.length; i++) {
      const obj = objects[i];
      if (!obj.filePointer || !obj.filePointer.isValid()) continue;

      // Check index block
      if (!this.bitFile.isBlockUsed(obj.filePointer.blockId)) return false;
    }
    return true;
  }

  /** Generate a text report about the filesystem. */
  generateReport(): string {
    const totalPages = this.data.length / NDFS_PAGE_SIZE;
    const usedPages = this.bitFile.calcUsedPages();
    const freePages = this.bitFile.getFreePages();
    const users = this.userFile.getUsers();
    const objects = this.objectFile.getObjects();

    let report = `NDFS Filesystem Report\n`;
    report += `======================\n`;
    report += `Volume: ${this.masterBlock.directoryName}\n`;
    report += `Total pages: ${totalPages}\n`;
    report += `Used pages: ${usedPages}\n`;
    report += `Free pages: ${freePages}\n`;
    report += `Users: ${users.length}\n`;
    report += `Files: ${objects.length}\n\n`;

    report += `Users:\n`;
    for (let i = 0; i < users.length; i++) {
      const u = users[i];
      report += `  [${u.userIndex}] ${u.userName} - Reserved: ${u.pagesReserved}, Used: ${u.pagesUsed}\n`;
    }

    report += `\nFiles:\n`;
    for (let i = 0; i < objects.length; i++) {
      const o = objects[i];
      report += `  ${o.userName}/${o.objectName}:${o.type} - ${o.bytesInFile} bytes (${o.pagesInFile} pages)\n`;
    }

    return report;
  }

  // ── XAT (Extended Attribute) support ────────────────────────────

  /**
   * Get XAT properties for a file, or null if not found.
   * @param path - "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
   */
  getFileProperties(path: string): XatProperties | null {
    const obj = this.findObject(path);
    if (!obj) return null;
    return objectEntryToXat(obj);
  }

  /**
   * Read a file's data along with its XAT properties.
   * @param path - "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
   */
  readFileWithProperties(path: string): { data: Uint8Array; properties: XatProperties } {
    const obj = this.findObject(path);
    if (!obj) throw new Error(`File not found: ${path}`);
    const data = this.readObjectData(obj);
    const properties = objectEntryToXat(obj);
    return { data, properties };
  }

  /**
   * Write a file and apply XAT properties to restore metadata.
   * The file is written first, then the XAT properties are applied
   * to the resulting object entry.
   *
   * @param path - "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
   * @param data - The raw bytes to write.
   * @param properties - XAT properties to apply after writing.
   */
  writeFileWithProperties(path: string, data: Uint8Array, properties: XatProperties): void {
    this.writeFile(path, data);

    // Now find the written entry and apply XAT properties
    const obj = this.findObject(path);
    if (obj) {
      // Only apply status-related fields, not size/pages (those are set by writeFile)
      if (typeof properties['ndfs.access_bits'] === 'number') {
        obj.accessBits = properties['ndfs.access_bits'] as number;
      }
      if (typeof properties['ndfs.file_type'] === 'number') {
        obj.fileType = properties['ndfs.file_type'] as number;
      }
      if (typeof properties['ndfs.date_created'] === 'number') {
        obj.dateCreated = properties['ndfs.date_created'] as number;
      }
      if (typeof properties['ndfs.last_read_date'] === 'number') {
        obj.lastDateRead = properties['ndfs.last_read_date'] as number;
      }
      if (typeof properties['ndfs.last_write_date'] === 'number') {
        obj.lastDateWritten = properties['ndfs.last_write_date'] as number;
      }
      // Properties change only this file's object entry.
      this.writeObjectPage(obj.objectIndex);
    }
  }

  // ══════════════════════════════════════════════════════════════════
  //  Private implementation
  // ══════════════════════════════════════════════════════════════════

  private readPage(blockId: number): Uint8Array {
    const offset = blockId * NDFS_PAGE_SIZE;
    if (offset + NDFS_PAGE_SIZE > this.data.length) {
      throw new Error(`Block ${blockId} out of range`);
    }
    return this.data.subarray(offset, offset + NDFS_PAGE_SIZE);
  }

  private writePage(blockId: number, pageData: Uint8Array): void {
    const offset = blockId * NDFS_PAGE_SIZE;
    if (offset + NDFS_PAGE_SIZE > this.data.length) {
      throw new Error(`Block ${blockId} out of range`);
    }
    this.data.set(pageData.subarray(0, NDFS_PAGE_SIZE), offset);
  }

  private loadStructures(): void {
    const mb = this.masterBlock;

    // Load user file
    if (mb.userFilePointer && mb.userFilePointer.isValid()) {
      const indexPage = this.readPage(mb.userFilePointer.blockId);
      this.userFile.loadFromPages(indexPage, (id) => this.readPage(id));

      // Link user names to object entries
      const users = this.userFile.getUsers();
      const userMap = new Map<number, string>();
      for (let i = 0; i < users.length; i++) {
        userMap.set(users[i].userIndex, users[i].userName);
      }

      // Load object file
      if (mb.objectFilePointer && mb.objectFilePointer.isValid()) {
        this.objectFile.loadFromPages(mb.objectFilePointer, (id) => this.readPage(id));

        // Resolve user names on objects
        const objects = this.objectFile.getObjects();
        for (let i = 0; i < objects.length; i++) {
          const name = userMap.get(objects[i].userIndex);
          if (name) objects[i].userName = name;
        }
      }
    }

    // Load bit file
    if (mb.bitFilePointer && mb.bitFilePointer.isValid()) {
      const totalPages = this.data.length / NDFS_PAGE_SIZE;
      this.bitFile.initialize(totalPages);

      // Determine bitmap size in pages
      const bitmapBytes = Math.ceil(totalPages / 8);
      const bitmapPages = Math.ceil(bitmapBytes / NDFS_PAGE_SIZE);

      // Read contiguous bitmap pages
      const bitmapData = new Uint8Array(bitmapPages * NDFS_PAGE_SIZE);
      for (let i = 0; i < bitmapPages; i++) {
        const page = this.readPage(mb.bitFilePointer.blockId + i);
        bitmapData.set(page, i * NDFS_PAGE_SIZE);
      }
      this.bitFile.loadBitmap(bitmapData.subarray(0, bitmapBytes));
    }
  }

  private readObjectData(obj: ObjectEntry): Uint8Array {
    if (!obj.filePointer || obj.filePointer.blockId === 0) {
      return new Uint8Array(0);
    }

    const result = new Uint8Array(obj.bytesInFile);
    let bytesRead = 0;

    if (obj.filePointer.type === PointerType.Contiguous) {
      // Sequential pages starting at blockId
      for (let i = 0; i < obj.pagesInFile && bytesRead < obj.bytesInFile; i++) {
        const page = this.readPage(obj.filePointer.blockId + i);
        const toCopy = Math.min(NDFS_PAGE_SIZE, obj.bytesInFile - bytesRead);
        result.set(page.subarray(0, toCopy), bytesRead);
        bytesRead += toCopy;
      }
    } else if (obj.filePointer.type === PointerType.Indexed) {
      bytesRead = this.readIndexedData(obj.filePointer.blockId, obj, result);
    } else if (obj.filePointer.type === PointerType.SubIndexed) {
      bytesRead = this.readSubIndexedData(obj.filePointer.blockId, obj, result);
    }

    return result;
  }

  private readIndexedData(
    indexBlockId: number,
    obj: ObjectEntry,
    result: Uint8Array,
  ): number {
    const indexPage = this.readPage(indexBlockId);
    let bytesRead = 0;

    for (let i = 0; i < MAX_OBJECT_FILE_POINTERS && bytesRead < obj.bytesInFile; i++) {
      const ptr = BlockPointer.fromBytes(indexPage, i * 4);

      if (ptr.blockId === 0) {
        // Sparse hole: fill with zeros
        const toCopy = Math.min(NDFS_PAGE_SIZE, obj.bytesInFile - bytesRead);
        // result is already zero-initialized
        bytesRead += toCopy;
      } else {
        const page = this.readPage(ptr.blockId);
        const toCopy = Math.min(NDFS_PAGE_SIZE, obj.bytesInFile - bytesRead);
        result.set(page.subarray(0, toCopy), bytesRead);
        bytesRead += toCopy;
      }
    }
    return bytesRead;
  }

  private readSubIndexedData(
    subIndexBlockId: number,
    obj: ObjectEntry,
    result: Uint8Array,
  ): number {
    const subIndexPage = this.readPage(subIndexBlockId);
    let bytesRead = 0;

    for (let si = 0; si < MAX_OBJECT_FILE_POINTERS && bytesRead < obj.bytesInFile; si++) {
      const indexPtr = BlockPointer.fromBytes(subIndexPage, si * 4);
      if (!indexPtr.isValid()) continue;

      const indexPage = this.readPage(indexPtr.blockId);
      for (let i = 0; i < MAX_OBJECT_FILE_POINTERS && bytesRead < obj.bytesInFile; i++) {
        const dataPtr = BlockPointer.fromBytes(indexPage, i * 4);
        if (dataPtr.blockId === 0) {
          const toCopy = Math.min(NDFS_PAGE_SIZE, obj.bytesInFile - bytesRead);
          bytesRead += toCopy;
        } else {
          const page = this.readPage(dataPtr.blockId);
          const toCopy = Math.min(NDFS_PAGE_SIZE, obj.bytesInFile - bytesRead);
          result.set(page.subarray(0, toCopy), bytesRead);
          bytesRead += toCopy;
        }
      }
    }
    return bytesRead;
  }

  /**
   * Allocate blocks and write file data to disk.
   * Returns { topBlockId, pointerType, indexBlocksUsed } describing the allocation.
   * Supports indexed (<=512 pages) and sub-indexed (>512 pages) layouts.
   */
  private allocateAndWriteData(
    fileData: Uint8Array,
    dataPages: number,
  ): { topBlockId: number; pointerType: PointerType; indexBlocksUsed: number } {
    const useSubIndexed = dataPages > MAX_OBJECT_FILE_POINTERS;

    if (!useSubIndexed) {
      // Simple indexed allocation
      const indexBlockId = this.bitFile.findFirstFreeBlock();
      if (indexBlockId < 0) throw new Error('No free blocks for index block');
      this.bitFile.markBlockUsed(indexBlockId);

      const indexPage = new Uint8Array(NDFS_PAGE_SIZE);

      for (let i = 0; i < dataPages; i++) {
        this.writeDataPageToIndex(fileData, i, indexPage, i);
      }

      this.writePage(indexBlockId, indexPage);
      return { topBlockId: indexBlockId, pointerType: PointerType.Indexed, indexBlocksUsed: 1 };
    }

    // Sub-indexed allocation: sub-index block -> index blocks -> data blocks
    const subIndexBlockId = this.bitFile.findFirstFreeBlock();
    if (subIndexBlockId < 0) throw new Error('No free blocks for sub-index block');
    this.bitFile.markBlockUsed(subIndexBlockId);

    const subIndexPage = new Uint8Array(NDFS_PAGE_SIZE);
    const numIndexBlocks = Math.ceil(dataPages / MAX_OBJECT_FILE_POINTERS);
    let indexBlocksUsed = 1; // count the sub-index block

    for (let si = 0; si < numIndexBlocks; si++) {
      const idxBlockId = this.bitFile.findFirstFreeBlock();
      if (idxBlockId < 0) throw new Error('No free blocks for index block');
      this.bitFile.markBlockUsed(idxBlockId);
      indexBlocksUsed++;

      const idxPtr = new BlockPointer(idxBlockId, PointerType.Contiguous);
      idxPtr.toBytes(subIndexPage, si * 4);

      const indexPage = new Uint8Array(NDFS_PAGE_SIZE);
      const startPage = si * MAX_OBJECT_FILE_POINTERS;
      const endPage = Math.min(startPage + MAX_OBJECT_FILE_POINTERS, dataPages);

      for (let i = startPage; i < endPage; i++) {
        this.writeDataPageToIndex(fileData, i, indexPage, i - startPage);
      }

      this.writePage(idxBlockId, indexPage);
    }

    this.writePage(subIndexBlockId, subIndexPage);
    return { topBlockId: subIndexBlockId, pointerType: PointerType.SubIndexed, indexBlocksUsed };
  }

  /**
   * Write a single data page (sparse-aware) and store its pointer in an index page.
   */
  private writeDataPageToIndex(
    fileData: Uint8Array,
    dataPageIndex: number,
    indexPage: Uint8Array,
    slotInIndex: number,
  ): void {
    const pageOffset = dataPageIndex * NDFS_PAGE_SIZE;
    const pageEnd = Math.min(pageOffset + NDFS_PAGE_SIZE, fileData.length);
    const pageSlice = fileData.subarray(pageOffset, pageEnd);

    // Check if page is all zeros (sparse)
    let allZeros = true;
    for (let b = 0; b < pageSlice.length; b++) {
      if (pageSlice[b] !== 0) {
        allZeros = false;
        break;
      }
    }

    if (allZeros && pageSlice.length === NDFS_PAGE_SIZE) {
      writeUint32BE(indexPage, slotInIndex * 4, 0);
    } else {
      const dataBlockId = this.bitFile.findFirstFreeBlock();
      if (dataBlockId < 0) throw new Error('No free blocks for data');
      this.bitFile.markBlockUsed(dataBlockId);

      const dataPage = new Uint8Array(NDFS_PAGE_SIZE);
      dataPage.set(pageSlice);
      this.writePage(dataBlockId, dataPage);

      const dataPtr = new BlockPointer(dataBlockId, PointerType.Contiguous);
      dataPtr.toBytes(indexPage, slotInIndex * 4);
    }
  }

  /**
   * Ensure the object-file directory data page holding `objectIndex` exists,
   * allocating and linking it on demand (SINTRAN/RetroCore do this so each
   * user's region grows as needed). The page index in the object-file index
   * block is objectIndex/32; for user U that maps to slots U*8..U*8+7.
   */
  private ensureObjectDirPage(objectIndex: number): void {
    const mb = this.masterBlock;
    if (!mb.objectFilePointer || !mb.objectFilePointer.isValid()) return;
    const pageIdx = Math.floor(objectIndex / ENTRIES_PER_PAGE);

    const allocPage = (): number => {
      const blk = this.bitFile.findFirstFreeBlock();
      if (blk < 0) throw new Error('No free blocks for object directory page');
      this.bitFile.markBlockUsed(blk);
      this.writePage(blk, new Uint8Array(NDFS_PAGE_SIZE));
      return blk;
    };

    if (mb.objectFilePointer.type === PointerType.Indexed) {
      const indexPage = this.readPage(mb.objectFilePointer.blockId);
      const ptr = BlockPointer.fromBytes(indexPage, pageIdx * 4);
      if (ptr.isValid()) return;
      const blk = allocPage();
      new BlockPointer(blk, PointerType.Contiguous).toBytes(indexPage, pageIdx * 4);
      this.writePage(mb.objectFilePointer.blockId, indexPage);
    } else if (mb.objectFilePointer.type === PointerType.SubIndexed) {
      const subIdx = Math.floor(pageIdx / MAX_OBJECT_FILE_POINTERS);
      const innerIdx = pageIdx % MAX_OBJECT_FILE_POINTERS;
      const subPage = this.readPage(mb.objectFilePointer.blockId);
      let subPtr = BlockPointer.fromBytes(subPage, subIdx * 4);
      if (!subPtr.isValid()) {
        const ib = allocPage();
        new BlockPointer(ib, PointerType.Contiguous).toBytes(subPage, subIdx * 4);
        this.writePage(mb.objectFilePointer.blockId, subPage);
        subPtr = new BlockPointer(ib, PointerType.Contiguous);
      }
      const innerPage = this.readPage(subPtr.blockId);
      const ptr = BlockPointer.fromBytes(innerPage, innerIdx * 4);
      if (ptr.isValid()) return;
      const blk = allocPage();
      new BlockPointer(blk, PointerType.Contiguous).toBytes(innerPage, innerIdx * 4);
      this.writePage(subPtr.blockId, innerPage);
    }
  }

  private createNewFile(
    objectName: string,
    fileType: string,
    user: UserEntry,
    fileData: Uint8Array,
  ): void {
    let dataPages = Math.ceil(fileData.length / NDFS_PAGE_SIZE);
    if (dataPages === 0) dataPages = 1;

    // Choose the object slot inside the owning user's region, and make sure
    // that user's directory page exists, before allocating file data.
    const slot = this.objectFile.findFreeUserSlot(user.userIndex);
    if (slot < 0) throw new Error(`User ${user.userName} object table is full`);
    this.ensureObjectDirPage(slot);

    const { topBlockId, pointerType, indexBlocksUsed } =
      this.allocateAndWriteData(fileData, dataPages);

    // Create object entry
    const entry = new ObjectEntry();
    entry.objectIndex = slot;
    entry.objectName = objectName.toUpperCase().substring(0, NDFS_NAME_MAX);
    entry.type = fileType.toUpperCase().substring(0, NDFS_TYPE_MAX);
    entry.userIndex = user.userIndex;
    entry.userName = user.userName;
    entry.pagesInFile = dataPages;
    entry.bytesInFile = fileData.length > 0 ? fileData.length : 1;
    entry.filePointer = new BlockPointer(topBlockId, pointerType);
    // New-file defaults: owner+friend full rights; allocation flag; and a
    // self-referential version chain + object index ([user|slot]) so SINTRAN
    // does not see a broken version chain (";2") and refuse to open the file.
    entry.accessBits = ACCESS_DEFAULT;
    entry.fileTypeFlags =
      pointerType === PointerType.Contiguous ? FT_CONTIGUOUS : FT_INDEXED;
    // objectIndex already encodes [user|fileEntry] (chosen in the user region).
    entry.diskObjectIndex = entry.objectIndex;
    entry.nextVersion = entry.objectIndex;
    entry.prevVersion = entry.objectIndex;
    this.objectFile.addObject(entry);

    // Update user pages used
    user.pagesUsed += dataPages + indexBlocksUsed;

    // Surgical writes: new object's page, owner's user page, bitmap.
    this.writeObjectPage(entry.objectIndex);
    this.writeUserPage(user.userIndex);
    this.writeBitFile();
  }

  private updateExistingFile(
    existing: ObjectEntry,
    user: UserEntry,
    fileData: Uint8Array,
  ): void {
    // Calculate old total blocks (data + index blocks)
    let oldIndexBlocks = 1;
    if (
      existing.filePointer &&
      existing.filePointer.type === PointerType.SubIndexed
    ) {
      // Sub-indexed: 1 sub-index + ceil(pages/512) index blocks
      oldIndexBlocks =
        1 + Math.ceil(existing.pagesInFile / MAX_OBJECT_FILE_POINTERS);
    }
    const oldTotal = existing.pagesInFile + oldIndexBlocks;

    // Free old blocks
    this.freeFileBlocks(existing);

    // Subtract old usage
    user.pagesUsed = user.pagesUsed >= oldTotal ? user.pagesUsed - oldTotal : 0;

    // Allocate new blocks
    let dataPages = Math.ceil(fileData.length / NDFS_PAGE_SIZE);
    if (dataPages === 0) dataPages = 1;

    const { topBlockId, pointerType, indexBlocksUsed } =
      this.allocateAndWriteData(fileData, dataPages);

    // Update existing entry
    existing.pagesInFile = dataPages;
    existing.bytesInFile = fileData.length > 0 ? fileData.length : 1;
    existing.filePointer = new BlockPointer(topBlockId, pointerType);

    // Update user pages used
    user.pagesUsed += dataPages + indexBlocksUsed;

    // Surgical writes: the file's object page, owner's user page, bitmap.
    this.writeObjectPage(existing.objectIndex);
    this.writeUserPage(user.userIndex);
    this.writeBitFile();
  }

  private freeFileBlocks(obj: ObjectEntry): void {
    if (!obj.filePointer || obj.filePointer.blockId === 0) return;

    if (obj.filePointer.type === PointerType.Indexed) {
      const indexPage = this.readPage(obj.filePointer.blockId);
      for (let i = 0; i < MAX_OBJECT_FILE_POINTERS; i++) {
        const ptr = BlockPointer.fromBytes(indexPage, i * 4);
        if (ptr.blockId > 0) {
          this.bitFile.markBlockFree(ptr.blockId);
        }
      }
      // Free the index block itself
      this.bitFile.markBlockFree(obj.filePointer.blockId);
    } else if (obj.filePointer.type === PointerType.Contiguous) {
      this.bitFile.freeBlocks(obj.filePointer.blockId, obj.pagesInFile);
    } else if (obj.filePointer.type === PointerType.SubIndexed) {
      const subIndexPage = this.readPage(obj.filePointer.blockId);
      for (let si = 0; si < MAX_OBJECT_FILE_POINTERS; si++) {
        const indexPtr = BlockPointer.fromBytes(subIndexPage, si * 4);
        if (!indexPtr.isValid()) continue;
        const indexPage = this.readPage(indexPtr.blockId);
        for (let i = 0; i < MAX_OBJECT_FILE_POINTERS; i++) {
          const dataPtr = BlockPointer.fromBytes(indexPage, i * 4);
          if (dataPtr.blockId > 0) this.bitFile.markBlockFree(dataPtr.blockId);
        }
        this.bitFile.markBlockFree(indexPtr.blockId);
      }
      this.bitFile.markBlockFree(obj.filePointer.blockId);
    }
  }

  /**
   * Persist all three structures to the image buffer.
   * Order: BitFile -> UserFile -> ObjectFile (matching C# reference).
   */
  // NDFS writes are immediate and surgical (matching RetroCommander/RetroCore):
  // a mutation rewrites ONLY the block(s) it touched, never the whole
  // filesystem. Each helper rebuilds a single 2048-byte page from the
  // in-memory model; rebuilding zero-filled also clears freed slots so a
  // deleted file/user does not reappear on reload.

  /** Write the BitFile allocation bitmap (small, contiguous). */
  private writeBitFile(): void {
    const mb = this.masterBlock;
    if (!mb.bitFilePointer || !mb.bitFilePointer.isValid()) return;
    const pages = this.bitFile.toPageBuffers();
    for (let i = 0; i < pages.length; i++) {
      this.writePage(mb.bitFilePointer.blockId + i, pages[i]);
    }
  }

  /** Write only the UserFile data page holding `userIndex`. */
  private writeUserPage(userIndex: number): void {
    const mb = this.masterBlock;
    if (!mb.userFilePointer || !mb.userFilePointer.isValid()) return;
    const pageIndex = Math.floor(userIndex / ENTRIES_PER_PAGE);
    if (pageIndex >= MAX_USER_FILE_POINTERS) return;
    const indexPage = this.readPage(mb.userFilePointer.blockId);
    const ptr = BlockPointer.fromBytes(indexPage, pageIndex * 4);
    if (!ptr.isValid()) return;
    this.writePage(ptr.blockId, this.userFile.toDataPage(pageIndex));
  }

  /** Resolve the on-disk data block backing ObjectFile page `pageIndex`. */
  private objectPageBlock(pageIndex: number): number | null {
    const mb = this.masterBlock;
    if (!mb.objectFilePointer || !mb.objectFilePointer.isValid()) return null;
    if (mb.objectFilePointer.type === PointerType.Indexed) {
      if (pageIndex >= MAX_OBJECT_FILE_POINTERS) return null;
      const indexPage = this.readPage(mb.objectFilePointer.blockId);
      const ptr = BlockPointer.fromBytes(indexPage, pageIndex * 4);
      return ptr.isValid() ? ptr.blockId : null;
    }
    if (mb.objectFilePointer.type === PointerType.SubIndexed) {
      const subIdx = Math.floor(pageIndex / MAX_OBJECT_FILE_POINTERS);
      const innerIdx = pageIndex % MAX_OBJECT_FILE_POINTERS;
      const subIndexPage = this.readPage(mb.objectFilePointer.blockId);
      const subPtr = BlockPointer.fromBytes(subIndexPage, subIdx * 4);
      if (!subPtr.isValid()) return null;
      const innerIndexPage = this.readPage(subPtr.blockId);
      const dataPtr = BlockPointer.fromBytes(innerIndexPage, innerIdx * 4);
      return dataPtr.isValid() ? dataPtr.blockId : null;
    }
    return null;
  }

  /**
   * Write only the ObjectFile data page holding `objectIndex`.
   * Rebuilt zero-filled, which clears any slot freed by a delete.
   */
  private writeObjectPage(objectIndex: number): void {
    const pageIndex = Math.floor(objectIndex / ENTRIES_PER_PAGE);
    const dataBlock = this.objectPageBlock(pageIndex);
    if (dataBlock === null) return;
    this.writePage(dataBlock, this.objectFile.toDataPage(pageIndex));
  }

  private findObject(path: string): ObjectEntry | null {
    const { userName, objectName, fileType } = this.parsePath(path);
    const searchName = objectName
      ? fileType
        ? `${objectName}:${fileType}`
        : objectName
      : '';

    const objects = this.objectFile.getObjects();
    for (let i = 0; i < objects.length; i++) {
      const o = objects[i];
      if (userName && o.userName.toUpperCase() !== userName.toUpperCase()) continue;

      const fullName = o.type ? `${o.objectName}:${o.type}` : o.objectName;
      if (fullName.toUpperCase() === searchName.toUpperCase()) return o;
      // Also try matching object name only (without type)
      if (!fileType && o.objectName.toUpperCase() === objectName.toUpperCase()) return o;
    }
    return null;
  }

  private parsePath(path: string): {
    userName: string;
    objectName: string;
    fileType: string;
  } {
    const normalized = path.replace(/^\/+|\/+$/g, '');
    const parts = normalized.split('/');

    let userName = '';
    let fileNamePart = '';

    if (parts.length >= 2) {
      userName = parts[0];
      fileNamePart = parts.slice(1).join('/');
    } else {
      fileNamePart = parts[0];
    }

    // Split filename into name and type (separator is : or last .)
    let objectName = fileNamePart;
    let fileType = '';

    const colonIdx = fileNamePart.indexOf(':');
    if (colonIdx >= 0) {
      objectName = fileNamePart.substring(0, colonIdx);
      fileType = fileNamePart.substring(colonIdx + 1);
    } else {
      const dotIdx = fileNamePart.lastIndexOf('.');
      if (dotIdx >= 0) {
        objectName = fileNamePart.substring(0, dotIdx);
        fileType = fileNamePart.substring(dotIdx + 1);
      }
    }

    return { userName, objectName, fileType };
  }

  private resolveUser(userName: string): UserEntry | null {
    if (userName) {
      return this.userFile.findUser(userName);
    }
    // Default to first user
    const users = this.userFile.getUsers();
    return users.length > 0 ? users[0] : null;
  }

  private ensureWritable(): void {
    if (this.readOnly) {
      throw new Error('Filesystem is read-only');
    }
  }
}
