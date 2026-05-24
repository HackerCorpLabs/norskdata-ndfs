"""
NDFS image creator: creates new empty NDFS disk images from templates.

Supports pre-defined templates (Floppy360KB, Floppy12MB, Smd75MB, Winchester74MB)
and custom configurations.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Optional

from ndfs.constants import (
    NDFS_PAGE_SIZE,
    NDFS_NAME_TERMINATOR,
    NDFS_NAME_MAX,
    MASTER_BLOCK_OFFSET,
    EXTENDED_INFO_OFFSET,
    USER_ENTRY_FLAG,
    FIRST_ALLOCATABLE_BLOCK,
)
from ndfs.endian import write_uint16_be, write_uint32_be
from ndfs.ndfs_name import write_ndfs_name
from ndfs.block_pointer import BlockPointer
from ndfs.types import PointerType, ImageTemplate, ImageCreationOptions


@dataclass
class TemplateSpec:
    """Internal specification for an NDFS disk image template."""
    ndfs_pages: int = 0
    file_blocks: int = 0
    object_file_block: int = 0
    user_file_block: int = 0
    bit_file_block: int = 0
    unreserved_pages: int = 0
    is_floppy: bool = False
    ext_valid: bool = False
    spare_blocks: int = 0


# Pre-defined template specifications
_TEMPLATES = {
    ImageTemplate.Floppy360KB: TemplateSpec(
        ndfs_pages=154,
        file_blocks=154,
        object_file_block=149,
        user_file_block=151,
        bit_file_block=153,
        unreserved_pages=1,
        is_floppy=True,
        ext_valid=False,
        spare_blocks=0,
    ),
    ImageTemplate.Floppy12MB: TemplateSpec(
        ndfs_pages=616,
        file_blocks=616,
        object_file_block=611,
        user_file_block=613,
        bit_file_block=615,
        unreserved_pages=1,
        is_floppy=True,
        ext_valid=False,
        spare_blocks=0,
    ),
    ImageTemplate.Smd75MB: TemplateSpec(
        ndfs_pages=36945,
        file_blocks=38400,
        object_file_block=18684,
        user_file_block=18686,
        bit_file_block=18472,
        unreserved_pages=36945,
        is_floppy=False,
        ext_valid=True,
        spare_blocks=1455,
    ),
    ImageTemplate.Winchester74MB: TemplateSpec(
        ndfs_pages=36396,
        file_blocks=36360,
        object_file_block=32771,
        user_file_block=32769,
        bit_file_block=18198,
        unreserved_pages=36396,
        is_floppy=False,
        ext_valid=True,
        spare_blocks=0,
    ),
}


def _get_template_spec(template: ImageTemplate) -> TemplateSpec:
    """Get a pre-defined template specification."""
    spec = _TEMPLATES.get(template)
    if spec is None:
        raise ValueError(f"No pre-defined spec for template: {template}")
    return spec


def _create_custom_spec(options: ImageCreationOptions) -> TemplateSpec:
    """Create a custom template specification from options."""
    if options.custom_pages is None or options.custom_pages < 20:
        raise ValueError("custom_pages must be specified (>= 20) for Custom template")

    ndfs_pages = options.custom_pages

    # For custom, assume no spare blocks (like Winchester)
    file_blocks = ndfs_pages

    # Place pointers near end (floppy style for small, hard disk style for large)
    is_floppy = ndfs_pages < 1000

    if is_floppy:
        object_file_block = ndfs_pages - 5
        user_file_block = ndfs_pages - 3
        bit_file_block = ndfs_pages - 1
    else:
        bit_file_block = ndfs_pages // 2
        object_file_block = int(ndfs_pages * 0.85)
        user_file_block = object_file_block + 2

    return TemplateSpec(
        ndfs_pages=ndfs_pages,
        file_blocks=file_blocks,
        object_file_block=object_file_block,
        user_file_block=user_file_block,
        bit_file_block=bit_file_block,
        unreserved_pages=ndfs_pages,
        is_floppy=is_floppy,
        ext_valid=not is_floppy and (options.include_extended_info or False),
        spare_blocks=0,
    )


def _mark_block_allocated(bitmap: bytearray, block_number: int) -> None:
    """Mark a block as allocated in the bitmap (LSB bit order within each byte)."""
    byte_index = block_number >> 3
    bit_index = block_number & 7
    if byte_index < len(bitmap):
        bitmap[byte_index] |= 1 << bit_index


def _write_extended_info(
    disk_image: bytearray,
    ndfs_pages: int,
    system_number: int,
    flag_word: int,
) -> None:
    """Write Extended Info block (bytes 2000-2015)."""
    ext = EXTENDED_INFO_OFFSET

    pages_lo = ndfs_pages & 0xFFFF
    pages_hi = (ndfs_pages >> 16) & 0xFFFF
    checksum = ((pages_lo ^ pages_hi ^ flag_word ^ 0 ^ 0 ^ 0) + system_number) & 0xFFFF

    write_uint16_be(disk_image, ext, checksum)
    write_uint16_be(disk_image, ext + 2, 0)
    write_uint16_be(disk_image, ext + 4, 0)
    write_uint16_be(disk_image, ext + 6, 0)
    write_uint16_be(disk_image, ext + 8, flag_word)
    write_uint16_be(disk_image, ext + 10, system_number)
    write_uint32_be(disk_image, ext + 12, ndfs_pages)


def _write_master_block(
    disk_image: bytearray,
    directory_name: str,
    spec: TemplateSpec,
) -> None:
    """Write Master Block (bytes 2016-2047)."""
    off = MASTER_BLOCK_OFFSET

    # Directory name
    write_ndfs_name(disk_image, off, directory_name, NDFS_NAME_MAX)

    # Object file pointer (Indexed)
    obj_ptr = BlockPointer(spec.object_file_block, PointerType.Indexed)
    obj_ptr.to_bytes(disk_image, off + 0x10)

    # User file pointer (Indexed)
    usr_ptr = BlockPointer(spec.user_file_block, PointerType.Indexed)
    usr_ptr.to_bytes(disk_image, off + 0x14)

    # Bit file pointer (Contiguous)
    bit_ptr = BlockPointer(spec.bit_file_block, PointerType.Contiguous)
    bit_ptr.to_bytes(disk_image, off + 0x18)

    # Unreserved pages
    write_uint32_be(disk_image, off + 0x1C, spec.unreserved_pages)


def _create_bit_file(disk_image: bytearray, spec: TemplateSpec) -> None:
    """Create Bit File (allocation bitmap) and mark reserved blocks."""
    bitmap_bytes = math.ceil(spec.ndfs_pages / 8)
    bitmap = bytearray(bitmap_bytes)

    # Mark system blocks as allocated
    _mark_block_allocated(bitmap, 0)  # Master block
    _mark_block_allocated(bitmap, spec.object_file_block)
    _mark_block_allocated(bitmap, spec.user_file_block)
    _mark_block_allocated(bitmap, spec.bit_file_block)

    # Also mark the object file data page and user file data page
    # Object file index block points to data pages; we allocate one data page
    obj_data_block = spec.object_file_block + 1
    if obj_data_block < spec.ndfs_pages:
        _mark_block_allocated(bitmap, obj_data_block)

    # User file data page
    usr_data_block = spec.user_file_block + 1
    if usr_data_block < spec.ndfs_pages:
        _mark_block_allocated(bitmap, usr_data_block)

    # Write bitmap to disk at bit_file_block
    bit_offset = spec.bit_file_block * NDFS_PAGE_SIZE
    disk_image[bit_offset:bit_offset + bitmap_bytes] = bitmap


def _create_object_file(disk_image: bytearray, spec: TemplateSpec) -> None:
    """Create Object File Index (initially empty with one data page pointer)."""
    obj_idx_off = spec.object_file_block * NDFS_PAGE_SIZE

    # Write pointer to first data page
    obj_data_block = spec.object_file_block + 1
    data_ptr = BlockPointer(obj_data_block, PointerType.Contiguous)
    data_ptr.to_bytes(disk_image, obj_idx_off)

    # Data page is already zero (empty)


def _create_user_file(
    disk_image: bytearray,
    spec: TemplateSpec,
    options: ImageCreationOptions,
) -> None:
    """Create User File with SYSTEM user and optional custom users."""
    usr_idx_off = spec.user_file_block * NDFS_PAGE_SIZE

    # Write pointer to first data page
    usr_data_block = spec.user_file_block + 1
    data_ptr = BlockPointer(usr_data_block, PointerType.Contiguous)
    data_ptr.to_bytes(disk_image, usr_idx_off)

    # Write SYSTEM user at slot 0 of the data page
    usr_data_off = usr_data_block * NDFS_PAGE_SIZE
    _write_user_entry(disk_image, usr_data_off, 0, "SYSTEM", spec.unreserved_pages)

    # Write additional users
    if options.users is not None:
        for i in range(len(options.users)):
            user_info = options.users[i]
            slot_index = i + 1  # SYSTEM is at 0
            entry_offset = usr_data_off + slot_index * 64
            _write_user_entry(
                disk_image,
                entry_offset,
                slot_index,
                user_info.name,
                user_info.reserved_pages,
            )


def _write_user_entry(
    disk_image: bytearray,
    offset: int,
    user_index: int,
    name: str,
    reserved_pages: int,
) -> None:
    """Write a single user entry at the given offset."""
    disk_image[offset] = USER_ENTRY_FLAG  # 0x81 valid user
    # Name at offset+2
    write_ndfs_name(disk_image, offset + 2, name, NDFS_NAME_MAX)
    # Pages reserved at offset+28
    write_uint32_be(disk_image, offset + 28, reserved_pages)
    # Pages used = 0 at offset+32 (already zero)
    # User index at offset+37
    disk_image[offset + 37] = user_index & 0xFF
    # Default file access 0x04FF at offset+38
    write_uint16_be(disk_image, offset + 38, 0x04FF)


def create_image(options: ImageCreationOptions) -> bytearray:
    """Create a new NDFS disk image in memory.

    Args:
        options: Image creation options specifying template and parameters.

    Returns:
        A bytearray containing the complete disk image.
    """
    # Get template spec
    if options.template == ImageTemplate.Custom:
        spec = _create_custom_spec(options)
    else:
        spec = _get_template_spec(options.template)

    # Calculate file size
    file_size = spec.file_blocks * NDFS_PAGE_SIZE

    # Create zero-filled disk image
    disk_image = bytearray(file_size)

    # Write extended info (hard disks only)
    if spec.ext_valid and (options.include_extended_info is None or options.include_extended_info):
        system_number = options.system_number if options.system_number is not None else 0
        flag_word = options.flag_word if options.flag_word is not None else 0
        _write_extended_info(disk_image, spec.ndfs_pages, system_number, flag_word)

    # Write master block
    dir_name = options.directory_name if options.directory_name else "EMPTY-DISK"
    _write_master_block(disk_image, dir_name, spec)

    # Create bit file
    _create_bit_file(disk_image, spec)

    # Create object file
    _create_object_file(disk_image, spec)

    # Create user file
    _create_user_file(disk_image, spec, options)

    return disk_image
