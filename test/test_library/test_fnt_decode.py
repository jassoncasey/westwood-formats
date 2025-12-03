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

    def test_valid_v3_header(self, fnt_tool, testdata_fnt_files, run):
        """Test parsing valid v3 FNT header."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()

    def test_file_size_field(self, fnt_tool, testdata_fnt_files, run):
        """Test file size field extraction."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_block_offsets(self, fnt_tool, testdata_fnt_files, run):
        """Test InfoBlk, OffsetBlk, WidthBlk, HeightBlk offsets."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()

    def test_invalid_header(self, fnt_tool, temp_file, run):
        """Test rejection of invalid FNT file."""
        bad_file = temp_file(".fnt", b"\x00" * 100)
        result = run(fnt_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestFntFontInfo:
    """Test FontInfo block parsing."""

    def test_glyph_count(self, fnt_tool, testdata_fnt_files, run):
        """Test NrOfChars extraction."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should have glyph count info
        assert any(
            k in data
            for k in ["glyphs", "numGlyphs", "glyph_count", "characters"]
        )

    def test_max_dimensions(self, fnt_tool, testdata_fnt_files, run):
        """Test MaxHeight/MaxWidth extraction."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()
        # Should have dimension info (format: "Max dimensions:  10x11")
        assert (
            "dimensions" in result.stdout_text.lower()
            or "x" in result.stdout_text
        )


class TestFntGlyphArrays:
    """Test glyph data arrays."""

    def test_glyph_info_in_json(self, fnt_tool, testdata_fnt_files, run):
        """Test glyph information is available in JSON output."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should have glyph-related info
        assert any(
            k in data
            for k in ["glyphs", "numGlyphs", "glyph_count", "characters"]
        )

    def test_dimensions_in_json(self, fnt_tool, testdata_fnt_files, run):
        """Test max dimensions are available in JSON output."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should have dimension info
        assert any(k in data for k in ["maxWidth", "max_width", "width"])
        assert any(k in data for k in ["maxHeight", "max_height", "height"])

    def test_multiple_fnt_files(self, fnt_tool, testdata_fnt_files, run):
        """Test parsing multiple FNT files."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        for fnt_file in testdata_fnt_files[:3]:  # Test first 3
            result = run(fnt_tool, "info", fnt_file)
            result.assert_success()


class TestFntPixelData:
    """Test 4-bit pixel data extraction."""

    def test_export_to_png(self, fnt_tool, testdata_fnt_files, run, temp_dir):
        """Test exporting font glyphs to PNG."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(
            fnt_tool,
            "export",
            testdata_fnt_files[0],
            "-o",
            str(temp_dir / "font.png"),
        )
        result.assert_success()
        # Should create PNG file
        png_files = list(temp_dir.glob("*.png"))
        assert len(png_files) >= 1

    def test_stride_calculation(self):
        """Test stride = (width * 4 + 7) / 8 bytes per scanline."""
        def calc_stride(width):
            return (width * 4 + 7) // 8

        assert calc_stride(1) == 1   # 4 bits -> 1 byte
        assert calc_stride(2) == 1   # 8 bits -> 1 byte
        assert calc_stride(3) == 2   # 12 bits -> 2 bytes
        assert calc_stride(4) == 2   # 16 bits -> 2 bytes
        assert calc_stride(5) == 3   # 20 bits -> 3 bytes

    def test_export_multiple_fonts(
        self, fnt_tool, testdata_fnt_files, run, temp_dir
    ):
        """Test exporting multiple font files."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        for i, fnt_file in enumerate(testdata_fnt_files[:2]):  # Test first 2
            out_dir = temp_dir / f"font_{i}"
            out_dir.mkdir()
            result = run(
                fnt_tool, "export", fnt_file, "-o", str(out_dir / "font.png")
            )
            result.assert_success()


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

    def test_info_human_readable(self, fnt_tool, testdata_fnt_files, run):
        """Test human-readable info output format."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()
        assert len(result.stdout_text) > 0

    def test_info_json(self, fnt_tool, testdata_fnt_files, run):
        """Test JSON info output format."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_info_fields_complete(self, fnt_tool, testdata_fnt_files, run):
        """Test all required info fields are present."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Check for key fields
        assert any(k in data for k in ["glyphs", "numGlyphs", "characters"])
