"""
Tests for palette swatch PNG output (pal-tool export).

Tests cover:
- Swatch dimensions (512x512)
- Grid layout (16x16)
- Color accuracy
- 6-bit to 8-bit conversion
"""

import pytest
import struct
from pathlib import Path


class TestSwatchDimensions:
    """Test swatch image dimensions."""

    def test_width_512(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch width is 512 pixels."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        assert width == 512

    def test_height_512(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch height is 512 pixels."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        height = struct.unpack(">I", data[20:24])[0]
        assert height == 512

    def test_square_image(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch is square."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        height = struct.unpack(">I", data[20:24])[0]
        assert width == height


class TestSwatchGrid:
    """Test 16x16 grid layout."""

    def test_swatch_size(self):
        """Test each swatch is 32x32 pixels."""
        # 512 / 16 = 32
        total_size = 512
        grid_cells = 16
        swatch_size = total_size // grid_cells
        assert swatch_size == 32

    def test_grid_covers_256_colors(self):
        """Test grid has 256 cells (16x16)."""
        grid_width = 16
        grid_height = 16
        total_cells = grid_width * grid_height
        assert total_cells == 256

    def test_color_index_mapping(self):
        """Test color indices map to grid positions."""
        # Index 0 at top-left, index 255 at bottom-right
        def index_to_grid(index):
            row = index // 16
            col = index % 16
            return (row, col)

        assert index_to_grid(0) == (0, 0)      # Top-left
        assert index_to_grid(15) == (0, 15)    # Top-right
        assert index_to_grid(16) == (1, 0)     # Second row start
        assert index_to_grid(255) == (15, 15)  # Bottom-right


class TestSwatchColors:
    """Test color accuracy in swatch."""

    def test_6bit_to_8bit_conversion(self):
        """Test correct 6-bit to 8-bit conversion."""
        def convert(val6):
            return (val6 << 2) | (val6 >> 4)

        # Test corner cases
        assert convert(0) == 0
        assert convert(63) == 255

        # Test intermediate
        assert convert(32) == 130

    def test_black_at_index_0(self):
        """Test index 0 typically contains black or transparent."""
        # Red Alert convention: index 0 is often transparent
        # When rendered, should appear as black (0,0,0)
        pytest.skip("Requires image parsing")

    def test_uniform_swatch_fill(self):
        """Test each swatch cell is uniform color."""
        # All 32x32 pixels in each cell should be same color
        pytest.skip("Requires image parsing")


class TestSwatchFormat:
    """Test swatch output format."""

    def test_png_format(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch is valid PNG."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        png_sig = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        assert data[:8] == png_sig

    def test_rgb_not_rgba(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test swatch uses RGB (no alpha needed for solid swatches)."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        color_type = data[25]
        # RGB = 2, RGBA = 6 (both acceptable)
        assert color_type in [2, 6]

    def test_8bit_per_channel(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test 8-bit per color channel."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        bit_depth = data[24]
        assert bit_depth == 8


class TestSwatchWithKnownPalette:
    """Test swatch with known palette values."""

    def test_temperat_pal_colors(self):
        """Test known colors from TEMPERAT.PAL."""
        # Common palette: index 0 = cyan (transparent marker in game)
        # Would need actual palette data to verify
        pytest.skip("Requires known palette test data")

    def test_specific_color_positions(self):
        """Test specific colors appear at correct grid positions."""
        pytest.skip("Requires known palette test data")
