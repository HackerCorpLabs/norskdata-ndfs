/**
 * ndtool parity helpers: thin wrappers around libndfs parity functions.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NDTOOL_PARITY_H
#define NDTOOL_PARITY_H

#include <ndfs/parity.h>

/* Use library functions directly -- no ndtool-specific copies needed. */
#define ndtool_strip_parity  ndfs_strip_parity
#define ndtool_set_parity    ndfs_set_parity
#define ndtool_is_text_type  ndfs_is_text_type

#endif /* NDTOOL_PARITY_H */
