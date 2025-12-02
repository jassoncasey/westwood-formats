"""
Tests for FNT font format decoding (libwestwood).

Tests cover:
- Header parsing (v3 format)
- FontInfo block
- Glyph offset/width/height arrays
- 4-bit pixel data extraction
"""

import pytest
from pathlib import Path


class TestFntHeaderParsing:
    """Test FNT header parsing."""

    def test_valid_v3_header(self, fnt_tool, run):
        """Test parsing valid v3 FNT header."""
        pytest.skip("Requires extracted FNT test files")

    def test_file_size_field(self, fnt_tool, run):
        """Test file size field extraction."""
        pytest.skip("Requires extracted FNT test files")

    def test_block_offsets(self, fnt_tool, run):
        """Test InfoBlk, OffsetBlk, WidthBlk, HeightBlk offsets."""
        pytest.skip("Requires extracted FNT test files")

    def test_invalid_header(self, fnt_tool, temp_file, run):
        """Test rejection of invalid FNT file."""
        bad_file = temp_file(".fnt", b"\x00" * 100)
        result = run(fnt_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestFntFontInfo:
    """Test FontInfo block parsing."""

    def test_glyph_count(self, fnt_tool, run):
        """Test NrOfChars extraction."""
        pytest.skip("Requires extracted FNT test files")

    def test_max_dimensions(self, fnt_tool, run):
        """Test MaxHeight/MaxWidth extraction."""
        pytest.skip("Requires extracted FNT test files")


class TestFntGlyphArrays:
    """Test glyph data arrays."""

    def test_offset_array(self):
        """Test glyph offset array parsing (uint16 entries)."""
        pytest.skip("Requires extracted FNT test files")

    def test_width_array(self):
        """Test width array parsing (uint8 entries)."""
        pytest.skip("Requires extracted FNT test files")

    def test_height_array(self):
        """Test height array parsing (Y-offset + height per glyph)."""
        pytest.skip("Requires extracted FNT test files")

    def test_shared_glyph_data(self):
        """Test identical glyphs share data (same offset)."""
        pytest.skip("Requires extracted FNT test files")


class TestFntPixelData:
    """Test 4-bit pixel data extraction."""

    def test_4bit_per_pixel(self):
        """Test pixels are 4-bit (16 colors)."""
        pytest.skip("Requires extracted FNT test files")

    def test_stride_calculation(self):
        """Test stride = (width * 4 + 7) / 8 bytes per scanline."""
        def calc_stride(width):
            return (width * 4 + 7) // 8

        assert calc_stride(1) == 1   # 4 bits -> 1 byte
        assert calc_stride(2) == 1   # 8 bits -> 1 byte
        assert calc_stride(3) == 2   # 12 bits -> 2 bytes
        assert calc_stride(4) == 2   # 16 bits -> 2 bytes
        assert calc_stride(5) == 3   # 20 bits -> 3 bytes

    def test_nibble_order(self):
        """Test low nibble = leftmost pixel."""
        pytest.skip("Requires extracted FNT test files")


class TestFntGrayscaleConversion:
    """Test 4-bit to grayscale conversion."""

    def test_intensity_formula(self):
        """Test intensity = value * 17."""
        for val in range(16):
            intensity = val * 17
            assert 0 <= intensity <= 255
        assert 0 * 17 == 0
        assert 15 * 17 == 255
        assert 8 * 17 == 136


class TestFntInfoOutput:
    """Test fnt-tool info command output."""

    def test_info_human_readable(self, fnt_tool, run):
        """Test human-readable info output format."""
        pytest.skip("Requires extracted FNT test files")

    def test_info_json(self, fnt_tool, run):
        """Test JSON info output format."""
        pytest.skip("Requires extracted FNT test files")

    def test_info_fields_complete(self, fnt_tool, run):
        """Test all required info fields are present."""
        # Required fields: format, version, glyphs, character_range,
        # max_dimensions, bit_depth
        pytest.skip("Requires extracted FNT test files")
