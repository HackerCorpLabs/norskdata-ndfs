# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Three standalone libraries — C99 (`ndfs-c`), Python (`ndfs-py`), TypeScript (`ndfs-ts`) — that read and write NDFS (Nord Disk File System) disk images from Norsk Data minicomputers running Sintran-III. Plus `ndtool`, a CLI built on the C library. The three libraries are **deliberate parallel ports of the same API and on-disk logic** — they are kept in lock-step.

## The most important fact about this repo

**The behavioral authority is RetroCore's C# NDFS** (`RetroCore/Emulated.Utilities/ND/FileSystem`, a.k.a. RetroCommander) — the golden reference these three libraries must match (see `docs/NDFS-VALIDATION-PLAN.md`). Among the three ports here, **`ndfs-c` is the lead**: Python and TypeScript mirror it, and almost every fix lands as `c/py`, `py/ts`, or `ts/py` — the same bug corrected across implementations in one or adjacent commits. When you change on-disk behavior in one language, the equivalent change is expected in the other two, plus a regression test in each. When the ports disagree, verify against RetroCore's behavior and `docs/NDFS-FORMAT.md` — do not assume. Note RetroCore's write model: **writes are immediate and surgical** — a mutation rewrites only the block(s) it touched, never the whole filesystem.

The file/module layout is intentionally parallel across all three (`master_block`, `block_pointer`, `bit_file`, `user_entry`, `object_entry`, `object_file`/`user_file`, `filesystem`, `image_creator`, `boot_loader`, `parity`, `xat`, `wildmatch`, `ndfs_name`). Test files mirror each other too (`test_golden`, `test_write_*`, `sparse-files`, `stress`, etc.). When porting a fix, the corresponding file and test almost always already exist under the same name.

`docs/NDFS-FORMAT.md` is the authoritative binary-format spec. Treat it as the contract all three implementations satisfy. `docs/NDFS-VALIDATION-PLAN.md` tracks cross-implementation validation status.

## Build & test

From the repo root (`make help` lists all targets):

```bash
make            # build ndtool CLI (Release) via CMake
make test       # build + run the C unit tests (alias for test-c)
make test-ts    # ndfs-ts: npm install && npm test
make test-py    # ndfs-py: PYTHONPATH=src python -m pytest tests/ -v
make format     # clang-format over C sources
```

Per-implementation:

```bash
# C library + ndtool + tests
cd ndfs-c && mkdir build && cd build && cmake .. && make && ./ndfs_tests
ctest --test-dir build --output-on-failure          # run via ctest

# Python (note: PYTHONPATH=src is required)
cd ndfs-py && PYTHONPATH=src python -m pytest tests/ -v
cd ndfs-py && PYTHONPATH=src python -m pytest tests/test_golden.py -v       # single file
cd ndfs-py && PYTHONPATH=src python -m pytest tests/test_golden.py::test_name   # single test

# TypeScript (vitest)
cd ndfs-ts && npm install && npm test
cd ndfs-ts && npx vitest run tests/golden.test.ts                          # single file
```

There are **no external dependencies** in any of the three libraries — keep it that way.

## Core on-disk model (shared by all three)

- **Page** = 2048 bytes (1024 × 16-bit words). Everything is **big-endian**. Strings are terminated by a single `0x27` (`'`) byte followed by NULs — *not* a field padded with terminators (this distinction has caused real SINTRAN-visibility bugs).
- **Page 0** holds the boot sector, an Extended Info Block (hard disks only, with a checksum), and the 32-byte **Master Block** at offset `0x07E0`. The master block has three `BlockPointer`s: Object File, User File, Bit File.
- **BlockPointer** (4 bytes): top 2 bits = type (`00` contiguous, `01` indexed, `10` sub-indexed, `11` reserved/invalid), low 30 bits = block ID (page LBA). Indexed = index block of up to 512 data-page pointers (≤512 data pages/file). Sub-indexed adds one level. `blockId == 0` inside an index block = sparse hole (reads as zeros).
- **User entries** (64 bytes, 32/page, max 256 users): the owning user is identified by the **User Index byte at offset 37**, not physical position. A user lives at slot `userIndex % 32` in page `userIndex / 32`.
- **Object (file) entries** (64 bytes): `Object Index` (offset 34) = `(userIndex << 8) | slot` — the **high byte is the owning user**. `Bytes-in-file` (offset 56) stores `actual_size − 1`. A single-version file is **self-referential** (Next = Prev = its own object index); zeroed version pointers make SINTRAN report a broken chain.
- **Per-user object-file partitioning (critical, source of several past bugs):** the object file is partitioned by user. User *U* owns object slots `U*256 .. U*256+255`, i.e. index-block pointer slots `U*8 .. U*8+7`. SINTRAN derives ownership from physical position, so **a new file must be written into its owner's region**, never the first globally-free slot. Max 256 files/user.
- **Bit file** = one allocation bit per page (`byte = blockId/8`, `bit = blockId%8`). Blocks 0–6 are reserved system blocks and must never be allocated for data.

## ND-100 even parity

Text files store **calculated even parity** (bit 7 set so each byte has an even number of 1-bits) — not mark parity. Applies to text types (`:MODE :SYMB :TEXT :C :BATC :FORT :PLAN` …), not to binary types (`:PROG :BPUN :DATA :VTM`). Read APIs take a `strip`/`set` mode. See `parity.*` in each implementation and the README's worked examples.

## XAT sidecar files

NDFS metadata (3-tier permissions, file-type flags, ND timestamps, ownership) has no host-FS equivalent, so extraction emits a `<file>.xat` JSON sidecar that restores all metadata on copy-back. See `xat.*` and `docs/NDFS-FORMAT.md#xat-sidecar-files`.

## Conventions (from the user's global instructions — these are hard rules)

- **C# rules do not apply here** (no C# in this repo), but the no-assumptions discipline does: do not present guesses as facts; verify against `docs/NDFS-FORMAT.md` or the C reference.
- **Always actually run the tests** before reporting success — never assume they pass. If you cannot run them, say so and ask.
- **Never create standalone/throwaway test programs.** Add cases to the existing unit-test suites and clean up after yourself.
- **Never mention Claude/AI in git commit messages.** Match the existing commit style: `area: short imperative summary`, often prefixed with the touched implementations, e.g. `c/py:`, `ts/py:`, `ndfs:`, `ndtool:`, `docs:`.
- On Windows this is a PowerShell environment; `cd e:\path` does not change drive on its own.
- When you create or modify a Markdown doc, give the user its full absolute path.
