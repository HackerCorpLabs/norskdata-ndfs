# Spec: extend XAT sidecar with device_number + version pointers

**Audience:** an implementer (human or LLM) working on the `ndfs-py` and `ndfs-ts`
libraries of this repo.
**Status:** the **C library (`ndfs-c`) reference implementation is already done**
and passing (173 tests). Your job is to **mirror it into Python and TypeScript**,
add the equivalent tests, and update the docs. Keep all three in lock-step (the
repo's core rule).

---

## 1. Goal

The XAT sidecar (`<file>.xat` JSON) preserves NDFS object-entry metadata when a
file is copied to/from a host filesystem. It currently carries **12** of the
object entry's fields. This change adds **3 more keys** so the sidecar records
the file's logical device and its version-chain linkage:

| New JSON key | Source field | Type |
|--------------|--------------|------|
| `ndfs.device_number` | `device_number` (object entry offset 30, uint16 BE) | integer |
| `ndfs.next_version`  | `next_version`  (offset 22, uint16 BE) | integer |
| `ndfs.prev_version`  | `prev_version`  (offset 24, uint16 BE) | integer |

## 2. Decisions (already made — do not relitigate)

1. **Write all 3 keys** in the serializer and read them back in the deserializer.
2. **Restore policy is conservative:**
   - **`device_number` IS applied** on import (it is a logical id, valid in a new
     location).
   - **`next_version` / `prev_version` are recorded but NOT applied** on import.
     They reference object-table slots that are reassigned when a file is
     imported, so restoring them would create a dangling/incorrect version chain.
     They exist in the sidecar for fidelity/diagnostics only.
3. **`ndfs.reserving_user` is OUT of scope.** Do **not** add it. (In RetroFS both
   `UserIndex` and `UserIndexOfReservingUser` read the same byte at offset 34, so
   it looked redundant — but its true SINTRAN semantics are currently unknown and
   disputed. Leave it out until clarified. See §7.)
4. Other RetroFS keys (`header`, `object_index`, `current_open_count`,
   `total_open_count`) are intentionally **not** added: they are volatile or
   allocation-specific and never safely restorable.

## 3. Cross-port parity requirement

All three libraries must end up writing and reading the **exact same JSON keys in
the same order**. The canonical order (from the C reference) places the 3 new
keys **after `ndfs.file_type` and before `ndfs.pages_in_file`**:

```
ndfs.object_name, ndfs.type, ndfs.user_name, ndfs.user_index,
ndfs.access_bits, ndfs.file_type_flags, ndfs.file_type,
ndfs.device_number, ndfs.next_version, ndfs.prev_version,
ndfs.pages_in_file, ndfs.bytes_in_file,
ndfs.date_created, ndfs.last_read_date, ndfs.last_write_date
```

## 4. C reference (already implemented — use as the worked example)

Files changed in `ndfs-c` (read these diffs to mirror exactly):

- `include/ndfs/xat.h` — added `uint16_t device_number, next_version, prev_version`
  to `ndfs_xat_properties_t`; updated the `ndfs_xat_to_object` doc comment.
- `src/xat.c`
  - `ndfs_xat_from_object`: copy all 3 from the entry.
  - `ndfs_xat_serialize`: emit the 3 keys (buffer bumped 1024 → 1536).
  - `ndfs_xat_deserialize`: parse the 3 keys.
  - `ndfs_xat_to_object`: **set `entry->device_number = xat->device_number`** and
    deliberately leave `next_version`/`prev_version` untouched (commented).
- `tests/test_xat.c` — added `test_xat_device_and_version` (registered in
  `run_xat_tests`): verifies (a) from_object captures all 3, (b) JSON round-trip
  preserves all 3, (c) apply restores `device_number` but leaves the entry's
  version pointers unchanged.

## 5. Python changes (`ndfs-py`)

File `src/ndfs/xat.py`:
1. Add the 3 key-name constants alongside the existing `ndfs.*` keys.
2. `object_entry_to_xat(entry)`: add
   `device_number`, `next_version`, `prev_version` from the entry (field names:
   `entry.device_number`, `entry.next_version`, `entry.prev_version` — confirm
   against `object_entry.py`).
3. The JSON serializer: emit the 3 keys in the canonical order (§3).
4. The JSON deserializer / `xat_to_object_entry(...)` (or wherever the property
   bag is applied to an entry): **set `entry.device_number`** from the bag; do
   **not** set `next_version` / `prev_version`.
5. Also check **the filesystem apply path** `NdfsFileSystem.write_file_with_properties`
   (in `src/ndfs/filesystem.py`): it currently applies a subset (access_bits,
   file_type, dates). Add `device_number` there too if it applies fields directly.
   Do **not** apply the version pointers.

## 6. TypeScript changes (`ndfs-ts`)

File `src/xat.ts`:
1. Add the 3 key constants.
2. `objectEntryToXat(entry)`: add `deviceNumber`, `nextVersion`, `prevVersion`
   (confirm field names against `object-entry.ts`) under the 3 new keys.
3. Serializer: emit the 3 keys in canonical order (§3).
4. `xatToObjectEntry(...)` (and `NdfsFileSystem.writeFileWithProperties` in
   `src/ndfs-filesystem.ts`): **restore `deviceNumber`**; do **not** restore the
   version pointers.

## 7. Open question to surface back (do not implement)

`ndfs.reserving_user` / `UserIndexOfReservingUser`: in the RetroFS C# code it
reads the same byte (offset 34) as `user_index`, but the project owner states it
is *not* the same thing and its purpose is unknown. **Do not add it.** If you find
authoritative ND/SINTRAN documentation describing offset 34/35 and a distinct
"reserving user", note it and stop for a decision.

## 8. Tests to add (Python and TypeScript)

Mirror the C `test_xat_device_and_version`:
- A unit test in `tests/test_xat.py` / `tests/xat.test.ts` that:
  1. Builds an object entry with `device_number`, `next_version`, `prev_version`
     set to distinct non-zero values.
  2. Converts to XAT, serializes to JSON, deserializes, and asserts all 3 values
     survive.
  3. Applies the XAT back onto a fresh entry whose version pointers hold sentinel
     values, and asserts: `device_number` was restored, but `next_version` and
     `prev_version` are **unchanged** (proving they are not applied).
- If there are existing tests asserting the full XAT key set or key count, update
  them to include the 3 new keys.

## 9. Docs to update

- `docs/NDFS-FORMAT.md` — the XAT sidecar section: list the 3 new keys and note
  the conservative restore rule (device restored; version pointers recorded only).
- `ndfs-c/tools/ndtool/README.md` — if it enumerates XAT keys, add the 3.
- `docs/NDFS-VALIDATION-PLAN.md` — add a short "fixed/added" entry.

## 10. Build & test (must pass before declaring done)

```bash
# C (reference, already green at 173 tests)
cd ndfs-c && cmake --build build --target ndfs_tests && ./build/ndfs_tests

# Python
cd ndfs-py && PYTHONPATH=src python -m pytest tests/ -v

# TypeScript
cd ndfs-ts && npx tsc --noEmit && npx vitest run
```

Done = all three suites green, identical XAT key set across ports, and the new
round-trip/restore test present in each.
