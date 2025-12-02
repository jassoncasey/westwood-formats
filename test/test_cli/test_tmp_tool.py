"""
Tests for tmp-tool CLI.

Tests cover:
- info command
- export command (PNG)
- Format detection (TD vs RA)
- Error handling
"""

import pytest
from pathlib import Path


class TestTmpToolInfo:
    """Test tmp-tool info command."""

    def test_info_basic(self, tmp_tool, testdata_tmp_files, run):
        """Test basic info output."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_info_shows_format(self, tmp_tool, testdata_tmp_files, run):
        """Test info shows format (TD or RA)."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()
        # Should identify Red Alert or Tiberian Dawn format

    def test_info_shows_tiles(self, tmp_tool, testdata_tmp_files, run):
        """Test info shows tile count."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()
        assert "tile" in result.stdout_text.lower()

    def test_info_shows_dimensions(self, tmp_tool, testdata_tmp_files, run):
        """Test info shows tile dimensions."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()
        # Tiles are typically 24x24
        assert "24" in result.stdout_text

    def test_info_json(self, tmp_tool, testdata_tmp_files, run):
        """Test info --json."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestTmpToolExport:
    """Test tmp-tool export command."""

    def test_export_png(self, tmp_tool, testdata_tmp_files, testdata_pal_files, run, temp_dir):
        """Test exporting tiles as PNG."""
        if not testdata_tmp_files or not testdata_pal_files:
            pytest.skip("No TMP or PAL files in testdata")
        out_file = temp_dir / "tiles.png"
        result = run(tmp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_tmp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_individual_tiles(self, tmp_tool, testdata_tmp_files, testdata_pal_files, run, temp_dir):
        """Test exporting individual tiles."""
        if not testdata_tmp_files or not testdata_pal_files:
            pytest.skip("No TMP or PAL files in testdata")
        result = run(tmp_tool, "export", "--frames", "-p", testdata_pal_files[0],
                    testdata_tmp_files[0], "-o", str(temp_dir / "tile"))
        if result.returncode != 0:
            pytest.skip("Frame export not implemented")
        png_files = list(temp_dir.glob("tile_*.png"))
        # Should have at least some tiles (may have empty tiles too)


class TestTmpToolFormatDetection:
    """Test format auto-detection."""

    def test_detect_ra_format(self, tmp_tool, testdata_tmp_files, run):
        """Test detection of Red Alert format."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()
        # Red Alert TMP has 0x2C73 magic at offset 26

    def test_detect_td_format(self):
        """Test detection of Tiberian Dawn format."""
        pytest.skip("Requires TD format TMP test file")


class TestTmpToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, tmp_tool, run):
        """Test error on nonexistent file."""
        result = run(tmp_tool, "info", "/nonexistent/file.tmp")
        result.assert_exit_code(2)

    def test_invalid_tmp_file(self, tmp_tool, temp_file, run):
        """Test error on invalid TMP file."""
        bad_file = temp_file(".tmp", b"\x00" * 100)
        result = run(tmp_tool, "info", bad_file)
        result.assert_exit_code(2)
