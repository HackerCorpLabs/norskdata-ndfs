# Spec: SINTRAN III initial-command buffer (read / write / repair)

**Audience:** an implementer (human or LLM) working on the `ndfs-c`, `ndfs-py`,
and `ndfs-ts` libraries of this repo.
**Status:** implemented and passing in **all three** libraries (C is the lead;
Python and TypeScript mirror it). Behavioural authority is RetroFS.NDFS /
RetroCore, both live-verified on real SINTRAN K03 / L07 / M06 packs.
**Cross-reference:** the full derivation + on-disk evidence lives in the NDInsight
note `SINTRAN/OS/26-INITIAL-COMMAND-BUFFER-ON-DISK.md`. This file is the repo-local
implementer spec.

Notation: **words are 16-bit, big-endian.** 1 page = 1024 words = 2048 bytes.
Octal is written `NNNNN₈`.

---

## 1. What the buffer is

The *initial commands* are the list SINTRAN executes first at every restart
(`@LIST-INITIAL-COMMANDS`), before the disk mode files. They are set with
`@INITIAL-COMMAND <cmd>` (first entry, must be `@ENTER-DIRECTORY`) and appended
with `@NEXT-INITIAL-COMMAND <cmd>`.

The buffer **has no file form.** It lives at the kernel symbol **`INIBU`** inside
the SINTRAN *command segment*, whose memory image is checkpointed into the
ordinary NDFS object file **`(SYSTEM)SEGFIL0:DATA`** (some packs: `SEGFIL:DATA`).
So it is read/written by reading `SEGFIL0`, locating `INIBU` inside it, and
patching a fixed 131-word region — no new addressing scheme.

> Because the buffer is part of the checkpointed command segment, a *running*
> SINTRAN may overwrite an offline edit when it next checkpoints. **Edit offline
> images only while nothing is booted from them.**

## 2. Locating the buffer (no byte-searching)

### 2.1 Anchor symbol `INIBU` (per version)

| Version | `INIBU` (octal) | hex |
|---|---|---|
| K03 | `067172₈` | `0x6E7A` |
| L07 | `074123₈` | `0x7853` |
| M06 | `102327₈` | `0x84D7` |

### 2.2 Region layout (identical in all three versions)

| Words | Content |
|---|---|
| `INIBU + 0 … +129` | command text (260 bytes) |
| `INIBU + 130` | **length cell** — byte count of the text (excl. terminator) |
| `INIBU + 131` | `INCOM` — executable code. **Never write here.** |

Constants: `CAP_BYTES = 262`, `LEN_CELL_BYTES = 260`, `MAX_TEXT_BYTES = 254`
(the limit the `NEXIN` handler enforces: byte offset ≤ 252 ⇒ length ≤ 254).

### 2.3 Algorithm

1. Read `(SYSTEM)SEGFIL0:DATA` (ordinary `read_file`). All offsets below are
   **relative to the start of this file**.
2. Find the **segment-table page**: the page where entry 0 is all-zero, that has
   ≥ 40 plausible entries, and is **self-describing** (some entry's `MADR` equals
   the page number, `SEGLE ≤ 32`). Segment-table entry = 8 words; the fields used
   are `LOGAD` (word 2), `SEGLE` (word 3, low 10 bits), `MADR` (word 4),
   `FLAG` (word 5, bit0 = `5OK`), `SGSTA` (word 6).
3. For the segment whose virtual range contains `INIBU`
   (`segBase = (LOGAD & 0x3F) * 1024`, `segBase ≤ INIBU < segBase + SEGLE*1024`):
   `byteOffset = MADR*2048 + (INIBU - segBase)*2`.
4. `textLen = wordBE(seg, byteOffset + 260)` — the length cell.

### 2.4 Encoding (the wire format)

```
command  := <7-bit ASCII text> 0x27 [0x00]   ; "'" (0x27) terminates; NUL pads to a word
buffer   := command* 0x27 0x00...            ; a LONE 0x27 (empty command) ends the buffer
```

- Text is plain 7-bit ASCII (high bit clear); strip parity (`& 0x7F`) on read.
- After each `'`, a single `0x00` is inserted **only** to reach an even offset.
- The **length cell** (`INIBU+130`) is authoritative — SINTRAN reads exactly that
  many bytes. **A writer that forgets it is a silent no-op.**

## 3. Self-consistency (the false-positive guard) — REQUIRED

Several segments' virtual ranges contain `INIBU` (save/image copies share the low
`LOGAD` bits), and on a header-corrupted pack a garbage code word can have a small
plausible length cell and printable leading bytes. Selecting merely "the first
segment that parses ≥ 1 command" therefore picks garbage (observed: a bogus `B,`
match on a real corrupted pack — and a *write* there would patch SINTRAN code).

**The locator MUST require self-consistency:** accept a segment only when the
stored length cell equals `encode(parsedCommands).textLen`
(`reencode_matches`). A real buffer round-trips exactly; a coincidental parse does
not. On a healthy pack this selects the running command segment; on a corrupted
one it correctly reports "not found".

## 4. Public API (all three libraries, lock-step)

The pure helpers operate on a `SEGFIL0` byte buffer (unit-testable); the
filesystem-level operations read `SEGFIL0` and patch it in place.

| Concern | ndfs-c | ndfs-py | ndfs-ts |
|---|---|---|---|
| module | `sintran.h` / `sintran.c` | `ndfs/sintran.py` | `sintran.ts` |
| find seg-table page | `ndfs_sintran_find_segment_table_page` | `find_segment_table_page` | `findSegmentTablePage` |
| parse | `ndfs_sintran_parse_buffer` | `parse_buffer` | `parseBuffer` |
| encode | `ndfs_sintran_encode_buffer` | `encode_buffer` | `encodeBuffer` |
| self-consistency | `ndfs_sintran_reencode_matches` | `reencode_matches` | `reencodeMatches` |
| locate | (internal) | `locate` | `locate` |
| repair target | (internal) | `locate_repair_target` | `locateRepairTarget` |
| candidates | `ndfs_diagnose_initial_commands` | `enumerate_candidates` | `enumerateCandidates` |
| **read** | `ndfs_read_initial_commands(fs, out)` | `fs.read_initial_commands()` | `fs.readInitialCommands()` |
| **write** | `ndfs_write_initial_commands(fs, cmds, n)` | `fs.write_initial_commands(cmds)` | `fs.writeInitialCommands(cmds)` |
| **repair** | `ndfs_repair_initial_commands(fs, …)` | `fs.repair_initial_commands(cmds)` | `fs.repairInitialCommands(cmds)` |
| in-place patch | `ndfs_patch_file_region(fs, path, off, data, len)` | `fs.patch_file_region(path, off, data)` | `fs.patchFileRegion(path, off, data)` |

`read` returns the decoded commands + location (or not-found). `write` replaces
the whole buffer and rewrites the length cell. Both are **in-place**: the fixed
131-word region is patched via the file's own block chain, so a large contiguous
`SEGFIL0` is never re-allocated and nothing else in the image moves.

### 4.1 Write procedure

```
1. locate byteOffset  (§2)                     # write: self-consistent; repair: keyword-scored
2. bytes, textLen = encode(cmds)               # textLen EXCLUDES the terminator; assert ≤ 254
3. region = zero(262); region[0:len(bytes)] = bytes; region[260:262] = textLen (BE)
4. patch_file_region(SEGFIL0, byteOffset, region)   # touches only the buffer page(s)
```

## 5. Repair (header-corrupted buffer)

When the length cell / leading bytes are clobbered (e.g. an interrupted
`@INITIAL-COMMAND` that reset the buffer to length 2 / a leading `'`), the normal
self-consistent locator correctly returns "not found". `repair_initial_commands`
finds the real (still-readable) buffer by **scoring surviving command keywords**
(`DIRECTORY`, `DISC-`, `SET-AVAIL`, `APPEND-BATCH`, `LOAD-MODE`, `HENT-MODE`,
`CONNECT`, `ENTER-DIR`, `SYSTEM-OUTPUT`) in each INIBU-containing segment, and
writes the good command set there **only when there is a single unambiguous
winner** (score ≥ 2 and strictly above the runner-up). Otherwise it refuses,
rather than risk patching SINTRAN code.

## 6. Persistence model

The libraries do **no file I/O** — they mutate the in-memory image and the caller
persists via `to_buffer` / `toBuffer` / `ndfs_to_buffer`. The write/repair helpers
update the in-memory image in place (via `patch_file_region`), exactly like every
other mutating operation in this repo; there is **no separate flush step** to
forget (unlike RetroFS's whole-image-buffered block device, where the analogous
password-clear had to add an explicit flush — not applicable here).

## 7. Tests

Each library has `test_sintran` mirroring the others: parser (pad / parity / lone
terminator / non-printable), encoder (textLen 54 / 74, uppercasing, 254-vs-256
limit, embedded-quote rejection, round trip), the self-consistency guard
(`reencode_matches(["B,"], 2) == false`), a **synthetic segment-table locate**,
and an end-to-end **cross-page `patch_file_region`** round trip. The full
read/write against a real multi-MB SINTRAN pack is validated in RetroFS.NDFS.

## 8. Golden values (real L07 pack, from NDInsight doc 26)

`SMD0-L.IMG`: segment-table page 1275, command segment 3, `MADR` 1408,
`byteOffset` `0x2C90A6` (2 920 614), `textLen` 54 for
`ENTER-DIR,,DI-75-1,0` · `ENTER-DIR,,DI-74-1,0` · `SET-AVAIL`; adding
`CC OFFLINE WRITE OK` gives `textLen` 74 (boots and lists correctly).
