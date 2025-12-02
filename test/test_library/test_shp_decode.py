"""
Tests for SHP sprite format decoding (libwestwood).

Tests cover:
- TD/RA format header parsing
- Frame offset table parsing
- LCW (Format80) compressed frames
- XOR delta (Format40) frames
- Frame reference chain resolution
"""

import pytest
from pathlib import Path


class TestShpHeaderParsing:
    """Test SHP header parsing."""

    def test_valid_td_ra_header(self, shp_tool, run):
        """Test parsing valid TD/RA SHP header."""
        pytest.skip("Requires extracted SHP test files")

    def test_frame_count(self, shp_tool, run):
        """Test frame count extraction."""
        pytest.skip("Requires extracted SHP test files")

    def test_dimensions(self, shp_tool, run):
        """Test width/height extraction."""
        pytest.skip("Requires extracted SHP test files")

    def test_invalid_header(self, shp_tool, temp_file, run):
        """Test rejection of invalid SHP file."""
        bad_file = temp_file(".shp", b"\x00" * 100)
        result = run(shp_tool, "info", bad_file)
        result.assert_exit_code(2)

    def test_truncated_file(self, shp_tool, temp_file, run):
        """Test handling of truncated SHP file."""
        truncated = temp_file(".shp", b"\x00" * 5)
        result = run(shp_tool, "info", truncated)
        result.assert_exit_code(2)


class TestShpFrameOffsetTable:
    """Test frame offset table parsing."""

    def test_offset_table_size(self):
        """Test offset table has correct number of entries (frames + 2)."""
        pytest.skip("Requires extracted SHP test files")

    def test_sentinel_entries(self):
        """Test last two sentinel entries are parsed correctly."""
        pytest.skip("Requires extracted SHP test files")

    def test_data_format_extraction(self):
        """Test DataFormat byte extraction (0x80, 0x40, 0x20)."""
        pytest.skip("Requires extracted SHP test files")

    def test_ref_offset_extraction(self):
        """Test reference frame offset extraction."""
        pytest.skip("Requires extracted SHP test files")


class TestShpLcwFrames:
    """Test LCW (Format80) frame decompression."""

    def test_lcw_frame_decode(self):
        """Test decoding of LCW-only frame."""
        pytest.skip("Requires extracted SHP test files")

    def test_multiple_lcw_frames(self):
        """Test SHP with multiple LCW base frames."""
        pytest.skip("Requires extracted SHP test files")


class TestShpXorDeltaFrames:
    """Test XOR delta frame decoding."""

    def test_xor_delta_against_lcw(self):
        """Test XOR delta frame against LCW base (Format 0x40)."""
        pytest.skip("Requires extracted SHP test files")

    def test_xor_delta_against_previous(self):
        """Test XOR delta frame against previous (Format 0x20)."""
        pytest.skip("Requires extracted SHP test files")

    def test_frame_chain_resolution(self):
        """Test resolving frame reference chain."""
        pytest.skip("Requires extracted SHP test files")

    def test_cumulative_deltas(self):
        """Test cumulative XOR delta application."""
        pytest.skip("Requires extracted SHP test files")


class TestShpFrameBreakdown:
    """Test frame type analysis."""

    def test_count_lcw_frames(self, shp_tool, run):
        """Test counting LCW base frames."""
        pytest.skip("Requires extracted SHP test files")

    def test_count_xor_frames(self, shp_tool, run):
        """Test counting XOR delta frames."""
        pytest.skip("Requires extracted SHP test files")


class TestShpInfoOutput:
    """Test shp-tool info command output."""

    def test_info_human_readable(self, shp_tool, run):
        """Test human-readable info output format."""
        pytest.skip("Requires extracted SHP test files")

    def test_info_json(self, shp_tool, run):
        """Test JSON info output format."""
        pytest.skip("Requires extracted SHP test files")

    def test_info_fields_complete(self, shp_tool, run):
        """Test all required info fields are present."""
        # Required fields: format, frames, dimensions, delta_buffer,
        # compression, lcw_frames, xor_frames
        pytest.skip("Requires extracted SHP test files")
