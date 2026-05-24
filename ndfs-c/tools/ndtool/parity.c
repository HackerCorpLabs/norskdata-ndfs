/**
 * ndtool parity: now delegates to libndfs parity functions.
 * This file is kept for build compatibility but contains no code.
 *
 * SPDX-License-Identifier: MIT
 */

#include "parity.h"

/* All parity functions are now in libndfs (src/parity.c).
 * The macros in parity.h redirect ndtool_* to ndfs_* functions.
 * This file is intentionally empty. */
