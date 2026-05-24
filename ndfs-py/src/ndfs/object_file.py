"""
NDFS object file: manages the collection of file (object) entries.

Can be indexed (single index block with up to 512 pointers to data pages)
or sub-indexed (sub-index block with 512 pointers to index blocks).

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

from typing import Callable, Dict, List, Optional, Union

from ndfs.constants import (
    NDFS_PAGE_SIZE,
    ENTRIES_PER_PAGE,
    ENTRY_SIZE,
    MAX_OBJECT_FILE_POINTERS,
)
from ndfs.block_pointer import BlockPointer
from ndfs.object_entry import ObjectEntry
from ndfs.types import PointerType

_BufType = Union[bytes, bytearray, memoryview]


class ObjectFile:
    """Manages the collection of NDFS file (object) entries."""

    __slots__ = ("index_pointer", "_entries", "_next_index")

    def __init__(self) -> None:
        self.index_pointer: Optional[BlockPointer] = None
        self._entries: Dict[int, ObjectEntry] = {}
        self._next_index: int = 0

    def get_objects(self) -> List[ObjectEntry]:
        """Get all object entries."""
        result: List[ObjectEntry] = []
        for v in self._entries.values():
            result.append(v)
        return result

    def get_object(self, index: int) -> Optional[ObjectEntry]:
        """Get an object entry by index."""
        return self._entries.get(index, None)

    def find_object(self, object_name: str, user_name: str) -> Optional[ObjectEntry]:
        """Find an object by name and user."""
        name_upper = object_name.upper()
        user_upper = user_name.upper()
        for entry in self._entries.values():
            if (
                entry.object_name.upper() == name_upper
                and entry.user_name.upper() == user_upper
            ):
                return entry
        return None

    def add_object(self, entry: ObjectEntry) -> None:
        """Add or update an object entry."""
        self._entries[entry.object_index] = entry
        if entry.object_index >= self._next_index:
            self._next_index = entry.object_index + 1

    def remove_object(self, index: int) -> bool:
        """Remove an object entry."""
        if index in self._entries:
            del self._entries[index]
            return True
        return False

    def get_user_objects(self, user_name_or_index: Union[str, int]) -> List[ObjectEntry]:
        """Get objects belonging to a user (by name or index)."""
        result: List[ObjectEntry] = []
        for entry in self._entries.values():
            if isinstance(user_name_or_index, str):
                if entry.user_name.upper() == user_name_or_index.upper():
                    result.append(entry)
            else:
                if entry.user_index == user_name_or_index:
                    result.append(entry)
        return result

    def get_next_available_index(self) -> int:
        """Get next available object index."""
        for i in range(self._next_index + 1):
            if i not in self._entries:
                return i
        return self._next_index

    def get_total_pages_used(self) -> int:
        """Get total pages used by all objects."""
        total = 0
        for e in self._entries.values():
            total += e.pages_in_file
        return total

    def clear(self) -> None:
        """Clear all entries."""
        self._entries.clear()
        self._next_index = 0

    def load_from_pages(
        self,
        pointer: BlockPointer,
        read_page: Callable[[int], _BufType],
    ) -> None:
        """Load entries from an indexed or sub-indexed structure.

        Args:
            pointer: The object file's block pointer (from master block).
            read_page: Callback to read a page by block ID.
        """
        self._entries.clear()
        self.index_pointer = pointer
        global_object_index = 0

        if pointer.type == PointerType.Indexed:
            # Single index block -> up to 512 data page pointers
            index_page = read_page(pointer.block_id)
            self._load_objects_from_index_block(index_page, read_page, global_object_index)
        elif pointer.type == PointerType.SubIndexed:
            # Sub-index block -> up to 512 index block pointers
            sub_index_page = read_page(pointer.block_id)
            for i in range(MAX_OBJECT_FILE_POINTERS):
                index_ptr = BlockPointer.from_bytes(sub_index_page, i * 4)
                if not index_ptr.is_valid():
                    continue
                index_page = read_page(index_ptr.block_id)
                global_object_index = self._load_objects_from_index_block(
                    index_page, read_page, global_object_index
                )

    def _load_objects_from_index_block(
        self,
        index_page: _BufType,
        read_page: Callable[[int], _BufType],
        start_index: int,
    ) -> int:
        """Load objects from a single index block."""
        object_index = start_index

        for i in range(MAX_OBJECT_FILE_POINTERS):
            ptr = BlockPointer.from_bytes(index_page, i * 4)
            if not ptr.is_valid():
                object_index += ENTRIES_PER_PAGE
                continue

            data_page = read_page(ptr.block_id)
            for j in range(ENTRIES_PER_PAGE):
                entry = ObjectEntry.from_bytes(data_page, j * ENTRY_SIZE)
                if entry is not None:
                    entry.object_index = object_index + j
                    self._entries[entry.object_index] = entry
                    if entry.object_index >= self._next_index:
                        self._next_index = entry.object_index + 1
            object_index += ENTRIES_PER_PAGE

        return object_index

    def to_data_pages(self) -> Dict[int, bytearray]:
        """Serialize all object entries into page-aligned buffers.

        Returns a dict mapping page_index to page data.
        """
        page_map: Dict[int, bytearray] = {}

        for entry in self._entries.values():
            page_index = entry.object_index // ENTRIES_PER_PAGE
            if page_index not in page_map:
                page_map[page_index] = bytearray(NDFS_PAGE_SIZE)
            page = page_map[page_index]
            slot_in_page = entry.object_index % ENTRIES_PER_PAGE
            entry.to_bytes(page, slot_in_page * ENTRY_SIZE)

        return page_map
