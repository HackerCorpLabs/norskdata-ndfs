/**
 * NDFS boot loader detection and extraction.
 *
 * Boot formats:
 *   - FLOMON: '!' delimiter followed by address=0 and count=0
 *   - BPUN: '!' delimiter followed by valid address and count, then data + checksum
 *   - Binary: Non-zero/non-uniform data in first 1024 bytes without '!' delimiter
 *   - None: Empty or uniform page 0
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

import { NDFS_PAGE_SIZE, MASTER_BLOCK_OFFSET } from './constants.js';
import { readUint16BE } from './endian.js';
import { BootFormat, BootCode } from './types.js';

/** Maximum scan range for '!' delimiter (stay within boot area, before master block). */
const BOOT_SCAN_LIMIT = Math.min(1024, MASTER_BLOCK_OFFSET);

/**
 * Detect the boot format of an NDFS page 0.
 */
export function detectBootFormat(page0: Uint8Array): BootFormat {
  if (page0.length < NDFS_PAGE_SIZE) return BootFormat.None;

  // Scan for '!' (0x21) delimiter in boot area
  let exclamationPos = -1;
  for (let i = 0; i < BOOT_SCAN_LIMIT; i++) {
    if (page0[i] === 0x21) {
      exclamationPos = i;
      break;
    }
  }

  if (exclamationPos >= 0) {
    const afterExcl = exclamationPos + 1;
    if (afterExcl + 4 <= page0.length) {
      const address = readUint16BE(page0, afterExcl);
      const count = readUint16BE(page0, afterExcl + 2);

      if (address === 0 && count === 0) {
        return BootFormat.FloMon;
      }

      // Valid address and count -> BPUN
      if (count > 0) {
        return BootFormat.BPUN;
      }
    }
  }

  // Check for non-zero/non-uniform data in first 1024 bytes (Binary format)
  let hasNonZero = false;
  let allSame = true;
  const firstByte = page0[0];
  for (let i = 0; i < BOOT_SCAN_LIMIT; i++) {
    if (page0[i] !== 0) hasNonZero = true;
    if (page0[i] !== firstByte) allSame = false;
    if (hasNonZero && !allSame) break;
  }

  if (hasNonZero && !allSame) {
    return BootFormat.Binary;
  }

  return BootFormat.None;
}

/**
 * Load boot code from an NDFS page 0.
 * Returns null if no boot format is detected.
 */
export function loadBootCode(page0: Uint8Array): BootCode | null {
  const format = detectBootFormat(page0);
  if (format === BootFormat.None) return null;

  if (format === BootFormat.Binary) {
    // Extract the first 1024 bytes as raw binary boot code
    const data = new Uint8Array(BOOT_SCAN_LIMIT);
    data.set(page0.subarray(0, BOOT_SCAN_LIMIT));
    return {
      format,
      startAddress: 0,
      bootAddress: 0,
      loadAddress: 0,
      wordCount: 0,
      data,
      checksumValid: false,
    };
  }

  // Find '!' delimiter
  let exclamationPos = -1;
  for (let i = 0; i < BOOT_SCAN_LIMIT; i++) {
    if (page0[i] === 0x21) {
      exclamationPos = i;
      break;
    }
  }

  if (exclamationPos < 0) return null;

  const afterExcl = exclamationPos + 1;
  const address = readUint16BE(page0, afterExcl);
  const count = readUint16BE(page0, afterExcl + 2);

  if (format === BootFormat.FloMon) {
    // FLOMON: data before '!'
    const data = new Uint8Array(exclamationPos);
    data.set(page0.subarray(0, exclamationPos));
    return {
      format,
      startAddress: 0,
      bootAddress: 0,
      loadAddress: 0,
      wordCount: 0,
      data,
      checksumValid: true,
    };
  }

  // BPUN: address(2) + count(2) + data(count*2) + checksum(2) + action(2)
  const dataStart = afterExcl + 4;
  const dataByteCount = count * 2;
  const checksumOffset = dataStart + dataByteCount;

  // Verify we have enough data
  if (checksumOffset + 2 > MASTER_BLOCK_OFFSET) {
    // Truncated BPUN - extract what we can
    const available = Math.min(dataByteCount, MASTER_BLOCK_OFFSET - dataStart);
    const data = new Uint8Array(available);
    data.set(page0.subarray(dataStart, dataStart + available));
    return {
      format,
      startAddress: address,
      bootAddress: address,
      loadAddress: address,
      wordCount: count,
      data,
      checksumValid: false,
    };
  }

  const data = new Uint8Array(dataByteCount);
  data.set(page0.subarray(dataStart, dataStart + dataByteCount));

  // Validate checksum: sum of all words (address + count + data words) should match
  let checksum = address + count;
  for (let i = 0; i < count; i++) {
    checksum += readUint16BE(page0, dataStart + i * 2);
  }
  checksum = checksum & 0xffff;

  const storedChecksum = readUint16BE(page0, checksumOffset);
  const checksumValid = (checksum === storedChecksum);

  // Action byte (after checksum)
  let bootAddress = address;
  if (checksumOffset + 4 <= page0.length) {
    const action = readUint16BE(page0, checksumOffset + 2);
    if (action > 0) bootAddress = action;
  }

  return {
    format,
    startAddress: address,
    bootAddress,
    loadAddress: address,
    wordCount: count,
    data,
    checksumValid,
  };
}
