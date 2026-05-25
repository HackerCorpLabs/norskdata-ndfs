/**
 * Tests for portable wildcard matching.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_framework.h"
#include <ndfs/ndfs.h>

static int test_exact_match(void)
{
    TEST_ASSERT(ndfs_wildmatch("STARTUP:MODE", "STARTUP:MODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("STARTUP:MODE", "STARTUP:SYMB", false) == false);
    return 0;
}

static int test_star_suffix(void)
{
    TEST_ASSERT(ndfs_wildmatch("*:MODE", "STARTUP:MODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("*:MODE", "A:MODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("*:MODE", ":MODE", false) == true);   /* * matches empty */
    TEST_ASSERT(ndfs_wildmatch("*:MODE", "X:SYMB", false) == false);
    return 0;
}

static int test_star_prefix_and_mid(void)
{
    TEST_ASSERT(ndfs_wildmatch("STARTUP*", "STARTUP:MODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("ST*MODE", "STARTUP:MODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("ST*MODE", "STMODE", false) == true);
    TEST_ASSERT(ndfs_wildmatch("ST*X", "STARTUP:MODE", false) == false);
    return 0;
}

static int test_only_star(void)
{
    TEST_ASSERT(ndfs_wildmatch("*", "ANYTHING", false) == true);
    TEST_ASSERT(ndfs_wildmatch("*", "", false) == true);
    TEST_ASSERT(ndfs_wildmatch("**", "abc", false) == true);  /* consecutive stars */
    return 0;
}

static int test_question(void)
{
    TEST_ASSERT(ndfs_wildmatch("FILE?:C", "FILE1:C", false) == true);
    TEST_ASSERT(ndfs_wildmatch("FILE?:C", "FILE:C", false) == false); /* ? needs one char */
    TEST_ASSERT(ndfs_wildmatch("???", "ABC", false) == true);
    TEST_ASSERT(ndfs_wildmatch("???", "AB", false) == false);
    return 0;
}

static int test_case_insensitive(void)
{
    TEST_ASSERT(ndfs_wildmatch("system/*", "SYSTEM/X", true) == true);
    TEST_ASSERT(ndfs_wildmatch("*:mode", "STARTUP:MODE", true) == true);
    TEST_ASSERT(ndfs_wildmatch("*:mode", "STARTUP:MODE", false) == false);
    return 0;
}

static int test_empty_pattern(void)
{
    TEST_ASSERT(ndfs_wildmatch("", "", false) == true);
    TEST_ASSERT(ndfs_wildmatch("", "x", false) == false);
    return 0;
}

static int test_null_safe(void)
{
    TEST_ASSERT(ndfs_wildmatch(NULL, "x", false) == false);
    TEST_ASSERT(ndfs_wildmatch("x", NULL, false) == false);
    TEST_ASSERT(ndfs_wildmatch(NULL, NULL, false) == false);
    return 0;
}

static int test_star_backtrack(void)
{
    /* Classic backtracking stressors. */
    TEST_ASSERT(ndfs_wildmatch("*a*b*c*", "xaybzc", false) == true);
    TEST_ASSERT(ndfs_wildmatch("a*a*a", "aaaa", false) == true);
    TEST_ASSERT(ndfs_wildmatch("a*b", "aXXXb", false) == true);
    TEST_ASSERT(ndfs_wildmatch("a*b", "aXXXc", false) == false);
    return 0;
}

void run_wildmatch_tests(void)
{
    TEST_SUITE_BEGIN("Wildmatch Tests");

    RUN_TEST(test_exact_match);
    RUN_TEST(test_star_suffix);
    RUN_TEST(test_star_prefix_and_mid);
    RUN_TEST(test_only_star);
    RUN_TEST(test_question);
    RUN_TEST(test_case_insensitive);
    RUN_TEST(test_empty_pattern);
    RUN_TEST(test_null_safe);
    RUN_TEST(test_star_backtrack);
}
