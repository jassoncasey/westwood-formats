"""
Tests for LCW (Format80) compression/decompression (libwestwood).

Tests cover:
- Command byte decoding
- Relative copy operations
- Absolute copy operations
- Fill operations
- Transfer operations
- Edge cases and malformed data
"""

import pytest


class TestLcwBuiltinTests:
    """Test LCW decompression using built-in test vectors in lcw-tool."""

    def test_builtin_vectors(self, lcw_tool, run):
        """Run lcw-tool's built-in test vectors."""
        result = run(lcw_tool, "test", "-v")
        result.assert_success()
        assert "0 failed" in result.stderr_text


class TestLcwCommandDecoding:
    """Test LCW command byte identification."""

    def test_cmd_0_short_copy_rel(self):
        """Test command 0: short relative copy (2 bytes)."""
        # Format: 0cccpppp pppppppp
        # c = count-3, p = position (relative)
        cmd = 0b00001000  # count=0+3=3, low 4 bits of position
        assert (cmd >> 6) == 0  # Command type 0

    def test_cmd_1_medium_copy_rel(self):
        """Test command 1: medium relative copy (3 bytes)."""
        # Format: 01cccccc pppppppp pppppppp
        # c = count-3, p = position (relative)
        cmd = 0b01000100  # count=4+3=7
        assert (cmd >> 6) == 1  # Command type 1

    def test_cmd_2_large_copy_abs(self):
        """Test command 2: large absolute copy (5 bytes)."""
        # Format: 11cccccc pppppppp pppppppp nnnnnnnn nnnnnnnn
        # c = fill/copy indicator, p = position, n = count
        cmd = 0b11000000
        assert (cmd >> 6) == 3  # Command type 3 (0b11)
        assert (cmd & 0x3F) == 0  # Large copy marker

    def test_cmd_3_transfer(self):
        """Test command 3: transfer bytes (variable)."""
        # Format: 10cccccc [data...]
        # c = count, data follows
        cmd = 0b10001010  # count=10
        assert (cmd >> 6) == 2  # Command type 2
        assert (cmd & 0x3F) == 10

    def test_cmd_4_fill(self):
        """Test command 4: fill operation (4 bytes)."""
        # Format: 11111110 nnnnnnnn nnnnnnnn vvvvvvvv
        # n = count, v = fill value
        cmd = 0xFE
        assert cmd == 0xFE  # Fill marker

    def test_cmd_end_marker(self):
        """Test end of data marker (0x80)."""
        cmd = 0x80
        assert (cmd >> 6) == 2  # Command type 2
        assert (cmd & 0x3F) == 0  # Zero count = end


class TestLcwRelativeCopy:
    """Test relative copy operations."""

    def test_short_copy_min_count(self, lcw_tool, run):
        """Test minimum count (3) for short copy."""
        # 0x83 = literal 3 bytes, 0x0003 = short copy count=3 offset=3
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "0003" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"

    def test_short_copy_max_count(self, lcw_tool, run):
        """Test maximum count (10) for short copy."""
        # 0x8a = literal 10 bytes, 0x700a = short copy count=10
        # (0x70>>4+3=10) offset=10
        result = run(lcw_tool, "decompress", "--hex", "-s", "20",
                    "8a" "41424344454647484950" "700a" "80")
        result.assert_success()
        # Original 10 bytes + copy of those 10 bytes
        expected = "4142434445464748495041424344454647484950"
        assert result.stdout_text.strip() == expected

    def test_short_copy_position_range(self, lcw_tool, run):
        """Test 12-bit position range (0-4095)."""
        # Short copy uses 12-bit offset: ((cmd & 0x0F) << 8) | next_byte
        # 0x0f ff = offset 0xfff = 4095 (max)
        # First write enough data, then reference back
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "0003" "80")
        result.assert_success()

    def test_medium_copy_extended_count(self, lcw_tool, run):
        """Test extended count (3-66) for medium copy."""
        # 0xC0-0xFD: count = (cmd & 0x3F) + 3
        # 0xC0 = count 3, pos is 16-bit little-endian
        # In absolute mode, pos=0 copies from start of output
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "c00000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"

    def test_medium_copy_position_range(self, lcw_tool, run):
        """Test 16-bit position range for medium copy."""
        # Medium copy uses 16-bit absolute/relative position
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "c00000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"


class TestLcwAbsoluteCopy:
    """Test absolute copy operations."""

    def test_absolute_copy_position(self, lcw_tool, run):
        """Test absolute position addressing."""
        # 0xC0 with 16-bit position in absolute mode (default)
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "c00000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"

    def test_absolute_copy_large_count(self, lcw_tool, run):
        """Test large count (up to 65535) for absolute copy."""
        # 0xFF = long copy: count(16) + pos(16)
        # First literal 5 bytes, then long copy
        result = run(lcw_tool, "decompress", "--hex", "-s", "10",
                    "85" "4142434445" "ff" "0500" "0000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "41424344454142434445"


class TestLcwFillOperation:
    """Test fill operations."""

    def test_fill_single_byte(self, lcw_tool, run):
        """Test fill with count=1."""
        # 0xFE count_lo count_hi value
        result = run(lcw_tool, "decompress", "--hex", "-s", "1",
                    "fe0100" "42" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "42"

    def test_fill_multiple_bytes(self, lcw_tool, run):
        """Test fill with count > 1."""
        # Fill 5 bytes with 0x55
        result = run(lcw_tool, "decompress", "--hex", "-s", "5",
                    "fe0500" "55" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "5555555555"

    def test_fill_value_range(self, lcw_tool, run):
        """Test fill with different byte values."""
        # Fill with 0x00
        result = run(lcw_tool, "decompress", "--hex", "-s", "3",
                    "fe0300" "00" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "000000"

        # Fill with 0xFF
        result = run(lcw_tool, "decompress", "--hex", "-s", "3",
                    "fe0300" "ff" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "ffffff"


class TestLcwTransferOperation:
    """Test literal byte transfer operations."""

    def test_transfer_short(self, lcw_tool, run):
        """Test short transfer (1-63 bytes)."""
        # 0x81-0xBF: literal (count & 0x3F) bytes
        result = run(lcw_tool, "decompress", "--hex", "-s", "3",
                    "83" "414243" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243"

    def test_transfer_max_short(self, lcw_tool, run):
        """Test maximum short transfer (63 bytes)."""
        # 0xBF = literal 63 bytes
        data = "41" * 63  # 63 'A' bytes
        result = run(lcw_tool, "decompress", "--hex", "-s", "63",
                    "bf" + data + "80")
        result.assert_success()
        assert result.stdout_text.strip() == data


class TestLcwModeDetection:
    """Test LCW mode detection (absolute vs relative)."""

    def test_detect_absolute_mode(self, lcw_tool, run):
        """Test detection of absolute addressing mode."""
        # Absolute mode (default): medium copy references from output start
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "c00000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"

    def test_detect_relative_mode(self, lcw_tool, run):
        """Test detection of relative addressing mode."""
        # Relative mode: 0x00 prefix indicates relative mode
        # Then medium copy references backwards from current position
        result = run(lcw_tool, "decompress", "--hex", "-s", "6", "-r",
                    "83414243" "0003" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "414243414243"


class TestLcwEdgeCases:
    """Test edge cases and error handling."""

    def test_empty_input(self, lcw_tool, run):
        """Test decompression of empty data (just end marker)."""
        # End marker only produces empty output
        result = run(lcw_tool, "decompress", "-s", "0", "--hex", "80")
        result.assert_success()
        assert result.stdout_text.strip() == ""

    def test_truncated_command(self, lcw_tool, run):
        """Test handling of truncated command sequence."""
        # 0x83 expects 3 literal bytes but we only give 2
        result = run(lcw_tool, "decompress", "--hex", "-s", "3",
                    "834142")
        assert result.returncode != 0  # Should fail

    def test_invalid_back_reference(self, lcw_tool, run):
        """Test handling of invalid back-reference position."""
        # Short copy with offset 10 but we only have 3 bytes written
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "83414243" "000a" "80")
        assert result.returncode != 0  # Should fail - offset too large

    def test_output_overflow(self, lcw_tool, run):
        """Test handling when output exceeds expected size."""
        # Try to write 5 bytes but only allocate 3
        result = run(lcw_tool, "decompress", "--hex", "-s", "3",
                    "85" "4142434445" "80")
        assert result.returncode != 0  # Should fail

    def test_overlapping_copy(self, lcw_tool, run):
        """Test copy where source and dest overlap (RLE pattern)."""
        # Write 'A', then copy 5 bytes from offset 1
        # Overlapping copy creates AAAAA
        result = run(lcw_tool, "decompress", "--hex", "-s", "6",
                    "81" "41" "2001" "80")
        result.assert_success()
        # First A, then copy 5 from offset 1 = 6 A's
        assert result.stdout_text.strip() == "414141414141"


class TestLcwRoundTrip:
    """Test compression/decompression round-trip."""

    def test_decompress_literal_sequence(self, lcw_tool, run):
        """Test decompressing a literal sequence."""
        # Multiple literal chunks followed by end marker
        result = run(lcw_tool, "decompress", "--hex", "-s", "8",
                    "84" "41424344" "84" "45464748" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "4142434445464748"

    def test_decompress_fill_then_copy(self, lcw_tool, run):
        """Test fill followed by copy."""
        # Fill 4 with 0x41, then copy 4 from start
        result = run(lcw_tool, "decompress", "--hex", "-s", "8",
                    "fe0400" "41" "c10000" "80")
        result.assert_success()
        assert result.stdout_text.strip() == "4141414141414141"

    def test_decompress_complex_sequence(self, lcw_tool, run):
        """Test complex sequence with multiple operations."""
        # Literal ABC, fill 2 with D, copy 3 from start
        result = run(lcw_tool, "decompress", "--hex", "-s", "8",
                    "83" "414243" "fe0200" "44" "c00000" "80")
        result.assert_success()
        # ABC + DD + ABC = ABCDDABC
        assert result.stdout_text.strip() == "4142434444414243"
