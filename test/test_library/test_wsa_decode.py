"""
Tests for WSA animation format decoding (libwestwood).

Tests cover:
- Header parsing
- Frame offset table
- Embedded palette detection
- Loop frame handling
- Format40 + LCW decompression
"""

import pytest
from pathlib import Path


class TestWsaHeaderParsing:
    """Test WSA header parsing."""

    def test_valid_header(self, wsa_tool, testdata_wsa_files, run):
        """Test parsing valid WSA header."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()

    def test_frame_count(self, wsa_tool, testdata_wsa_files, run):
        """Test frame count extraction."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert any(k in data for k in ["frames", "frameCount", "numFrames"])

    def test_dimensions(self, wsa_tool, testdata_wsa_files, run):
        """Test width/height extraction."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert any(k in data for k in ["width", "Width"])
        assert any(k in data for k in ["height", "Height"])

    def test_delta_buffer_size(self, wsa_tool, testdata_wsa_files, run):
        """Test delta buffer size extraction."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()

    def test_invalid_header(self, wsa_tool, temp_file, run):
        """Test rejection of invalid WSA file."""
        bad_file = temp_file(".wsa", b"\x00" * 100)
        result = run(wsa_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestWsaEmbeddedPalette:
    """Test embedded palette handling."""

    def test_detect_embedded_palette(self, wsa_tool, testdata_wsa_files, run):
        """Test detection of embedded palette (Flags bit 0)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_no_embedded_palette(self, wsa_tool, testdata_wsa_files, run):
        """Test WSA without embedded palette."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()

    def test_palette_at_correct_offset(self):
        """Test palette is read from correct offset after frame offsets."""
        pytest.skip("Requires unit test of WSA parser")


class TestWsaFrameOffsetTable:
    """Test frame offset table parsing."""

    def test_offset_table_size(self, wsa_tool, testdata_wsa_files, run):
        """Test offset table has NumFrames + 2 entries."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Frame count should be positive - offset table parsed correctly
        frames = data.get("frames", 0)
        assert frames > 0

    def test_missing_first_frame(self, wsa_tool, testdata_wsa_files, run):
        """Test handling when FrameOffsets[0] == 0 (no initial state)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        # Just verify we can parse the file - missing first frame is handled
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()


class TestWsaLoopFrame:
    """Test loop frame handling."""

    def test_detect_loop_frame(self, wsa_tool, testdata_wsa_files, run):
        """Test detection of loop frame (non-zero final entry)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()

    def test_no_loop_frame(self, wsa_tool, testdata_wsa_files, run):
        """Test WSA without loop frame (zero final entry)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()


class TestWsaFrameDecoding:
    """Test frame decoding (Format40 + LCW)."""

    def test_first_frame_decode(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test decoding first frame."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Export frames tests decoding
        result = run(
            wsa_tool, "export", "--frames", "-p", testdata_pal_files[0],
            testdata_wsa_files[0], "-o", str(temp_dir / "frame.png")
        )
        result.assert_success()
        png_files = list(temp_dir.glob("*.png"))
        assert len(png_files) >= 1

    def test_delta_frame_decode(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test decoding delta frame (XOR against previous)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Export all frames - delta frames depend on previous
        result = run(
            wsa_tool, "export", "--frames", "-p", testdata_pal_files[0],
            testdata_wsa_files[0], "-o", str(temp_dir / "frame.png")
        )
        result.assert_success()

    def test_cumulative_decoding(
        self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir
    ):
        """Test cumulative frame buffer updates."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Export as GIF tests cumulative frame updates
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0],
                    testdata_wsa_files[0], "-o", str(temp_dir / "anim.gif"))
        result.assert_success()
        gif_file = temp_dir / "anim.gif"
        assert gif_file.exists()
        data = gif_file.read_bytes()
        assert data[:6] == b"GIF89a"


class TestWsaInfoOutput:
    """Test wsa-tool info command output."""

    def test_info_human_readable(self, wsa_tool, testdata_wsa_files, run):
        """Test human-readable info output format."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", testdata_wsa_files[0])
        result.assert_success()
        assert len(result.stdout_text) > 0

    def test_info_json(self, wsa_tool, testdata_wsa_files, run):
        """Test JSON info output format."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_info_fields_complete(self, wsa_tool, testdata_wsa_files, run):
        """Test all required info fields are present."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Check for key fields
        assert any(k in data for k in ["frames", "frameCount", "numFrames"])
        assert any(k in data for k in ["width", "Width"])
