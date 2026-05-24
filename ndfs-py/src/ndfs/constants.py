"""
NDFS (Norsk Data File System) constants.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""

# Page size in bytes (1024 x 16-bit words).
NDFS_PAGE_SIZE: int = 2048

# Name terminator character (ASCII single quote).
NDFS_NAME_TERMINATOR: int = 0x27

# Maximum length of a directory/user/object name.
NDFS_NAME_MAX: int = 16

# Maximum length of a file type string.
NDFS_TYPE_MAX: int = 4

# Number of entries (user or object) per page.
ENTRIES_PER_PAGE: int = 32

# Size of a single entry (user or object) in bytes.
ENTRY_SIZE: int = 64

# Maximum number of pointers in the user file index block.
MAX_USER_FILE_POINTERS: int = 8

# Maximum number of pointers in an object file index block.
MAX_OBJECT_FILE_POINTERS: int = 512

# Maximum number of users.
MAX_USERS: int = 256

# Maximum number of friends per user.
MAX_FRIENDS: int = 8

# Byte offset of the master block within page 0.
MASTER_BLOCK_OFFSET: int = 2016

# Byte offset of the extended info block within page 0.
EXTENDED_INFO_OFFSET: int = 2000

# Size of the master block in bytes.
MASTER_BLOCK_SIZE: int = 32

# Size of the extended info block in bytes.
EXTENDED_INFO_SIZE: int = 16

# First block ID available for allocation (0-6 are system).
FIRST_ALLOCATABLE_BLOCK: int = 7

# Valid user entry flag byte.
USER_ENTRY_FLAG: int = 0x81

# Valid object entry header bit (bit 7).
OBJECT_ENTRY_IN_USE: int = 0x80
