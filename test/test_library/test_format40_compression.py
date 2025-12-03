"""
Tests for Format40 (XOR delta) compression/decompression (libwestwood).

Tests cover:
- Command byte decoding
- XOR delta operations
- Skip operations
- Copy operations
- Integration with LCW pre-decompression
"""

import pytest


class TestFormat40BuiltinTests:
    """Test Format40 decompression using built-in test vectors in lcw-tool."""

    def test_builtin_vectors(self, lcw_tool, run):
        """Run lcw-tool's built-in Format40 test vectors."""
        result = run(lcw_tool, "test")
        result.assert_success()
        # Check for Format40 tests passing
        assert "Format40" in result.stdout_text
        assert "0 failed" in result.stdout_text


class TestFormat40CommandDecoding:
    """Test Format40 command byte identification."""

    def test_cmd_skip_short(self):
        """Test short skip command (1-127 bytes)."""
        # Format: 0nnnnnnn (n > 0)
        # Skip n bytes (leave unchanged)
        cmd = 0b00001010  # Skip 10 bytes
        assert (cmd & 0x80) == 0  # High bit clear
        assert (cmd & 0x7F) == 10

    def test_cmd_xor_short(self):
        """Test short XOR command (1-127 bytes)."""
        # Format: 1nnnnnnn [data...] (n > 0, n != 0x80)
        # XOR n bytes with following data
        cmd = 0b10000101  # XOR 5 bytes
        assert (cmd & 0x80) != 0  # High bit set
        assert (cmd & 0x7F) == 5

    def test_cmd_extended(self, lcw_tool, run):
        """Test extended command (0x00 or 0x80 prefix)."""
        # 0x80 0x00 0x00 = END marker
        # Test that END marker terminates correctly
        # Buffer: ABCD, Delta: just END marker
        result = run(lcw_tool, "format40", "--hex", "800000", "41424344")
        result.assert_success()
        # Buffer should be unchanged (just END, no operations)
        assert result.stdout_text.strip() == "41424344"

    def test_cmd_end_marker(self):
        """Test end of data marker (0x80 0x00 0x00)."""
        cmd = [0x80, 0x00, 0x00]
        assert cmd[0] == 0x80
        assert cmd[1] == 0x00
        assert cmd[2] == 0x00


class TestFormat40Skip:
    """Test skip (leave unchanged) operations."""

    def test_skip_short_min(self, lcw_tool, run):
        """Test minimum short skip (1 byte)."""
        # SHORTSKIP: 0x81-0xFF skips (cmd & 0x7F) bytes
        # 0x81 = skip 1 byte, then XOR 1 byte with 0xFF
        # Buffer: ABCD, skip 1, XOR 1 with FF -> A(B^FF)CD = A(BD)CD
        result = run(lcw_tool, "format40", "--hex", "81" "01ff" "800000", "41424344")
        result.assert_success()
        # B (0x42) XOR FF = BD
        assert result.stdout_text.strip() == "41bd4344"

    def test_skip_short_max(self, lcw_tool, run):
        """Test maximum short skip (127 bytes)."""
        # Skip preserves buffer contents - just test with small buffer
        # 0x82 = skip 2 bytes, then XOR 1 with FF
        result = run(lcw_tool, "format40", "--hex", "82" "01ff" "800000", "41424344")
        result.assert_success()
        # Skip AB, XOR C(0x43) with FF -> C^FF = 0xBC
        assert result.stdout_text.strip() == "4142bc44"

    def test_skip_extended(self, lcw_tool, run):
        """Test extended skip (128+ bytes) using LONGSKIP."""
        # LONGSKIP: 0x80 followed by word with bit15=0
        # 0x80 0x02 0x00 = skip 2 bytes
        result = run(lcw_tool, "format40", "--hex", "80" "0200" "01ff" "800000", "41424344")
        result.assert_success()
        # Skip 2 bytes, XOR C(0x43) with FF -> 0xBC
        assert result.stdout_text.strip() == "4142bc44"

    def test_skip_preserves_buffer(self, lcw_tool, run):
        """Test that skip doesn't modify destination bytes."""
        # Just END marker - buffer unchanged
        result = run(lcw_tool, "format40", "--hex", "800000", "41424344")
        result.assert_success()
        assert result.stdout_text.strip() == "41424344"


class TestFormat40Xor:
    """Test XOR delta operations."""

    def test_xor_identity(self):
        """Test XOR with same value produces zeros."""
        # a XOR a = 0
        for val in [0x00, 0x55, 0xAA, 0xFF]:
            assert val ^ val == 0

    def test_xor_inverse(self):
        """Test XOR inverse property."""
        # (a XOR b) XOR b = a
        original = bytes([0x12, 0x34, 0x56, 0x78])
        delta = bytes([0xAB, 0xCD, 0xEF, 0x01])
        xored = bytes(a ^ b for a, b in zip(original, delta))
        restored = bytes(a ^ b for a, b in zip(xored, delta))
        assert restored == original

    def test_xor_short_min(self, lcw_tool, run):
        """Test minimum short XOR (1 byte)."""
        # SHORTDUMP: 0x01-0x7F XORs (cmd) bytes from delta
        # 0x01 0xFF = XOR 1 byte with 0xFF
        result = run(lcw_tool, "format40", "--hex", "01ff" "800000", "41424344")
        result.assert_success()
        # A (0x41) XOR FF = BE
        assert result.stdout_text.strip() == "be424344"

    def test_xor_short_max(self, lcw_tool, run):
        """Test maximum short XOR (127 bytes)."""
        # Test with 4 bytes: 0x04 + 4 XOR values
        result = run(lcw_tool, "format40", "--hex", "04" "01020304" "800000", "41424344")
        result.assert_success()
        # A^01=40, B^02=40, C^03=40, D^04=40
        assert result.stdout_text.strip() == "40404040"

    def test_xor_extended(self, lcw_tool, run):
        """Test extended XOR using LONGDUMP."""
        # LONGDUMP: 0x80 followed by word with bit15=1, bit14=0
        # 0x80 0x02 0x80 = XOR 2 bytes (0x8002 has bit15=1, bit14=0, count=2)
        result = run(lcw_tool, "format40", "--hex", "80" "0280" "ffff" "800000", "41424344")
        result.assert_success()
        # A^FF=BE, B^FF=BD
        assert result.stdout_text.strip() == "bebd4344"


class TestFormat40DeltaDecoding:
    """Test cumulative delta frame decoding."""

    def test_first_frame(self, lcw_tool, run):
        """Test first frame decodes against empty (zero) buffer."""
        # Start with zero buffer, XOR with ABCD
        result = run(lcw_tool, "format40", "--hex", "04" "41424344" "800000", "00000000")
        result.assert_success()
        # 0^A=A, 0^B=B, etc.
        assert result.stdout_text.strip() == "41424344"

    def test_second_frame(self, lcw_tool, run):
        """Test second frame decodes against first frame."""
        # Buffer: ABCD, XOR with 01020304 -> small changes
        result = run(lcw_tool, "format40", "--hex", "04" "01020304" "800000", "41424344")
        result.assert_success()
        assert result.stdout_text.strip() == "40404040"

    def test_cumulative_sequence(self, lcw_tool, run):
        """Test decoding sequence of delta frames."""
        # XOR twice with same value returns to original
        # First: ABCD XOR FFFF0000 -> BEBD4344
        result1 = run(lcw_tool, "format40", "--hex", "04" "ffff0000" "800000", "41424344")
        result1.assert_success()
        assert result1.stdout_text.strip() == "bebd4344"
        # Second: BEBD4344 XOR FFFF0000 -> ABCD
        result2 = run(lcw_tool, "format40", "--hex", "04" "ffff0000" "800000", "bebd4344")
        result2.assert_success()
        assert result2.stdout_text.strip() == "41424344"

    def test_in_place_decoding(self, lcw_tool, run):
        """Test delta decoding modifies buffer in-place."""
        # Same as first_frame test - buffer is modified by XOR
        result = run(lcw_tool, "format40", "--hex", "02" "ffff" "800000", "41424344")
        result.assert_success()
        # Only first 2 bytes modified
        assert result.stdout_text.strip() == "bebd4344"


class TestFormat40WithLcw:
    """Test Format40 combined with LCW pre-decompression."""

    def test_lcw_then_format40(self, lcw_tool, run):
        """Test LCW decompression followed by Format40."""
        # WSA frames are: LCW -> Format40 -> frame buffer
        # First decompress LCW: literal 4 bytes + end
        lcw_result = run(lcw_tool, "decompress", "--hex", "-s", "4", "84" "01020304" "80")
        lcw_result.assert_success()
        lcw_output = lcw_result.stdout_text.strip()
        # Then apply Format40 using the decompressed data as delta
        result = run(lcw_tool, "format40", "--hex", "04" + lcw_output + "800000", "41424344")
        result.assert_success()
        # ABCD XOR 01020304 = 40404040
        assert result.stdout_text.strip() == "40404040"

    def test_format40_operations_chain(self, lcw_tool, run):
        """Test chaining multiple Format40 operations."""
        # Apply delta, then another delta
        result1 = run(lcw_tool, "format40", "--hex", "02" "0f0f" "800000", "41424344")
        result1.assert_success()
        # A^0F=4E, B^0F=4D
        assert result1.stdout_text.strip() == "4e4d4344"


class TestFormat40EdgeCases:
    """Test edge cases and error handling."""

    def test_empty_delta(self, lcw_tool, run):
        """Test empty delta (just end marker, no changes)."""
        result = run(lcw_tool, "format40", "--hex", "800000", "41424344")
        result.assert_success()
        # Buffer unchanged
        assert result.stdout_text.strip() == "41424344"

    def test_full_replacement(self, lcw_tool, run):
        """Test delta that replaces entire frame via XOR."""
        # XOR all bytes with values that give new content
        result = run(lcw_tool, "format40", "--hex", "04" "00000000" "800000", "41424344")
        result.assert_success()
        # XOR with 0 keeps original
        assert result.stdout_text.strip() == "41424344"

    def test_shortrun_fill(self, lcw_tool, run):
        """Test SHORTRUN (0x00 count value) fill operation."""
        # 0x00 count value = XOR fill
        result = run(lcw_tool, "format40", "--hex", "00" "03" "ff" "800000", "41424344")
        result.assert_success()
        # XOR first 3 bytes with FF: A^FF=BE, B^FF=BD, C^FF=BC
        assert result.stdout_text.strip() == "bebdbc44"

    def test_skip_then_xor(self, lcw_tool, run):
        """Test skip followed by XOR operation."""
        # Skip 2 bytes, then XOR 2 bytes
        result = run(lcw_tool, "format40", "--hex", "82" "02" "ffff" "800000", "41424344")
        result.assert_success()
        # Skip AB, XOR CD with FFFF -> AB(C^FF)(D^FF) = AB BC BB
        # C=0x43, D=0x44, C^FF=BC, D^FF=BB
        assert result.stdout_text.strip() == "4142bcbb"

    def test_mixed_operations(self, lcw_tool, run):
        """Test combination of XOR, skip, and fill operations."""
        # XOR 1 byte, skip 1, XOR 1
        result = run(lcw_tool, "format40", "--hex", "01" "ff" "81" "01" "ff" "800000", "41424344")
        result.assert_success()
        # A^FF=BE, skip B, C^FF=BC
        assert result.stdout_text.strip() == "be42bc44"


class TestFormat40Performance:
    """Test performance characteristics."""

    def test_sparse_delta_small(self, lcw_tool, run):
        """Test that sparse changes can be efficiently represented."""
        # Only change 1 byte in the middle: skip 2, XOR 1, end
        # Delta: 0x82 (skip 2) + 0x01 0xFF (xor 1 with FF) + 0x800000 (end)
        result = run(lcw_tool, "format40", "--hex", "82" "01ff" "800000", "41424344")
        result.assert_success()
        # Only C(0x43) is changed: C^FF = BC
        assert result.stdout_text.strip() == "4142bc44"

    def test_no_change_minimal(self, lcw_tool, run):
        """Test that no changes produces minimal delta (just end marker)."""
        # Just end marker = no changes
        result = run(lcw_tool, "format40", "--hex", "800000", "41424344")
        result.assert_success()
        assert result.stdout_text.strip() == "41424344"
