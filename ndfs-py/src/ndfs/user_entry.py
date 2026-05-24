"""
NDFS user entry: 64-byte record in the user file.

Byte offsets:
  0:     Flag (0x81 = valid user)
  1:     Enter count
  2-17:  User name (16 bytes, terminated by 0x27)
  18-19: Password (16-bit, big-endian)
  20-23: Date created (ND time, big-endian)
  24-27: Last date entered (ND time, big-endian)
  28-31: Pages reserved (32-bit, big-endian)
  32-35: Pages used (32-bit, big-endian)
  36:    Directory index
  37:    User index
  38-39: Default file access (16-bit, big-endian)
  40-55: Friends (8 x 2-byte entries, big-endian)
  56-63: Reserved

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

from typing import List, Optional, Union

from ndfs.constants import (
    ENTRY_SIZE,
    USER_ENTRY_FLAG,
    NDFS_NAME_MAX,
    MAX_FRIENDS,
)
from ndfs.endian import read_uint16_be, read_uint32_be, write_uint16_be, write_uint32_be
from ndfs.ndfs_name import read_ndfs_name, write_ndfs_name
from ndfs.user_friend import UserFriend

_BufType = Union[bytes, bytearray, memoryview]


class UserEntry:
    """A single NDFS user record (64 bytes on disk)."""

    __slots__ = (
        "user_index",
        "user_name",
        "password",
        "enter_count",
        "date_created",
        "last_date_entered",
        "pages_reserved",
        "pages_used",
        "directory_index",
        "default_file_access",
        "friends",
    )

    def __init__(self) -> None:
        self.user_index: int = 0
        self.user_name: str = ""
        self.password: int = 0
        self.enter_count: int = 0
        self.date_created: int = 0
        self.last_date_entered: int = 0
        self.pages_reserved: int = 0
        self.pages_used: int = 0
        self.directory_index: int = 0
        self.default_file_access: int = 0x4FF
        self.friends: List[UserFriend] = []
        for _ in range(MAX_FRIENDS):
            self.friends.append(UserFriend())

    @classmethod
    def from_bytes(cls, data: _BufType, offset: int) -> Optional[UserEntry]:
        """Parse a user entry from 64 bytes at *offset*.

        Returns None if the entry is not a valid user (flag != 0x81).
        """
        if len(data) < offset + ENTRY_SIZE:
            raise ValueError("Insufficient data for user entry")

        # Check valid user flag
        if (data[offset] & USER_ENTRY_FLAG) != USER_ENTRY_FLAG:
            return None

        entry = cls()
        entry.enter_count = data[offset + 1]
        entry.user_name = read_ndfs_name(data, offset + 2, NDFS_NAME_MAX)
        entry.password = read_uint16_be(data, offset + 18)
        entry.date_created = read_uint32_be(data, offset + 20)
        entry.last_date_entered = read_uint32_be(data, offset + 24)
        entry.pages_reserved = read_uint32_be(data, offset + 28)
        entry.pages_used = read_uint32_be(data, offset + 32)
        entry.directory_index = data[offset + 36]
        entry.user_index = data[offset + 37]
        entry.default_file_access = read_uint16_be(data, offset + 38)

        # Parse friends (8 x 2 bytes starting at offset 40)
        for i in range(MAX_FRIENDS):
            entry.friends[i] = UserFriend.from_bytes(data, offset + 40 + i * 2)

        return entry

    def to_bytes(self) -> bytearray:
        """Serialize to a 64-byte bytearray."""
        buf = bytearray(ENTRY_SIZE)

        buf[0] = USER_ENTRY_FLAG
        buf[1] = self.enter_count & 0xFF

        write_ndfs_name(buf, 2, self.user_name, NDFS_NAME_MAX)
        write_uint16_be(buf, 18, self.password)
        write_uint32_be(buf, 20, self.date_created)
        write_uint32_be(buf, 24, self.last_date_entered)
        write_uint32_be(buf, 28, self.pages_reserved)
        write_uint32_be(buf, 32, self.pages_used)
        buf[36] = self.directory_index
        buf[37] = self.user_index & 0xFF
        write_uint16_be(buf, 38, self.default_file_access)

        for i in range(MAX_FRIENDS):
            self.friends[i].to_bytes(buf, 40 + i * 2)

        return buf

    def is_over_quota(self) -> bool:
        """Check if user has exceeded quota."""
        return self.pages_used > self.pages_reserved

    def get_free_pages(self) -> int:
        """Get remaining free pages in quota."""
        return self.pages_reserved - self.pages_used

    def set_name(self, name: str) -> None:
        """Set user name (max 16 chars, uppercased)."""
        if not name or len(name.strip()) == 0:
            raise ValueError("User name cannot be empty")
        self.user_name = name.upper().strip()[:NDFS_NAME_MAX]

    def is_friend(self, user_id: int) -> bool:
        """Check if *user_id* is in this user's friend list."""
        for i in range(len(self.friends)):
            if self.friends[i].entry_used and self.friends[i].friend_user_index == user_id:
                return True
        return False

    def add_friend(self, friend_id: int, permissions: int) -> bool:
        """Add a friend. Returns False if no empty slot."""
        for i in range(len(self.friends)):
            if not self.friends[i].entry_used:
                self.friends[i].set_friend(friend_id, permissions)
                return True
        return False

    def remove_friend(self, friend_id: int) -> bool:
        """Remove a friend. Returns False if not found."""
        for i in range(len(self.friends)):
            if self.friends[i].entry_used and self.friends[i].friend_user_index == friend_id:
                self.friends[i].clear()
                return True
        return False

    def get_friend(self, friend_id: int) -> Optional[UserFriend]:
        """Get friend entry for a user."""
        for i in range(len(self.friends)):
            if self.friends[i].entry_used and self.friends[i].friend_user_index == friend_id:
                return self.friends[i]
        return None
