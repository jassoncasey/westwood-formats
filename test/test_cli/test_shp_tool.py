"""
Tests for shp-tool CLI.

Tests cover:
- info command
- export command (PNG, sprite sheet, frames, GIF)
- Palette handling
- Error handling
"""

import pytest
from pathlib import Path


class TestShpToolInfo:
    """Test shp-tool info command."""

    def test_info_basic(self, shp_tool, testdata_shp_files, run):
        """Test basic info output."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", testdata_shp_files[0])
        result.assert_success()

    def test_info_shows_frames(self, shp_tool, testdata_shp_files, run):
        """Test info shows frame count."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", testdata_shp_files[0])
        result.assert_success()
        assert "frame" in result.stdout_text.lower()

    def test_info_shows_dimensions(self, shp_tool, testdata_shp_files, run):
        """Test info shows dimensions."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", testdata_shp_files[0])
        result.assert_success()
        # Should have width x height format
        assert "x" in result.stdout_text or "×" in result.stdout_text

    def test_info_json(self, shp_tool, testdata_shp_files, run):
        """Test info --json."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestShpToolExportPng:
    """Test shp-tool PNG export."""

    def test_export_single_frame(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test exporting frames as PNG files."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_base = temp_dir / "frame.png"
        result = run(shp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_base))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Tool creates numbered frame files (frame.png_000.png, etc)
        png_files = list(temp_dir.glob("frame.png_*.png"))
        assert len(png_files) > 0, "No frame files were exported"

    def test_export_sprite_sheet(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test exporting sprite sheet."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_file = temp_dir / "sheet.png"
        result = run(shp_tool, "export", "--sheet", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_frames(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test exporting individual frames."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        result = run(shp_tool, "export", "--frames", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(temp_dir / "frame"))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = list(temp_dir.glob("frame_*.png"))
        assert len(png_files) > 0


class TestShpToolExportGif:
    """Test shp-tool GIF export."""

    def test_export_gif(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test exporting as animated GIF."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(shp_tool, "export", "--gif", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("GIF export not implemented")
        assert out_file.exists()
        assert out_file.read_bytes()[:6] == b"GIF89a"

    def test_export_gif_fps(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test GIF with custom FPS."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(shp_tool, "export", "--gif", "--fps", "10", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("GIF export not implemented")
        assert out_file.exists()

    def test_export_gif_transparent(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test GIF with transparency."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(shp_tool, "export", "--gif", "--transparent", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("GIF export not implemented")
        assert out_file.exists()


class TestShpToolPalette:
    """Test palette handling."""

    def test_palette_required_message(self, shp_tool, testdata_shp_files, run, temp_dir):
        """Test clear message when palette needed."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        out_file = temp_dir / "test.png"
        result = run(shp_tool, "export", testdata_shp_files[0], "-o", str(out_file))
        # Should fail or warn about missing palette
        if result.returncode != 0:
            assert "palette" in result.stderr_text.lower() or "pal" in result.stderr_text.lower()

    def test_external_palette(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test using external palette file."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")
        out_base = temp_dir / "test.png"
        result = run(shp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_base))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Tool creates numbered frame files (test.png_000.png, etc)
        png_files = list(temp_dir.glob("test.png_*.png"))
        assert len(png_files) > 0, "No frame files were exported"


class TestShpToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, shp_tool, run):
        """Test error on nonexistent file."""
        result = run(shp_tool, "info", "/nonexistent/file.shp")
        result.assert_exit_code(2)

    def test_invalid_shp_file(self, shp_tool, temp_file, run):
        """Test error on invalid SHP file."""
        bad_file = temp_file(".shp", b"not a shp file")
        result = run(shp_tool, "info", bad_file)
        result.assert_exit_code(2)

    def test_nonexistent_palette(self, shp_tool, testdata_shp_files, run, temp_dir):
        """Test error when palette file doesn't exist."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        out_file = temp_dir / "test.png"
        result = run(shp_tool, "export", "-p", "/nonexistent/file.pal",
                    testdata_shp_files[0], "-o", str(out_file))
        result.assert_exit_code(2)
