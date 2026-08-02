"""Object blocks: more than 256 files in one user area.

A SINTRAN user area holds 256 files per *object block*, one block by default and
up to 16 (4096 files) on version K. Before 2026-08-01 every implementation here
assumed 256 was a hard limit, so these tests cover the structure that assumption
hid.

Ground truth for the numbers asserted here was measured on a live SINTRAN III K
pack - see norskdata-ndfs/docs/NDFS-OBJECT-BLOCKS-SPEC.md.
"""
from __future__ import annotations

import pytest

from ndfs.constants import (
    ENTRIES_PER_PAGE,
    FILES_PER_OBJECT_BLOCK,
    MAX_OBJECT_BLOCKS,
    MAX_VERIFIED_MULTIBLOCK_USER,
    PAGES_PER_INDEX_BLOCK,
    PAGES_PER_OBJECT_BLOCK,
)
from ndfs.object_entry import compute_file_number
from ndfs.object_file import ObjectFile
from ndfs.user_entry import UserEntry


# --------------------------------------------------------------------------
# Byte 47: MXOBL / ACOBL
# --------------------------------------------------------------------------


def test_default_user_entry_is_one_block():
    """A fresh user gets one object block, which is what SINTRAN does."""
    ue = UserEntry()
    assert ue.max_object_blocks == 1
    assert ue.allocated_object_blocks == 1


def test_byte47_nibbles_are_zero_based():
    """A default user stores 0x00 - the reason this byte looked unused for years."""
    ue = UserEntry()
    buf = ue.to_bytes()
    assert buf[47] == 0x00


@pytest.mark.parametrize(
    "raw,expected_max,expected_allocated",
    [
        (0x00, 1, 1),    # default user: 256 files
        (0x31, 4, 2),    # BIGMAN on the verified pack: max 1024, 2 blocks in use
        (0xFF, 16, 16),  # the SINTRAN K ceiling: 4096 files
        (0x10, 2, 1),    # granted a second block, not yet used
    ],
)
def test_byte47_round_trip(raw, expected_max, expected_allocated):
    """Parse and re-serialise byte 47 without losing anything.

    0x31 is the real value read off user BIGMAN, whose reported MAXIMUM NUMBER OF
    FILES was 1024 (= 4 blocks) while holding 482 files (= 2 blocks in use).
    """
    data = bytearray(64)
    data[0] = 0x81  # entry-used | user-entry flag, or from_bytes rejects the slot
    data[47] = raw

    ue = UserEntry.from_bytes(data, 0)
    assert ue is not None
    assert ue.max_object_blocks == expected_max
    assert ue.allocated_object_blocks == expected_allocated
    assert ue.to_bytes()[47] == raw


def test_byte47_write_is_clamped():
    """A caller cannot store a count SINTRAN could not express."""
    ue = UserEntry()
    ue.max_object_blocks = 99
    ue.allocated_object_blocks = 99
    assert ue.to_bytes()[47] == 0xFF  # 16 max, 16 allocated

    ue.max_object_blocks = 0
    ue.allocated_object_blocks = 0
    assert ue.to_bytes()[47] == 0x00  # 1 max, 1 allocated

    # Allocated can never exceed max.
    ue.max_object_blocks = 2
    ue.allocated_object_blocks = 9
    assert ue.to_bytes()[47] == 0x11  # max 2, allocated clamped to 2


# --------------------------------------------------------------------------
# The file number is not the physical position
# --------------------------------------------------------------------------


def test_file_number_matches_the_live_machine():
    """The vector that proves the whole model, measured with LIST-FILES.

    On the verified pack, user 8 (BIGMAN):
        F0010 -> FILE 8     physical 2056
        F0250 -> FILE 190   physical 2238
        F0500 -> FILE 307   physical 18483

    F0500 is the important one: it is in the user's SECOND object block, and the
    old model reads its physical position 18483 as file 51 owned by user 72.
    """
    assert compute_file_number(2056, 8) == 8
    assert compute_file_number(2238, 8) == 190
    assert compute_file_number(18483, 8) == 307


def test_file_number_rejects_a_position_outside_the_user():
    """-1 rather than a plausible wrong answer, so a mismatch is visible."""
    # Physical 18483 belongs to user 8's second block, not to user 0.
    assert compute_file_number(18483, 0) == -1


def test_first_block_file_number_equals_slot():
    """Inside block 0 the number and the slot agree - which is why this went unnoticed."""
    for slot in (0, 1, 31, 32, 255):
        physical = ObjectFile.physical_slot(3, 0, slot)
        assert compute_file_number(physical, 3) == slot


def test_file_number_crosses_the_block_boundary_contiguously():
    """255 -> 256 must not jump or repeat: it is the boundary the old model broke on."""
    user = 8
    numbers = []
    for block in range(3):
        for slot in range(FILES_PER_OBJECT_BLOCK):
            physical = ObjectFile.physical_slot(user, block, slot)
            numbers.append(compute_file_number(physical, user))

    assert numbers == list(range(3 * FILES_PER_OBJECT_BLOCK))


def test_block_placement_stride_is_one_index_block():
    """Block n sits at n*512 + user*8 - the placement measured on the pack.

    BIGMAN (user 8) had entries on pages 64-71 and 576-583.
    """
    first_page_block0 = ObjectFile.physical_slot(8, 0, 0) // ENTRIES_PER_PAGE
    first_page_block1 = ObjectFile.physical_slot(8, 1, 0) // ENTRIES_PER_PAGE
    last_page_block0 = ObjectFile.physical_slot(8, 0, FILES_PER_OBJECT_BLOCK - 1) // ENTRIES_PER_PAGE
    last_page_block1 = ObjectFile.physical_slot(8, 1, FILES_PER_OBJECT_BLOCK - 1) // ENTRIES_PER_PAGE

    assert first_page_block0 == 64
    assert last_page_block0 == 71
    assert first_page_block1 == 576
    assert last_page_block1 == 583
    assert first_page_block1 - first_page_block0 == PAGES_PER_INDEX_BLOCK


# --------------------------------------------------------------------------
# Free-slot search across blocks
# --------------------------------------------------------------------------


def _fill(of: ObjectFile, user: int, block: int, count: int) -> None:
    """Occupy *count* slots at the start of one of a user's object blocks."""
    for slot in range(count):
        physical = ObjectFile.physical_slot(user, block, slot)
        entry = _entry(user)
        entry.object_index = physical
        of._entries[physical] = entry


def _entry(user: int):
    from ndfs.object_entry import ObjectEntry

    e = ObjectEntry()
    e.user_index = user
    e.object_name = "X"
    return e


def test_free_slot_stays_in_the_first_block_while_it_has_room():
    of = ObjectFile()
    _fill(of, 5, 0, 10)
    slot = of.find_free_user_slot(5, max_object_blocks=4, allocated_object_blocks=1)
    assert compute_file_number(slot, 5) == 10


def test_free_slot_reuses_a_hole():
    """Deleting a file leaves a hole and SINTRAN never compacts - so we reuse it."""
    of = ObjectFile()
    _fill(of, 5, 0, 10)
    hole = ObjectFile.physical_slot(5, 0, 4)
    del of._entries[hole]

    slot = of.find_free_user_slot(5, max_object_blocks=4, allocated_object_blocks=1)
    assert slot == hole
    assert compute_file_number(slot, 5) == 4


def test_full_first_block_grows_into_the_second():
    """File 257: the case that used to fail outright."""
    of = ObjectFile()
    _fill(of, 5, 0, FILES_PER_OBJECT_BLOCK)

    slot = of.find_free_user_slot(5, max_object_blocks=4, allocated_object_blocks=1)
    assert slot >= 0
    assert compute_file_number(slot, 5) == 256   # first file of block 1


def test_full_at_the_users_maximum():
    """Only now is 'object table is full' the truth."""
    of = ObjectFile()
    _fill(of, 5, 0, FILES_PER_OBJECT_BLOCK)

    slot = of.find_free_user_slot(5, max_object_blocks=1, allocated_object_blocks=1)
    assert slot == -1


def test_search_spans_every_allocated_block():
    """A hole in block 0 is still found when block 1 is already allocated."""
    of = ObjectFile()
    _fill(of, 5, 0, FILES_PER_OBJECT_BLOCK)
    _fill(of, 5, 1, 5)
    hole = ObjectFile.physical_slot(5, 0, 100)
    del of._entries[hole]

    slot = of.find_free_user_slot(5, max_object_blocks=4, allocated_object_blocks=2)
    assert slot == hole


def test_high_user_first_block_is_placed_normally():
    """Users past 64 keep working - they live in later index-block groups.

    This library reaches 256 users by giving index-block group g to users
    g*64..g*64+63, and test_object_directory_growth exercises 120 users, so
    placement for a high user's FIRST block must stay unchanged.
    """
    of = ObjectFile()
    slot = of.find_free_user_slot(MAX_VERIFIED_MULTIBLOCK_USER + 1)
    assert slot >= 0


def test_second_block_of_a_user_overlaps_the_high_user_region():
    """The collision that makes the multi-block and multi-user layouts incompatible.

    SINTRAN puts user U's object block n in index-block group n at offset U*8; this
    library puts user (n*64 + U)'s FIRST block in exactly the same place. Both are
    real - the multi-user layout is exercised with 120 users, the multi-block one was
    measured on a live SINTRAN K pack - so the filesystem layer has to refuse one of
    them. See NdfsFileSystem._allocate_user_object_slot.
    """
    users_per_group = PAGES_PER_INDEX_BLOCK // PAGES_PER_OBJECT_BLOCK
    second_block_of_user_8 = ObjectFile.physical_slot(8, 1, 0)
    first_block_of_user_72 = ObjectFile.physical_slot(users_per_group + 8, 0, 0)
    assert second_block_of_user_8 == first_block_of_user_72


def test_defaults_preserve_single_block_behaviour():
    """Callers that know nothing about object blocks keep working."""
    of = ObjectFile()
    _fill(of, 5, 0, FILES_PER_OBJECT_BLOCK)
    assert of.find_free_user_slot(5) == -1


# --------------------------------------------------------------------------
# Constants sanity
# --------------------------------------------------------------------------


def test_block_geometry_constants_agree():
    assert PAGES_PER_OBJECT_BLOCK * ENTRIES_PER_PAGE == FILES_PER_OBJECT_BLOCK
    assert FILES_PER_OBJECT_BLOCK == 256
    assert MAX_OBJECT_BLOCKS * FILES_PER_OBJECT_BLOCK == 4096
    # 64 users fit in one index block, which is exactly why users >= 64 are unsafe.
    assert PAGES_PER_INDEX_BLOCK // PAGES_PER_OBJECT_BLOCK == MAX_VERIFIED_MULTIBLOCK_USER + 1


# --------------------------------------------------------------------------
# End to end: create more than 1000 files in one user area
# --------------------------------------------------------------------------


def _make_fs(pages=6000, name="BLOCKS"):
    from ndfs.filesystem import NdfsFileSystem
    from ndfs.types import ImageCreationOptions, ImageTemplate

    opts = ImageCreationOptions(
        template=ImageTemplate.Custom,
        directory_name=name,
        custom_pages=pages,
    )
    return NdfsFileSystem.create_image(opts)


def test_create_more_than_256_files_allocates_a_second_block():
    """Creating file 257 used to raise 'object table is full'."""
    fs = _make_fs()
    user = fs.get_users()[0]
    user.max_object_blocks = 4          # as if @GIVE-OBJECT-BLOCKS had been run
    assert user.allocated_object_blocks == 1

    for i in range(FILES_PER_OBJECT_BLOCK + 1):   # 257 files
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")

    assert user.allocated_object_blocks == 2, "the 257th file must open a second block"

    numbers = sorted(
        e.file_number for e in fs.get_object_entries() if e.user_index == user.user_index
    )
    assert numbers == list(range(FILES_PER_OBJECT_BLOCK + 1))


def test_create_1000_files_and_read_them_all_back():
    """The case the request is really about: >1000 files in one user area.

    Exercises four object blocks, so it also crosses the 256/512/768 boundaries
    and confirms the n*512 page stride generalises beyond the single transition
    measured on the real pack.
    """
    count = 1000
    fs = _make_fs()
    user = fs.get_users()[0]
    user.max_object_blocks = 8          # 2048 files allowed

    for i in range(count):
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", bytes([i & 0xFF]))

    entries = [e for e in fs.get_object_entries() if e.user_index == user.user_index]
    assert len(entries) == count

    # Numbering must be contiguous 0..999 across every block boundary, with no
    # duplicates and nothing unassigned (-1).
    numbers = sorted(e.file_number for e in entries)
    assert numbers == list(range(count))

    # Four blocks are needed for 1000 files, and the count must have been recorded.
    assert user.allocated_object_blocks == 4

    # Every file must still be readable, and hold what we wrote.
    for i in (0, 255, 256, 511, 512, 767, 768, 999):
        data = fs.read_file(f"{user.user_name}/F{i:04d}:DATA")
        assert data == bytes([i & 0xFF]), f"F{i:04d} came back wrong"


def test_block_count_survives_a_reload():
    """ACOBL must be written to byte 47, not just held in memory."""
    fs = _make_fs()
    user = fs.get_users()[0]
    user.max_object_blocks = 4

    for i in range(FILES_PER_OBJECT_BLOCK + 5):
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")

    from ndfs.filesystem import NdfsFileSystem

    reloaded = NdfsFileSystem(fs.to_buffer(), read_only=True)
    ru = reloaded.get_users()[0]
    assert ru.max_object_blocks == 4
    assert ru.allocated_object_blocks == 2

    numbers = sorted(
        e.file_number for e in reloaded.get_object_entries() if e.user_index == ru.user_index
    )
    assert numbers == list(range(FILES_PER_OBJECT_BLOCK + 5))


def test_creation_fails_only_at_the_users_real_ceiling():
    """With one block the limit is 256 - and that error is now the truth."""
    fs = _make_fs()
    user = fs.get_users()[0]
    assert user.max_object_blocks == 1

    for i in range(FILES_PER_OBJECT_BLOCK):
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")

    with pytest.raises(IOError):
        fs.write_file(f"{user.user_name}/OVERFLOW:DATA", b"x")


# --------------------------------------------------------------------------
# give_object_blocks - the @GIVE-OBJECT-BLOCKS equivalent
# --------------------------------------------------------------------------


def test_give_object_blocks_adds_to_the_maximum():
    """The command ADDS blocks, it does not set the total.

    Matches the live measurement: BIGMAN was a default user (1 block, 256 files)
    and @GIVE-OBJECT-BLOCKS BIGMAN,3 took the reported MAXIMUM NUMBER OF FILES to
    1024, i.e. 4 blocks.
    """
    fs = _make_fs()
    user = fs.get_users()[0]
    assert user.max_object_blocks == 1

    new_max = fs.give_object_blocks(user.user_name, 3)

    assert new_max == 4
    assert user.max_object_blocks == 4
    assert fs.get_object_block_info(user.user_name)["max_files"] == 1024


def test_give_object_blocks_does_not_allocate_them():
    """Only the ceiling moves. Blocks are allocated on demand, as SINTRAN does."""
    fs = _make_fs()
    user = fs.get_users()[0]

    fs.give_object_blocks(user.user_name, 5)

    assert user.max_object_blocks == 6
    assert user.allocated_object_blocks == 1, "granting must not pre-allocate"


def test_give_object_blocks_persists_through_a_reload():
    """Byte 47 must reach the image, not just the in-memory entry."""
    from ndfs.filesystem import NdfsFileSystem

    fs = _make_fs()
    user = fs.get_users()[0]
    fs.give_object_blocks(user.user_name, 3)

    reloaded = NdfsFileSystem(fs.to_buffer(), read_only=True)
    assert reloaded.get_users()[0].max_object_blocks == 4


def test_give_object_blocks_refuses_to_pass_sixteen():
    """16 blocks = 4096 files is the SINTRAN K ceiling."""
    fs = _make_fs()
    user = fs.get_users()[0]
    fs.give_object_blocks(user.user_name, 15)
    assert user.max_object_blocks == MAX_OBJECT_BLOCKS

    with pytest.raises(ValueError) as err:
        fs.give_object_blocks(user.user_name, 1)
    assert "at most" in str(err.value)


def test_give_object_blocks_rejects_a_grant_that_would_overshoot():
    """Asking for more than remains fails outright rather than silently clamping.

    Clamping would leave the caller believing they got what they asked for.
    """
    fs = _make_fs()
    user = fs.get_users()[0]

    with pytest.raises(ValueError) as err:
        fs.give_object_blocks(user.user_name, 20)
    assert "At most 15 more" in str(err.value)
    assert user.max_object_blocks == 1, "a refused grant must change nothing"


def test_give_object_blocks_validates_its_arguments():
    fs = _make_fs()
    user = fs.get_users()[0]

    with pytest.raises(ValueError):
        fs.give_object_blocks(user.user_name, 0)
    with pytest.raises(ValueError):
        fs.give_object_blocks("NO-SUCH-USER", 1)


def test_take_object_blocks_lowers_the_maximum():
    """Library-only convenience - SINTRAN has no such command."""
    fs = _make_fs()
    user = fs.get_users()[0]
    fs.give_object_blocks(user.user_name, 3)

    assert fs.take_object_blocks(user.user_name, 2) == 2
    assert user.max_object_blocks == 2


def test_take_object_blocks_refuses_to_orphan_files():
    """The maximum may never fall below the blocks actually holding files."""
    fs = _make_fs()
    user = fs.get_users()[0]
    fs.give_object_blocks(user.user_name, 3)
    for i in range(FILES_PER_OBJECT_BLOCK + 1):     # forces a second block
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")
    assert user.allocated_object_blocks == 2

    with pytest.raises(ValueError) as err:
        fs.take_object_blocks(user.user_name, 3)    # would drop max to 1
    assert "orphan" in str(err.value)
    assert user.max_object_blocks == 4


# --------------------------------------------------------------------------
# The two limits a copy loop can hit, and telling them apart
# --------------------------------------------------------------------------


def test_soft_limit_says_to_grant_more_blocks():
    """Full but grantable: the error must name the remedy."""
    from ndfs.errors import ObjectBlockLimitError

    fs = _make_fs()
    user = fs.get_users()[0]
    for i in range(FILES_PER_OBJECT_BLOCK):
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")

    with pytest.raises(ObjectBlockLimitError) as err:
        fs.write_file(f"{user.user_name}/OVERFLOW:DATA", b"x")

    assert err.value.is_hard_limit is False
    assert "give_object_blocks" in str(err.value)
    assert "GIVE-OBJECT-BLOCKS" in str(err.value)


def test_hard_limit_says_nothing_can_be_granted():
    """At 16 blocks there is no remedy, and the message must not suggest one."""
    from ndfs.errors import ObjectBlockLimitError

    fs = _make_fs()
    user = fs.get_users()[0]
    # Pretend the user is already at the ceiling with every block full. Filling
    # 4096 files for real would take minutes; what is under test is which branch
    # the error takes, and that reads MXOBL only.
    user.max_object_blocks = MAX_OBJECT_BLOCKS
    user.allocated_object_blocks = MAX_OBJECT_BLOCKS

    err = ObjectBlockLimitError(
        user.user_name, MAX_OBJECT_BLOCKS, MAX_OBJECT_BLOCKS, is_hard_limit=True
    )
    assert err.is_hard_limit is True
    assert "hard limit" in str(err)
    assert "4096 files" in str(err)
    assert "give_object_blocks" not in str(err), "must not point at a command that cannot help"


def test_object_block_limit_error_is_still_an_ioerror():
    """Existing callers catch IOError; the typed error must not break them."""
    from ndfs.errors import ObjectBlockLimitError

    assert issubclass(ObjectBlockLimitError, IOError)


def test_get_object_block_info_reports_the_whole_position():
    fs = _make_fs()
    user = fs.get_users()[0]
    fs.give_object_blocks(user.user_name, 3)
    for i in range(10):
        fs.write_file(f"{user.user_name}/F{i:04d}:DATA", b"x")

    info = fs.get_object_block_info(user.user_name)

    assert info["user_name"] == user.user_name
    assert info["max_object_blocks"] == 4
    assert info["allocated_object_blocks"] == 1
    assert info["files_in_use"] == 10
    assert info["max_files"] == 1024
    assert info["allocated_files"] == 256
    assert info["can_grant"] is True
    assert info["grantable_blocks"] == 12


def test_grant_refuses_on_a_read_only_filesystem():
    """A grant that only changes memory would silently vanish, so refuse it.

    All four ports behave the same way here.
    """
    from ndfs.filesystem import NdfsFileSystem

    fs = _make_fs()
    user_name = fs.get_users()[0].user_name
    read_only = NdfsFileSystem(fs.to_buffer(), read_only=True)

    with pytest.raises(IOError):
        read_only.give_object_blocks(user_name, 1)
    with pytest.raises(IOError):
        read_only.take_object_blocks(user_name, 1)


# --------------------------------------------------------------------------
# Writing: overflow blocks skip a group that is another user's home
# --------------------------------------------------------------------------


def test_overflow_block_skips_a_group_owned_by_another_user():
    """An overflow block must not land in an existing user's home group.

    User U's block 1 would naturally sit in group ``home+1``, at slot ``U % 64``. That
    slot is user ``(home+1)*64 + U%64``'s HOME. SINTRAN skips such a group (measured
    live 2026-08-02: user 8 overflowing past 256 files did not take group 1, where user
    72 lives). This library goes further and never places a block there at all.

    Nothing else in the suite reaches this path: the 120-user growth test gives each
    user only 250 files, so none of them overflows.
    """
    from ndfs.constants import ENTRIES_PER_PAGE, PAGES_PER_INDEX_BLOCK, USERS_PER_INDEX_BLOCK

    fs = _make_fs(pages=20000, name="SKIPTEST")

    # Users are created in order, so make enough to reach index 72.
    while len(fs.get_users()) <= USERS_PER_INDEX_BLOCK + 8:
        fs.add_user(f"U{len(fs.get_users()):03d}", 200)

    low = fs.get_users()[8]
    colliding_index = USERS_PER_INDEX_BLOCK + low.user_index      # 64 + 8 = 72
    assert fs.get_user(colliding_index) is not None, "the colliding user must exist"

    fs.give_object_blocks(low.user_name, 3)
    for i in range(FILES_PER_OBJECT_BLOCK + 1):                   # 257 files: forces block 1
        fs.write_file(f"{low.user_name}/F{i:04d}:DATA", b"x")

    groups = sorted({
        (e.object_index // ENTRIES_PER_PAGE) // PAGES_PER_INDEX_BLOCK
        for e in fs.get_object_entries()
        if e.user_index == low.user_index
    })

    home = low.user_index // USERS_PER_INDEX_BLOCK
    assert len(groups) == 2, f"expected two groups, got {groups}"
    assert groups[0] == home
    assert groups[1] != home + 1, (
        f"overflow landed in group {groups[1]}, which is user {colliding_index}'s home"
    )


def test_overflow_block_numbers_stay_contiguous_when_a_group_is_skipped():
    """Skipping a group must not disturb the file numbers.

    The number comes from the group's ordinal RANK, not its index, so a skipped group
    changes where the block lives but not what the files are called.
    """
    from ndfs.constants import USERS_PER_INDEX_BLOCK

    fs = _make_fs(pages=20000, name="SKIPNUM")
    while len(fs.get_users()) <= USERS_PER_INDEX_BLOCK + 8:
        fs.add_user(f"U{len(fs.get_users()):03d}", 200)

    low = fs.get_users()[8]
    fs.give_object_blocks(low.user_name, 3)
    total = FILES_PER_OBJECT_BLOCK + 10
    for i in range(total):
        fs.write_file(f"{low.user_name}/F{i:04d}:DATA", b"x")

    from ndfs.filesystem import NdfsFileSystem

    reloaded = NdfsFileSystem(fs.to_buffer(), read_only=True)
    numbers = sorted(
        e.file_number for e in reloaded.get_object_entries()
        if e.user_index == low.user_index
    )
    assert numbers == list(range(total))
