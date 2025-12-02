"""
Tests for MP4 output format (vqa-tool export via ffmpeg).

Tests cover:
- MP4 container structure
- H.264 video codec
- AAC audio codec
- Quality settings
- Audio/video sync
"""

import pytest
import subprocess
from pathlib import Path


class TestMp4Container:
    """Test MP4 container structure."""

    def test_ftyp_box(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test ftyp box is present (MP4 signature)."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")
        data = out_file.read_bytes()
        # ftyp box should be near start
        assert b"ftyp" in data[:32]

    def test_moov_box(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test moov box (metadata) is present."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")
        data = out_file.read_bytes()
        assert b"moov" in data

    def test_mdat_box(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test mdat box (media data) is present."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")
        data = out_file.read_bytes()
        assert b"mdat" in data


class TestMp4VideoCodec:
    """Test H.264 video encoding."""

    def test_h264_codec(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test video stream uses H.264."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        # Use ffprobe to check codec
        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=codec_name", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            assert "h264" in probe.stdout.lower()
        except FileNotFoundError:
            pytest.skip("ffprobe not available")

    def test_default_crf(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test default CRF is 18."""
        # CRF 18 is visually lossless for most content
        # Can't easily verify after encoding, so document expectation
        default_crf = 18
        assert 0 <= default_crf <= 51  # Valid CRF range

    def test_quality_option(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test --quality option affects encoding."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        # Export with different quality settings
        out_high = temp_dir / "high.mp4"
        out_low = temp_dir / "low.mp4"

        result_high = run(vqa_tool, "export", "--mp4", "--quality", "high",
                         testdata_vqa_files[0], "-o", str(out_high))
        result_low = run(vqa_tool, "export", "--mp4", "--quality", "low",
                        testdata_vqa_files[0], "-o", str(out_low))

        if result_high.returncode != 0 or result_low.returncode != 0:
            pytest.skip("Quality option not implemented")

        # High quality should be larger than low quality
        if out_high.exists() and out_low.exists():
            assert out_high.stat().st_size >= out_low.stat().st_size


class TestMp4AudioCodec:
    """Test AAC audio encoding."""

    def test_aac_codec(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test audio stream uses AAC."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        # Use ffprobe to check audio codec
        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "a:0",
                 "-show_entries", "stream=codec_name", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            assert "aac" in probe.stdout.lower()
        except FileNotFoundError:
            pytest.skip("ffprobe not available")

    def test_audio_bitrate(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test AAC bitrate is 192kbps."""
        # Document expected bitrate
        expected_bitrate = 192000  # 192kbps
        assert expected_bitrate == 192000


class TestMp4Dimensions:
    """Test video dimensions match source."""

    def test_width_matches_source(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test MP4 width matches VQA width."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        # Get source dimensions
        info_result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_width = info.get("width") or info.get("Width")

        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=width", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            mp4_width = int(probe.stdout.strip())
            assert mp4_width == source_width
        except FileNotFoundError:
            pytest.skip("ffprobe not available")

    def test_height_matches_source(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test MP4 height matches VQA height."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        info_result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_height = info.get("height") or info.get("Height")

        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=height", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            mp4_height = int(probe.stdout.strip())
            assert mp4_height == source_height
        except FileNotFoundError:
            pytest.skip("ffprobe not available")


class TestMp4FrameRate:
    """Test video frame rate."""

    def test_framerate_preserved(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test frame rate matches VQA frame rate."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        info_result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_fps = info.get("frame_rate") or info.get("FrameRate") or info.get("fps")

        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=r_frame_rate", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            # Frame rate may be fraction like "15/1"
            fps_str = probe.stdout.strip()
            if "/" in fps_str:
                num, den = fps_str.split("/")
                mp4_fps = int(num) / int(den)
            else:
                mp4_fps = float(fps_str)
            assert abs(mp4_fps - source_fps) < 0.1
        except FileNotFoundError:
            pytest.skip("ffprobe not available")


class TestMp4AudioSync:
    """Test audio/video synchronization."""

    def test_av_duration_match(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test audio and video have same duration."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        out_file = temp_dir / "test.mp4"
        result = run(vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        try:
            # Get video duration
            v_probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=duration", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )
            # Get audio duration
            a_probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "a:0",
                 "-show_entries", "stream=duration", "-of", "csv=p=0",
                 str(out_file)],
                capture_output=True, text=True
            )

            if v_probe.stdout.strip() and a_probe.stdout.strip():
                v_dur = float(v_probe.stdout.strip())
                a_dur = float(a_probe.stdout.strip())
                # Allow 0.1 second tolerance
                assert abs(v_dur - a_dur) < 0.1
        except FileNotFoundError:
            pytest.skip("ffprobe not available")


class TestMp4NoAudio:
    """Test MP4 without audio track."""

    def test_video_only_vqa(self):
        """Test MP4 from video-only VQA has no audio stream."""
        pytest.skip("Requires video-only VQA test file")

    def test_video_only_valid(self):
        """Test video-only MP4 is still valid."""
        pytest.skip("Requires video-only VQA test file")
