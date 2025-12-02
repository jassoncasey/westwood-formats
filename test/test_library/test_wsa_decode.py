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

    def test_valid_header(self, wsa_tool, run):
        """Test parsing valid WSA header."""
        pytest.skip("Requires extracted WSA test files")

    def test_frame_count(self, wsa_tool, run):
        """Test frame count extraction."""
        pytest.skip("Requires extracted WSA test files")

    def test_dimensions(self, wsa_tool, run):
        """Test width/height extraction."""
        pytest.skip("Requires extracted WSA test files")

    def test_delta_buffer_size(self, wsa_tool, run):
        """Test delta buffer size extraction."""
        pytest.skip("Requires extracted WSA test files")

    def test_invalid_header(self, wsa_tool, temp_file, run):
        """Test rejection of invalid WSA file."""
        bad_file = temp_file(".wsa", b"\x00" * 100)
        result = run(wsa_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestWsaEmbeddedPalette:
    """Test embedded palette handling."""

    def test_detect_embedded_palette(self, wsa_tool, run):
        """Test detection of embedded palette (Flags bit 0)."""
        pytest.skip("Requires extracted WSA test files")

    def test_no_embedded_palette(self, wsa_tool, run):
        """Test WSA without embedded palette."""
        pytest.skip("Requires extracted WSA test files")

    def test_palette_at_correct_offset(self):
        """Test palette is read from correct offset after frame offsets."""
        pytest.skip("Requires extracted WSA test files")


class TestWsaFrameOffsetTable:
    """Test frame offset table parsing."""

    def test_offset_table_size(self):
        """Test offset table has NumFrames + 2 entries."""
        pytest.skip("Requires extracted WSA test files")

    def test_missing_first_frame(self):
        """Test handling when FrameOffsets[0] == 0 (no initial state)."""
        pytest.skip("Requires extracted WSA test files")


class TestWsaLoopFrame:
    """Test loop frame handling."""

    def test_detect_loop_frame(self, wsa_tool, run):
        """Test detection of loop frame (non-zero final entry)."""
        pytest.skip("Requires extracted WSA test files")

    def test_no_loop_frame(self, wsa_tool, run):
        """Test WSA without loop frame (zero final entry)."""
        pytest.skip("Requires extracted WSA test files")


class TestWsaFrameDecoding:
    """Test frame decoding (Format40 + LCW)."""

    def test_first_frame_decode(self):
        """Test decoding first frame."""
        pytest.skip("Requires extracted WSA test files")

    def test_delta_frame_decode(self):
        """Test decoding delta frame (XOR against previous)."""
        pytest.skip("Requires extracted WSA test files")

    def test_cumulative_decoding(self):
        """Test cumulative frame buffer updates."""
        pytest.skip("Requires extracted WSA test files")


class TestWsaInfoOutput:
    """Test wsa-tool info command output."""

    def test_info_human_readable(self, wsa_tool, run):
        """Test human-readable info output format."""
        pytest.skip("Requires extracted WSA test files")

    def test_info_json(self, wsa_tool, run):
        """Test JSON info output format."""
        pytest.skip("Requires extracted WSA test files")

    def test_info_fields_complete(self, wsa_tool, run):
        """Test all required info fields are present."""
        # Required fields: frames, dimensions, delta_buffer,
        # has_embedded_palette, has_loop_frame, compression
        pytest.skip("Requires extracted WSA test files")
