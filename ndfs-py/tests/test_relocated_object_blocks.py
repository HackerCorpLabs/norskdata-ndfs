"""File numbering when SINTRAN has RELOCATED a user's overflow object block.

`testdata/BIGDISK0-K-201users.img.gz` is a real SINTRAN III K pack captured on
2026-08-02 from a live machine, carrying **201 users** - the first pack anyone here
has had with more than 64 users AND a multi-block user at the same time.

On it, user 8 holds 300 files: block 0 in index-block group 0, and its overflow block
in group **4** - not group 1. SINTRAN SKIPS a group whose slot belongs to someone
else, and RELOCATES an overflow block when the rightful home owner later needs it.
The block was watched moving twice:

    initially group 2
    user 136 (home = group 2, slot 8) created files  -> moved to group 3
    user 200 (home = group 3, slot 8) created files  -> moved to group 4

All 300 files survived every move; SINTRAN reported them throughout as FILE 0..299.

**The bug this pins down.** The old formula took the logical block from the PHYSICAL
group (``block = page // 512``), giving user 8 numbers 0..255 and then 1024..1067
where SINTRAN says 256..299. The logical block is the ORDINAL RANK of the group among
those the user occupies, ascending.

No synthetic image can exercise this: our own writer never relocates. Only a pack that
SINTRAN itself rearranged can.
"""
from __future__ import annotations

import gzip
import os

import pytest

from ndfs.filesystem import NdfsFileSystem

FIXTURE = os.path.join(
    os.path.dirname(__file__), "..", "..", "testdata", "BIGDISK0-K-201users.img.gz"
)


@pytest.fixture(scope="module")
def pack():
    path = os.path.abspath(FIXTURE)
    if not os.path.exists(path):
        pytest.skip(f"captured pack not present: {path}")
    with gzip.open(path, "rb") as fh:
        return NdfsFileSystem(bytearray(fh.read()), read_only=True)


def numbers_of(pack, user_index):
    return sorted(
        e.file_number for e in pack.get_object_entries() if e.user_index == user_index
    )


def groups_of(pack, user_index):
    from ndfs.constants import ENTRIES_PER_PAGE, PAGES_PER_INDEX_BLOCK

    return sorted({
        (e.object_index // ENTRIES_PER_PAGE) // PAGES_PER_INDEX_BLOCK
        for e in pack.get_object_entries()
        if e.user_index == user_index
    })


def test_pack_has_more_than_sixty_four_users(pack):
    """If this fails the fixture was replaced and the rest of the file tests nothing."""
    assert len(pack.get_users()) == 201


def test_user_eights_overflow_block_is_relocated(pack):
    """The naive model puts block 1 at group 1; SINTRAN put it at group 4.

    Groups 1, 2 and 3 are the homes of users 72, 136 and 200.
    """
    assert groups_of(pack, 8) == [0, 4]


def test_user_eight_is_numbered_zero_to_299(pack):
    """The assertion the physical-group formula failed: it gave 0..255 then 1024..1067."""
    assert numbers_of(pack, 8) == list(range(300))


@pytest.mark.parametrize("user_index,expected", [(72, 6), (136, 3), (200, 2)])
def test_displacing_users_are_intact(pack, user_index, expected):
    """Relocating user 8's block disturbed none of the users that displaced it."""
    assert numbers_of(pack, user_index) == list(range(expected))


def test_high_users_own_their_files(pack):
    """A user past 63 still attributes correctly - byte 34 carries the true owner."""
    by_index = {u.user_index: u.user_name for u in pack.get_users()}
    assert by_index[200] == "BIGU200"
    assert len(numbers_of(pack, 200)) == 2
