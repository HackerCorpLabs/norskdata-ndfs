/**
 * XAT (Extended Attribute) sidecar file support for NDFS.
 *
 * Preserves NDFS metadata (access bits, dates, file type flags, user info, etc.)
 * when files are copied to/from host filesystems.
 *
 * When extracting a file from NDFS, a companion `.xat` JSON file is created
 * alongside the data file containing all the NDFS metadata that would
 * otherwise be lost. When copying back in, the `.xat` file is read to
 * restore the metadata.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import { ObjectEntry } from './object-entry.js';

/** Property key constants for XAT files. */
export const XAT_KEYS = {
  OBJECT_NAME: 'ndfs.object_name',
  TYPE: 'ndfs.type',
  USER_NAME: 'ndfs.user_name',
  USER_INDEX: 'ndfs.user_index',
  ACCESS_BITS: 'ndfs.access_bits',
  FILE_TYPE_FLAGS: 'ndfs.file_type_flags',
  FILE_TYPE: 'ndfs.file_type',
  PAGES_IN_FILE: 'ndfs.pages_in_file',
  BYTES_IN_FILE: 'ndfs.bytes_in_file',
  DATE_CREATED: 'ndfs.date_created',
  LAST_READ_DATE: 'ndfs.last_read_date',
  LAST_WRITE_DATE: 'ndfs.last_write_date',
} as const;

/** XAT property bag: string-keyed record of primitives. */
export type XatProperties = Record<string, string | number | boolean>;

/** XAT file extension. */
export const XAT_EXTENSION = '.xat';

/**
 * Serialize an ObjectEntry to XAT properties.
 * Captures all NDFS metadata fields.
 */
export function objectEntryToXat(entry: ObjectEntry): XatProperties {
  const props: XatProperties = {};
  props[XAT_KEYS.OBJECT_NAME] = entry.objectName;
  props[XAT_KEYS.TYPE] = entry.type;
  props[XAT_KEYS.USER_NAME] = entry.userName;
  props[XAT_KEYS.USER_INDEX] = entry.userIndex;
  props[XAT_KEYS.ACCESS_BITS] = entry.accessBits;
  props[XAT_KEYS.FILE_TYPE_FLAGS] = entry.fileTypeFlags;
  props[XAT_KEYS.FILE_TYPE] = entry.fileType;
  props[XAT_KEYS.PAGES_IN_FILE] = entry.pagesInFile;
  props[XAT_KEYS.BYTES_IN_FILE] = entry.bytesInFile;
  props[XAT_KEYS.DATE_CREATED] = entry.dateCreated;
  props[XAT_KEYS.LAST_READ_DATE] = entry.lastDateRead;
  props[XAT_KEYS.LAST_WRITE_DATE] = entry.lastDateWritten;
  return props;
}

/**
 * Apply XAT properties to an ObjectEntry (for copy-in / restore).
 * Only updates fields that are present in the XAT properties.
 */
export function xatToObjectEntry(xat: XatProperties, entry: ObjectEntry): void {
  if (XAT_KEYS.OBJECT_NAME in xat && typeof xat[XAT_KEYS.OBJECT_NAME] === 'string') {
    entry.objectName = xat[XAT_KEYS.OBJECT_NAME] as string;
  }
  if (XAT_KEYS.TYPE in xat && typeof xat[XAT_KEYS.TYPE] === 'string') {
    entry.type = xat[XAT_KEYS.TYPE] as string;
  }
  if (XAT_KEYS.USER_NAME in xat && typeof xat[XAT_KEYS.USER_NAME] === 'string') {
    entry.userName = xat[XAT_KEYS.USER_NAME] as string;
  }
  if (XAT_KEYS.USER_INDEX in xat && typeof xat[XAT_KEYS.USER_INDEX] === 'number') {
    entry.userIndex = xat[XAT_KEYS.USER_INDEX] as number;
  }
  if (XAT_KEYS.ACCESS_BITS in xat && typeof xat[XAT_KEYS.ACCESS_BITS] === 'number') {
    entry.accessBits = xat[XAT_KEYS.ACCESS_BITS] as number;
  }
  if (XAT_KEYS.FILE_TYPE in xat && typeof xat[XAT_KEYS.FILE_TYPE] === 'number') {
    entry.fileType = xat[XAT_KEYS.FILE_TYPE] as number;
  }
  if (
    XAT_KEYS.FILE_TYPE_FLAGS in xat &&
    typeof xat[XAT_KEYS.FILE_TYPE_FLAGS] === 'number'
  ) {
    entry.fileTypeFlags = xat[XAT_KEYS.FILE_TYPE_FLAGS] as number;
  }
  if (XAT_KEYS.PAGES_IN_FILE in xat && typeof xat[XAT_KEYS.PAGES_IN_FILE] === 'number') {
    entry.pagesInFile = xat[XAT_KEYS.PAGES_IN_FILE] as number;
  }
  if (XAT_KEYS.BYTES_IN_FILE in xat && typeof xat[XAT_KEYS.BYTES_IN_FILE] === 'number') {
    entry.bytesInFile = xat[XAT_KEYS.BYTES_IN_FILE] as number;
  }
  if (XAT_KEYS.DATE_CREATED in xat && typeof xat[XAT_KEYS.DATE_CREATED] === 'number') {
    entry.dateCreated = xat[XAT_KEYS.DATE_CREATED] as number;
  }
  if (XAT_KEYS.LAST_READ_DATE in xat && typeof xat[XAT_KEYS.LAST_READ_DATE] === 'number') {
    entry.lastDateRead = xat[XAT_KEYS.LAST_READ_DATE] as number;
  }
  if (XAT_KEYS.LAST_WRITE_DATE in xat && typeof xat[XAT_KEYS.LAST_WRITE_DATE] === 'number') {
    entry.lastDateWritten = xat[XAT_KEYS.LAST_WRITE_DATE] as number;
  }
}

/**
 * Serialize XAT properties to a JSON string (pretty-printed).
 */
export function serializeXat(props: XatProperties): string {
  return JSON.stringify(props, null, 2);
}

/**
 * Deserialize XAT properties from a JSON string.
 */
export function deserializeXat(json: string): XatProperties {
  const parsed = JSON.parse(json);
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    throw new Error('Invalid XAT data: expected a JSON object');
  }
  return parsed as XatProperties;
}

/**
 * Get the .xat filename for a given data file.
 * E.g., "README.TEXT" -> "README.TEXT.xat"
 */
export function getXatFileName(dataFile: string): string {
  return dataFile + XAT_EXTENSION;
}

/**
 * Check if a filename is an XAT sidecar file.
 */
export function isXatFile(fileName: string): boolean {
  return fileName.length > XAT_EXTENSION.length &&
    fileName.toLowerCase().endsWith(XAT_EXTENSION);
}
