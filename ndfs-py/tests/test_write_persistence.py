"""
Tests for write persistence -- export/reimport round-trips.

SPDX-License-Identifier: MIT
Copyright (c) 1985-2026 Ronny Hansen
HackerCorp Labs - https://github.com/HackerCorpLabs
"""
import pytest

from ndfs.filesystem import NdfsFileSystem
from ndfs.constants import NDFS_PAGE_SIZE
from ndfs.types import ImageTemplate, ImageCreationOptions


def _make_fs(pages=200):
    opts = ImageCreationOptions(
        template=ImageTemplate.Custom,
        directory_name="PERSIST",
        custom_pages=pages,
    )
    return NdfsFileSystem.create_image(opts)


class TestFileRoundTrip:
    def test_single_file_survives_export(self):
        ndfs1 = _make_fs()
        ndfs1.write_file("SYSTEM/PERSIST:DATA", b"persistent data")

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        content = ndfs2.read_file("SYSTEM/PERSIST:DATA")
        assert content == b"persistent data"

    def test_multiple_files_survive_export(self):
        ndfs1 = _make_fs()
        for i in range(5):
            ndfs1.write_file(f"SYSTEM/FILE{i}:DATA", f"content {i}".encode())

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        for i in range(5):
            content = ndfs2.read_file(f"SYSTEM/FILE{i}:DATA")
            assert content == f"content {i}".encode()

    def test_large_file_survives_export(self):
        ndfs1 = _make_fs()
        data = bytearray(5000)
        for i in range(len(data)):
            data[i] = i & 0xFF
        ndfs1.write_file("SYSTEM/BIGFILE:DATA", data)

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        content = ndfs2.read_file("SYSTEM/BIGFILE:DATA")
        assert len(content) == 5000
        for i in range(len(data)):
            assert content[i] == data[i]


class TestUserRoundTrip:
    def test_added_user_survives_export(self):
        ndfs1 = _make_fs()
        ndfs1.add_user("NEWUSER", 500)

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        users = ndfs2.get_users()
        assert len(users) == 2
        names = []
        for i in range(len(users)):
            names.append(users[i].user_name)
        assert "SYSTEM" in names
        assert "NEWUSER" in names

    def test_user_quota_survives_export(self):
        ndfs1 = _make_fs()
        ndfs1.update_user_quota(0, 2000)

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        user = ndfs2.get_user(0)
        assert user.pages_reserved == 2000


class TestDeletionRoundTrip:
    def test_deletion_persists_after_export(self):
        ndfs1 = _make_fs()
        ndfs1.write_file("SYSTEM/A:DATA", b"aaa")
        ndfs1.write_file("SYSTEM/B:DATA", b"bbb")
        ndfs1.delete_file("SYSTEM/A:DATA")

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        assert ndfs2.file_exists("SYSTEM/A:DATA") is False
        assert ndfs2.file_exists("SYSTEM/B:DATA") is True
        assert ndfs2.read_file("SYSTEM/B:DATA") == b"bbb"

    def test_free_pages_persist_after_export(self):
        ndfs1 = _make_fs()
        free_initial = ndfs1.get_free_pages()

        ndfs1.write_file("SYSTEM/TEMP:DATA", b"temporary")
        ndfs1.delete_file("SYSTEM/TEMP:DATA")

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        assert ndfs2.get_free_pages() == free_initial


class TestDirectoryNamePersists:
    def test_directory_name_survives_export(self):
        ndfs1 = _make_fs()
        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)
        assert ndfs2.get_directory_name() == "PERSIST"


class TestOverwriteRoundTrip:
    def test_overwritten_file_persists(self):
        ndfs1 = _make_fs()
        ndfs1.write_file("SYSTEM/FILE:DATA", b"version 1")
        ndfs1.write_file("SYSTEM/FILE:DATA", b"version 2")

        exported = ndfs1.to_buffer()
        ndfs2 = NdfsFileSystem(exported)

        content = ndfs2.read_file("SYSTEM/FILE:DATA")
        assert content == b"version 2"
