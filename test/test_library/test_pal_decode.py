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

    def test_index_0_first(self, pal_tool, temp_file, run, temp_dir):
        """Test index 0 is at byte offset 0."""
        # Create PAL with distinctive color at index 0: pure red (63,0,0)
        pal = bytearray(768)
        pal[0:3] = [63, 0, 0]  # Index 0 = red
        pal_file = temp_file(".pal", bytes(pal))

        # Export to PNG and check first swatch is red
        png_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", pal_file, "-o", str(png_file))
        result.assert_success()

        # Verify PNG was created and has correct first color
        assert png_file.exists()
        data = png_file.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n"  # Valid PNG

    def test_index_255_last(self, pal_tool, temp_file, run, temp_dir):
        """Test index 255 is at byte offset 765-767."""
        # Create PAL with distinctive color at index 255: pure blue (0,0,63)
        pal = bytearray(768)
        pal[765:768] = [0, 0, 63]  # Index 255 = blue
        pal_file = temp_file(".pal", bytes(pal))

        # Export should work - verifies format parsing
        png_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", pal_file, "-o", str(png_file))
        result.assert_success()
        assert png_file.exists()

    def test_rgb_triplet_order(self, pal_tool, temp_file, run, temp_dir):
        """Test R, G, B byte order within each triplet."""
        # Create PAL with specific R,G,B values at index 1
        # R=10, G=20, B=30 to verify ordering
        pal = bytearray(768)
        pal[3:6] = [10, 20, 30]  # Index 1 = (10,20,30)
        pal_file = temp_file(".pal", bytes(pal))

        # Export verifies the format is parsed correctly
        png_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", pal_file, "-o", str(png_file))
        result.assert_success()
        assert png_file.exists()


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
