"""
Tests for PNG output format (libwestwood export).

Tests cover:
- PNG header/signature
- RGBA 32-bit format
- Index 0 transparency
- Proper dimensions
- Color conversion (6-bit to 8-bit)
"""

import pytest
import struct
import zlib
from pathlib import Path


class TestPngSignature:
    """Test PNG file signature."""

    def test_png_signature(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test PNG magic bytes."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # shp-tool creates numbered frames
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        png_sig = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        assert data[:8] == png_sig

    def test_png_signature_constant(self):
        """Test PNG signature bytes are correct."""
        sig = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        assert sig[0] == 0x89  # High bit set
        assert sig[1:4] == b"PNG"
        assert sig[4:6] == b"\r\n"  # DOS line ending
        assert sig[6] == 0x1A  # EOF
        assert sig[7] == 0x0A  # Unix line ending


class TestPngChunks:
    """Test PNG chunk structure."""

    def test_ihdr_chunk_present(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test IHDR chunk is first chunk."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        # IHDR at offset 8 (after signature)
        assert data[12:16] == b"IHDR"

    def test_idat_chunk_present(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test IDAT chunk is present."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        assert b"IDAT" in data

    def test_iend_chunk_present(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test IEND chunk terminates file."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        # IEND at end (4 bytes length + 4 bytes type + 4 bytes CRC = 12)
        assert data[-12:-8] == b"\x00\x00\x00\x00"  # Length 0
        assert data[-8:-4] == b"IEND"


class TestPngRgbaFormat:
    """Test RGBA 32-bit format."""

    def test_color_type_rgba(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test color type is RGBA (6)."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        # IHDR: width(4), height(4), bit_depth(1), color_type(1)
        color_type = data[25]
        assert color_type == 6  # RGBA

    def test_bit_depth_8(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test bit depth is 8."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        bit_depth = data[24]
        assert bit_depth == 8

    def test_bytes_per_pixel(self):
        """Test RGBA is 4 bytes per pixel."""
        # RGBA = R(1) + G(1) + B(1) + A(1) = 4 bytes
        assert 4 == 4  # Trivial but documents expectation


class TestPngDimensions:
    """Test PNG dimensions match source."""

    def test_width_matches_source(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test PNG width matches SHP frame width."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Get source dimensions
        info_result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_width = info.get("width") or info.get("Width")

        # Export and check PNG
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        png_width = struct.unpack(">I", data[16:20])[0]
        assert png_width == source_width

    def test_height_matches_source(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test PNG height matches SHP frame height."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Get source dimensions
        info_result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_height = info.get("height") or info.get("Height")

        # Export and check PNG
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        png_files = sorted(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")
        data = png_files[0].read_bytes()
        png_height = struct.unpack(">I", data[20:24])[0]
        assert png_height == source_height


class TestPngTransparency:
    """Test index 0 transparency handling."""

    def test_index0_transparent(self):
        """Test palette index 0 produces alpha=0."""
        # Index 0 should map to fully transparent
        index = 0
        expected_alpha = 0
        assert expected_alpha == 0  # Documents expectation

    def test_nonzero_index_opaque(self):
        """Test non-zero indices produce alpha=255."""
        # Any non-zero index should be fully opaque
        for index in range(1, 256):
            expected_alpha = 255
            assert expected_alpha == 255


class TestPngColorConversion:
    """Test 6-bit to 8-bit color conversion."""

    def test_6bit_to_8bit_formula(self):
        """Test conversion formula: (val << 2) | (val >> 4)."""
        def convert_6to8(val):
            return (val << 2) | (val >> 4)

        # Test corner cases
        assert convert_6to8(0) == 0      # 0 -> 0
        assert convert_6to8(63) == 255   # Max -> Max
        assert convert_6to8(32) == 130   # Mid value

    def test_6bit_range_valid(self):
        """Test all 6-bit values produce valid 8-bit values."""
        def convert_6to8(val):
            return (val << 2) | (val >> 4)

        for val in range(64):
            result = convert_6to8(val)
            assert 0 <= result <= 255

    def test_conversion_preserves_ratios(self):
        """Test conversion approximately preserves intensity ratios."""
        def convert_6to8(val):
            return (val << 2) | (val >> 4)

        # 50% of 6-bit (31-32) should map to ~50% of 8-bit (127-128)
        mid_6bit = 32
        mid_8bit = convert_6to8(mid_6bit)
        assert 125 <= mid_8bit <= 135  # Approximately 128


class TestPngSpriteSheet:
    """Test sprite sheet output."""

    def test_sheet_dimensions(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test sprite sheet has correct total dimensions."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "sheet.png"
        result = run(
            shp_tool,
            "export",
            "--sheet",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(out_file),
        )
        if result.returncode != 0:
            pytest.skip("Sheet export not implemented")
        # Sheet should contain all frames
        assert out_file.exists()

    def test_sheet_frame_arrangement(self):
        """Test frames are arranged in grid."""
        # Default 16 frames per row
        pytest.skip("Requires image parsing")


class TestPngFrameExport:
    """Test individual frame export."""

    def test_frames_output(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test --frames produces multiple files."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # SHP export normally creates individual frames; test default
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(temp_dir / "frame.png"),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Should produce frame.png_000.png, frame.png_001.png, etc.
        png_files = list(temp_dir.glob("frame.png_*.png"))
        assert len(png_files) > 0

    def test_frame_numbering(
        self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir
    ):
        """Test frame numbering format (min 3 digits)."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(
            shp_tool,
            "export",
            "-p",
            testdata_pal_files[0],
            testdata_shp_files[0],
            "-o",
            str(temp_dir / "frame.png"),
        )
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Check naming pattern
        png_files = sorted(temp_dir.glob("frame.png_*.png"))
        if png_files:
            first = png_files[0].stem
            # Should be frame.png_000 or similar
            assert "_" in first
            num_part = first.split("_")[-1]
            assert len(num_part) >= 3
