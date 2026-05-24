"""Tests for ND-100 even parity helpers."""

from ndfs.parity import strip_parity, set_parity, is_text_type


class TestStripParity:
    def test_clears_bit_7(self):
        data = bytes([0xC8, 0xE5, 0xEC, 0xEC, 0xEF])
        result = strip_parity(data)
        assert result == bytes([0x48, 0x65, 0x6C, 0x6C, 0x6F])  # Hello

    def test_no_change_when_bit_7_clear(self):
        data = bytes([0x48, 0x65, 0x6C])
        result = strip_parity(data)
        assert result == data

    def test_empty_input(self):
        assert strip_parity(b"") == b""


class TestSetParity:
    def test_sets_even_parity(self):
        data = bytes([
            0x48,  # H: 2 ones (even) -> 0x48
            0x65,  # e: 4 ones (even) -> 0x65
            0x20,  # ' ': 1 one (odd) -> 0xA0
            0x57,  # W: 5 ones (odd) -> 0xD7
            0x64,  # d: 3 ones (odd) -> 0xE4
        ])
        result = set_parity(data)
        assert result[0] == 0x48
        assert result[1] == 0x65
        assert result[2] == 0xA0
        assert result[3] == 0xD7
        assert result[4] == 0xE4

    def test_every_byte_has_even_parity(self):
        data = bytes(range(256))
        result = set_parity(data)
        for b in result:
            ones = bin(b).count("1")
            assert ones % 2 == 0, f"byte 0x{b:02X} has {ones} ones (not even)"

    def test_round_trips_with_strip(self):
        original = b"Hello World"
        with_parity = set_parity(original)
        stripped = strip_parity(with_parity)
        assert stripped == bytes(b & 0x7F for b in original)

    def test_empty_input(self):
        assert set_parity(b"") == b""


class TestIsTextType:
    def test_text_types(self):
        assert is_text_type("MODE") is True
        assert is_text_type("SYMB") is True
        assert is_text_type("TEXT") is True
        assert is_text_type("C") is True
        assert is_text_type("BATC") is True
        assert is_text_type("FORT") is True

    def test_binary_types(self):
        assert is_text_type("PROG") is False
        assert is_text_type("BPUN") is False
        assert is_text_type("DATA") is False
        assert is_text_type("VTM") is False

    def test_case_insensitive(self):
        assert is_text_type("mode") is True
        assert is_text_type("Mode") is True

    def test_empty(self):
        assert is_text_type("") is False
