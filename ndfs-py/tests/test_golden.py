"""
Golden byte-vector tests: real 64-byte entries from a genuine SINTRAN image
(tor-disk.img). Pin the on-disk format to ground truth + prove lossless
round-trips.

SPDX-License-Identifier: MIT
"""
from ndfs.object_entry import ObjectEntry
from ndfs.user_entry import UserEntry

GOLDEN_OBJ = bytes.fromhex(
    "900053494e5452414e270000000000000000444154410000000007ff0020"
    "00000000000000000028962696859a08a18b9a08a18b0000003f0001e7ff00000001"
)
GOLDEN_USR = bytes.fromhex(
    "810353595354454d27000000000000000000575796269683b22b73ec0000717a"
    "00004cc20000000004ff00000000000087018707811800000000000000000000"
)


def test_golden_object_fields():
    e = ObjectEntry.from_bytes(GOLDEN_OBJ, 0)
    assert e.object_name == "SINTRAN"
    assert e.type == "DATA"
    assert e.next_version == 0
    assert e.prev_version == 0
    assert e.access_bits == 0x07FF
    assert e.file_type_flags == 0x0020
    assert e.user_index == 0
    assert e.total_open_count == 40
    assert e.date_created == 0x96269685
    assert e.last_date_written == 0x9A08A18B
    assert e.pages_in_file == 63
    assert e.bytes_in_file == 124928


def test_golden_object_roundtrip():
    e = ObjectEntry.from_bytes(GOLDEN_OBJ, 0)
    out = bytearray(64)
    e.to_bytes(out, 0)
    assert bytes(out) == GOLDEN_OBJ


def test_golden_user_fields():
    u = UserEntry.from_bytes(GOLDEN_USR, 0)
    assert u.user_name == "SYSTEM"
    assert u.enter_count == 3
    assert u.pages_reserved == 29050
    assert u.pages_used == 19650
    assert u.user_index == 0
    assert u.default_file_access == 0x04FF
    assert u.friends[0].entry_used is True
    assert u.friends[1].entry_used is True
    assert u.friends[2].entry_used is True
    assert u.friends[3].entry_used is False


def test_golden_user_roundtrip():
    u = UserEntry.from_bytes(GOLDEN_USR, 0)
    assert bytes(u.to_bytes()) == GOLDEN_USR


# Object entry whose type field is intentionally empty on disk: offset 18 =
# 0x27 (terminator) + NULs. SINTRAN writes such entries (e.g. TERMINAL).
# Parsing must NOT default the empty type to "DATA", or the round-trip
# clobbers 27 00 00 00 with 44 41 54 41. Regression for Bug 1.
GOLDEN_OBJ_EMPTY_TYPE = bytes.fromhex(
    "90005445524d494e414c270000000000"
    "0000270000000000000007ff00200000"
    "0000000000000028962696859a08a18b"
    "9a08a18b0000003f0001e7ff00000001"
)


def test_golden_empty_type_preserved():
    e = ObjectEntry.from_bytes(GOLDEN_OBJ_EMPTY_TYPE, 0)
    assert e.object_name == "TERMINAL"
    # Empty type must stay empty — not "DATA".
    assert e.type == ""


def test_golden_empty_type_roundtrip():
    e = ObjectEntry.from_bytes(GOLDEN_OBJ_EMPTY_TYPE, 0)
    out = bytearray(64)
    e.to_bytes(out, 0)
    # The empty type field (27 00 00 00) must survive byte-for-byte.
    assert bytes(out) == GOLDEN_OBJ_EMPTY_TYPE
