/**
 * NDFS user entry: 64-byte record in the user file.
 *
 * Byte offsets:
 *   0:     Flag (0x81 = valid user)
 *   1:     Enter count
 *   2-17:  User name (16 bytes, terminated by 0x27)
 *   18-19: Password (16-bit, big-endian)
 *   20-23: Date created (ND time, big-endian)
 *   24-27: Last date entered (ND time, big-endian)
 *   28-31: Pages reserved (32-bit, big-endian)
 *   32-35: Pages used (32-bit, big-endian)
 *   36:    Directory index
 *   37:    User index
 *   38-39: Reserved
 *   40-41: Default file access (16-bit, big-endian)
 *   42-47: Reserved / tracking (byte 47 = mxobl/acobl nibbles)
 *   48-63: Friends (8 x 2-byte entries, big-endian)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import {
  ENTRY_SIZE,
  USER_ENTRY_FLAG,
  NDFS_NAME_MAX,
  NDFS_NAME_TERMINATOR,
  MAX_FRIENDS,
} from './constants.js';
import { readUint16BE, readUint32BE, writeUint16BE, writeUint32BE } from './endian.js';
import { readNdfsName, writeNdfsName } from './ndfs-name.js';
import { UserFriend } from './user-friend.js';

export class UserEntry {
  /** Byte 0, the flags byte, kept whole. fromBytes only requires the
   * USER_ENTRY_FLAG bits (bit 7 "entry used" + bit 0 "user entry"), so a real
   * pack may carry other bits here that we do not model; toBytes writes this
   * back verbatim rather than re-hardcoding 0x81, which would destroy them on
   * every read/modify/write. A fresh entry must keep it set, or the next
   * fromBytes reads the slot as free. */
  uf: number = USER_ENTRY_FLAG;
  userIndex: number = 0;
  userName: string = '';
  password: number = 0;
  enterCount: number = 0;
  dateCreated: number = 0;
  lastDateEntered: number = 0;
  pagesReserved: number = 0;
  pagesUsed: number = 0;
  directoryIndex: number = 0;
  defaultFileAccess: number = 0x4ff;
  friends: UserFriend[];
  /** Verbatim on-disk 64 bytes, used as the base when re-serializing so
   * unmodelled bytes (38-39, 42-47) survive. Null for freshly-built entries. */
  raw: Uint8Array | null = null;

  constructor() {
    this.friends = [];
    for (let i = 0; i < MAX_FRIENDS; i++) {
      this.friends.push(new UserFriend());
    }
  }

  /**
   * Parse a user entry from 64 bytes at offset.
   * Returns null if the entry is not a valid user (flag != 0x81).
   */
  static fromBytes(data: Uint8Array, offset: number): UserEntry | null {
    if (data.length < offset + ENTRY_SIZE) {
      throw new Error('Insufficient data for user entry');
    }

    // Check valid user flag
    if ((data[offset] & USER_ENTRY_FLAG) !== USER_ENTRY_FLAG) return null;

    const entry = new UserEntry();
    entry.raw = data.slice(offset, offset + ENTRY_SIZE);
    // Keep the WHOLE flags byte, not just the 0x81 bits tested above. Anything
    // else set here is a flag we do not model yet, and toBytes writes it
    // straight back out.
    entry.uf = data[offset];
    entry.enterCount = data[offset + 1];
    entry.userName = readNdfsName(data, offset + 2, NDFS_NAME_MAX);
    entry.password = readUint16BE(data, offset + 18);
    entry.dateCreated = readUint32BE(data, offset + 20);
    entry.lastDateEntered = readUint32BE(data, offset + 24);
    entry.pagesReserved = readUint32BE(data, offset + 28);
    entry.pagesUsed = readUint32BE(data, offset + 32);
    entry.directoryIndex = data[offset + 36];
    entry.userIndex = data[offset + 37];
    // Default file access is at offset 40 (verified on-disk); 38-39 is unused.
    entry.defaultFileAccess = readUint16BE(data, offset + 40);

    // Parse friends (8 x 2 bytes starting at offset 48)
    for (let i = 0; i < MAX_FRIENDS; i++) {
      entry.friends[i] = UserFriend.fromBytes(data, offset + 48 + i * 2);
    }

    return entry;
  }

  /** Serialize to a 64-byte Uint8Array. */
  toBytes(): Uint8Array {
    const buf = new Uint8Array(ENTRY_SIZE);
    if (this.raw && this.raw.length === ENTRY_SIZE) {
      buf.set(this.raw, 0);
    }

    // Flags byte written verbatim so unmodelled bits survive a read/modify/
    // write. This used to be a hardcoded USER_ENTRY_FLAG, which silently
    // cleared every other bit in byte 0 of every user in the page - the
    // user-file writer re-serializes all slots, not just the edited one.
    buf[0] = this.uf & 0xff;
    buf[1] = this.enterCount & 0xff;

    writeNdfsName(buf, 2, this.userName, NDFS_NAME_MAX);
    writeUint16BE(buf, 18, this.password);
    writeUint32BE(buf, 20, this.dateCreated);
    writeUint32BE(buf, 24, this.lastDateEntered);
    writeUint32BE(buf, 28, this.pagesReserved);
    writeUint32BE(buf, 32, this.pagesUsed);
    buf[36] = this.directoryIndex;
    buf[37] = this.userIndex & 0xff;
    writeUint16BE(buf, 40, this.defaultFileAccess);

    for (let i = 0; i < MAX_FRIENDS; i++) {
      this.friends[i].toBytes(buf, 48 + i * 2);
    }

    return buf;
  }

  /** Check if user has exceeded quota. */
  isOverQuota(): boolean {
    return this.pagesUsed > this.pagesReserved;
  }

  /** Get remaining free pages in quota. */
  getFreePages(): number {
    return this.pagesReserved - this.pagesUsed;
  }

  /** Set user name (max 16 chars, uppercased). */
  setName(name: string): void {
    if (!name || name.trim().length === 0) throw new Error('User name cannot be empty');
    this.userName = name.toUpperCase().trim().substring(0, NDFS_NAME_MAX);
  }

  /** Check if userId is in this user's friend list. */
  isFriend(userId: number): boolean {
    for (let i = 0; i < this.friends.length; i++) {
      if (this.friends[i].entryUsed && this.friends[i].friendUserIndex === userId) return true;
    }
    return false;
  }

  /** Add a friend. Returns false if no empty slot. */
  addFriend(friendId: number, permissions: number): boolean {
    for (let i = 0; i < this.friends.length; i++) {
      if (!this.friends[i].entryUsed) {
        this.friends[i].setFriend(friendId, permissions);
        return true;
      }
    }
    return false;
  }

  /** Remove a friend. Returns false if not found. */
  removeFriend(friendId: number): boolean {
    for (let i = 0; i < this.friends.length; i++) {
      if (this.friends[i].entryUsed && this.friends[i].friendUserIndex === friendId) {
        this.friends[i].clear();
        return true;
      }
    }
    return false;
  }

  /** Get friend entry for a user. */
  getFriend(friendId: number): UserFriend | null {
    for (let i = 0; i < this.friends.length; i++) {
      if (this.friends[i].entryUsed && this.friends[i].friendUserIndex === friendId) {
        return this.friends[i];
      }
    }
    return null;
  }
}
