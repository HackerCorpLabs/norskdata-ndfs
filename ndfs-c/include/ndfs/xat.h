/**
 * XAT (Extended Attribute) sidecar file support for NDFS.
 *
 * Preserves NDFS metadata (access bits, dates, file type flags, user info, etc.)
 * when files are copied to/from host filesystems.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 1985-2026 Ronny Hansen
 * HackerCorp Labs - https://github.com/HackerCorpLabs
 */

#ifndef NDFS_XAT_H
#define NDFS_XAT_H

#include "types.h"
#include "object_entry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** XAT properties structure: holds all NDFS metadata for sidecar files. */
typedef struct {
    char     object_name[NDFS_NAME_MAX + 1];
    char     type[NDFS_TYPE_MAX + 1];
    char     user_name[NDFS_NAME_MAX + 1];
    uint8_t  user_index;
    uint16_t access_bits;
    uint16_t file_type_flags;
    uint8_t  file_type;
    uint32_t pages_in_file;
    uint32_t bytes_in_file;
    uint32_t date_created;
    uint32_t last_read_date;
    uint32_t last_write_date;
    uint16_t device_number;   /* logical device id (restored on import)        */
    uint16_t next_version;    /* version-chain links — recorded, NOT restored  */
    uint16_t prev_version;    /* (they reference object slots that change)      */
} ndfs_xat_properties_t;

/** XAT file extension. */
#define NDFS_XAT_EXTENSION ".xat"

/**
 * Extract XAT properties from an object entry.
 *
 * @param entry  Source object entry.
 * @param out    Receives the XAT properties.
 */
ndfs_error_t ndfs_xat_from_object(const ndfs_object_entry_t *entry,
                                  ndfs_xat_properties_t *out);

/**
 * Apply XAT properties to an object entry.
 * Restores fields that are valid in a new location (access_bits, file_type,
 * names, user_index, device_number). Version pointers and allocation/runtime
 * state are intentionally NOT restored — see ndfs_xat_to_object.
 *
 * @param xat    Source XAT properties.
 * @param entry  Target object entry to modify.
 */
ndfs_error_t ndfs_xat_to_object(const ndfs_xat_properties_t *xat,
                                ndfs_object_entry_t *entry);

/**
 * Serialize XAT properties to a JSON string.
 * Caller must free the returned string with free().
 *
 * @param xat       Source properties.
 * @param out_json  Receives malloc'd null-terminated JSON string.
 */
ndfs_error_t ndfs_xat_serialize(const ndfs_xat_properties_t *xat,
                                char **out_json);

/**
 * Deserialize XAT properties from a JSON string.
 *
 * @param json  Null-terminated JSON string.
 * @param out   Receives the parsed properties.
 */
ndfs_error_t ndfs_xat_deserialize(const char *json,
                                  ndfs_xat_properties_t *out);

/**
 * Get the .xat filename for a given data filename.
 * Appends ".xat" to the filename.
 *
 * @param data_file  Source data filename.
 * @param out        Destination buffer for the .xat filename.
 * @param out_len    Size of destination buffer.
 */
void ndfs_xat_filename(const char *data_file, char *out, size_t out_len);

/**
 * Check if a filename is an XAT sidecar file.
 *
 * @param filename  The filename to check.
 * @return true if filename ends with ".xat".
 */
bool ndfs_xat_is_xat_file(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* NDFS_XAT_H */
