/**
 * Public type definitions for the NDFS library.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

/** Block pointer type encoded in the top 2 bits. */
export enum PointerType {
  Contiguous = 0,
  Indexed = 1,
  SubIndexed = 2,
  Reserved = 3,
}

/** Boot code format detected in page 0. */
export enum BootFormat {
  None = 'none',
  Binary = 'binary',
  BPUN = 'bpun',
  FloMon = 'flomon',
}

/** Access level for a user relative to a file. */
export enum FileAccessType {
  Own = 0,
  Friend = 1,
  Public = 2,
}

/** File operation types for permission checks. */
export enum FileOperationType {
  Read = 0,
  Write = 1,
  Append = 2,
  Execute = 3,
  Delete = 4,
  List = 5,
}

/** Checksum validation state for extended info. */
export enum ChecksumValidation {
  Valid = 'valid',
  ValidLowByteOnly = 'valid_low_byte',
  Invalid = 'invalid',
}

/** Disk image template for image creation. */
export enum ImageTemplate {
  Floppy360KB = 'floppy_360kb',
  Floppy12MB = 'floppy_12mb',
  Smd75MB = 'smd_75mb',
  Winchester74MB = 'winchester_74mb',
  Custom = 'custom',
}

/** File type flags (bit field). */
export enum FileTypeFlags {
  None = 0,
  TerminalFile = 1 << 0,
  PeripheralFile = 1 << 1,
  SpoolingFile = 1 << 2,
  IndexedFile = 1 << 3,
  ContiguousFile = 1 << 4,
  AllocatedFile = 1 << 5,
  MagneticTapeFile = 1 << 6,
  LibraryFile = 1 << 7,
}

/** A file/directory entry returned by listDirectory(). */
export interface FileEntry {
  name: string;
  type: string;
  fullName: string;
  userName: string;
  size: number;
  pages: number;
  isDirectory: boolean;
  lastModified: Date | null;
}

/** Boot code extracted from page 0. */
export interface BootCode {
  format: BootFormat;
  startAddress: number;
  bootAddress: number;
  loadAddress: number;
  wordCount: number;
  data: Uint8Array;
  checksumValid: boolean;
}

/** Options for creating a new NDFS disk image. */
export interface ImageCreationOptions {
  template: ImageTemplate;
  directoryName?: string;
  customPages?: number;
  includeExtendedInfo?: boolean;
  systemNumber?: number;
  flagWord?: number;
  users?: Array<{ name: string; reservedPages: number }>;
}
