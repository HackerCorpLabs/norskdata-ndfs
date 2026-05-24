/**
 * NDFS (Norsk Data File System) types and constants.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_TYPES_H
#define NDFS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ─────────────────────────────────────────────────── */

typedef int ndfs_error_t;

#define NDFS_OK                  0
#define NDFS_ERR_NULL_PTR       -1
#define NDFS_ERR_INVALID_ARG    -2
#define NDFS_ERR_TOO_SMALL      -3
#define NDFS_ERR_NOT_ALIGNED    -4
#define NDFS_ERR_INVALID_IMAGE  -5
#define NDFS_ERR_OUT_OF_RANGE   -6
#define NDFS_ERR_NOT_FOUND      -7
#define NDFS_ERR_ALREADY_EXISTS -8
#define NDFS_ERR_NO_SPACE       -9
#define NDFS_ERR_READ_ONLY      -10
#define NDFS_ERR_NO_SLOTS       -11
#define NDFS_ERR_HAS_FILES      -12
#define NDFS_ERR_ALLOC          -13
#define NDFS_ERR_CORRUPT        -14

/* ── Constants ───────────────────────────────────────────────────── */

/** Page size in bytes (1024 x 16-bit words). */
#define NDFS_PAGE_SIZE            2048

/** Name terminator character (ASCII single quote). */
#define NDFS_NAME_TERMINATOR      0x27

/** Maximum length of a directory/user/object name. */
#define NDFS_NAME_MAX             16

/** Maximum length of a file type string. */
#define NDFS_TYPE_MAX             4

/** Number of entries (user or object) per page. */
#define NDFS_ENTRIES_PER_PAGE     32

/** Size of a single entry (user or object) in bytes. */
#define NDFS_ENTRY_SIZE           64

/** Maximum number of pointers in the user file index block. */
#define NDFS_MAX_USER_FILE_PTRS   8

/** Maximum number of pointers in an object file index block. */
#define NDFS_MAX_OBJECT_FILE_PTRS 512

/** Maximum number of users. */
#define NDFS_MAX_USERS            256

/** Maximum number of friends per user. */
#define NDFS_MAX_FRIENDS          8

/** Byte offset of the master block within page 0. */
#define NDFS_MASTER_BLOCK_OFFSET  2016

/** Byte offset of the extended info block within page 0. */
#define NDFS_EXTENDED_INFO_OFFSET 2000

/** Size of the master block in bytes. */
#define NDFS_MASTER_BLOCK_SIZE    32

/** Size of the extended info block in bytes. */
#define NDFS_EXTENDED_INFO_SIZE   16

/** First block ID available for allocation (0-6 are system). */
#define NDFS_FIRST_ALLOC_BLOCK    7

/** Valid user entry flag byte. */
#define NDFS_USER_ENTRY_FLAG      0x81

/** Valid object entry header bit (bit 7). */
#define NDFS_OBJECT_IN_USE        0x80

/* ── Enums ───────────────────────────────────────────────────────── */

/** Block pointer type encoded in the top 2 bits. */
typedef enum {
    NDFS_PTR_CONTIGUOUS  = 0,
    NDFS_PTR_INDEXED     = 1,
    NDFS_PTR_SUBINDEXED  = 2,
    NDFS_PTR_RESERVED    = 3
} ndfs_pointer_type_t;

/** Boot code format detected in page 0. */
typedef enum {
    NDFS_BOOT_NONE   = 0,
    NDFS_BOOT_BINARY = 1,
    NDFS_BOOT_BPUN   = 2,
    NDFS_BOOT_FLOMON = 3
} ndfs_boot_format_t;

/** Access level for a user relative to a file. */
typedef enum {
    NDFS_ACCESS_OWN    = 0,
    NDFS_ACCESS_FRIEND = 1,
    NDFS_ACCESS_PUBLIC = 2
} ndfs_file_access_type_t;

/** Checksum validation state for extended info. */
typedef enum {
    NDFS_CHECKSUM_VALID          = 0,
    NDFS_CHECKSUM_VALID_LOW_BYTE = 1,
    NDFS_CHECKSUM_INVALID        = 2
} ndfs_checksum_validation_t;

/** File type flags (bit field). */
typedef enum {
    NDFS_FTYPE_NONE           = 0,
    NDFS_FTYPE_TERMINAL       = 1 << 0,
    NDFS_FTYPE_PERIPHERAL     = 1 << 1,
    NDFS_FTYPE_SPOOLING       = 1 << 2,
    NDFS_FTYPE_INDEXED        = 1 << 3,
    NDFS_FTYPE_CONTIGUOUS     = 1 << 4,
    NDFS_FTYPE_ALLOCATED      = 1 << 5,
    NDFS_FTYPE_MAGNETIC_TAPE  = 1 << 6,
    NDFS_FTYPE_LIBRARY        = 1 << 7
} ndfs_file_type_flags_t;

/** A file/directory entry returned by ndfs_list_directory(). */
typedef struct {
    char     name[NDFS_NAME_MAX + 1];
    char     type[NDFS_TYPE_MAX + 1];
    char     full_name[NDFS_NAME_MAX + NDFS_TYPE_MAX + 2]; /* "NAME:TYPE" */
    char     user_name[NDFS_NAME_MAX + 1];
    uint32_t size;
    uint32_t pages;
    bool     is_directory;
} ndfs_file_entry_t;

#ifdef __cplusplus
}
#endif

#endif /* NDFS_TYPES_H */
