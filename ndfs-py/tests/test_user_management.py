"""
Tests for user management -- add, remove, quota, password, max, duplicate, uppercase.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
import pytest

from ndfs.filesystem import NdfsFileSystem
from ndfs.constants import NDFS_PAGE_SIZE
from ndfs.types import ImageTemplate, ImageCreationOptions, UserCreationInfo


def _make_fs(pages=200):
    opts = ImageCreationOptions(
        template=ImageTemplate.Custom,
        directory_name="USERMGMT",
        custom_pages=pages,
    )
    return NdfsFileSystem.create_image(opts)


class TestAddUser:
    def test_add_single_user(self):
        ndfs = _make_fs()
        result = ndfs.add_user("ALICE", 500)
        assert result is True
        users = ndfs.get_users()
        assert len(users) == 2

    def test_add_multiple_users(self):
        ndfs = _make_fs()
        ndfs.add_user("ALICE", 500)
        ndfs.add_user("BOB", 300)
        ndfs.add_user("CHARLIE", 200)
        users = ndfs.get_users()
        assert len(users) == 4


class TestRemoveUser:
    def test_remove_user_without_files(self):
        ndfs = _make_fs()
        ndfs.add_user("TEMP", 100)
        users = ndfs.get_users()
        temp_user = None
        for i in range(len(users)):
            if users[i].user_name == "TEMP":
                temp_user = users[i]
                break
        assert temp_user is not None
        result = ndfs.remove_user(temp_user.user_index)
        assert result is True
        assert len(ndfs.get_users()) == 1

    def test_cannot_remove_user_with_files(self):
        ndfs = _make_fs()
        ndfs.write_file("SYSTEM/FILE:DATA", b"data")
        result = ndfs.remove_user(0)
        assert result is False


class TestUserQuota:
    def test_update_quota(self):
        ndfs = _make_fs()
        result = ndfs.update_user_quota(0, 2000)
        assert result is True
        user = ndfs.get_user(0)
        assert user.pages_reserved == 2000

    def test_update_nonexistent_user_quota(self):
        ndfs = _make_fs()
        result = ndfs.update_user_quota(99, 1000)
        assert result is False


class TestUserPassword:
    def test_clear_password_by_index(self):
        ndfs = _make_fs()
        result = ndfs.clear_user_password(0)
        assert result is True
        user = ndfs.get_user(0)
        assert user.password == 0

    def test_clear_password_by_name(self):
        ndfs = _make_fs()
        result = ndfs.clear_user_password("SYSTEM")
        assert result is True

    def test_clear_password_nonexistent(self):
        ndfs = _make_fs()
        result = ndfs.clear_user_password("NOBODY")
        assert result is False


class TestDuplicateUser:
    def test_reject_duplicate_username(self):
        ndfs = _make_fs()
        result = ndfs.add_user("SYSTEM", 500)
        assert result is False

    def test_reject_case_insensitive_duplicate(self):
        ndfs = _make_fs()
        result = ndfs.add_user("system", 500)
        # Should find SYSTEM (case-insensitive) and reject
        assert result is False


class TestUppercaseNames:
    def test_user_name_uppercased(self):
        ndfs = _make_fs()
        ndfs.add_user("lowercase", 100)
        users = ndfs.get_users()
        found = False
        for i in range(len(users)):
            if users[i].user_name == "LOWERCASE":
                found = True
                break
        assert found is True

    def test_find_user_case_insensitive(self):
        ndfs = _make_fs()
        ndfs.add_user("MixedCase", 100)
        # Should be able to list files for that user
        entries = ndfs.list_directory("MIXEDCASE")
        assert len(entries) == 0  # No files yet


class TestMaxUsers:
    def test_add_many_users(self):
        ndfs = _make_fs(500)
        # Add 30 users (SYSTEM is already there)
        for i in range(30):
            result = ndfs.add_user(f"USER{i:03d}", 10)
            assert result is True
        users = ndfs.get_users()
        assert len(users) == 31  # SYSTEM + 30


class TestUserPersistence:
    def test_users_survive_export(self):
        ndfs1 = _make_fs()
        ndfs1.add_user("ALICE", 500)
        ndfs1.add_user("BOB", 300)

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        users = ndfs2.get_users()
        assert len(users) == 3
        names = []
        for i in range(len(users)):
            names.append(users[i].user_name)
        assert "SYSTEM" in names
        assert "ALICE" in names
        assert "BOB" in names
