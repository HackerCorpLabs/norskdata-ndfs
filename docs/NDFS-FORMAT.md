# NDFS On-Disk Format Specification

Nord Disk File System (NDFS) is the filesystem used by Norsk Data minicomputers (ND-100, ND-110, ND-120) running the Sintran-III operating system. This document specifies the binary on-disk format.

## Fundamentals

| Property | Value |
|----------|-------|
| Page (block) size | 2048 bytes (1024 x 16-bit words) |
| Byte order | Big-endian (network order) |
| String terminator | `0x27` (ASCII single quote `'`) |
| Maximum name length | 16 characters (7-bit ASCII) |
| Maximum type length | 4 characters |
| Entry size | 64 bytes (user and object entries) |
| Entries per page | 32 |
| Maximum users | 256 |
| Maximum friends per user | 8 |
| System blocks (reserved) | 0-6 |

## Page 0 Layout

Page 0 (first 2048 bytes of the image) contains three structures:

```
Offset      Size    Content
0x0000      1024    Boot sector (BPUN, FLOMON, or raw binary)
0x0400      1600    Reserved / unused
0x07D0      16      Extended Info Block (hard disks only)
0x07E0      32      Master Block
```

## Master Block (32 bytes at offset 0x07E0)

```
Offset  Size  Field              Format
0x00    16    Directory Name      ASCII, terminated by 0x27
0x10    4     Object File Ptr     BlockPointer (big-endian)
0x14    4     User File Ptr       BlockPointer (big-endian)
0x18    4     Bit File Ptr        BlockPointer (big-endian)
0x1C    4     Unreserved Pages    uint32 big-endian
```

### BlockPointer (4 bytes, big-endian)

```
Bits 31-30: Pointer Type
  00 = Contiguous (sequential pages from blockId)
  01 = Indexed (index block with up to 512 data page pointers)
  10 = Sub-Indexed (sub-index -> index blocks -> data pages)
  11 = Reserved (invalid)

Bits 29-0: Block ID (30-bit LBA page address)
```

A BlockPointer is valid when `blockId > 0` and `type != Reserved`.

### Pointer Type Usage

| Structure | Typical Type |
|-----------|-------------|
| Object File | Indexed (01) or Sub-Indexed (10) |
| User File | Indexed (01) |
| Bit File | Contiguous (00) |

## Extended Info Block (16 bytes at offset 0x07D0)

Present on hard disks only. Floppies (FLOMON format) do not have valid extended info.

```
Offset  Size  Field              ND Word    Format
0x07D0  2     Checksum           1750       uint16 big-endian
0x07D2  2     Reserved 1         1751       uint16 (must be 0)
0x07D4  2     Reserved 2         1752       uint16 (must be 0)
0x07D6  2     Reserved 3         1753       uint16 (must be 0)
0x07D8  2     Flag Word          1754       uint16 big-endian
0x07DA  2     System Number      1755       uint16 big-endian
0x07DC  4     Pages Available    1756-1757  uint32 big-endian
```

### Checksum Calculation

```
pagesLo  = PagesAvailable & 0xFFFF
pagesHi  = (PagesAvailable >> 16) & 0xFFFF
checksum = (pagesLo XOR pagesHi XOR FlagWord XOR Reserved1 XOR Reserved2 XOR Reserved3)
         + SystemNumber
checksum = checksum & 0xFFFF
```

### Validation States

| State | Condition |
|-------|-----------|
| Valid | Stored checksum == calculated checksum |
| ValidLowByteOnly | Low bytes match, high byte of stored is 0x00 |
| Invalid | No match |

### FLOMON Detection

If all extended info fields are zero and a FLOMON boot signature is present in the boot sector, the image is a floppy. Extended info is marked invalid for floppies.

## User Entry (64 bytes)

```
Offset  Size  Field              Format
0       1     Flags              0x81 = valid user entry
1       1     Enter Count        Login counter (0-255)
2       16    User Name          ASCII, terminated by 0x27
18      2     Password           uint16 big-endian
20      4     Date Created       ND timestamp, big-endian
24      4     Last Date Entered  ND timestamp, big-endian
28      4     Pages Reserved     uint32 big-endian (quota)
32      4     Pages Used         uint32 big-endian
36      1     Directory Index    Multi-directory support
37      1     User Index         0-255
38      2     Reserved
40      2     Default File Access uint16 big-endian (default: 0x04FF)
42      6     Reserved / tracking (byte 47 = mxobl/acobl nibbles)
48      16    Friends            8 x 2-byte entries, big-endian
```

**Verified against real images** (`DefaultFileAccess`@40 reads 0x04FF/0x03FF;
friends@48). Loaders identify the owning user by the stored **User Index byte
(offset 37)**, not by physical position; users are placed at physical slot
`userIndex % 32` in page `userIndex / 32` (8 pages × 32 = 256 users max).

### User Flags

- Bit 7: Entry Used (must be 1)
- Bit 0: User/Object flag (1 = user entry)
- Valid user: `(flags & 0x81) == 0x81`

## User Friend Entry (2 bytes, big-endian)

```
Bit 15:     Entry used (1 = active)
Bits 14-13: Reserved
Bit 12:     Directory access
Bit 11:     Common access
Bit 10:     Append access
Bit 9:      Write access
Bit 8:      Read access
Bits 7-0:   Friend user index (0-255)
```

## Object (File) Entry (64 bytes)

```
Offset  Size  Field              Format
0       2     Header             word; bit15 in use, bit14 write-open, bit12 modified
2       16    Object Name        ASCII, terminated by 0x27 then NULs
18      4     Type               ASCII (max 4 chars), terminated by 0x27
22      2     Next Version       uint16 BE (object index of next version)
24      2     Prev Version       uint16 BE (object index of previous version)
26      2     Access Bits        uint16 BE (15-bit, see Access Permissions)
28      2     File Type Flags    uint16 BE: T P S I C A M L (Indexed=0x08, etc.)
30      2     Device Number      uint16 BE
32      1     File Type Code     0=DATA, 1=PROG, 2=SYMB, 3=TEXT
34      2     Object Index       uint16 BE = (UserIndex << 8) | fileEntry
                                 (byte 34 = owning user index; byte 35 = slot)
36      2     Current Open Count uint16 BE
38      2     Total Open Count   uint16 BE
40      4     Date Created       ND timestamp, uint32 BE
44      4     Last Date Read     ND timestamp, uint32 BE
48      4     Last Date Written  ND timestamp, uint32 BE
52      4     Pages in File      uint32 big-endian (data pages; excludes index)
56      4     Bytes in File - 1  uint32 big-endian (actual bytes = stored + 1)
60      4     File Pointer       BlockPointer, big-endian
```

**Important**:
- Bytes-in-file stores `actual_size - 1`; add 1 when reading.
- A single-version file is **self-referential**: Next = Prev = its own Object
  Index. Zeroed version pointers make SINTRAN report a broken chain (`;2`).
- **Object Index encodes ownership and physical position**: the high byte is
  the owning user, and the object file is partitioned so user *U* owns object
  slots `U*256 .. U*256+255` (index-block pointer slots `U*8 .. U*8+7`). SINTRAN
  derives the owner from the slot's physical position; a file must be written
  into its owner's region, not the first free global slot. Max 256 files/user.
- Name/Type fields use a **single** `0x27` terminator followed by NULs (not a
  field padded with terminators).

## Bit File (Allocation Bitmap)

One bit per page. Stored contiguously starting at the Bit File Pointer's block ID.

### Bit Layout

```
byte_index = blockId / 8
bit_index  = blockId % 8
is_used    = (bitmap[byte_index] & (1 << bit_index)) != 0
```

### System Blocks

Blocks 0-6 are reserved for system use and must never be allocated for file data.

## File Allocation

### Contiguous (Type 00)

Data pages are sequential starting at `blockId`:
```
Page 0: blockId + 0
Page 1: blockId + 1
Page N: blockId + N
```

### Indexed (Type 01)

The `blockId` points to an index block containing up to 512 BlockPointers (4 bytes each). Each pointer references a data page. Maximum file size: 512 pages (~1 MB).

```
Index Block (2048 bytes):
  [0-3]:     BlockPointer to data page 0
  [4-7]:     BlockPointer to data page 1
  ...
  [2044-2047]: BlockPointer to data page 511
```

### Sub-Indexed (Type 10)

The `blockId` points to a sub-index block containing up to 512 pointers to index blocks. Each index block contains up to 512 data page pointers. Maximum file size: 262,144 pages (~512 MB).

```
Sub-Index Block -> Index Block 0 -> Data pages 0-511
                -> Index Block 1 -> Data pages 512-1023
                -> ...
```

### Sparse Files

A BlockPointer with `blockId == 0` in an index block represents a sparse hole. When read, the corresponding page is filled with zeros. No disk space is allocated for sparse holes.

## User File Structure

The User File Pointer (from the master block) points to an **indexed** structure:

```
Index Block: up to 8 BlockPointers
  -> Data Page 0: User entries 0-31
  -> Data Page 1: User entries 32-63
  ...
  -> Data Page 7: User entries 224-255
```

Maximum 256 users (8 pages x 32 entries/page).

## Object File Structure

The Object File Pointer points to an **indexed** or **sub-indexed** structure:

```
Indexed:     Index Block -> up to 512 data pages (16,384 files max)
Sub-Indexed: Sub-Index -> up to 512 index blocks -> 262,144 data pages
```

**Per-user partition (critical):** the object file is partitioned by user.
User *U* owns the **8 consecutive index-block pointer slots** `U*8 .. U*8+7`,
i.e. 8 data pages × 32 entries = **256 object slots per user**, spanning object
indices `U*256 .. U*256+255`. A file's Object Index (offset 34) therefore
encodes `(U << 8) | fileEntry`, and a new file MUST be placed in a free slot of
its owner's region (allocating/linking that user's directory page on demand) —
not the first free global slot. Writing to a global slot puts the file in the
wrong user's region as far as SINTRAN is concerned.

## Path Format

NDFS uses a flat hierarchy with users as virtual directories:

```
/                        Root (lists users)
/USERNAME                User directory (lists user's files)
/USERNAME/FILENAME:TYPE  File reference (colon separates name from type)
```

The colon (`:`) is the extension separator. A period (`.`) may also be used and will be converted to colon internally.

## ND Timestamp Format (32-bit)

```
Bits 31-26: Year (0-63, add 1950 for calendar year)
Bits 25-22: Month (1-12)
Bits 21-17: Day (1-31)
Bits 16-12: Hour (0-23)
Bits 11-6:  Minute (0-59)
Bits 5-0:   Second (0-59)
```

Valid year range: 1950-2013.

## Access Permissions (15-bit)

```
Bits 14-10: Public permissions  (5 bits)
Bits 9-5:   Friend permissions  (5 bits)
Bits 4-0:   Owner permissions   (5 bits)

Per tier (5 bits):
  Bit 4: Delete
  Bit 3: Execute/Common
  Bit 2: Append
  Bit 1: Write
  Bit 0: Read
```

Default: `0x04FF` (owner full, friends read/write, public read).

## Boot Sector Formats

The first 1024 bytes of page 0 may contain boot code in one of these formats:

### BPUN (Bootable Punched Tape)

```
ASCII Preamble:
  Octal digits terminated by CR (0x0D)
  B (start address) = value before last CR
  C (boot address) = value after last CR before '!'

'!' delimiter (0x21)

Binary Section:
  Address:  2 bytes big-endian (load address)
  Count:    2 bytes big-endian (word count)
  Data:     count * 2 bytes (program words)
  Checksum: 2 bytes big-endian (sum of all words)
  Action:   2 bytes big-endian (0 = execute, else stay in OPCOM)
```

### FLOMON (Floppy Monitor)

A BPUN variant where address=0 and count=0 after the `!` delimiter. Used on ND floppy disks.

### Binary

Raw machine code with no BPUN/FLOMON markers. Detected by checking for non-zero, non-uniform data in the first 1024 bytes.

## Disk Image Templates

| Template | Pages | File Blocks | Obj Block | User Block | Bit Block |
|----------|-------|-------------|-----------|------------|-----------|
| Floppy 360KB | 154 | 154 | 149 | 151 | 153 |
| Floppy 1.2MB | 616 | 616 | 611 | 613 | 615 |
| SMD 75MB | 36,945 | 38,400 | 18,684 | 18,686 | 18,472 |
| Winchester 74MB | 36,396 | 36,360 | 32,771 | 32,769 | 18,198 |

### Block Placement Strategy

**Floppies**: System blocks near the end (`pages - 5`, `pages - 3`, `pages - 1`).

**Hard disks**: Bit file at 50% (`pages / 2`), object/user files at 85% (`floor(pages * 0.85)`).

### Spare Block Overhead

| Controller | Overhead |
|-----------|----------|
| SMD | 3.94% |
| SCSI | 5.93% |
| Winchester | 0% |
| Floppy | 0% |

## Persistence Order

When writing changes to disk, structures must be persisted in this order:
1. Bit File (allocation bitmap)
2. User File
3. Object File

## XAT Sidecar Files

### The Problem

NDFS files carry rich metadata that has no equivalent on modern filesystems: 3-tier access permissions (Own/Friend/Public), file type flags (Indexed, Contiguous, Allocated, etc.), ND-100 timestamps, user ownership, and file type codes (DATA, PROG, SYMB, TEXT). When a file is copied from an NDFS disk image to a Windows, Linux, or macOS filesystem, all of this metadata is lost. If the file is later copied back into an NDFS image, it arrives as a plain DATA file with default permissions -- the original identity is gone.

This is particularly problematic for archival work, where disk images are extracted for analysis and then reconstituted, and for development workflows where source files are edited on a modern system and then transferred back to an ND-100 emulator.

### The Solution: XAT Files

XAT (Extended Attribute) sidecar files preserve NDFS metadata alongside extracted data files. For every file extracted from an NDFS image, a companion `.xat` file is created containing the metadata as JSON. When the file is copied back, the `.xat` file is read and all metadata is restored.

```
NDFS image                Host filesystem
-----------               ---------------
SYSTEM/README:TEXT   -->   README.TEXT        (file data)
                          README.TEXT.xat    (NDFS metadata as JSON)
```

### File Format

XAT files use simple JSON with `ndfs.*` prefixed keys:

```json
{
  "ndfs.object_name": "README",
  "ndfs.type": "TEXT",
  "ndfs.user_name": "SYSTEM",
  "ndfs.user_index": 0,
  "ndfs.access_bits": 1279,
  "ndfs.file_type_flags": 8,
  "ndfs.file_type": 3,
  "ndfs.device_number": 0,
  "ndfs.next_version": 0,
  "ndfs.prev_version": 0,
  "ndfs.pages_in_file": 1,
  "ndfs.bytes_in_file": 1024,
  "ndfs.date_created": 0,
  "ndfs.last_read_date": 0,
  "ndfs.last_write_date": 0
}
```

### Properties

| Key | Type | Description |
|-----|------|-------------|
| `ndfs.object_name` | string | File name (max 16 chars, uppercase) |
| `ndfs.type` | string | File type extension (max 4 chars: DATA, PROG, SYMB, TEXT, MODE, C, etc.) |
| `ndfs.user_name` | string | Owner user name |
| `ndfs.user_index` | integer | Owner user index (0-255) |
| `ndfs.access_bits` | integer | 15-bit permission encoding (see Access Permissions above) |
| `ndfs.file_type_flags` | integer | Bit field: Terminal(0x01), Peripheral(0x02), Spooling(0x04), Indexed(0x08), Contiguous(0x10), Allocated(0x20), MagneticTape(0x40), Library(0x80) |
| `ndfs.file_type` | integer | Type code: 0=DATA, 1=PROG, 2=SYMB, 3=TEXT |
| `ndfs.device_number` | integer | Logical device number (restored on import) |
| `ndfs.next_version` | integer | Next version pointer (recorded only, NOT restored -- references object-table slots that change on import) |
| `ndfs.prev_version` | integer | Previous version pointer (recorded only, NOT restored) |
| `ndfs.pages_in_file` | integer | File size in 2048-byte pages |
| `ndfs.bytes_in_file` | integer | File size in bytes |
| `ndfs.date_created` | integer | Creation date (ND timestamp, see ND Timestamp Format) |
| `ndfs.last_read_date` | integer | Last read date (ND timestamp) |
| `ndfs.last_write_date` | integer | Last write date (ND timestamp) |

### Filename Convention

The XAT filename is the data filename with `.xat` appended:

```
README.TEXT      -->  README.TEXT.xat
PROGRAM.PROG     -->  PROGRAM.PROG.xat
data             -->  data.xat
```

### Round-Trip Workflow

1. **Extract**: Read file from NDFS, write data to host file, serialize metadata to `.xat`
2. **Edit**: Modify the data file on the host system (metadata in `.xat` is untouched)
3. **Import**: Read data file and `.xat` from host, write to NDFS with restored metadata

This preserves the file's identity across the round-trip: its permissions, type, ownership, and dates survive even though the host filesystem cannot represent them natively.
