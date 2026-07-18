# The block-IO seam (`ndfs-c`)

**Status:** implemented (buffer + host-file + custom backends, pinned page cache).
**Scope:** `ndfs-c` only. `ndfs-py` / `ndfs-ts` are unchanged and still use their
own resident-buffer model.

This document is the contract for the C library's single storage path. Read it
before writing a backend or touching `read_page`/`write_page_ptr`.

---

## 1. Why a seam

Historically the whole disk image lived in one RAM buffer and every page access
was pointer arithmetic into it. That is fine on a PC but blocks the embedded
consumer (NDModulE, an RP2350 firmware):

- A 75 MB SMD image does not fit the Pico's PSRAM — the library must read the
  image a page at a time and keep only a handful resident.
- The image is a file on a FAT SD card reached through `f_lseek`/`f_read`; there
  is no flat pointer to hand the library.

So the library now talks to storage through **one** vtable — not an `#ifdef`
fork. There is a single page path; backends differ only in how a 2048-byte page
is fetched/stored. The host build ships two backends (in-RAM buffer, host file)
and both run in the test-suite on every commit, so the embedded path cannot
silently rot.

```
  +------------------------------------------------------------------+
  |  ndfs core (filesystem.c)                                        |
  |    reads/writes pages ONLY through a pinned page cache           |
  |    the cache pulls/pushes via  ndfs_block_io { read/write/... }  |
  +------------------------------------+-----------------------------+
                                       |
        +------------------------------+------------------------------+
        |                              |                              |
  +-----v------+               +-------v------+              +--------v-------+
  | buffer     |               | host-file    |              | custom /       |
  | backend    |               | backend      |              | embedded       |
  | (in-RAM)   |               | fopen/fseek/  |              | (consumer      |
  | open_buffer|               | fread/fwrite  |              |  supplied)     |
  +------------+               +--------------+              +----------------+
```

---

## 2. The contract (`include/ndfs/block_io.h`)

```c
typedef struct ndfs_block_io {
    bool (*read_block) (void *ctx, uint32_t page_id, uint8_t *out /* 2048 */);
    bool (*write_block)(void *ctx, uint32_t page_id, const uint8_t *in /* 2048 */);
    void (*destroy)    (void *ctx);
    void  *ctx;
} ndfs_block_io;

ndfs_error_t ndfs_open_block(const ndfs_block_io *io, uint32_t total_pages,
                             bool read_only, ndfs_filesystem_t **out_fs);
```

Rules a backend must obey:

- **One page is `NDFS_PAGE_SIZE` (2048) bytes.** The byte offset of page *N* is
  `N * NDFS_PAGE_SIZE`. Everything is whole pages.
- **`read_block`** fills `out` with the 2048 bytes of `page_id` and returns
  `true`. A `false` aborts the current operation with `NDFS_ERR_IO`.
- **`write_block`** stores 2048 bytes as `page_id` and returns `true`. It may be
  **`NULL`** for a read-only backend; the library then rejects every mutation.
- **`destroy`** (optional) is called exactly once from `ndfs_close()` — and on a
  failed open — to release `ctx`. Leave it **`NULL`** when the caller owns `ctx`
  and does not want the library to free it (the usual embedded case).
- `total_pages = image_size_bytes / NDFS_PAGE_SIZE` (whole pages only).
- The vtable is copied by value into the handle; `ctx` and the function pointers
  must stay valid for the handle's lifetime.

`page_id` is always `< total_pages` when the library calls you (it range-checks
first), but a defensive bounds check in the backend is cheap and recommended.

---

## 3. The page cache and *why pinning exists*

`read_page(fs, id)` / `write_page_ptr(fs, id)` hand out a **pointer into a cache
slot**, and callers keep using that pointer. With the whole image no longer
resident there are only `NDFS_CACHE_SLOTS` (8) slots — 16 KiB — so a slot can be
reused for another page while a caller still holds a pointer into it. That would
be a dangling pointer.

Two mechanisms keep pointers valid:

1. **LRU eviction only ever picks an *unpinned* slot.** A raw pointer deref
   (`ndfs_bp_from_bytes(page, …)`) does **not** refresh the slot's LRU stamp, so
   a held pointer survives only while fewer than 7 *other* pages are touched in
   between.
2. **Any pointer held across an unbounded loop of page loads is pinned** with
   `cache_pin(fs, id)` / `cache_unpin(fs, id)` so LRU can never evict it. The
   deepest real chain holds **3** pages live at once (sub-index + group-index +
   data); at most **2** are ever pinned simultaneously, so 8 slots is generous.

The cache is **keyed by `page_id`** (a page occupies at most one slot), so a
read and a write of the same page alias the *same* slot — preserving the in-place
mutation the write path relies on. Writes are **write-back**: `write_page_ptr`
marks the slot dirty; the dirty page is pushed to the backend via `write_block`
on eviction, on unpin, and on `ndfs_close`.

> **Durability note.** Because writes are write-back, a writable handle must be
> closed (`ndfs_close`) to guarantee all dirty pages reach the backend. Same-handle
> reads and `ndfs_to_buffer` are always coherent (they see the dirty cache), so
> only cross-handle persistence depends on the close-time flush.

### Which functions pin (audit)

Only functions that hold an outer page pointer across inner page reads pin. As
of this writing:

| Function | Pinned page(s) |
|----------|----------------|
| `load_structures` (user file) | user index page |
| `load_structures` (object Indexed) | object index page |
| `load_structures` (object SubIndexed) | sub-index + group-index |
| `read_indexed_data` | index page |
| `read_subindexed_data` | sub-index + group-index |
| `free_file_blocks` (SubIndexed) | sub-index page |
| `count_real_data_pages_in_object` (SubIndexed) | sub-index page |
| `ndfs_get_file_blocks` (SubIndexed) | sub-index page |
| `allocate_and_write_data` (Indexed) | index page |
| `allocate_and_write_data` (SubIndexed) | sub-index + group-index |

Short-lived holders (e.g. `ensure_object_dir_page`, which re-reads a page across
a single allocation) need no pin — they stay within the 7-intervening-access
headroom of rule 1. If you add a new loop that holds an outer page pointer across
inner `read_page`/`write_page_ptr` calls, **pin it**.

---

## 4. Backends the library ships

- **Buffer backend** (`filesystem.c`). Wraps `{data, size, owns}`; `read_block`
  and `write_block` are `memcpy`. `ndfs_open_buffer` installs it with
  `owns = false` (caller keeps the buffer); `ndfs_open_buffer_copy` copies first
  and sets `owns = true`. Signatures and semantics are unchanged from before the
  seam — existing callers (including `ndtool`) need no changes.
- **Host-file backend** (`src/backend_stdio.c`, gated by
  `NDFS_WITH_STDIO_BACKEND`, default ON for host builds). `ctx` holds a `FILE*`;
  `read_block`/`write_block` are `fseek`+`fread`/`fwrite`, each write `fflush`ed.
  Opener: `ndfs_open_file(path, read_only, &fs)` (`rb` for read-only, `r+b`
  otherwise — the image must already exist).

The **embedded backend is not shipped here** — the consumer fills `ndfs_block_io`
with its own media routines and calls `ndfs_open_block`.

---

## 5. Worked example — a custom backend

```c
#include <ndfs/ndfs.h>

/* Consumer media handle. */
typedef struct { my_media *m; uint32_t total_pages; } media_ref;

static bool media_read_block(void *ctx, uint32_t page_id, uint8_t *out) {
    media_ref *r = ctx;
    if (page_id >= r->total_pages) return false;
    return my_media_read(r->m, (uint64_t)page_id * NDFS_PAGE_SIZE, out, NDFS_PAGE_SIZE);
}

static bool media_write_block(void *ctx, uint32_t page_id, const uint8_t *in) {
    media_ref *r = ctx;
    if (page_id >= r->total_pages) return false;
    return my_media_write(r->m, (uint64_t)page_id * NDFS_PAGE_SIZE, in, NDFS_PAGE_SIZE);
}

void mount_and_list(my_media *m, uint32_t total_pages) {
    media_ref ref = { m, total_pages };
    ndfs_block_io io = {
        .read_block  = media_read_block,
        .write_block = media_write_block,   /* NULL for read-only media */
        .destroy     = NULL,                /* we own `ref`; don't free it */
        .ctx         = &ref,
    };
    ndfs_filesystem_t *fs = NULL;
    if (ndfs_open_block(&io, total_pages, /*read_only=*/false, &fs) != NDFS_OK) return;

    /* ... use the normal ndfs_* API ... */

    ndfs_close(fs);   /* flushes dirty pages through media_write_block */
}
```

The RP2350 firmware binds the seam exactly this way, with `my_media_read/write`
replaced by `ndm_media_read/write`.

---

## 6. Guarantees & non-goals

- **No behavioural regression.** All pre-existing unit tests pass unchanged; the
  seam adds `tests/test_backend_stdio.c`, which opens the same image via buffer,
  host-file, and a mock block backend and asserts byte-identical results.
- **Source-compatible API.** `ndfs_open_buffer(_copy)`, `ndfs_close`,
  `ndfs_to_buffer`, and every read/write/list call keep their signatures. The
  `ndtool` CLI is **unmodified**.
- **Freestanding-friendly.** `snprintf` remains the core's only stdio symbol;
  the `fopen` family is confined to the gated host-file backend.
- **Not here:** no on-disk format / endian / parse changes; no live/write-lock
  (that belongs to the embedded consumer — the library only has a read-only
  flag); py/ts ports untouched.
