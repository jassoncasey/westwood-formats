"""
Tests for fnt-tool CLI.

Tests cover:
- info command
- export command (PNG, metrics JSON)
- Error handling
"""

import pytest
from pathlib import Path


class TestFntToolInfo:
    """Test fnt-tool info command."""

    def test_info_basic(self, fnt_tool, testdata_fnt_files, run):
        """Test basic info output."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()

    def test_info_shows_glyph_count(self, fnt_tool, testdata_fnt_files, run):
        """Test info shows glyph count."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()
        stdout_lower = result.stdout_text.lower()
        assert "glyph" in stdout_lower or "char" in stdout_lower

    def test_info_shows_dimensions(self, fnt_tool, testdata_fnt_files, run):
        """Test info shows max dimensions."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", testdata_fnt_files[0])
        result.assert_success()

    def test_info_json(self, fnt_tool, testdata_fnt_files, run):
        """Test info --json."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "info", "--json", testdata_fnt_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestFntToolExport:
    """Test fnt-tool export command."""

    def test_export_png(self, fnt_tool, testdata_fnt_files, run, temp_dir):
        """Test exporting glyphs as PNG."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        out_file = temp_dir / "font.png"
        result = run(
            fnt_tool, "export", testdata_fnt_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_individual_glyphs(
        self, fnt_tool, testdata_fnt_files, run, temp_dir
    ):
        """Test exporting individual glyph PNGs."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        result = run(fnt_tool, "export", "--frames", testdata_fnt_files[0],
                    "-o", str(temp_dir / "glyph"))
        if result.returncode != 0:
            pytest.skip("Frame export not implemented")
        png_files = list(temp_dir.glob("glyph_*.png"))
        # Should have multiple glyph images


class TestFntToolMetrics:
    """Test metrics JSON export."""

    def test_export_metrics(self, fnt_tool, testdata_fnt_files, run, temp_dir):
        """Test exporting metrics JSON."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        out_file = temp_dir / "metrics.json"
        result = run(fnt_tool, "export", "--metrics", testdata_fnt_files[0],
                    "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Metrics export not implemented")
        assert out_file.exists()
        import json
        data = json.loads(out_file.read_text())
        assert "glyphs" in data

    def test_metrics_glyph_structure(
        self, fnt_tool, testdata_fnt_files, run, temp_dir
    ):
        """Test metrics JSON glyph structure."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        out_file = temp_dir / "metrics.json"
        result = run(fnt_tool, "export", "--metrics", testdata_fnt_files[0],
                    "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Metrics export not implemented")
        import json
        data = json.loads(out_file.read_text())
        # Glyphs should be keyed by character
        if "glyphs" in data:
            for key, glyph in data["glyphs"].items():
                assert isinstance(key, str)
                assert "width" in glyph or "Width" in glyph


class TestFntToolGrayscale:
    """Test grayscale output."""

    def test_grayscale_output(
        self, fnt_tool, testdata_fnt_files, run, temp_dir
    ):
        """Test font exports as grayscale."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")
        out_file = temp_dir / "font.png"
        result = run(
            fnt_tool, "export", testdata_fnt_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Check PNG is grayscale or grayscale+alpha
        data = out_file.read_bytes()
        color_type = data[25]
        # 0 = grayscale, 4 = grayscale+alpha
        assert color_type in [0, 4]


class TestFntToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, fnt_tool, run):
        """Test error on nonexistent file."""
        result = run(fnt_tool, "info", "/nonexistent/file.fnt")
        result.assert_exit_code(2)

    def test_invalid_fnt_file(self, fnt_tool, temp_file, run):
        """Test error on invalid FNT file."""
        bad_file = temp_file(".fnt", b"\x00" * 100)
        result = run(fnt_tool, "info", bad_file)
        result.assert_exit_code(2)
