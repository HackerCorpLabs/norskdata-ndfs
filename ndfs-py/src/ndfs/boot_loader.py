"""
NDFS boot loader: detects and loads boot code from NDFS block 0.

Supports BPUN, FLOMON, and raw binary formats.

NDFS disks may contain bootable code in the first page (block 0, 2048 bytes).
Formats:

1. BPUN Format (http://www.ndwiki.org/wiki/BPUN_File_Format):
   - ASCII preamble with start/boot addresses (octal, CR-terminated)
   - '!' delimiter
   - Binary section: Address(2) + Count(2) + Data + Checksum(2) + Action(2)
   - All multi-byte values are big-endian

2. FLOMON Format (Floppy Monitor - simplified BPUN):
   - Same preamble as BPUN
   - After '!' delimiter, if Address=0, Count=0, Checksum=0:
     Next byte is word count (single byte, NOT a word)
     Data follows as: 00 HI 00 LO (4 bytes per word)

3. Raw Binary:
   - Direct machine code starting at offset 0

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

from typing import Optional, Union

from ndfs.constants import NDFS_PAGE_SIZE
from ndfs.types import BootFormat, BootCode

_BufType = Union[bytes, bytearray, memoryview]

BPUN_DELIMITER = 0x21  # '!'


def _is_valid_binary_code(data: _BufType) -> bool:
    """Heuristic check if data contains valid ND-100 binary code."""
    if len(data) < 16:
        return False

    all_zeros = True
    all_ff = True
    all_f6 = True

    limit = min(256, len(data))
    for i in range(limit):
        if data[i] != 0x00:
            all_zeros = False
        if data[i] != 0xFF:
            all_ff = False
        if data[i] != 0xF6:
            all_f6 = False
        if not all_zeros and not all_ff and not all_f6:
            break

    if all_zeros or all_ff or all_f6:
        return False

    # Check for BPUN marker - if present, it's not raw binary
    check_limit = min(512, len(data))
    for i in range(check_limit):
        if data[i] == BPUN_DELIMITER:
            return False

    return True


def _try_parse_bpun_binary_section(
    data: _BufType,
    pos: int,
) -> Optional[BootCode]:
    """Try to parse BPUN binary section starting after a '!' delimiter.

    Returns a BootCode or None if invalid.
    """
    if pos + 4 > len(data):
        return None

    # Address (2 bytes, big-endian)
    address = (data[pos] << 8) | data[pos + 1]
    pos += 2

    # Count (2 bytes, big-endian) -- word count
    count = (data[pos] << 8) | data[pos + 1]
    pos += 2

    # Validate: count * 2 bytes + checksum(2) + action(2) must fit
    data_bytes = count * 2
    total_needed = pos + data_bytes + 4
    if total_needed > NDFS_PAGE_SIZE:
        return None

    # Read code data and calculate checksum
    code_data = bytearray(data_bytes)
    checksum = 0
    for i in range(0, data_bytes, 2):
        if pos + 1 >= len(data):
            return None
        hi = data[pos]
        lo = data[pos + 1]
        code_data[i] = hi
        code_data[i + 1] = lo
        word = (hi << 8) | lo
        checksum = (checksum + word) & 0xFFFF
        pos += 2

    # Read file checksum (2 bytes, big-endian)
    if pos + 1 >= len(data):
        return None
    file_checksum = (data[pos] << 8) | data[pos + 1]
    pos += 2

    # Check for FLOMON format (address=0, count=0, checksum=0)
    if address == 0 and count == 0 and file_checksum == 0:
        return _load_flomon_format(data, pos)

    # Standard BPUN format
    boot = BootCode()
    boot.format = BootFormat.BPUN
    boot.load_address = address
    boot.word_count = count
    boot.data = bytes(code_data)
    boot.checksum_valid = (file_checksum == checksum)
    boot.start_address = 0
    boot.boot_address = 0

    return boot


def _load_flomon_format(data: _BufType, pos: int) -> BootCode:
    """Load FLOMON format (special BPUN variant for floppy boot)."""
    boot = BootCode()
    boot.format = BootFormat.FLOMON
    boot.load_address = 0

    if pos >= len(data):
        boot.data = b""
        boot.word_count = 0
        return boot

    # Next byte is word count (single byte)
    word_count = data[pos]
    pos += 1
    boot.word_count = word_count

    # FLOMON: each word stored as 4 bytes: 00 HI 00 LO
    code_data = bytearray(word_count * 2)
    code_index = 0

    for _ in range(word_count):
        if pos + 3 >= len(data):
            break

        pad1 = data[pos]
        if pad1 != 0:
            break
        hi = data[pos + 1]
        pad2 = data[pos + 2]
        if pad2 != 0:
            break
        lo = data[pos + 3]
        pos += 4

        code_data[code_index] = hi
        code_data[code_index + 1] = lo
        code_index += 2

    boot.data = bytes(code_data[:code_index])
    boot.checksum_valid = True  # FLOMON has no checksum to validate
    return boot


def load_boot_code(data: _BufType) -> Optional[BootCode]:
    """Load boot code from raw page 0 bytes.

    Args:
        data: First page (at least 2048 bytes) from NDFS disk.

    Returns:
        BootCode with format and data, or None if no valid boot code found.
    """
    if len(data) < NDFS_PAGE_SIZE:
        return None

    # Find all '!' delimiters and try each one
    delimiter_positions = []
    for i in range(min(len(data), NDFS_PAGE_SIZE)):
        if data[i] == BPUN_DELIMITER:
            delimiter_positions.append(i)

    # Try each delimiter position
    for delim_pos in delimiter_positions:
        result = _try_parse_bpun_binary_section(data, delim_pos + 1)
        if result is not None:
            return result

    # Check for raw binary
    if _is_valid_binary_code(data):
        boot = BootCode()
        boot.format = BootFormat.BINARY
        boot.start_address = 0
        boot.load_address = 0
        boot.data = bytes(data[:NDFS_PAGE_SIZE])
        boot.checksum_valid = False
        return boot

    return None


def detect_boot_format(data: _BufType) -> BootFormat:
    """Detect boot format without loading full code.

    Args:
        data: First page (at least 2048 bytes) from NDFS disk.

    Returns:
        The detected BootFormat.
    """
    boot = load_boot_code(data)
    if boot is None:
        return BootFormat.NONE
    return boot.format


def is_bootable(data: _BufType) -> bool:
    """Check if NDFS disk has valid boot code.

    Args:
        data: First page (at least 2048 bytes) from NDFS disk.

    Returns:
        True if valid boot code exists.
    """
    return detect_boot_format(data) != BootFormat.NONE
