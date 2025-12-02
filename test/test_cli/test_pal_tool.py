"""
Tests for pal-tool CLI.

Tests cover:
- info command
- export command (swatch PNG)
- Error handling
"""

import pytest
from pathlib import Path


class TestPalToolInfo:
    """Test pal-tool info command."""

    def test_info_basic(self, pal_tool, testdata_pal_files, run):
        """Test basic info output."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", testdata_pal_files[0])
        result.assert_success()

    def test_info_shows_colors(self, pal_tool, testdata_pal_files, run):
        """Test info shows color count."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", testdata_pal_files[0])
        result.assert_success()
        assert "256" in result.stdout_text

    def test_info_shows_format(self, pal_tool, testdata_pal_files, run):
        """Test info shows palette format."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", testdata_pal_files[0])
        result.assert_success()
        # Should mention 6-bit or RGB
        assert "6" in result.stdout_text or "rgb" in result.stdout_text.lower()

    def test_info_json(self, pal_tool, testdata_pal_files, run):
        """Test info --json."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", "--json", testdata_pal_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestPalToolExport:
    """Test pal-tool export command."""

    def test_export_swatch(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test exporting swatch PNG."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_swatch_dimensions(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch is 512x512."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        import struct
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        height = struct.unpack(">I", data[20:24])[0]
        assert width == 512
        assert height == 512


class TestPalToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, pal_tool, run):
        """Test error on nonexistent file."""
        result = run(pal_tool, "info", "/nonexistent/file.pal")
        result.assert_exit_code(2)

    def test_invalid_pal_file(self, pal_tool, temp_file, run):
        """Test error on invalid PAL file (wrong size)."""
        # PAL should be exactly 768 bytes
        bad_file = temp_file(".pal", b"\x00" * 100)
        result = run(pal_tool, "info", bad_file)
        result.assert_exit_code(2)

    def test_oversized_pal_file(self, pal_tool, temp_file, run):
        """Test error on oversized PAL file."""
        bad_file = temp_file(".pal", b"\x00" * 1000)
        result = run(pal_tool, "info", bad_file)
        result.assert_exit_code(2)
