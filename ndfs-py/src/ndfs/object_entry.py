"""
NDFS object (file) entry: 64-byte record in the object file.

Byte offsets:
  0:     Header (bit 7 = in use, i.e. 0x80)
  1:     Reserved
  2-17:  Object name (16 bytes, terminated by 0x27)
  18-21: File type (4 bytes, terminated by 0x27)
  22-31: Reserved / versioning / access
  32:    File type code (0=DATA, 1=PROG, 2=SYMB, 3=TEXT)
  33:    Reserved
  34:    User index (owner)
  35-51: Reserved / tracking
  52-55: Pages in file (32-bit, big-endian)
  56-59: Bytes in file - 1 (32-bit, big-endian; actual = stored + 1)
  60-63: File pointer (BlockPointer, big-endian)

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

from typing import Optional, Union

from ndfs.constants import (
    ENTRY_SIZE,
    OBJECT_ENTRY_IN_USE,
    NDFS_NAME_MAX,
    NDFS_TYPE_MAX,
)
from ndfs.endian import read_uint32_be, write_uint32_be
from ndfs.ndfs_name import read_ndfs_name, write_ndfs_name
from ndfs.block_pointer import BlockPointer

_BufType = Union[bytes, bytearray, memoryview]


class ObjectEntry:
    """A single NDFS file (object) entry (64 bytes on disk)."""

    __slots__ = (
        "header",
        "object_index",
        "object_name",
        "type",
        "user_name",
        "user_index",
        "file_type",
        "pages_in_file",
        "bytes_in_file",
        "file_pointer",
        "access_bits",
        "date_created",
        "last_date_read",
        "last_date_written",
    )

    def __init__(self) -> None:
        self.header: int = OBJECT_ENTRY_IN_USE
        self.object_index: int = 0
        self.object_name: str = ""
        self.type: str = "DATA"
        self.user_name: str = ""
        self.user_index: int = 0
        self.file_type: int = 0  # 0=DATA, 1=PROG, 2=SYMB, 3=TEXT
        self.pages_in_file: int = 0
        self.bytes_in_file: int = 0
        self.file_pointer: Optional[BlockPointer] = None
        self.access_bits: int = 0
        self.date_created: int = 0
        self.last_date_read: int = 0
        self.last_date_written: int = 0

    @property
    def full_name(self) -> str:
        """Full name in 'NAME:TYPE' format."""
        if self.type:
            return f"{self.object_name}:{self.type}"
        return self.object_name

    @property
    def file_type_as_text(self) -> str:
        """File type as text string."""
        if self.file_type == 0:
            return "DATA"
        elif self.file_type == 1:
            return "PROG"
        elif self.file_type == 2:
            return "SYMB"
        elif self.file_type == 3:
            return "TEXT"
        else:
            return f"TYPE{self.file_type}"

    @classmethod
    def from_bytes(cls, data: _BufType, offset: int) -> Optional[ObjectEntry]:
        """Parse an object entry from 64 bytes at *offset*.

        Returns None if the entry is not in use (bit 7 of byte 0 not set).
        """
        if len(data) < offset + ENTRY_SIZE:
            raise ValueError("Insufficient data for object entry")

        # Check in-use bit
        if (data[offset] & OBJECT_ENTRY_IN_USE) == 0:
            return None

        entry = cls()
        entry.header = data[offset]

        # Object name (16 bytes at offset+2)
        entry.object_name = read_ndfs_name(data, offset + 2, NDFS_NAME_MAX)

        # File type (4 bytes at offset+18)
        type_str = read_ndfs_name(data, offset + 18, NDFS_TYPE_MAX)
        entry.type = type_str if len(type_str) > 0 else "DATA"

        # File type code (byte 32)
        entry.file_type = data[offset + 32]

        # User index (byte 34)
        entry.user_index = data[offset + 34]

        # Pages in file (bytes 52-55, big-endian)
        entry.pages_in_file = read_uint32_be(data, offset + 52)

        # Bytes in file (bytes 56-59, big-endian) + 1
        entry.bytes_in_file = read_uint32_be(data, offset + 56) + 1

        # File pointer (bytes 60-63)
        entry.file_pointer = BlockPointer.from_bytes(data, offset + 60)

        return entry

    def to_bytes(self, buffer: bytearray, offset: int) -> None:
        """Serialize to a 64-byte region in a buffer."""
        if len(buffer) < offset + ENTRY_SIZE:
            raise ValueError("Insufficient buffer for object entry")

        # Clear the entry area
        for i in range(ENTRY_SIZE):
            buffer[offset + i] = 0

        # Header (0x80 = in use)
        buffer[offset] = OBJECT_ENTRY_IN_USE

        # Object name
        write_ndfs_name(buffer, offset + 2, self.object_name, NDFS_NAME_MAX)

        # File type string
        write_ndfs_name(buffer, offset + 18, self.type, NDFS_TYPE_MAX)

        # File type code
        buffer[offset + 32] = self.file_type & 0xFF

        # User index
        buffer[offset + 34] = self.user_index & 0xFF

        # Pages in file
        write_uint32_be(buffer, offset + 52, self.pages_in_file)

        # Bytes in file - 1
        bytes_minus_one = self.bytes_in_file - 1 if self.bytes_in_file > 0 else 0
        write_uint32_be(buffer, offset + 56, bytes_minus_one)

        # File pointer
        if self.file_pointer is not None:
            self.file_pointer.to_bytes(buffer, offset + 60)
