/**
 * NDFS object file: manages the collection of file (object) entries.
 *
 * Can be indexed (single index block with up to 512 pointers to data pages)
 * or sub-indexed (sub-index block with 512 pointers to index blocks).
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
} from './constants.js';
import { BlockPointer } from './block-pointer.js';
import { ObjectEntry } from './object-entry.js';
import { PointerType } from './types.js';

export class ObjectFile {
  indexPointer: BlockPointer | null = null;
  private entries: Map<number, ObjectEntry> = new Map();
  private nextIndex: number = 0;

  /** Get all object entries. */
  getObjects(): ObjectEntry[] {
    const result: ObjectEntry[] = [];
    this.entries.forEach((v) => result.push(v));
    return result;
  }

  /** Get an object entry by index. */
  getObject(index: number): ObjectEntry | null {
    return this.entries.get(index) ?? null;
  }

  /** Find an object by name and user. */
  findObject(objectName: string, userName: string): ObjectEntry | null {
    const nameUpper = objectName.toUpperCase();
    const userUpper = userName.toUpperCase();
    const iter = this.entries.values();
    let next = iter.next();
    while (!next.done) {
      const entry = next.value;
      if (
        entry.objectName.toUpperCase() === nameUpper &&
        entry.userName.toUpperCase() === userUpper
      ) {
        return entry;
      }
      next = iter.next();
    }
    return null;
  }

  /** Add or update an object entry. */
  addObject(entry: ObjectEntry): void {
    this.entries.set(entry.objectIndex, entry);
    if (entry.objectIndex >= this.nextIndex) {
      this.nextIndex = entry.objectIndex + 1;
    }
  }

  /** Remove an object entry. */
  removeObject(index: number): boolean {
    return this.entries.delete(index);
  }

  /** Get objects belonging to a user (by name or index). */
  getUserObjects(userNameOrIndex: string | number): ObjectEntry[] {
    const result: ObjectEntry[] = [];
    this.entries.forEach((entry) => {
      if (typeof userNameOrIndex === 'string') {
        if (entry.userName.toUpperCase() === userNameOrIndex.toUpperCase()) {
          result.push(entry);
        }
      } else {
        if (entry.userIndex === userNameOrIndex) result.push(entry);
      }
    });
    return result;
  }

  /** Get next available object index. */
  getNextAvailableIndex(): number {
    // Find first gap
    for (let i = 0; i < this.nextIndex + 1; i++) {
      if (!this.entries.has(i)) return i;
    }
    return this.nextIndex;
  }

  /** Get total pages used by all objects. */
  getTotalPagesUsed(): number {
    let total = 0;
    this.entries.forEach((e) => {
      total += e.pagesInFile;
    });
    return total;
  }

  /** Clear all entries. */
  clear(): void {
    this.entries.clear();
    this.nextIndex = 0;
  }

  /**
   * Load entries from an indexed or sub-indexed structure.
   * pointer: the object file's block pointer (from master block).
   * readPage: callback to read a page by block ID.
   */
  loadFromPages(
    pointer: BlockPointer,
    readPage: (blockId: number) => Uint8Array,
  ): void {
    this.entries.clear();
    this.indexPointer = pointer;
    let globalObjectIndex = 0;

    if (pointer.type === PointerType.Indexed) {
      // Single index block -> up to 512 data page pointers
      const indexPage = readPage(pointer.blockId);
      this.loadObjectsFromIndexBlock(indexPage, readPage, globalObjectIndex);
    } else if (pointer.type === PointerType.SubIndexed) {
      // Sub-index block -> up to 512 index block pointers
      const subIndexPage = readPage(pointer.blockId);
      for (let i = 0; i < MAX_OBJECT_FILE_POINTERS; i++) {
        const indexPtr = BlockPointer.fromBytes(subIndexPage, i * 4);
        if (!indexPtr.isValid()) continue;

        const indexPage = readPage(indexPtr.blockId);
        globalObjectIndex = this.loadObjectsFromIndexBlock(
          indexPage,
          readPage,
          globalObjectIndex,
        );
      }
    }
  }

  private loadObjectsFromIndexBlock(
    indexPage: Uint8Array,
    readPage: (blockId: number) => Uint8Array,
    startIndex: number,
  ): number {
    let objectIndex = startIndex;

    for (let i = 0; i < MAX_OBJECT_FILE_POINTERS; i++) {
      const ptr = BlockPointer.fromBytes(indexPage, i * 4);
      if (!ptr.isValid()) {
        objectIndex += ENTRIES_PER_PAGE;
        continue;
      }

      const dataPage = readPage(ptr.blockId);
      for (let j = 0; j < ENTRIES_PER_PAGE; j++) {
        const entry = ObjectEntry.fromBytes(dataPage, j * ENTRY_SIZE);
        if (entry !== null) {
          entry.objectIndex = objectIndex + j;
          this.entries.set(entry.objectIndex, entry);
          if (entry.objectIndex >= this.nextIndex) {
            this.nextIndex = entry.objectIndex + 1;
          }
        }
      }
      objectIndex += ENTRIES_PER_PAGE;
    }

    return objectIndex;
  }

  /**
   * Serialize all object entries into page-aligned buffers.
   * Returns the data pages that contain entries.
   */
  toDataPages(): Map<number, Uint8Array> {
    const pageMap = new Map<number, Uint8Array>();

    this.entries.forEach((entry) => {
      const pageIndex = Math.floor(entry.objectIndex / ENTRIES_PER_PAGE);
      if (!pageMap.has(pageIndex)) {
        pageMap.set(pageIndex, new Uint8Array(NDFS_PAGE_SIZE));
      }
      const page = pageMap.get(pageIndex)!;
      const slotInPage = entry.objectIndex % ENTRIES_PER_PAGE;
      entry.toBytes(page, slotInPage * ENTRY_SIZE);
    });

    return pageMap;
  }
}
