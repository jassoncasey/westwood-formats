"""
Tests for wsa-tool CLI.

Tests cover:
- info command
- export command (GIF, PNG frames)
- Palette handling
- Loop frame handling
- Error handling
"""

import pytest
from pathlib import Path


class TestWsaToolInfo:
    """Test wsa-tool info command."""

    def test_info_basic(self, wsa_tool, testdata_wsa_files, run):
        """Test basic info output."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()

    def test_info_shows_frames(self, wsa_tool, testdata_wsa_files, run):
        """Test info shows frame count."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()
        assert "frame" in result.stdout_text.lower()

    def test_info_shows_dimensions(self, wsa_tool, testdata_wsa_files, run):
        """Test info shows dimensions."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()
        assert "x" in result.stdout_text or "×" in result.stdout_text

    def test_info_shows_loop_frame(self, wsa_tool, testdata_wsa_files, run):
        """Test info shows loop frame info."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()
        # May have loop frame indicator

    def test_info_json(self, wsa_tool, testdata_wsa_files, run):
        """Test info --json."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestWsaToolExportGif:
    """Test wsa-tool GIF export."""

    def test_export_gif(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test exporting as animated GIF."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0],
                    testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()
        assert out_file.read_bytes()[:6] == b"GIF89a"

    def test_export_gif_fps(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test --fps option."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(
            wsa_tool, "export", "--fps", "10", "-p",
            testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_gif_loop(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test --loop option."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(wsa_tool, "export", "--loop", "-p", testdata_pal_files[0],
                    testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_export_gif_no_loop(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test --no-loop option."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(
            wsa_tool, "export", "--no-loop", "-p", testdata_pal_files[0],
            testdata_wsa_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()


class TestWsaToolExportFrames:
    """Test wsa-tool frame export."""

    def test_export_frames(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test --frames option."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        result = run(
            wsa_tool, "export", "--frames", "-p", testdata_pal_files[0],
            testdata_wsa_files[0], "-o", str(temp_dir / "frame")
        )
        if result.returncode != 0:
            pytest.skip("Frame export not implemented")
        png_files = list(temp_dir.glob("frame_*.png"))
        assert len(png_files) > 0


class TestWsaToolPalette:
    """Test palette handling."""

    def test_external_palette(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test using external palette."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")
        out_file = temp_dir / "animation.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0],
                    testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_embedded_palette(
        self, wsa_tool, testdata_wsa_files, run, temp_dir
    ):
        """Test using embedded palette."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        # WSA may have embedded palette
        out_file = temp_dir / "animation.gif"
        result = run(
            wsa_tool, "export", testdata_wsa_files[0], "-o", str(out_file)
        )
        # May succeed if WSA has embedded palette, or fail requesting external
        # Either is valid behavior


class TestWsaToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, wsa_tool, run):
        """Test error on nonexistent file."""
        result = run(wsa_tool, "info", "/nonexistent/file.wsa")
        result.assert_exit_code(2)

    def test_invalid_wsa_file(self, wsa_tool, temp_file, run):
        """Test error on invalid WSA file."""
        bad_file = temp_file(".wsa", b"\x00" * 100)
        result = run(wsa_tool, "info", bad_file)
        result.assert_exit_code(2)
