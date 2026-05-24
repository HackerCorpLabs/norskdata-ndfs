"""
XAT (Extended Attribute) sidecar file support for NDFS.

Preserves NDFS metadata (access bits, dates, file type flags, user info, etc.)
when files are copied to/from host filesystems.

When extracting a file from NDFS, a companion `.xat` JSON file is created
alongside the data file containing all the NDFS metadata that would
otherwise be lost. When copying back in, the `.xat` file is read to
restore the metadata.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
from __future__ import annotations

import json
from typing import Dict, Optional, Union

from ndfs.object_entry import ObjectEntry

# Property key constants
XAT_OBJECT_NAME = "ndfs.object_name"
XAT_TYPE = "ndfs.type"
XAT_USER_NAME = "ndfs.user_name"
XAT_USER_INDEX = "ndfs.user_index"
XAT_ACCESS_BITS = "ndfs.access_bits"
XAT_FILE_TYPE_FLAGS = "ndfs.file_type_flags"
XAT_FILE_TYPE = "ndfs.file_type"
XAT_PAGES_IN_FILE = "ndfs.pages_in_file"
XAT_BYTES_IN_FILE = "ndfs.bytes_in_file"
XAT_DATE_CREATED = "ndfs.date_created"
XAT_LAST_READ_DATE = "ndfs.last_read_date"
XAT_LAST_WRITE_DATE = "ndfs.last_write_date"

ALL_XAT_KEYS = [
    XAT_OBJECT_NAME,
    XAT_TYPE,
    XAT_USER_NAME,
    XAT_USER_INDEX,
    XAT_ACCESS_BITS,
    XAT_FILE_TYPE_FLAGS,
    XAT_FILE_TYPE,
    XAT_PAGES_IN_FILE,
    XAT_BYTES_IN_FILE,
    XAT_DATE_CREATED,
    XAT_LAST_READ_DATE,
    XAT_LAST_WRITE_DATE,
]

XAT_EXTENSION = ".xat"


def object_entry_to_xat(entry: ObjectEntry) -> dict:
    """Serialize an ObjectEntry to XAT properties dict.

    Captures all NDFS metadata fields.
    """
    props: Dict[str, Union[str, int, bool]] = {}
    props[XAT_OBJECT_NAME] = entry.object_name
    props[XAT_TYPE] = entry.type
    props[XAT_USER_NAME] = entry.user_name
    props[XAT_USER_INDEX] = entry.user_index
    props[XAT_ACCESS_BITS] = entry.access_bits
    props[XAT_FILE_TYPE_FLAGS] = 0
    props[XAT_FILE_TYPE] = entry.file_type
    props[XAT_PAGES_IN_FILE] = entry.pages_in_file
    props[XAT_BYTES_IN_FILE] = entry.bytes_in_file
    props[XAT_DATE_CREATED] = entry.date_created
    props[XAT_LAST_READ_DATE] = entry.last_date_read
    props[XAT_LAST_WRITE_DATE] = entry.last_date_written
    return props


def xat_to_object_entry(xat: dict, entry: ObjectEntry) -> None:
    """Apply XAT properties to an ObjectEntry (for copy-in / restore).

    Only updates fields that are present in the XAT properties.
    """
    if XAT_OBJECT_NAME in xat and isinstance(xat[XAT_OBJECT_NAME], str):
        entry.object_name = xat[XAT_OBJECT_NAME]
    if XAT_TYPE in xat and isinstance(xat[XAT_TYPE], str):
        entry.type = xat[XAT_TYPE]
    if XAT_USER_NAME in xat and isinstance(xat[XAT_USER_NAME], str):
        entry.user_name = xat[XAT_USER_NAME]
    if XAT_USER_INDEX in xat and isinstance(xat[XAT_USER_INDEX], int):
        entry.user_index = xat[XAT_USER_INDEX]
    if XAT_ACCESS_BITS in xat and isinstance(xat[XAT_ACCESS_BITS], int):
        entry.access_bits = xat[XAT_ACCESS_BITS]
    if XAT_FILE_TYPE in xat and isinstance(xat[XAT_FILE_TYPE], int):
        entry.file_type = xat[XAT_FILE_TYPE]
    if XAT_PAGES_IN_FILE in xat and isinstance(xat[XAT_PAGES_IN_FILE], int):
        entry.pages_in_file = xat[XAT_PAGES_IN_FILE]
    if XAT_BYTES_IN_FILE in xat and isinstance(xat[XAT_BYTES_IN_FILE], int):
        entry.bytes_in_file = xat[XAT_BYTES_IN_FILE]
    if XAT_DATE_CREATED in xat and isinstance(xat[XAT_DATE_CREATED], int):
        entry.date_created = xat[XAT_DATE_CREATED]
    if XAT_LAST_READ_DATE in xat and isinstance(xat[XAT_LAST_READ_DATE], int):
        entry.last_date_read = xat[XAT_LAST_READ_DATE]
    if XAT_LAST_WRITE_DATE in xat and isinstance(xat[XAT_LAST_WRITE_DATE], int):
        entry.last_date_written = xat[XAT_LAST_WRITE_DATE]


def serialize_xat(props: dict) -> str:
    """Serialize XAT properties to a JSON string (pretty-printed)."""
    return json.dumps(props, indent=2)


def deserialize_xat(json_str: str) -> dict:
    """Deserialize XAT properties from a JSON string."""
    parsed = json.loads(json_str)
    if not isinstance(parsed, dict):
        raise ValueError("Invalid XAT data: expected a JSON object")
    return parsed


def get_xat_filename(data_file: str) -> str:
    """Get the .xat filename for a given data file.

    E.g., "README.TEXT" -> "README.TEXT.xat"
    """
    return data_file + XAT_EXTENSION


def is_xat_file(filename: str) -> bool:
    """Check if a filename is an XAT sidecar file."""
    return len(filename) > len(XAT_EXTENSION) and filename.lower().endswith(XAT_EXTENSION)
