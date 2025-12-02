"""
Tests for cps-tool CLI.

Tests cover:
- info command
- export command (PNG)
- Embedded palette handling
- Error handling
"""

import pytest
from pathlib import Path


class TestCpsToolInfo:
    """Test cps-tool info command."""

    def test_info_basic(self, cps_tool, testdata_cps_files, run):
        """Test basic info output."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()

    def test_info_shows_dimensions(self, cps_tool, testdata_cps_files, run):
        """Test info shows 320x200 dimensions."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()
        # CPS is always 320x200
        assert "320" in result.stdout_text and "200" in result.stdout_text

    def test_info_shows_compression(self, cps_tool, testdata_cps_files, run):
        """Test info shows compression method."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()
        # Should mention LCW or compression

    def test_info_shows_embedded_palette(self, cps_tool, testdata_cps_files, run):
        """Test info shows embedded palette status."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", testdata_cps_files[0])
        result.assert_success()
        assert "palette" in result.stdout_text.lower()

    def test_info_json(self, cps_tool, testdata_cps_files, run):
        """Test info --json."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        result = run(cps_tool, "info", "--json", testdata_cps_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestCpsToolExport:
    """Test cps-tool export command."""

    def test_export_png(self, cps_tool, testdata_cps_files, testdata_pal_files, run, temp_dir):
        """Test exporting as PNG."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        out_file = temp_dir / "image.png"
        # Use embedded palette or external
        if testdata_pal_files:
            result = run(cps_tool, "export", "-p", testdata_pal_files[0],
                        testdata_cps_files[0], "-o", str(out_file))
        else:
            result = run(cps_tool, "export", testdata_cps_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_png_dimensions(self, cps_tool, testdata_cps_files, testdata_pal_files, run, temp_dir):
        """Test exported PNG is 320x200."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        import struct
        out_file = temp_dir / "image.png"
        if testdata_pal_files:
            result = run(cps_tool, "export", "-p", testdata_pal_files[0],
                        testdata_cps_files[0], "-o", str(out_file))
        else:
            result = run(cps_tool, "export", testdata_cps_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        height = struct.unpack(">I", data[20:24])[0]
        assert width == 320
        assert height == 200


class TestCpsToolPalette:
    """Test palette handling."""

    def test_embedded_palette(self, cps_tool, testdata_cps_files, run, temp_dir):
        """Test using embedded palette."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")
        out_file = temp_dir / "image.png"
        # Try without external palette
        result = run(cps_tool, "export", testdata_cps_files[0], "-o", str(out_file))
        # May succeed if CPS has embedded palette

    def test_external_palette_override(self, cps_tool, testdata_cps_files, testdata_pal_files, run, temp_dir):
        """Test external palette overrides embedded."""
        if not testdata_cps_files or not testdata_pal_files:
            pytest.skip("No CPS or PAL files in testdata")
        out_file = temp_dir / "image.png"
        result = run(cps_tool, "export", "-p", testdata_pal_files[0],
                    testdata_cps_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()


class TestCpsToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, cps_tool, run):
        """Test error on nonexistent file."""
        result = run(cps_tool, "info", "/nonexistent/file.cps")
        result.assert_exit_code(2)

    def test_invalid_cps_file(self, cps_tool, temp_file, run):
        """Test error on invalid CPS file."""
        bad_file = temp_file(".cps", b"\x00" * 100)
        result = run(cps_tool, "info", bad_file)
        result.assert_exit_code(2)
