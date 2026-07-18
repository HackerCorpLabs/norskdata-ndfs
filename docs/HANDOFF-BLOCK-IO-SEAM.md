# Handoff: restructure ndfs-c around a block-IO seam

**Target:** `ndfs-c` (the C library) only. Python/TS ports are out of scope for this task.
**Requested by:** Ronny (repo owner), 2026-07-18.
**Status:** SPEC / not started. This document is the brief for a fresh implementer.

All line numbers below were captured against `ndfs-c/src/filesystem.c` at commit `5e00834`
(2824 lines). Re-verify them before editing — they will drift.

---

## 1. Why

Today the whole disk image lives in one RAM buffer (`fs->data`) and every page access is
pointer arithmetic into it (`read_page(fs,id)` returns `fs->data + id*NDFS_PAGE_SIZE`). That is
fine on a PC but blocks two things the embedded consumer (NDModulE, a Pi Pico RP2350 firmware)
needs:

1. **Big disks.** A 75 MB SMD image does not fit the Pico's 8 MB PSRAM. The library must read
   the image a page at a time from storage, keeping only a handful of pages resident.
2. **Direct media access.** The image is a file on a FAT SD card, reached through
   `f_lseek`/`f_read`/`f_write`. There is no flat pointer to hand the library.

**The proposed factoring (owner's decision):** make a block-IO seam the library's *one* storage
path — NOT an `#ifdef` fork. The library always talks to storage through a small vtable
(`read_block`/`write_block`). Ship **three backends** behind the existing/expanded public API:

```
  +------------------------------------------------------------------+
  |  ndfs core (filesystem.c)                                        |
  |    reads/writes pages ONLY through a page cache                  |
  |    page cache pulls/pushes via  ndfs_block_io { read/write/ctx } |
  +------------------------------------+-----------------------------+
                                       |
        +------------------------------+------------------------------+
        |                              |                              |
  +-----v------+               +-------v------+              +--------v-------+
  | buffer     |               | host-file    |              | embedded block |
  | backend    |               | backend      |              | backend        |
  | (in-RAM,   |               | fopen/fseek/  |              | supplied by    |
  |  today's   |               | fread/fwrite  |              | consumer, e.g. |
  |  behaviour)|               | (NEW, CLI)    |              | ndm_media R/W  |
  +------------+               +--------------+              +----------------+
```

Result: the seam is exercised by the host test-suite on every commit (host-file and buffer
backends both run on the PC), so the embedded path can't silently rot. **Keep the in-RAM buffer
as a backend** — nothing loses a capability (fast host use, `ndfs_to_buffer`, `ndtool` all keep
working).

---

## 2. Hard requirements

1. **No behavioural regression.** The existing Unity tests in `ndfs-c/tests` MUST stay green
   with zero test edits (other than genuinely new tests you add). `make test` is the gate — run
   it after every step. Baseline today: 100% pass, ~100 s.
2. **Public API stays source-compatible.** `ndfs_open_buffer`, `ndfs_open_buffer_copy`,
   `ndfs_close`, `ndfs_to_buffer`, and all read/write/list calls keep their current signatures
   and semantics. `ndfs_open_buffer*` become thin wrappers that install the **buffer backend**.
3. **Add, don't fork.** No second copy of the traversal logic. There is ONE page path; backends
   differ only in how a 2048-byte page is fetched/stored.
4. **`ndtool` and image_creator keep working** unchanged from the caller's side.
5. **Freestanding-friendly.** The core + buffer + block backends must not add new libc
   dependencies beyond what the core already uses (today the only stdio symbol in the core is
   `snprintf`). The **host-file backend may** use `<stdio.h>` (`fopen`…) — it is a PC-only
   backend and is compiled out / not linked on the embedded target. Gate it so an embedded build
   can exclude it (e.g. `NDFS_WITH_STDIO_BACKEND`, default ON for host builds).
6. **Documented.** Update `ndfs-c/README.md` and add a short `docs/BLOCK-IO-SEAM.md` describing
   the backend contract and how to write one. Include a worked example backend.

---

## 3. The seam

### 3.1 Public types (new, in `include/ndfs/filesystem.h` or a new `include/ndfs/block_io.h`)

```c
/* Fetch/store exactly one NDFS page (NDFS_PAGE_SIZE = 2048 bytes).
 * page_id * NDFS_PAGE_SIZE is the byte offset within the image.
 * Return true on success. A false from read_block aborts the operation
 * with NDFS_ERR_IO. write_block is NULL for read-only backends. */
typedef struct ndfs_block_io {
    bool  (*read_block) (void *ctx, uint32_t page_id, uint8_t *out /*2048*/);
    bool  (*write_block)(void *ctx, uint32_t page_id, const uint8_t *in /*2048*/);
    void   *ctx;
} ndfs_block_io;

/* Open over an arbitrary block backend.
 * total_pages = image_size_bytes / NDFS_PAGE_SIZE (whole pages only, as today).
 * read_only forces write_block unused and rejects mutations. */
ndfs_error_t ndfs_open_block(const ndfs_block_io *io, uint32_t total_pages,
                             bool read_only, ndfs_filesystem_t **out_fs);
```

Add an error code `NDFS_ERR_IO` if one does not already exist.

### 3.2 Built-in backends the library ships

- **buffer backend** — wraps a `{uint8_t *data; size_t size; bool owns;}` ctx; `read_block`
  memcpys from `data+off`, `write_block` memcpys into it. `ndfs_open_buffer` /
  `ndfs_open_buffer_copy` are re-expressed as: build this ctx, call the common open path.
  `ndfs_to_buffer` (see §4) reads every page through the seam into a fresh malloc.
- **host-file backend** (NEW, `src/backend_stdio.c`, gated by `NDFS_WITH_STDIO_BACKEND`) —
  ctx holds a `FILE *`; `read_block` = `fseek`+`fread`, `write_block` = `fseek`+`fwrite`.
  Add a convenience opener, e.g.:
  ```c
  ndfs_error_t ndfs_open_file(const char *path, bool read_only,
                              ndfs_filesystem_t **out_fs);   /* host only */
  ```
  Point `ndtool` at this so the CLI streams instead of slurping (optional but ideal — it makes
  the seam the default real-world path).

The **embedded block backend** is NOT shipped here — the consumer (NDModulE) provides it by
filling in `ndfs_block_io` with `ndm_media_read`/`ndm_media_write`. This repo just needs to make
`ndfs_open_block` exist and work.

---

## 4. What actually changes inside `filesystem.c` (audit result)

An audit of all 2824 lines found the seam is **clean and localized** — this is a page-cache
insertion, not a rewrite:

- **`read_page` (line 96) and `write_page_ptr` (line 103) are the ONLY two block-id→pointer
  converters.** Every one of the ~40 `read_page`/`write_page_ptr` call sites goes through them.
  Intercept these two and you intercept 100% of page content access.
- **`fs->data` is touched raw in exactly three lifecycle spots:**
  - assigned in `open_internal` (`fs->data = data;`, ~line 1407),
  - freed in `ndfs_close` (`if (fs->owns_buffer) free(fs->data);`, ~line 1475),
  - whole-image `memcpy` in `ndfs_to_buffer` (~line 2781).
  No `fs->data + offset` arithmetic exists anywhere else.
- **Field decoding is all copy-OUT** (`ndfs_bp_from_bytes`, `*_from_bytes`, `mb_parse` copy into
  value structs). There is **no struct-overlay onto the page buffer**, so field-decode bodies
  need **zero rewrite** — only what `read_page` *returns* changes (a cache slot instead of a
  buffer offset).

### 4.1 The one real hazard: pinned pages

`read_page`/`write_page_ptr` return a pointer the caller keeps using. Callers **pin an outer
page pointer across up to hundreds of thousands of inner `read_page` calls** (e.g. an index page
held while iterating its data pages). Max **3 distinct pages are live simultaneously** (worst
case is the read path `read_subindexed_data` and write path `allocate_and_write_data`: sub-index
+ group-index + data). A naive LRU that evicts a still-referenced slot would return a **dangling
pointer**.

**Design: pin/handle page cache.**

```
  N = 8 slots x 2048 bytes = 16 KiB   (3 for the deepest chain + headroom for
                                       read+write coherence; cheap, safe default)

  read_page(id):     find id in cache -> pin++, return slot.buf
                     else load via read_block into a free/evictable slot,
                     pin++, return slot.buf
  write_page_ptr(id): as read_page but mark slot dirty; write-back via
                     write_block on release / eviction / close
  release:           an explicit unpin at the end of each holding function
```

- Only an **unpinned** slot may be evicted. If all slots are pinned and a new page is needed,
  that is a bug in the caller's release discipline — assert / return NULL (NDFS_ERR_IO).
- **~10 holder functions** get an explicit release added at end-of-scope, behind the same code
  (no `#ifdef` needed once the seam is the only path — but keep them tidy). **Riskiest holders,
  do these first and test after each:**
  `read_subindexed_data` (485), `load_structures` (292), `allocate_and_write_data` (968),
  `ensure_object_dir_page` (693), `write_user_page` (1294). Then the rest of the `read_page`
  callers (see the grep list: lines 300–654 read side, 725–989 write side).
- **Return-type note:** `read_page` returns `const uint8_t *` and `write_page_ptr` returns
  `uint8_t *`. To keep call sites unchanged, the cache slot IS the returned buffer; the pin
  bookkeeping is keyed by page_id, and release is `release_page(fs, id)` (or a handle). Choose
  whichever keeps the ~40 call sites least disturbed — a page_id-keyed pin table is simplest.

### 4.2 `ndfs_to_buffer` and `open_internal`

- `ndfs_to_buffer`: replace the single `memcpy(copy, fs->data, fs->size)` with a loop that
  `read_block`s each of `total_pages` pages into `copy`. Semantics identical; works for every
  backend.
- `open_internal`: today takes `(data, size, owns_buffer)`. Refactor so the common path takes a
  bound `ndfs_block_io` + `total_pages`; the buffer openers construct the buffer backend and
  call it. `load_structures` (which reads the master block, bit file, users, objects via
  `read_page`) works unchanged once `read_page` is cache-backed.

---

## 5. RAM floor note (context for the embedded consumer — informational)

Dropping the whole-image buffer does NOT make mount O(1). `load_structures` still materializes
into RAM: the allocation **bitmap** (`ceil(total_pages/8)` bytes), **all object entries**
(`fs->objects[]`, grown to the real file count, capped `MAX_INTERNAL_OBJECTS = 16384`), and up
to `MAX_INTERNAL_USERS` user entries. That resident metadata — not the 16 KiB page cache — is
the real memory floor, and it is **independent of image size**:

- Floppy (dozens of files): a few KB → fits SRAM.
- SMD/Winchester (many files): up to ~MB → the consumer will `malloc` from PSRAM.

**No action required in this repo** beyond: keep all allocations going through `malloc`/`free`
(the consumer can redirect those to a PSRAM arena). Do NOT add fixed-size static tables sized
for the largest disk.

---

## 6. Suggested step order (test after every step)

```
  1  Add ndfs_block_io + ndfs_open_block skeleton; buffer backend struct.
     Re-express ndfs_open_buffer/_copy on the buffer backend. NO cache yet
     (read_block into a scratch, return-by-value won't work — so do step 2
     together): keep fs->data as the buffer backend's storage for now.
  2  Insert the pin/handle page cache; route read_page/write_page_ptr through
     it. Add release_page calls to the 5 riskiest holders, then the rest.
     >>> make test  after the 5, and again after the rest. <<<
  3  Rewrite ndfs_to_buffer to loop via read_block. make test.
  4  Add the host-file backend (backend_stdio.c) + ndfs_open_file, gated by
     NDFS_WITH_STDIO_BACKEND. Add a test that opens a fixture image by path
     and diffs its parse against the buffer path. make test.
  5  (optional, ideal) point ndtool at ndfs_open_file so the CLI streams.
  6  Docs: docs/BLOCK-IO-SEAM.md + README backend section + worked example.
```

Validation beyond `make test`: open the **same** fixture image via buffer, host-file, and (in a
harness) a mock block backend, and assert byte-identical master-block / dir / file-read results.
That three-way equivalence is the proof the seam is transparent.

---

## 7. Non-goals / leave alone

- Do **not** touch `ndfs-py` or `ndfs-ts` for this task.
- Do **not** change on-disk format, endian handling, or any parse logic.
- Do **not** add a live/write-lock concept here — that belongs to the embedded consumer
  (NDModulE gates writes when a controller is serving a running SINTRAN). The library just needs
  a read-only flag, which it already has.
- Keep `snprintf` as the core's only stdio dependency; confine `fopen`-family to the gated
  host-file backend.

---

## 8. Consumer-side contract (for reference — implemented in the NDModulE repo, not here)

The Pico firmware will bind the seam like this (so you know the shape the embedded backend
takes):

```c
static bool pico_read_block(void *ctx, uint32_t page_id, uint8_t *out) {
    media_ref *m = ctx;
    return ndm_media_read(m->ctrl, m->unit, page_id * NDFS_PAGE_SIZE, out, NDFS_PAGE_SIZE);
}
static bool pico_write_block(void *ctx, uint32_t page_id, const uint8_t *in) {
    media_ref *m = ctx;
    return ndm_media_write(m->ctrl, m->unit, page_id * NDFS_PAGE_SIZE, in, NDFS_PAGE_SIZE);
}
/* ndfs_open_block(&(ndfs_block_io){pico_read_block, pico_write_block, &ref},
 *                 total_pages, read_only, &fs); */
```

Nothing in this contract requires anything from the library beyond §3.1. When §6 is done and
`make test` is green, this handoff is complete.
