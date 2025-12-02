"""
Tests for CPS image format decoding (libwestwood).

Tests cover:
- Header parsing
- Compression type detection
- Embedded palette handling
- LCW decompression
- 320x200 pixel output
"""

import pytest
from pathlib import Path


class TestCpsHeaderParsing:
    """Test CPS header parsing."""

    def test_valid_header(self, cps_tool, run):
        """Test parsing valid CPS header."""
        pytest.skip("Requires extracted CPS test files")

    def test_file_size_validation(self, cps_tool, run):
        """Test FileSize + 2 == actual file size."""
        pytest.skip("Requires extracted CPS test files")

    def test_compression_field(self, cps_tool, run):
        """Test compression method extraction."""
        pytest.skip("Requires extracted CPS test files")

    def test_uncompressed_size(self, cps_tool, run):
        """Test UncompSize is 64000 (320*200)."""
        pytest.skip("Requires extracted CPS test files")

    def test_palette_size_field(self, cps_tool, run):
        """Test PaletteSize extraction (768 or 0)."""
        pytest.skip("Requires extracted CPS test files")

    def test_invalid_header(self, cps_tool, temp_file, run):
        """Test rejection of invalid CPS file."""
        bad_file = temp_file(".cps", b"\x00" * 100)
        result = run(cps_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestCpsCompressionTypes:
    """Test compression type detection and handling."""

    def test_compression_none(self):
        """Test uncompressed CPS (method 0x0000)."""
        pytest.skip("Requires extracted CPS test files")

    def test_compression_lcw(self):
        """Test LCW compressed CPS (method 0x0004)."""
        pytest.skip("Requires extracted CPS test files")

    def test_compression_lzw12(self):
        """Test LZW-12 compressed CPS (method 0x0001)."""
        pytest.skip("Requires extracted CPS test files")

    def test_compression_lzw14(self):
        """Test LZW-14 compressed CPS (method 0x0002)."""
        pytest.skip("Requires extracted CPS test files")

    def test_compression_rle(self):
        """Test RLE compressed CPS (method 0x0003)."""
        pytest.skip("Requires extracted CPS test files")


class TestCpsEmbeddedPalette:
    """Test embedded palette handling."""

    def test_detect_embedded_palette(self, cps_tool, run):
        """Test detection of embedded palette (PaletteSize > 0)."""
        pytest.skip("Requires extracted CPS test files")

    def test_no_embedded_palette(self, cps_tool, run):
        """Test CPS without embedded palette."""
        pytest.skip("Requires extracted CPS test files")

    def test_palette_at_offset_10(self):
        """Test palette starts at offset 10 after header."""
        pytest.skip("Requires extracted CPS test files")


class TestCpsImageData:
    """Test image data decompression."""

    def test_output_size(self):
        """Test decompressed output is exactly 64000 bytes."""
        pytest.skip("Requires extracted CPS test files")

    def test_dimensions(self):
        """Test image is 320x200 pixels."""
        pytest.skip("Requires extracted CPS test files")

    def test_linear_layout(self):
        """Test pixels are row-major (row 0 first, left-to-right)."""
        pytest.skip("Requires extracted CPS test files")


class TestCpsInfoOutput:
    """Test cps-tool info command output."""

    def test_info_human_readable(self, cps_tool, run):
        """Test human-readable info output format."""
        pytest.skip("Requires extracted CPS test files")

    def test_info_json(self, cps_tool, run):
        """Test JSON info output format."""
        pytest.skip("Requires extracted CPS test files")

    def test_info_fields_complete(self, cps_tool, run):
        """Test all required info fields are present."""
        # Required fields: dimensions, compression, has_embedded_palette,
        # compressed_size, uncompressed_size
        pytest.skip("Requires extracted CPS test files")
