# NDFS Object Blocks — more than 256 files per user

**Status:** decoded and verified 2026-08-01 against a live SINTRAN III K pack.
**Applies to:** all four implementations (`ndfs-c`, `ndfs-py`, `ndfs-ts`, and RetroFS `RetroFS.NDFS`).

Until 2026-08-01 every implementation in this repository assumed a hard limit of **256 files per
user area**. That is the *default*, not the limit. A SINTRAN user can hold up to **4096 files**, and
the extra structure was invisible because no pack anyone had examined used it.

This document is the reference for that structure. It supersedes the "Max 256 files/user" statements
that used to appear in `NDFS-FORMAT.md`, `CLAUDE.md` and `NDFS-VALIDATION-PLAN.md`.

---

## 1. The model

A user's file entries live in **object blocks**. One object block is:

```
8 object-file pages x 32 entries per page = 256 files
```

A user area gets **one** block when created and may be granted up to **16**, giving the version-K
ceiling of **4096 files**. Blocks are allocated **on demand**, as files are created — a user may
have a maximum of 4 and still only have 2 in use.

Operator view, on the machine:

| Command | Effect |
|---|---|
| `@GIVE-OBJECT-BLOCKS (<directory:>user)` | **Adds** blocks to the maximum. User SYSTEM only. |
| `@USER-STATISTICS <user>` | Prints `MAXIMUM NUMBER OF FILES`, which is `MXOBL * 256`. |

There is **no documented command to remove object blocks**, and none was found while carving. That
is consistent with the structure: blocks hold files, and file numbers are stable identifiers, so
freeing a block would orphan every file in it.

---

## 2. Where the counts are stored: user-entry byte 47

Byte 47 of the 64-byte user entry holds two **zero-based** nibbles:

```
 7   4 3   0
+-----+-----+
|MXOBL|ACOBL|      MXOBL = high nibble + 1   maximum blocks allowed
+-----+-----+      ACOBL = low  nibble + 1   blocks actually allocated
```

A default user therefore stores **`0x00`** — one block maximum, one allocated, 256 files. This is
why the field looked unused for so long: on every pack examined before this work, it was zero.

Both nibbles must be clamped to 1..16 on write. All four implementations do this.

---

## 3. Where the blocks physically live

The object file is one global structure for the pack. Each **index block** holds 512 page pointers;
each page holds 32 entries.

> **Object block `n` (0-based) for user `U` occupies object-file pages
> `n*512 + U*8` through `n*512 + U*8 + 7`.**

So successive blocks of the same user are **one whole index block apart** — a stride of 512 pages.
The object file behaves as a two-dimensional array indexed by (block, user).

> **REFINED 2026-08-02 — this holds only for an UNOBSTRUCTED block.** A user's blocks all sit at
> slot `U mod 64` of an index-block group, and block 0 is in the home group `U div 64` — which is
> exactly `U*8`. But an overflow block **skips** any group whose slot belongs to another user, and
> SINTRAN **relocates** it if that owner later needs the group. So the physical group is not
> `n`, and it can change over the life of the pack. See section 6.1.

### Consequence: the object file must be SubIndexed

An **Indexed** object file has a single index block = 512 page pointers, addressing pages 0..511 —
which is exactly block 0 for users 0..63. **Block 1 begins at page 512 and cannot be addressed at
all.** Granting a second object block on such a pack requires converting the object file to
**SubIndexed** first (allocate a sub-index, move the existing index block under it, rewrite
`MasterBlock.ObjectFilePointer`).

The verified pack is SubIndexed:

```
object_file_pointer   type=SubIndexed   block_id=18079
user_file_pointer     type=Indexed      block_id=18686
bit_file_pointer      type=Contiguous   block_id=18468
```

**UNKNOWN:** whether SINTRAN's own `@GIVE-OBJECT-BLOCKS` performs that conversion, refuses, or
assumes the object file is already sub-indexed. The pack used for this work was already SubIndexed
before the experiment, so it does not answer the question.

---

## 4. The file number is NOT the physical position

This is the defect the structure caused, and the most important part of this document.

The physical position of an entry is `page * 32 + slotInPage`. The number SINTRAN reports to the
user — the `n` in `FILE n` — is **per user** and is derived like this:

```
block      = page / 512                                   <-- WRONG, see 6.2
slot       = (page % 512 - user*8) * 32 + slotInPage
fileNumber = block * 256 + slot
```

> **CORRECTED 2026-08-02.** `block` is **not** the physical group — it is the ORDINAL RANK of the
> group among those the user occupies, because SINTRAN relocates overflow blocks. On the 201-user
> pack this formula gives user 8 the numbers 0..255 then **1024..1067** where SINTRAN says
> **256..299**. Also `user*8` must be `(user % 64)*8` for the within-group slot. The corrected
> rule, and why the `F0500 = FILE 307` vector below could not discriminate the two, is in
> section 6.2. Consequence: **this cannot be a pure function of (position, user)** — it needs the
> user's whole set of occupied groups, so all four ports now apply it as a post-load pass.

The two coincide **only inside a user's first object block**. Beyond it, both the number *and* the
"owner" derived from the position's high byte are wrong.

Worked example from the verified pack:

| | value |
|---|---|
| File | `F0500`, owned by user 8 (`BIGMAN`) |
| SINTRAN reports | `FILE 307` |
| Physical position | 18483 (page 577, slot 19) |
| Old model: number | `18483 & 0xFF` = **51** — wrong |
| Old model: owner | `18483 >> 8` = **72** — a user that does not exist |
| Correct | block `577/512` = 1, slot `(65-64)*32+19` = 51, number `1*256+51` = **307** |

Each implementation used to expose this as a pure function (retained for the single-block case;
see 6.2 for why the real rule needs more context):

| Port | Function |
|---|---|
| C | `ndfs_oe_compute_file_number(object_index, user_index)` |
| Python | `ndfs.object_entry.compute_file_number(object_index, user_index)` |
| TypeScript | `computeFileNumber(objectIndex, userIndex)` |
| C# | `ObjectEntry.ComputeFileNumber(objectIndex, userIndex)` |

All four return **-1** when the position lies outside any object block belonging to that user.
That is deliberate: a caller noticing a mismatch is far better than silently receiving a plausible
but wrong number.

---

## 5. What is verified, and how

Everything above was measured on `BIGDISK0-K-100.IMG`, a live SINTRAN III K pack, using a
purpose-built control group.

The user `BIGMAN` was created, granted 3 extra object blocks, and filled with 482 one-page files.
**Every other user on the pack has the default single block**, so any difference isolates cleanly.

| Observation | Result |
|---|---|
| `@USER-STATISTICS BIGMAN` before | `MAXIMUM NUMBER OF FILES : 256` |
| after `@GIVE-OBJECT-BLOCKS BIGMAN,3` | `MAXIMUM NUMBER OF FILES : 1024` |
| byte 47, BIGMAN | `0x31` → MXOBL 4, ACOBL 2 |
| byte 47, all other users | `0x00` → MXOBL 1, ACOBL 1 |
| BIGMAN entry pages | 64-71 **and 576-583** (= `0*512+64` and `1*512+64`) |
| `LIST-FILES F0010` / `F0250` / `F0500` | `FILE 8` / `FILE 190` / `FILE 307` |
| Computed numbers after the fix | 8 / 190 / 307 — all match |
| BIGMAN entries loaded | 483, numbers 0..482, 227 above 255, no duplicates |

Three independent quantities agree on the nibble decode (the operator command, the reported maximum,
and the file count), so it is not a coincidence of one sample.

Full write-up with the raw captures:
`NDInsight SINTRAN/XMSG/DOC/NDFS-OBJECT-BLOCKS-DECODED-2026-08-01.md`.

---

## 6. Known gaps — do not guess these

### 6.1 Block placement and relocation [SETTLED 2026-08-02 on live SINTRAN]

**There is no collision, and there never were two competing layouts.**

First, the arithmetic. The "multi-user" formula this library used and the "multi-block"
formula measured on a pack are the same expression:

```
(U // 64)*512 + (U % 64)*8   ==   8*U        for every U
```

They cannot disagree about a first block.

Second, what SINTRAN actually does with LATER blocks, measured on a live SINTRAN III K
pack carrying 201 users:

> A user's object blocks all sit at the **same slot `U mod 64`**, varying only by
> index-block group. Block 0 is in the **home group `U div 64`**. An overflow block takes
> that slot in a **higher** group, **skipping** any group whose slot is another user's
> home — and SINTRAN **RELOCATES** the overflow block when that home owner later needs it.

Observed, by prediction, twice:

| Event | User 8's overflow block |
|---|---|
| user 8 given 300 files | group 2 (not group 1 — user 72 lives there) |
| user 136 created files (home = group 2) | → group 3 |
| user 200 created files (home = group 3) | → **group 4** |

All 300 of user 8's files survived both moves and stayed numbered FILE 0..299 throughout.
Nothing is lost and no page is ever shared.

### 6.2 Consequence: the file number is NOT a function of position

Because the physical group moves, `block = page // 512` is wrong. The logical block is
the **ordinal rank** of the group among the groups that user occupies, ascending:

```
groups   = sorted(distinct groups the user has entries in)
block    = groups.index(this entry's group)
slot     = (page % 512 - (U % 64)*8) * 32 + entry_in_page
fileNumber = block * 256 + slot
```

On the captured pack the old formula gave user 8 numbers 0..255 then **1024..1067**, where
SINTRAN says **256..299**. It looked right on the earlier BIGMAN pack only because nothing
had displaced that pack's overflow block, so rank happened to equal group number — the
`F0500 = FILE 307` vector holds under both readings and could not discriminate them.

**This means `compute_file_number(object_index, user_index)` cannot be a pure function.**
It needs the user's full set of occupied groups, so it is applied as a post-pass once the
object file is loaded. All four ports now do this.

### 6.3 What this library does on WRITE

SINTRAN relocates; this library instead declines to place an overflow block in a group
that is an existing user's home, so it never creates a layout needing relocation. That is
stricter than SINTRAN and always safe.

### 6.4 Regression fixture

`testdata/BIGDISK0-K-201users.img.gz` — the captured pack (78 MB raw, 2.7 MB gzipped).
Tests: `ndfs-py/tests/test_relocated_object_blocks.py` and
`RetroFS.Tests/NdfsRelocatedObjectBlockTests.cs`. Verified to FAIL under the old
physical-group formula, so it genuinely guards the rule.

**A third block.** The stride is measured from one transition (block 0 -> 1). A user with more than
512 files would confirm `n*512` generalises rather than being a coincidence of the second block.

**Indexed-to-SubIndexed conversion.** See section 3.

---

## 7. The API

Every port exposes the same three operations. Names follow each language's convention; the
behaviour is identical.

| Operation | Python | C | TypeScript | C# (RetroFS) |
|---|---|---|---|---|
| Grant blocks | `fs.give_object_blocks(user, n)` | `ndfs_give_object_blocks()` | `fs.giveObjectBlocks(user, n)` | `fs.GiveObjectBlocks(user, n)` |
| Remove blocks | `fs.take_object_blocks(user, n)` | `ndfs_take_object_blocks()` | `fs.takeObjectBlocks(user, n)` | `fs.TakeObjectBlocks(user, n)` |
| Report position | `fs.get_object_block_info(user)` | `ndfs_get_object_block_info()` | `fs.getObjectBlockInfo(user)` | `fs.GetObjectBlockInfo(user)` |

RetroCore's own implementation has the same operations as
`NDFSDirectory.GiveObjectBlocksByID/TakeObjectBlocksByID/CountUserFiles`.

### 7.1 Granting — what it does and does not do

`give_object_blocks` mirrors `@GIVE-OBJECT-BLOCKS <user>,<count>`: it **ADDS** blocks to the
maximum rather than setting a total. Granting 3 blocks to a default user takes the ceiling from
256 to 1024, which is exactly what the live machine reported for BIGMAN.

It deliberately does **not**:

- **Allocate the blocks.** Only MXOBL moves. SINTRAN allocates a block the first time a file needs
  it, and so does every port — a user granted 4 blocks who holds 300 files still has ACOBL 2.
- **Reserve disk pages.** Object blocks cap the NUMBER of files, not their size. Page quota is a
  separate axis (`@GIVE-USER-SPACE`). A user can hit either limit independently.
- **Convert an Indexed object file to SubIndexed.** See section 3. Growth into block 1 fails later,
  when a file actually needs it.

A grant that would pass 16 blocks is **refused outright, not clamped** — clamping would leave the
caller believing it received what it asked for, and the shortfall would surface much later as a
premature "user is full".

### 7.2 Removing blocks has no SINTRAN equivalent

`take_object_blocks` exists only so a tool can undo its own grant on an image it is building.
**No SINTRAN command removes object blocks**, and none was found while carving — consistent with
the structure, since a file's number is derived from the block it sits in, so dropping a block
would orphan every file in it. All ports refuse to lower the maximum below ACOBL, or below 1.

### 7.3 The two limits, and why they must be distinguishable

A copy or import loop hits one of two very different walls, and telling a user to run
`@GIVE-OBJECT-BLOCKS` when they already have 16 blocks is useless advice. So the failure carries
the distinction as a flag rather than in message text:

| Situation | Condition | Remedy |
|---|---|---|
| **Soft** | every permitted block is full, MXOBL < 16 | grant more blocks |
| **Hard** | MXOBL == 16, i.e. 4096 files | none — delete files or use another user area |

| Port | Soft | Hard |
|---|---|---|
| Python | `ObjectBlockLimitError.is_hard_limit == False` | `... == True` |
| C | `NDFS_ERR_OBJ_BLOCKS_FULL` | `NDFS_ERR_OBJ_BLOCKS_MAX` |
| TypeScript | `ObjectBlockLimitError.isHardLimit === false` | `... === true` |
| C# | `ObjectBlockLimitException.IsHardLimit == false` | `... == true` |

The Python, TypeScript and C# exceptions derive from what the file system threw before object
blocks were understood (`IOError`, `Error`, `InvalidOperationException`), so existing handlers keep
working.

### 7.4 Tools

`ndtool` (the C port's command-line tool):

```
ndtool --objblocks NAME IMAGE            show blocks, files held, room to grant
ndtool --giveobjblocks NAME N IMAGE      grant N more blocks
ndtool --takeobjblocks NAME N IMAGE      lower the maximum by N
ndtool --stat USER/FILE:TYPE IMAGE       includes the SINTRAN file number
```

`--stat` reports the user-visible file number alongside the raw on-disk word, with the block and
slot it decomposes into — the two differ for any file past a user's first block.

RetroCommander shows object blocks, files in use against the maximum, and a FULL warning on the
user view, with **F6** bound to the grant.

---

## 8. Implementation checklist

Read path — **done** in all four ports:

- [x] Parse byte 47 into max/allocated block counts, and write it back clamped to 1..16.
- [x] Compute the user-visible file number with the section 4 formula, keeping the physical position
      as a separate field for write-back.

Create path — **done** in Python, C, TypeScript and RetroFS:

- [x] Free-slot search spanning all *allocated* blocks rather than `U*8..U*8+7`.
- [x] Allocate the next block when the allocated ones are full and `ACOBL < MXOBL`, raising ACOBL.
- [x] Fail only at `ACOBL == MXOBL`, distinguishing the soft limit from the hard one.
- [x] Set the file number on newly created entries, not only on load.
- [x] Refuse to grow into block `n` for user `U` when user `n*64 + U` exists (section 6).

**Not done in RetroCore's own implementation.** `ObjectFile.AddObjectFileToDirectory` still searches
one block and requires an Indexed object file, so it can READ a multi-block pack but cannot create
file 257. It now reports which of the two limits it hit instead of returning null silently.

Still open everywhere:

- [ ] Convert an Indexed object file to SubIndexed so a second block becomes addressable
      (section 3). All ports currently fail when growth needs it.

Tests — **done**:

- [x] Byte-47 round trip for a multi-block user.
- [x] Directory walk finds entries in block 2+.
- [x] The `F0500` = 307 vector.
- [x] Create past 256 allocates a block instead of failing.
- [x] Create past `MXOBL*256` fails cleanly, with the right one of the two errors.
- [x] 1000+ files created and read back, numbering contiguous across the 256 boundary.
- [x] Grant/remove round trip, persisted through a reload.
- [x] 300 files created through `ndtool` on a real image, across 300 separate process runs.

Still untested anywhere — see section 6:

- [ ] **A live SINTRAN with more than 64 users AND a multi-block user.** Until that exists, the
      multi-user layout in these libraries is unverified against real hardware and may be the part
      that is wrong.
