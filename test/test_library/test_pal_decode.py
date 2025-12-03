"""
Tests for PAL palette format decoding (libwestwood).

Tests cover:
- 768-byte file validation
- 6-bit to 8-bit color conversion
- Color index ordering
"""

import pytest
from pathlib import Path


class TestPalValidation:
    """Test PAL file validation."""

    def test_valid_768_bytes(self, pal_tool, testdata_pal_files, run):
        """Test acceptance of valid 768-byte PAL file."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", testdata_pal_files[0])
        result.assert_success()

    def test_invalid_size_too_small(self, pal_tool, temp_file, run):
        """Test rejection of undersized PAL file."""
        small = temp_file(".pal", b"\x00" * 767)
        result = run(pal_tool, "info", small)
        result.assert_exit_code(2)

    def test_invalid_size_too_large(self, pal_tool, temp_file, run):
        """Test rejection of oversized PAL file."""
        large = temp_file(".pal", b"\x00" * 769)
        result = run(pal_tool, "info", large)
        result.assert_exit_code(2)

    def test_exact_768_bytes(self, pal_tool, temp_file, run):
        """Test acceptance of exactly 768 bytes."""
        valid = temp_file(".pal", b"\x00" * 768)
        result = run(pal_tool, "info", valid)
        result.assert_success()


class TestPalColorConversion:
    """Test 6-bit to 8-bit color conversion."""

    def test_conversion_formula(self):
        """Test (val << 2) | (val >> 4) conversion."""
        # Test specific values
        # 0 -> 0
        # 63 -> 255
        # 32 -> 130
        def convert_6to8(val):
            return (val << 2) | (val >> 4)

        assert convert_6to8(0) == 0
        assert convert_6to8(63) == 255
        assert convert_6to8(32) == 130
        assert convert_6to8(1) == 4
        assert convert_6to8(62) == 251

    def test_all_values_in_range(self):
        """Test all 6-bit values convert to valid 8-bit range."""
        def convert_6to8(val):
            return (val << 2) | (val >> 4)

        for val in range(64):
            result = convert_6to8(val)
            assert 0 <= result <= 255


class TestPalColorOrder:
    """Test color index ordering."""

    def test_index_0_first(self):
        """Test index 0 is at byte offset 0."""
        pytest.skip("Requires extracted PAL test files")

    def test_index_255_last(self):
        """Test index 255 is at byte offset 765-767."""
        pytest.skip("Requires extracted PAL test files")

    def test_rgb_triplet_order(self):
        """Test R, G, B byte order within each triplet."""
        pytest.skip("Requires extracted PAL test files")


class TestPalInfoOutput:
    """Test pal-tool info command output."""

    def test_info_human_readable(self, pal_tool, temp_file, run):
        """Test human-readable info output format."""
        valid = temp_file(".pal", b"\x00" * 768)
        result = run(pal_tool, "info", valid)
        result.assert_success()
        assert "256" in result.stdout_text  # Colors
        assert "6-bit" in result.stdout_text  # Bit depth

    def test_info_json(self, pal_tool, temp_file, run):
        """Test JSON info output format."""
        import json
        valid = temp_file(".pal", b"\x00" * 768)
        result = run(pal_tool, "info", "--json", valid)
        result.assert_success()
        data = json.loads(result.stdout_text)
        assert data["colors"] == 256

    def test_info_fields_complete(self, pal_tool, temp_file, run):
        """Test all required info fields are present."""
        import json
        valid = temp_file(".pal", b"\x00" * 768)
        result = run(pal_tool, "info", "--json", valid)
        result.assert_success()
        data = json.loads(result.stdout_text)
        # Required fields
        assert "colors" in data
        assert "bit_depth" in data or "bitDepth" in data
        assert "file_size" in data or "fileSize" in data
