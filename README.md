# norskdata-ndfs

[![Release builds](https://github.com/HackerCorpLabs/norskdata-ndfs/actions/workflows/release.yml/badge.svg)](https://github.com/HackerCorpLabs/norskdata-ndfs/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/HackerCorpLabs/norskdata-ndfs?sort=semver)](https://github.com/HackerCorpLabs/norskdata-ndfs/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Read and write NDFS (Norsk Data File System) disk images from Sintran-III minicomputers.

Three standalone libraries with identical APIs, plus a CLI tool:

| Library | Language | Tests | Install |
|---------|----------|-------|---------|
| [ndfs-ts](ndfs-ts/) | TypeScript | 280 | `npm install norskdata-ndfs` |
| [ndfs-py](ndfs-py/) | Python | 320 | `pip install norskdata-ndfs` |
| [ndfs-c](ndfs-c/) | C99 | 159 | `cmake .. && make` |
| [ndtool](ndfs-c/tools/ndtool/README.md) | CLI (C) | -- | Built with libndfs |

**Total: 759 tests. No external dependencies in any library.**

## What It Does

- **Read and write files** to/from NDFS disk images (contiguous, indexed, sub-indexed allocation with sparse hole support)
- **Create new images** from templates (360KB floppy, 1.2MB floppy, 75MB SMD, 74MB Winchester, custom)
- **User management** (add, remove, quota with addquota/remquota, password clearing, max 256 users)
- **ND-100 even parity** (strip on read, set on write -- proper calculated parity per byte, not mark parity)
- **XAT sidecar files** for preserving NDFS metadata (permissions, file types, dates) when copying to/from host filesystems
- **Boot loader detection** (BPUN, FLOMON, Binary formats)
- **Filesystem check** (fsck: bitmap consistency, orphaned blocks, cross-links, quota verification)
- **Interactive shell** (ndtool --shell: ls, cat, hexdump, edit, get, put, rm, mv, bitmap, fsck, stat, users, save)

## Quick Examples

### TypeScript

```typescript
import { NdfsFileSystem, ImageTemplate } from 'norskdata-ndfs';

// Read a text file with parity stripped
const ndfs = new NdfsFileSystem(imageData);
const text = ndfs.readFile('SYSTEM/STARTUP:MODE', 'strip');

// Write with ND-100 even parity
ndfs.writeFile('RONNY/SOURCE:C', sourceBytes, 'set');
```

### Python

```python
from ndfs import NdfsFileSystem, ImageTemplate, ImageCreationOptions

# Create a new floppy image
ndfs = NdfsFileSystem.create_image(ImageCreationOptions(
    template=ImageTemplate.Floppy360KB,
    directory_name='MY-DISK',
))

# Write with parity, read with parity stripped
ndfs.write_file('SYSTEM/HELLO:TEXT', b'Hello!', parity='set')
text = ndfs.read_file('SYSTEM/HELLO:TEXT', parity='strip')
```

### C

```c
#include <ndfs/ndfs.h>

ndfs_filesystem_t *fs = NULL;
ndfs_image_options_t opts;
ndfs_image_options_init(&opts);
opts.template_type = NDFS_TMPL_FLOPPY_360KB;
ndfs_create_image(&fs, &opts);

// Write with parity
ndfs_write_file_parity(fs, "SYSTEM/HELLO:TEXT",
    (const uint8_t *)"Hello!", 6, NDFS_PARITY_SET);

// Read with parity stripped
uint8_t *data; size_t size;
ndfs_read_file_parity(fs, "SYSTEM/HELLO:TEXT",
    NDFS_PARITY_STRIP, &data, &size);
```

### ndtool CLI

```bash
ndtool -t disk.ndfs                          # List all files
ndtool -i -v disk.ndfs                       # Info with bitmap visualization
ndtool --fsck disk.ndfs                      # Full filesystem check
ndtool -x -p -d -l -o output/ disk.ndfs     # Extract all, strip parity, lowercase
ndtool -p --put source.c RONNY/SOURCE:C disk.ndfs   # Copy in with parity
ndtool --create floppy360 --name MYDISK new.ndfs     # Create new image
ndtool --shell disk.ndfs                     # Interactive mode
```

See the [ndtool README](ndfs-c/tools/ndtool/README.md) for the full command reference.

## ND-100 Even Parity

The ND-100 stores text files with even parity. Bit 7 of each byte is set so the total number of 1-bits is even:

```
'H' = 0x48 (01001000) -> 2 ones (even) -> bit 7 = 0 -> 0x48
' ' = 0x20 (00100000) -> 1 one  (odd)  -> bit 7 = 1 -> 0xA0
'W' = 0x57 (01010111) -> 5 ones (odd)  -> bit 7 = 1 -> 0xD7
```

Applies to text types: :MODE, :SYMB, :TEXT, :C, :BATC, :FORT, :PLAN, etc.
Does **not** apply to binary types: :PROG, :BPUN, :DATA, :VTM.

## XAT Sidecar Files

NDFS files carry rich metadata (3-tier permissions, file type flags, ND timestamps, user ownership) that has no equivalent on modern filesystems. XAT sidecar files preserve this metadata as JSON when files are extracted:

```
SYSTEM/README:TEXT  -->  README.TEXT      (file data)
                        README.TEXT.xat  (NDFS metadata as JSON)
```

When copying back, the `.xat` file restores all metadata. Essential for archival round-trips and ND-100 development workflows. See the [format spec](docs/NDFS-FORMAT.md#xat-sidecar-files) for details.

## On-Disk Format

See [NDFS-FORMAT.md](docs/NDFS-FORMAT.md) for the complete binary format specification covering master blocks, block pointers, user/object entries, allocation bitmaps, boot sectors, and XAT sidecar files.

## Building

```bash
# TypeScript
cd ndfs-ts && npm install && npm test

# Python
cd ndfs-py && PYTHONPATH=src python -m pytest tests/ -v

# C library + ndtool
cd ndfs-c && mkdir build && cd build && cmake .. && make && ./ndfs_tests
```

Or, from the repository root, use the convenience Makefile:

```bash
make            # build the ndtool CLI (Release)
make release    # build ndtool, statically linked where supported
make test       # build + run the C unit tests
make help       # list all targets
```

## Downloads

Pre-built `ndtool` binaries for **Windows (x64)**, **Linux (x64 / arm64)**, and
**macOS (Apple Silicon)** are attached to every
[GitHub Release](https://github.com/HackerCorpLabs/norskdata-ndfs/releases/latest).
Each archive contains a single self-contained binary with no external runtime
dependencies. Releases are built automatically by the
[release workflow](.github/workflows/release.yml) when a `v*` tag is pushed.

## License

MIT - Copyright (c) 1985-2026 Ronny Hansen, [HackerCorp Labs](https://github.com/HackerCorpLabs)
