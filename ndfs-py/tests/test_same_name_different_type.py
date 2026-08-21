"""A SINTRAN file is identified by NAME:TYPE, not by NAME alone.

Real packs carry several files that share a base name and differ only in type:
(TCP-IP)TCP-IP-LO-D02 exists as :MODE and :LIST, (TCP-IP)TCP-START-D02 as
:MODE and :LIST, and (TCP-IP)SKP-C00 as :DEFS, :IMPT and :INTL at once.

find_object used to match on the name only, so write_file believed AAA:MODE
was the same file as an existing AAA:LIST. The second write updated the LIST
entry's data and left its type alone, which meant the MODE file never appeared
and the LIST file silently held the MODE file's bytes. Measured 2026-08-19
while installing the COSMOS TCP/IP kit onto a SINTRAN M pack: both
TCP-IP-LO-D02:MODE and TCP-START-D02:MODE were lost that way, and the loss was
only visible by comparing file sizes against the source pack.
"""
import pytest

from ndfs import NdfsFileSystem
from ndfs.types import ImageCreationOptions, ImageTemplate


def make_fs() -> NdfsFileSystem:
    return NdfsFileSystem.create_image(
        ImageCreationOptions(template=ImageTemplate.Floppy12MB, directory_name="TESTD")
    )


def entries_named(fs: NdfsFileSystem, name: str):
    """(type, bytes) for every entry with this object name, sorted by type."""
    out = []
    for e in fs.get_object_entries():
        if e.object_name.strip().upper() == name.upper():
            out.append((e.type.strip().upper(), e.bytes_in_file))
    return sorted(out)


def test_same_name_different_types_coexist():
    fs = make_fs()
    fs.write_file("SYSTEM/AAA:LIST", b"L" * 100)
    fs.write_file("SYSTEM/AAA:MODE", b"M" * 200)
    fs.write_file("SYSTEM/AAA:DEFS", b"D" * 300)

    assert entries_named(fs, "AAA") == [("DEFS", 300), ("LIST", 100), ("MODE", 200)]


def test_each_type_reads_back_its_own_content():
    fs = make_fs()
    fs.write_file("SYSTEM/AAA:LIST", b"L" * 100)
    fs.write_file("SYSTEM/AAA:MODE", b"M" * 200)

    assert fs.read_file("SYSTEM/AAA:LIST")[:100] == b"L" * 100
    assert fs.read_file("SYSTEM/AAA:MODE")[:200] == b"M" * 200


def test_overwriting_one_type_leaves_the_others_alone():
    fs = make_fs()
    fs.write_file("SYSTEM/AAA:LIST", b"L" * 100)
    fs.write_file("SYSTEM/AAA:MODE", b"M" * 200)
    fs.write_file("SYSTEM/AAA:DEFS", b"D" * 300)

    fs.write_file("SYSTEM/AAA:MODE", b"m" * 50)

    assert entries_named(fs, "AAA") == [("DEFS", 300), ("LIST", 100), ("MODE", 50)]
    assert fs.read_file("SYSTEM/AAA:LIST")[:100] == b"L" * 100
    assert fs.read_file("SYSTEM/AAA:DEFS")[:300] == b"D" * 300


def test_file_exists_and_delete_respect_the_type():
    fs = make_fs()
    fs.write_file("SYSTEM/AAA:LIST", b"L" * 100)

    assert fs.file_exists("SYSTEM/AAA:LIST")
    assert not fs.file_exists("SYSTEM/AAA:MODE")

    fs.write_file("SYSTEM/AAA:MODE", b"M" * 200)
    fs.delete_file("SYSTEM/AAA:MODE")

    assert fs.file_exists("SYSTEM/AAA:LIST")
    assert not fs.file_exists("SYSTEM/AAA:MODE")


def test_contiguous_file_does_not_collide_with_another_type():
    fs = make_fs()
    fs.write_file("SYSTEM/BBB:PROG", b"P" * 100)

    # A different type is a different file, so this must be allowed.
    fs.create_contiguous_file("SYSTEM/BBB:DATA", 2)
    assert fs.file_exists("SYSTEM/BBB:PROG")
    assert fs.file_exists("SYSTEM/BBB:DATA")

    # The same NAME:TYPE really is a duplicate, and must still be refused.
    with pytest.raises(FileExistsError):
        fs.create_contiguous_file("SYSTEM/BBB:DATA", 2)


def test_the_real_world_case_from_the_tcp_ip_kit():
    """The exact shape that lost two mode files on a real install."""
    fs = make_fs()
    for name, typ, size in (
        ("TCP-IP-LO-D02", "LIST", 4848),
        ("TCP-IP-LO-D02", "MODE", 2307),
        ("TCP-START-D02", "LIST", 684),
        ("TCP-START-D02", "MODE", 581),
        ("SKP-C00", "DEFS", 17260),
        ("SKP-C00", "IMPT", 7759),
        ("SKP-C00", "INTL", 7149),
    ):
        fs.write_file("SYSTEM/%s:%s" % (name, typ), bytes([0x41]) * size)

    assert entries_named(fs, "TCP-IP-LO-D02") == [("LIST", 4848), ("MODE", 2307)]
    assert entries_named(fs, "TCP-START-D02") == [("LIST", 684), ("MODE", 581)]
    assert entries_named(fs, "SKP-C00") == [
        ("DEFS", 17260),
        ("IMPT", 7759),
        ("INTL", 7149),
    ]
    assert fs.verify_integrity()
