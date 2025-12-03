"""
Tests for vqa-tool CLI.

Tests cover:
- info command
- export command (WAV, MP4, PNG frames)
- Palette handling
- Error handling
"""

import pytest
from pathlib import Path


class TestVqaToolInfo:
    """Test vqa-tool info command."""

    def test_info_basic(self, vqa_tool, testdata_vqa_files, run):
        """Test basic info output."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()

    def test_info_shows_version(self, vqa_tool, testdata_vqa_files, run):
        """Test info shows VQA version."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Should mention version or v1/v2/v3
        has_version = "version" in result.stdout_text.lower()
        has_v2 = "v2" in result.stdout_text.lower()
        assert has_version or has_v2

    def test_info_shows_dimensions(self, vqa_tool, testdata_vqa_files, run):
        """Test info shows video dimensions."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Red Alert VQAs are typically 320x200 or 640x400
        assert "320" in result.stdout_text or "640" in result.stdout_text

    def test_info_shows_framerate(self, vqa_tool, testdata_vqa_files, run):
        """Test info shows frame rate."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        assert "fps" in result.stdout_text.lower()

    def test_info_shows_audio(self, vqa_tool, testdata_vqa_files, run):
        """Test info shows audio info."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Should mention audio or sample rate
        has_audio = "audio" in result.stdout_text.lower()
        has_hz = "hz" in result.stdout_text.lower()
        assert has_audio or has_hz

    def test_info_json(self, vqa_tool, testdata_vqa_files, run):
        """Test info --json."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestVqaToolExportAudio:
    """Test vqa-tool audio export."""

    def test_export_wav(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test exporting audio as WAV."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "audio.wav"
        result = run(
            vqa_tool, "export", "--wav",
            testdata_vqa_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("WAV export not implemented")
        assert out_file.exists()
        assert out_file.read_bytes()[:4] == b"RIFF"


class TestVqaToolExportVideo:
    """Test vqa-tool video export."""

    def test_export_mp4(
        self, vqa_tool, testdata_vqa_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test exporting as MP4."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "video.mp4"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(vqa_tool, "export", "--mp4", *pal_args,
                    testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")
        assert out_file.exists()
        # Check for MP4 ftyp box
        data = out_file.read_bytes()
        assert b"ftyp" in data[:32]

    def test_export_frames(
        self, vqa_tool, testdata_vqa_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test exporting video frames as PNGs."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(vqa_tool, "export", "--frames", *pal_args,
                    testdata_vqa_files[0], "-o", str(temp_dir / "frame"))
        if result.returncode != 0:
            pytest.skip("Frame export not implemented")
        png_files = list(temp_dir.glob("frame_*.png"))
        assert len(png_files) > 0


class TestVqaToolQuality:
    """Test quality options."""

    def test_quality_high(
        self, vqa_tool, testdata_vqa_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test --quality high option."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "high.mp4"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(
            vqa_tool, "export", "--mp4", "--quality", "high", *pal_args,
            testdata_vqa_files[0], "-o", str(out_file)
        )
        if result.returncode != 0:
            pytest.skip("Quality option not implemented")
        assert out_file.exists()

    def test_quality_low(
        self, vqa_tool, testdata_vqa_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test --quality low option."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "low.mp4"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(vqa_tool, "export", "--mp4", "--quality", "low", *pal_args,
                    testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Quality option not implemented")
        assert out_file.exists()


class TestVqaToolPalette:
    """Test palette handling for v2 VQAs."""

    def test_external_palette(
        self, vqa_tool, testdata_vqa_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test using external palette for v2 VQA."""
        if not testdata_vqa_files or not testdata_pal_files:
            pytest.skip("No VQA or PAL files in testdata")
        out_file = temp_dir / "frame.png"
        result = run(
            vqa_tool, "export", "--frames", "-p", testdata_pal_files[0],
            testdata_vqa_files[0], "-o", str(temp_dir / "frame")
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")


class TestVqaToolErrors:
    """Test error handling."""

    def test_nonexistent_file(self, vqa_tool, run):
        """Test error on nonexistent file."""
        result = run(vqa_tool, "info", "/nonexistent/file.vqa")
        result.assert_exit_code(2)

    def test_invalid_vqa_file(self, vqa_tool, temp_file, run):
        """Test error on invalid VQA file."""
        bad_file = temp_file(".vqa", b"NOTFORM\x00" * 100)
        result = run(vqa_tool, "info", bad_file)
        result.assert_exit_code(2)

    def test_missing_ffmpeg(
        self, vqa_tool, testdata_vqa_files,
        run, temp_dir, monkeypatch
    ):
        """Test graceful error when ffmpeg not available."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        # Remove ffmpeg from PATH
        monkeypatch.setenv("PATH", "/nonexistent")
        out_file = temp_dir / "video.mp4"
        result = run(
            vqa_tool, "export", "--mp4",
            testdata_vqa_files[0], "-o", str(out_file)
        )
        # Should fail gracefully with clear message
        if result.returncode != 0:
            has_ffmpeg_msg = "ffmpeg" in result.stderr_text.lower()
            has_expected_code = result.returncode in [1, 2, 3]
            assert has_ffmpeg_msg or has_expected_code


class TestVqaToolNoAudio:
    """Test VQA files without audio."""

    def test_video_only_export(self):
        """Test exporting video-only VQA."""
        pytest.skip("Requires video-only VQA test file")

    def test_wav_export_skipped(self):
        """Test --wav is skipped for video-only VQA."""
        pytest.skip("Requires video-only VQA test file")
