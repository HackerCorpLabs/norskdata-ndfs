"""
NdfsFileSystem: main class for reading and writing NDFS disk images.

Operates on a bytearray buffer representing the entire disk image.
Supports contiguous, indexed, and sub-indexed file allocation,
sparse files, user management, and quota tracking.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Tuple, Union

from ndfs.constants import (
    NDFS_PAGE_SIZE,
    ENTRIES_PER_PAGE,
    ENTRY_SIZE,
    MAX_OBJECT_FILE_POINTERS,
    MAX_USER_FILE_POINTERS,
    FIRST_ALLOCATABLE_BLOCK,
    MAX_USERS,
    NDFS_NAME_MAX,
    NDFS_TYPE_MAX,
)
from ndfs.endian import read_uint32_be, write_uint32_be
from ndfs.block_pointer import BlockPointer
from ndfs.master_block import MasterBlock
from ndfs.bit_file import BitFile
from ndfs.user_file import UserFile
from ndfs.object_file import ObjectFile
from ndfs.user_entry import UserEntry
from ndfs.object_entry import ObjectEntry, ACCESS_DEFAULT, FT_INDEXED
from ndfs.types import PointerType, FileEntry, ImageCreationOptions, BootFormat, BootCode
from ndfs.xat import object_entry_to_xat, xat_to_object_entry

_BufType = Union[bytes, bytearray, memoryview]


class NdfsFileSystem:
    """Main class for reading and writing NDFS disk images.

    Args:
        data: The raw disk image bytes (must be a multiple of 2048).
        read_only: If True, write operations will raise an error.
    """

    def __init__(
        self,
        data: Union[bytes, bytearray, memoryview],
        read_only: bool = False,
    ) -> None:
        # Always work on a mutable copy
        self._data: bytearray = bytearray(data)
        self._read_only: bool = read_only

        if len(self._data) < NDFS_PAGE_SIZE:
            raise ValueError("Image too small: must be at least one NDFS page (2048 bytes)")
        if len(self._data) % NDFS_PAGE_SIZE != 0:
            raise ValueError("Image size must be a multiple of NDFS page size (2048 bytes)")

        # Parse master block
        page0 = self._read_page(0)
        self._master_block: MasterBlock = MasterBlock.from_bytes(page0)
        if not self._master_block.is_valid():
            raise ValueError("Invalid NDFS master block")
        self._master_block.image_size = len(self._data) // NDFS_PAGE_SIZE

        self._bit_file: BitFile = BitFile()
        self._user_file: UserFile = UserFile()
        self._object_file: ObjectFile = ObjectFile()

        # Load filesystem structures
        self._load_structures()

    # -- Factory methods ---------------------------------------------------

    @classmethod
    def create_image(cls, options: ImageCreationOptions) -> NdfsFileSystem:
        """Create a new NDFS disk image from options.

        Args:
            options: Image creation options specifying template and parameters.

        Returns:
            A new NdfsFileSystem instance backed by the created image.
        """
        from ndfs.image_creator import create_image as _create_image
        data = _create_image(options)
        return cls(data, read_only=False)

    # -- Lifecycle ---------------------------------------------------------

    def to_buffer(self) -> bytearray:
        """Export the current image as a new bytearray."""
        return bytearray(self._data)

    # -- Read operations ---------------------------------------------------

    def get_master_block(self) -> MasterBlock:
        """Get the parsed master block."""
        return self._master_block

    def get_directory_name(self) -> str:
        """Get the volume/directory name."""
        return self._master_block.directory_name

    def list_directory(self, path: str = "") -> List[FileEntry]:
        """List directory contents.

        - path="" or "/": lists users as directories.
        - path="USERNAME": lists that user's files.
        """
        normalized = path.strip("/")
        entries: List[FileEntry] = []

        if normalized == "":
            # Root: list users as directories
            users = self._user_file.get_users()
            for i in range(len(users)):
                u = users[i]
                entries.append(FileEntry(
                    name=u.user_name,
                    type="",
                    full_name=u.user_name,
                    user_name=u.user_name,
                    size=0,
                    pages=0,
                    is_directory=True,
                    last_modified=None,
                ))
        else:
            parts = normalized.split("/")
            if len(parts) > 1:
                raise ValueError("NDFS does not support subdirectories")
            user_name = parts[0]
            objects = self._object_file.get_user_objects(user_name)
            for i in range(len(objects)):
                obj = objects[i]
                full_name = f"{obj.object_name}:{obj.type}" if obj.type else obj.object_name
                entries.append(FileEntry(
                    name=obj.object_name,
                    type=obj.type,
                    full_name=full_name,
                    user_name=obj.user_name,
                    size=obj.bytes_in_file,
                    pages=obj.pages_in_file,
                    is_directory=False,
                    last_modified=None,
                ))
        return entries

    def read_file(self, path: str, parity: str = "none") -> bytes:
        """Read a file's contents.

        Args:
            path: "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
            parity: Parity handling -- "none" (default, raw bytes),
                "strip" (clear bit 7, for reading ND text as ASCII).
        """
        from ndfs.parity import strip_parity as _strip

        obj = self._find_object(path)
        if obj is None:
            raise FileNotFoundError(f"File not found: {path}")
        data = bytes(self._read_object_data(obj))
        if parity == "strip":
            data = _strip(data)
        return data

    def get_metadata(self, path: str) -> Optional[FileEntry]:
        """Get file metadata, or None if not found."""
        obj = self._find_object(path)
        if obj is None:
            return None
        full_name = f"{obj.object_name}:{obj.type}" if obj.type else obj.object_name
        return FileEntry(
            name=obj.object_name,
            type=obj.type,
            full_name=full_name,
            user_name=obj.user_name,
            size=obj.bytes_in_file,
            pages=obj.pages_in_file,
            is_directory=False,
            last_modified=None,
        )

    def file_exists(self, path: str) -> bool:
        """Check if a file exists."""
        return self._find_object(path) is not None

    # -- Write operations --------------------------------------------------

    def write_file(
        self,
        path: str,
        file_data: Union[bytes, bytearray, memoryview],
        parity: str = "none",
    ) -> None:
        """Write (create or overwrite) a file.

        Args:
            path: "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
            file_data: The raw bytes to write.
            parity: Parity handling -- "none" (default, write raw bytes),
                "set" (apply ND-100 even parity before writing, for text files).
        """
        from ndfs.parity import set_parity as _set

        self._ensure_writable()

        if parity == "set":
            file_data = _set(bytes(file_data))

        user_name, object_name, file_type = self._parse_path(path)
        if not object_name:
            raise ValueError("Invalid path: filename required")

        # Find user
        user = self._resolve_user(user_name)
        if user is None:
            raise ValueError(f"User not found: {user_name or '(default)'}")

        # Calculate required pages (data pages + 1 index block)
        data_pages = math.ceil(len(file_data) / NDFS_PAGE_SIZE)
        if data_pages == 0:
            data_pages = 1
        total_required = data_pages + 1  # +1 for index block

        # Check for existing file
        existing = self._object_file.find_object(object_name, user.user_name)

        # Determine additional pages needed
        additional_needed = total_required
        if existing is not None:
            existing_total = existing.pages_in_file + 1
            additional_needed = total_required - existing_total if total_required > existing_total else 0

        # Check and expand quota if needed
        if additional_needed > 0:
            available_to_user = user.pages_reserved - user.pages_used
            if available_to_user < additional_needed:
                expansion = additional_needed - available_to_user
                free_on_disk = self._bit_file.get_free_pages()
                if free_on_disk < expansion:
                    raise IOError(
                        f"Insufficient disk space: need {expansion} pages, "
                        f"only {free_on_disk} available"
                    )
                user.pages_reserved += expansion

        if existing is not None:
            self._update_existing_file(existing, user, file_data)
        else:
            self._create_new_file(object_name, file_type, user, file_data)

        self._persist_all()

    def delete_file(self, path: str) -> None:
        """Delete a file."""
        self._ensure_writable()

        obj = self._find_object(path)
        if obj is None:
            raise FileNotFoundError(f"File not found: {path}")

        # Free blocks
        if obj.file_pointer is not None and obj.file_pointer.block_id > 0:
            self._free_file_blocks(obj)

        # Update user pages used
        user = self._user_file.get_user(obj.user_index)
        if user is not None:
            total_blocks = obj.pages_in_file
            if (
                obj.file_pointer is not None
                and (
                    obj.file_pointer.type == PointerType.Indexed
                    or obj.file_pointer.type == PointerType.SubIndexed
                )
            ):
                total_blocks += 1  # index block
            user.pages_used = user.pages_used - total_blocks if user.pages_used >= total_blocks else 0

        self._object_file.remove_object(obj.object_index)
        self._persist_all()

    def rename(self, old_path: str, new_path: str) -> None:
        """Rename a file."""
        self._ensure_writable()

        obj = self._find_object(old_path)
        if obj is None:
            raise FileNotFoundError(f"File not found: {old_path}")

        _, object_name, file_type = self._parse_path(new_path)
        if not object_name:
            raise ValueError("Invalid new path")

        obj.object_name = object_name.upper()[:NDFS_NAME_MAX]
        obj.type = file_type.upper()[:NDFS_TYPE_MAX]
        self._persist_all()

    # -- User management ---------------------------------------------------

    def get_users(self) -> List[UserEntry]:
        """Get all users."""
        return self._user_file.get_users()

    def get_user(self, index: int) -> Optional[UserEntry]:
        """Get a user by index."""
        return self._user_file.get_user(index)

    def add_user(self, name: str, reserved_pages: int) -> bool:
        """Add a new user."""
        self._ensure_writable()

        if self._user_file.find_user(name) is not None:
            return False  # already exists
        idx = self._user_file.get_next_available_index()
        if idx < 0:
            return False  # no slots

        user = UserEntry()
        user.set_name(name)
        user.user_index = idx
        user.pages_reserved = reserved_pages
        self._user_file.add_user(user)
        self._persist_all()
        return True

    def remove_user(self, index: int) -> bool:
        """Remove a user (only if they have no files)."""
        self._ensure_writable()

        files = self._object_file.get_user_objects(index)
        if len(files) > 0:
            return False  # user has files

        ok = self._user_file.remove_user(index)
        if ok:
            self._persist_all()
        return ok

    def update_user_quota(self, index: int, new_pages: int) -> bool:
        """Update a user's page quota."""
        self._ensure_writable()
        ok = self._user_file.update_user_quota(index, new_pages)
        if ok:
            self._persist_all()
        return ok

    def clear_user_password(self, index_or_name: Union[int, str]) -> bool:
        """Clear a user's password (set to 0)."""
        self._ensure_writable()

        user: Optional[UserEntry] = None
        if isinstance(index_or_name, int):
            user = self._user_file.get_user(index_or_name)
        else:
            user = self._user_file.find_user(index_or_name)
        if user is None:
            return False

        user.password = 0
        self._persist_all()
        return True

    # -- Bitmap queries ----------------------------------------------------

    def is_block_used(self, block_id: int) -> bool:
        return self._bit_file.is_block_used(block_id)

    def get_free_pages(self) -> int:
        return self._bit_file.get_free_pages()

    def get_used_pages(self) -> int:
        return self._bit_file.calc_used_pages()

    # -- Boot loader -------------------------------------------------------

    def detect_boot_format(self) -> BootFormat:
        """Detect the boot format of this disk image."""
        from ndfs.boot_loader import detect_boot_format as _detect
        page0 = self._read_page(0)
        return _detect(page0)

    def load_boot_code(self) -> Optional[BootCode]:
        """Load boot code from page 0."""
        from ndfs.boot_loader import load_boot_code as _load
        page0 = self._read_page(0)
        return _load(page0)

    def is_bootable(self) -> bool:
        """Check if this disk image has valid boot code."""
        from ndfs.boot_loader import is_bootable as _is_bootable
        page0 = self._read_page(0)
        return _is_bootable(page0)

    # -- Low-level access --------------------------------------------------

    def get_object_entries(self) -> List[ObjectEntry]:
        return self._object_file.get_objects()

    def get_object_entry(self, name: str, user_name: str) -> Optional[ObjectEntry]:
        return self._object_file.find_object(name, user_name)

    # -- Diagnostics -------------------------------------------------------

    def verify_integrity(self) -> bool:
        """Basic integrity verification."""
        if not self._master_block.is_valid():
            return False

        # Check all file blocks are marked in bitmap
        objects = self._object_file.get_objects()
        for i in range(len(objects)):
            obj = objects[i]
            if obj.file_pointer is None or not obj.file_pointer.is_valid():
                continue
            # Check index block
            if not self._bit_file.is_block_used(obj.file_pointer.block_id):
                return False
        return True

    def generate_report(self) -> str:
        """Generate a text report about the filesystem."""
        total_pages = len(self._data) // NDFS_PAGE_SIZE
        used_pages = self._bit_file.calc_used_pages()
        free_pages = self._bit_file.get_free_pages()
        users = self._user_file.get_users()
        objects = self._object_file.get_objects()

        lines: List[str] = []
        lines.append("NDFS Filesystem Report")
        lines.append("======================")
        lines.append(f"Volume: {self._master_block.directory_name}")
        lines.append(f"Total pages: {total_pages}")
        lines.append(f"Used pages: {used_pages}")
        lines.append(f"Free pages: {free_pages}")
        lines.append(f"Users: {len(users)}")
        lines.append(f"Files: {len(objects)}")
        lines.append("")

        lines.append("Users:")
        for i in range(len(users)):
            u = users[i]
            lines.append(
                f"  [{u.user_index}] {u.user_name} - "
                f"Reserved: {u.pages_reserved}, Used: {u.pages_used}"
            )

        lines.append("")
        lines.append("Files:")
        for i in range(len(objects)):
            o = objects[i]
            lines.append(
                f"  {o.user_name}/{o.object_name}:{o.type} - "
                f"{o.bytes_in_file} bytes ({o.pages_in_file} pages)"
            )

        return "\n".join(lines) + "\n"

    # -- XAT (Extended Attribute) support --------------------------------

    def get_file_properties(self, path: str) -> Optional[dict]:
        """Get XAT properties for a file, or None if not found.

        Args:
            path: "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
        """
        obj = self._find_object(path)
        if obj is None:
            return None
        return object_entry_to_xat(obj)

    def read_file_with_properties(self, path: str) -> Tuple[bytes, dict]:
        """Read a file's data along with its XAT properties.

        Args:
            path: "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"

        Returns:
            Tuple of (data_bytes, xat_properties_dict).
        """
        obj = self._find_object(path)
        if obj is None:
            raise FileNotFoundError(f"File not found: {path}")
        data = bytes(self._read_object_data(obj))
        properties = object_entry_to_xat(obj)
        return data, properties

    def write_file_with_properties(
        self,
        path: str,
        data: Union[bytes, bytearray, memoryview],
        properties: dict,
    ) -> None:
        """Write a file and apply XAT properties to restore metadata.

        The file is written first, then the XAT properties are applied
        to the resulting object entry.

        Args:
            path: "USERNAME/FILENAME:TYPE" or "FILENAME:TYPE"
            data: The raw bytes to write.
            properties: XAT properties dict to apply after writing.
        """
        self.write_file(path, data)

        # Now find the written entry and apply status-related XAT fields
        obj = self._find_object(path)
        if obj is not None:
            if "ndfs.access_bits" in properties and isinstance(properties["ndfs.access_bits"], int):
                obj.access_bits = properties["ndfs.access_bits"]
            if "ndfs.file_type" in properties and isinstance(properties["ndfs.file_type"], int):
                obj.file_type = properties["ndfs.file_type"]
            if "ndfs.date_created" in properties and isinstance(properties["ndfs.date_created"], int):
                obj.date_created = properties["ndfs.date_created"]
            if "ndfs.last_read_date" in properties and isinstance(properties["ndfs.last_read_date"], int):
                obj.last_date_read = properties["ndfs.last_read_date"]
            if "ndfs.last_write_date" in properties and isinstance(properties["ndfs.last_write_date"], int):
                obj.last_date_written = properties["ndfs.last_write_date"]
            self._persist_all()

    # ==================================================================
    #  Private implementation
    # ==================================================================

    def _read_page(self, block_id: int) -> memoryview:
        """Read a page from the image buffer."""
        offset = block_id * NDFS_PAGE_SIZE
        if offset + NDFS_PAGE_SIZE > len(self._data):
            raise IndexError(f"Block {block_id} out of range")
        return memoryview(self._data)[offset:offset + NDFS_PAGE_SIZE]

    def _write_page(self, block_id: int, page_data: Union[bytes, bytearray, memoryview]) -> None:
        """Write a page to the image buffer."""
        offset = block_id * NDFS_PAGE_SIZE
        if offset + NDFS_PAGE_SIZE > len(self._data):
            raise IndexError(f"Block {block_id} out of range")
        self._data[offset:offset + NDFS_PAGE_SIZE] = page_data[:NDFS_PAGE_SIZE]

    def _load_structures(self) -> None:
        """Load user file, object file, and bit file from the image."""
        mb = self._master_block

        # Load user file
        if mb.user_file_pointer is not None and mb.user_file_pointer.is_valid():
            index_page = self._read_page(mb.user_file_pointer.block_id)
            self._user_file.load_from_pages(index_page, lambda bid: self._read_page(bid))

            # Link user names to object entries
            users = self._user_file.get_users()
            user_map: Dict[int, str] = {}
            for i in range(len(users)):
                user_map[users[i].user_index] = users[i].user_name

            # Load object file
            if mb.object_file_pointer is not None and mb.object_file_pointer.is_valid():
                self._object_file.load_from_pages(
                    mb.object_file_pointer,
                    lambda bid: self._read_page(bid),
                )

                # Resolve user names on objects
                objects = self._object_file.get_objects()
                for i in range(len(objects)):
                    name = user_map.get(objects[i].user_index)
                    if name is not None:
                        objects[i].user_name = name

        # Load bit file
        if mb.bit_file_pointer is not None and mb.bit_file_pointer.is_valid():
            total_pages = len(self._data) // NDFS_PAGE_SIZE
            self._bit_file.initialize(total_pages)

            # Determine bitmap size in pages
            bitmap_bytes = math.ceil(total_pages / 8)
            bitmap_pages = math.ceil(bitmap_bytes / NDFS_PAGE_SIZE)

            # Read contiguous bitmap pages
            bitmap_data = bytearray(bitmap_pages * NDFS_PAGE_SIZE)
            for i in range(bitmap_pages):
                page = self._read_page(mb.bit_file_pointer.block_id + i)
                bitmap_data[i * NDFS_PAGE_SIZE:(i + 1) * NDFS_PAGE_SIZE] = page
            self._bit_file.load_bitmap(bitmap_data[:bitmap_bytes])

    def _read_object_data(self, obj: ObjectEntry) -> bytearray:
        """Read all data bytes for an object entry."""
        if obj.file_pointer is None or obj.file_pointer.block_id == 0:
            return bytearray(0)

        result = bytearray(obj.bytes_in_file)
        bytes_read = 0

        if obj.file_pointer.type == PointerType.Contiguous:
            # Sequential pages starting at block_id
            for i in range(obj.pages_in_file):
                if bytes_read >= obj.bytes_in_file:
                    break
                page = self._read_page(obj.file_pointer.block_id + i)
                to_copy = min(NDFS_PAGE_SIZE, obj.bytes_in_file - bytes_read)
                result[bytes_read:bytes_read + to_copy] = page[:to_copy]
                bytes_read += to_copy
        elif obj.file_pointer.type == PointerType.Indexed:
            bytes_read = self._read_indexed_data(obj.file_pointer.block_id, obj, result)
        elif obj.file_pointer.type == PointerType.SubIndexed:
            bytes_read = self._read_sub_indexed_data(obj.file_pointer.block_id, obj, result)

        return result

    def _read_indexed_data(
        self, index_block_id: int, obj: ObjectEntry, result: bytearray
    ) -> int:
        """Read data from an indexed file structure."""
        index_page = self._read_page(index_block_id)
        bytes_read = 0

        for i in range(MAX_OBJECT_FILE_POINTERS):
            if bytes_read >= obj.bytes_in_file:
                break
            ptr = BlockPointer.from_bytes(index_page, i * 4)

            if ptr.block_id == 0:
                # Sparse hole: result is already zero-initialized
                to_copy = min(NDFS_PAGE_SIZE, obj.bytes_in_file - bytes_read)
                bytes_read += to_copy
            else:
                page = self._read_page(ptr.block_id)
                to_copy = min(NDFS_PAGE_SIZE, obj.bytes_in_file - bytes_read)
                result[bytes_read:bytes_read + to_copy] = page[:to_copy]
                bytes_read += to_copy
        return bytes_read

    def _read_sub_indexed_data(
        self, sub_index_block_id: int, obj: ObjectEntry, result: bytearray
    ) -> int:
        """Read data from a sub-indexed file structure."""
        sub_index_page = self._read_page(sub_index_block_id)
        bytes_read = 0

        for si in range(MAX_OBJECT_FILE_POINTERS):
            if bytes_read >= obj.bytes_in_file:
                break
            index_ptr = BlockPointer.from_bytes(sub_index_page, si * 4)
            if not index_ptr.is_valid():
                continue

            index_page = self._read_page(index_ptr.block_id)
            for i in range(MAX_OBJECT_FILE_POINTERS):
                if bytes_read >= obj.bytes_in_file:
                    break
                data_ptr = BlockPointer.from_bytes(index_page, i * 4)
                if data_ptr.block_id == 0:
                    to_copy = min(NDFS_PAGE_SIZE, obj.bytes_in_file - bytes_read)
                    bytes_read += to_copy
                else:
                    page = self._read_page(data_ptr.block_id)
                    to_copy = min(NDFS_PAGE_SIZE, obj.bytes_in_file - bytes_read)
                    result[bytes_read:bytes_read + to_copy] = page[:to_copy]
                    bytes_read += to_copy
        return bytes_read

    def _ensure_object_dir_page(self, object_index: int) -> None:
        """Ensure the object-file directory page holding *object_index* exists,
        allocating and linking it on demand (each user's region grows as
        needed). Page index in the object-file index block is object_index/32;
        for user U that maps to index-pointer slots U*8..U*8+7.
        """
        mb = self._master_block
        if mb.object_file_pointer is None or not mb.object_file_pointer.is_valid():
            return
        page_idx = object_index // ENTRIES_PER_PAGE

        def alloc_page() -> int:
            blk = self._bit_file.find_first_free_block()
            if blk < 0:
                raise IOError("No free blocks for object directory page")
            self._bit_file.mark_block_used(blk)
            self._write_page(blk, bytearray(NDFS_PAGE_SIZE))
            return blk

        if mb.object_file_pointer.type == PointerType.Indexed:
            index_page = bytearray(self._read_page(mb.object_file_pointer.block_id))
            ptr = BlockPointer.from_bytes(index_page, page_idx * 4)
            if ptr.is_valid():
                return
            blk = alloc_page()
            BlockPointer(blk, PointerType.Contiguous).to_bytes(index_page, page_idx * 4)
            self._write_page(mb.object_file_pointer.block_id, index_page)
        elif mb.object_file_pointer.type == PointerType.SubIndexed:
            sub_idx = page_idx // MAX_OBJECT_FILE_POINTERS
            inner_idx = page_idx % MAX_OBJECT_FILE_POINTERS
            sub_page = bytearray(self._read_page(mb.object_file_pointer.block_id))
            sub_ptr = BlockPointer.from_bytes(sub_page, sub_idx * 4)
            if not sub_ptr.is_valid():
                ib = alloc_page()
                BlockPointer(ib, PointerType.Contiguous).to_bytes(sub_page, sub_idx * 4)
                self._write_page(mb.object_file_pointer.block_id, sub_page)
                sub_ptr = BlockPointer(ib, PointerType.Contiguous)
            inner_page = bytearray(self._read_page(sub_ptr.block_id))
            ptr = BlockPointer.from_bytes(inner_page, inner_idx * 4)
            if ptr.is_valid():
                return
            blk = alloc_page()
            BlockPointer(blk, PointerType.Contiguous).to_bytes(inner_page, inner_idx * 4)
            self._write_page(sub_ptr.block_id, inner_page)

    def _create_new_file(
        self,
        object_name: str,
        file_type: str,
        user: UserEntry,
        file_data: Union[bytes, bytearray, memoryview],
    ) -> None:
        """Create a new file on the filesystem."""
        data_pages = math.ceil(len(file_data) / NDFS_PAGE_SIZE)
        if data_pages == 0:
            data_pages = 1

        # Choose the object slot inside the OWNING USER's region (SINTRAN
        # partitions the object file: user U owns slots U*256..U*256+255 and
        # the object-index high byte is the owner) and ensure its directory
        # page exists, before allocating file data.
        obj_index = self._object_file.find_free_user_slot(user.user_index)
        if obj_index < 0:
            raise IOError("User object table is full")
        self._ensure_object_dir_page(obj_index)

        # Allocate index block
        index_block_id = self._bit_file.find_first_free_block()
        if index_block_id < 0:
            raise IOError("No free blocks for index block")
        self._bit_file.mark_block_used(index_block_id)

        # Allocate data blocks and write data (sparse-aware)
        index_page = bytearray(NDFS_PAGE_SIZE)

        for i in range(data_pages):
            page_offset = i * NDFS_PAGE_SIZE
            page_end = min(page_offset + NDFS_PAGE_SIZE, len(file_data))
            page_slice = file_data[page_offset:page_end]

            # Check if page is all zeros (sparse)
            all_zeros = True
            for b in range(len(page_slice)):
                if page_slice[b] != 0:
                    all_zeros = False
                    break

            if all_zeros and len(page_slice) == NDFS_PAGE_SIZE:
                # Sparse hole: write block_id=0 to index
                write_uint32_be(index_page, i * 4, 0)
            else:
                # Allocate data block
                data_block_id = self._bit_file.find_first_free_block()
                if data_block_id < 0:
                    raise IOError("No free blocks for data")
                self._bit_file.mark_block_used(data_block_id)

                # Write data to page
                data_page = bytearray(NDFS_PAGE_SIZE)
                data_page[:len(page_slice)] = page_slice
                self._write_page(data_block_id, data_page)

                # Write pointer to index block
                data_ptr = BlockPointer(data_block_id, PointerType.Contiguous)
                data_ptr.to_bytes(index_page, i * 4)

        # Write index block to disk
        self._write_page(index_block_id, index_page)

        # Create object entry
        entry = ObjectEntry()
        entry.object_index = obj_index
        entry.object_name = object_name.upper()[:NDFS_NAME_MAX]
        entry.type = file_type.upper()[:NDFS_TYPE_MAX]
        entry.user_index = user.user_index
        entry.user_name = user.user_name
        entry.pages_in_file = data_pages
        entry.bytes_in_file = len(file_data) if len(file_data) > 0 else 1
        entry.file_pointer = BlockPointer(index_block_id, PointerType.Indexed)
        # New-file defaults: owner+friend full rights; indexed allocation flag.
        entry.access_bits = ACCESS_DEFAULT
        entry.file_type_flags = FT_INDEXED
        # object_index already encodes [user|fileEntry]; the object-index word
        # and the self-referential version chain all equal it.
        entry.disk_object_index = obj_index
        entry.next_version = obj_index
        entry.prev_version = obj_index
        self._object_file.add_object(entry)

        # Update user pages used
        user.pages_used += data_pages + 1  # data pages + index block

    def _update_existing_file(
        self,
        existing: ObjectEntry,
        user: UserEntry,
        file_data: Union[bytes, bytearray, memoryview],
    ) -> None:
        """Update an existing file with new data."""
        # Free old blocks
        old_total = existing.pages_in_file + 1  # data + index
        self._free_file_blocks(existing)

        # Subtract old usage
        user.pages_used = user.pages_used - old_total if user.pages_used >= old_total else 0

        # Create new allocation
        data_pages = math.ceil(len(file_data) / NDFS_PAGE_SIZE)
        if data_pages == 0:
            data_pages = 1

        index_block_id = self._bit_file.find_first_free_block()
        if index_block_id < 0:
            raise IOError("No free blocks for index block")
        self._bit_file.mark_block_used(index_block_id)

        index_page = bytearray(NDFS_PAGE_SIZE)

        for i in range(data_pages):
            page_offset = i * NDFS_PAGE_SIZE
            page_end = min(page_offset + NDFS_PAGE_SIZE, len(file_data))
            page_slice = file_data[page_offset:page_end]

            all_zeros = True
            for b in range(len(page_slice)):
                if page_slice[b] != 0:
                    all_zeros = False
                    break

            if all_zeros and len(page_slice) == NDFS_PAGE_SIZE:
                write_uint32_be(index_page, i * 4, 0)
            else:
                data_block_id = self._bit_file.find_first_free_block()
                if data_block_id < 0:
                    raise IOError("No free blocks for data")
                self._bit_file.mark_block_used(data_block_id)

                data_page = bytearray(NDFS_PAGE_SIZE)
                data_page[:len(page_slice)] = page_slice
                self._write_page(data_block_id, data_page)

                data_ptr = BlockPointer(data_block_id, PointerType.Contiguous)
                data_ptr.to_bytes(index_page, i * 4)

        self._write_page(index_block_id, index_page)

        # Update existing entry
        existing.pages_in_file = data_pages
        existing.bytes_in_file = len(file_data) if len(file_data) > 0 else 1
        existing.file_pointer = BlockPointer(index_block_id, PointerType.Indexed)

        # Update user pages used
        user.pages_used += data_pages + 1

    def _free_file_blocks(self, obj: ObjectEntry) -> None:
        """Free all blocks associated with a file."""
        if obj.file_pointer is None or obj.file_pointer.block_id == 0:
            return

        if obj.file_pointer.type == PointerType.Indexed:
            index_page = self._read_page(obj.file_pointer.block_id)
            for i in range(MAX_OBJECT_FILE_POINTERS):
                ptr = BlockPointer.from_bytes(index_page, i * 4)
                if ptr.block_id > 0:
                    self._bit_file.mark_block_free(ptr.block_id)
            # Free the index block itself
            self._bit_file.mark_block_free(obj.file_pointer.block_id)
        elif obj.file_pointer.type == PointerType.Contiguous:
            self._bit_file.free_blocks(obj.file_pointer.block_id, obj.pages_in_file)
        elif obj.file_pointer.type == PointerType.SubIndexed:
            sub_index_page = self._read_page(obj.file_pointer.block_id)
            for si in range(MAX_OBJECT_FILE_POINTERS):
                index_ptr = BlockPointer.from_bytes(sub_index_page, si * 4)
                if not index_ptr.is_valid():
                    continue
                index_page = self._read_page(index_ptr.block_id)
                for i in range(MAX_OBJECT_FILE_POINTERS):
                    data_ptr = BlockPointer.from_bytes(index_page, i * 4)
                    if data_ptr.block_id > 0:
                        self._bit_file.mark_block_free(data_ptr.block_id)
                self._bit_file.mark_block_free(index_ptr.block_id)
            self._bit_file.mark_block_free(obj.file_pointer.block_id)

    def _persist_all(self) -> None:
        """Persist all three structures to the image buffer.

        Order: BitFile -> UserFile -> ObjectFile (matching C# reference).
        """
        mb = self._master_block

        # BitFile: write contiguous pages
        if mb.bit_file_pointer is not None and mb.bit_file_pointer.is_valid():
            pages = self._bit_file.to_page_buffers()
            for i in range(len(pages)):
                self._write_page(mb.bit_file_pointer.block_id + i, pages[i])

        # UserFile: write index + data pages
        if mb.user_file_pointer is not None and mb.user_file_pointer.is_valid():
            _, data_pages = self._user_file.to_page_buffers()

            # Read existing index page to get data block pointers
            existing_index = self._read_page(mb.user_file_pointer.block_id)

            # Write data pages to the locations pointed to by existing index pointers
            for i in range(MAX_USER_FILE_POINTERS):
                ptr = BlockPointer.from_bytes(existing_index, i * 4)
                if ptr.is_valid() and i < len(data_pages):
                    self._write_page(ptr.block_id, data_pages[i])

        # ObjectFile: write data pages
        if mb.object_file_pointer is not None and mb.object_file_pointer.is_valid():
            data_page_map = self._object_file.to_data_pages()

            empty = bytearray(NDFS_PAGE_SIZE)
            if mb.object_file_pointer.type == PointerType.Indexed:
                index_page = self._read_page(mb.object_file_pointer.block_id)
                # Iterate ALL linked pages, not just those still holding entries:
                # a page emptied by deletion must be zeroed, else the stale
                # entry's in-use bit survives and the file reappears on reload.
                for page_index in range(MAX_OBJECT_FILE_POINTERS):
                    ptr = BlockPointer.from_bytes(index_page, page_index * 4)
                    if not ptr.is_valid():
                        continue
                    page_data = data_page_map.get(page_index)
                    self._write_page(ptr.block_id, page_data if page_data is not None else empty)
            elif mb.object_file_pointer.type == PointerType.SubIndexed:
                sub_index_page = self._read_page(mb.object_file_pointer.block_id)
                for si in range(MAX_OBJECT_FILE_POINTERS):
                    sub_ptr = BlockPointer.from_bytes(sub_index_page, si * 4)
                    if not sub_ptr.is_valid():
                        continue
                    inner_index_page = self._read_page(sub_ptr.block_id)
                    for ii in range(MAX_OBJECT_FILE_POINTERS):
                        data_ptr = BlockPointer.from_bytes(inner_index_page, ii * 4)
                        if not data_ptr.is_valid():
                            continue
                        page_index = si * MAX_OBJECT_FILE_POINTERS + ii
                        page_data = data_page_map.get(page_index)
                        self._write_page(data_ptr.block_id, page_data if page_data is not None else empty)

    def _find_object(self, path: str) -> Optional[ObjectEntry]:
        """Find an object entry by path."""
        user_name, object_name, file_type = self._parse_path(path)
        if file_type:
            search_name = f"{object_name}:{file_type}"
        else:
            search_name = object_name

        objects = self._object_file.get_objects()
        for i in range(len(objects)):
            o = objects[i]
            if user_name and o.user_name.upper() != user_name.upper():
                continue

            full_name = f"{o.object_name}:{o.type}" if o.type else o.object_name
            if full_name.upper() == search_name.upper():
                return o
            # Also try matching object name only (without type)
            if not file_type and o.object_name.upper() == object_name.upper():
                return o
        return None

    def _parse_path(self, path: str) -> Tuple[str, str, str]:
        """Parse a path into (user_name, object_name, file_type)."""
        normalized = path.strip("/")
        parts = normalized.split("/")

        user_name = ""
        file_name_part = ""

        if len(parts) >= 2:
            user_name = parts[0]
            file_name_part = "/".join(parts[1:])
        else:
            file_name_part = parts[0]

        # Split filename into name and type (separator is : or last .)
        object_name = file_name_part
        file_type = ""

        colon_idx = file_name_part.find(":")
        if colon_idx >= 0:
            object_name = file_name_part[:colon_idx]
            file_type = file_name_part[colon_idx + 1:]
        else:
            dot_idx = file_name_part.rfind(".")
            if dot_idx >= 0:
                object_name = file_name_part[:dot_idx]
                file_type = file_name_part[dot_idx + 1:]

        return user_name, object_name, file_type

    def _resolve_user(self, user_name: str) -> Optional[UserEntry]:
        """Resolve a user by name, or return the first user if no name given."""
        if user_name:
            return self._user_file.find_user(user_name)
        # Default to first user
        users = self._user_file.get_users()
        return users[0] if len(users) > 0 else None

    def _ensure_writable(self) -> None:
        """Raise if the filesystem is read-only."""
        if self._read_only:
            raise IOError("Filesystem is read-only")
