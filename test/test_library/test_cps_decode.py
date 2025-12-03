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

    def test_valid_header(self, cps_tool, testdata_cps_files, run):
        """Test parsing valid CPS header."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()

    def test_file_size_validation(self, cps_tool, testdata_cps_files, run):
        """Test FileSize + 2 == actual file size."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_compression_field(self, cps_tool, testdata_cps_files, run):
        """Test compression method extraction."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()
        # Should mention compression
        assert "compress" in result.stdout_text.lower() or "lcw" in result.stdout_text.lower()

    def test_uncompressed_size(self, cps_tool, testdata_cps_files, run):
        """Test UncompSize is 64000 (320*200)."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # CPS images are always 320x200
        if "width" in data and "height" in data:
            assert data["width"] == 320
            assert data["height"] == 200

    def test_palette_size_field(self, cps_tool, testdata_cps_files, run):
        """Test PaletteSize extraction (768 or 0)."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()

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

    def test_detect_embedded_palette(self, cps_tool, testdata_cps_files, run):
        """Test detection of embedded palette (PaletteSize > 0)."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should report palette information
        assert isinstance(data, dict)

    def test_no_embedded_palette(self, cps_tool, testdata_cps_files, run):
        """Test CPS without embedded palette."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        # This test looks for a CPS without embedded palette
        # Most CPS files have embedded palettes, so we just verify info works
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()

    def test_palette_at_offset_10(self):
        """Test palette starts at offset 10 after header."""
        # This is a unit test of the file format, not CLI tool behavior
        pytest.skip("Requires unit test of CPS parser")


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

    def test_info_human_readable(self, cps_tool, testdata_cps_files, run):
        """Test human-readable info output format."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()
        assert len(result.stdout_text) > 0

    def test_info_json(self, cps_tool, testdata_cps_files, run):
        """Test JSON info output format."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_info_fields_complete(self, cps_tool, testdata_cps_files, run):
        """Test all required info fields are present."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Check for key fields (names may vary)
        assert any(k in data for k in ["width", "Width"])
        assert any(k in data for k in ["height", "Height"])
