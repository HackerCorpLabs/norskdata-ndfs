"""Tests for portable wildcard matching."""

from ndfs.wildmatch import wildmatch


def test_exact_match():
    assert wildmatch("STARTUP:MODE", "STARTUP:MODE") is True
    assert wildmatch("STARTUP:MODE", "STARTUP:SYMB") is False


def test_star_suffix():
    assert wildmatch("*:MODE", "STARTUP:MODE") is True
    assert wildmatch("*:MODE", ":MODE") is True  # * matches empty
    assert wildmatch("*:MODE", "X:SYMB") is False


def test_star_prefix_and_mid():
    assert wildmatch("STARTUP*", "STARTUP:MODE") is True
    assert wildmatch("ST*MODE", "STARTUP:MODE") is True
    assert wildmatch("ST*X", "STARTUP:MODE") is False


def test_lone_and_consecutive_stars():
    assert wildmatch("*", "ANYTHING") is True
    assert wildmatch("*", "") is True
    assert wildmatch("**", "abc") is True


def test_question():
    assert wildmatch("FILE?:C", "FILE1:C") is True
    assert wildmatch("FILE?:C", "FILE:C") is False
    assert wildmatch("???", "AB") is False


def test_case_insensitive():
    assert wildmatch("*:mode", "STARTUP:MODE", True) is True
    assert wildmatch("*:mode", "STARTUP:MODE", False) is False


def test_empty_pattern():
    assert wildmatch("", "") is True
    assert wildmatch("", "x") is False


def test_backtrack():
    assert wildmatch("*a*b*c*", "xaybzc") is True
    assert wildmatch("a*a*a", "aaaa") is True
    assert wildmatch("a*b", "aXXXc") is False
