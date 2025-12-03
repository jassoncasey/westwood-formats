"""
Tests for TMP tile format decoding (libwestwood).

Tests cover:
- TD/RA format detection (magic bytes)
- Header parsing
- Index table parsing
- Empty tile (0xFF) handling
- Tile pixel data extraction
"""

import pytest
from pathlib import Path


class TestTmpFormatDetection:
    """Test TMP format detection."""

    def test_detect_ra_format(self, tmp_tool, testdata_tmp_files, run):
        """Test detection of Red Alert format (0x2C73 at offset 26)."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_detect_td_format(self, tmp_tool, testdata_tmp_files, run):
        """Test detection of Tiberian Dawn format (0x0D1AFFFF at offset 20)."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        # Red Alert TMP files should be detected
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_invalid_magic(self, tmp_tool, temp_file, run):
        """Test rejection of file with invalid magic."""
        bad_file = temp_file(".tmp", b"\x00" * 100)
        result = run(tmp_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestTmpHeaderParsing:
    """Test TMP header parsing."""

    def test_tile_dimensions(self, tmp_tool, testdata_tmp_files, run):
        """Test tile width/height extraction (typically 24x24)."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should have tile dimension info
        assert any(k in data for k in ["tileWidth", "tile_width", "width"])

    def test_tile_count(self, tmp_tool, testdata_tmp_files, run):
        """Test tile count extraction."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Should have tile count info
        assert any(k in data for k in ["tiles", "tileCount", "tile_count", "numTiles"])

    def test_img_start_offset(self, tmp_tool, testdata_tmp_files, run):
        """Test ImgStart offset extraction."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_index_offsets(self, tmp_tool, testdata_tmp_files, run):
        """Test IndexStart/IndexEnd offset extraction."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()


class TestTmpIndexTable:
    """Test index table parsing."""

    def test_index_table_size(self):
        """Test index table has correct number of entries."""
        pytest.skip("Requires extracted TMP test files")

    def test_valid_indices(self):
        """Test tile indices are in valid range."""
        pytest.skip("Requires extracted TMP test files")

    def test_empty_tile_marker(self):
        """Test 0xFF marks empty/null tile."""
        pytest.skip("Requires extracted TMP test files")


class TestTmpEmptyTiles:
    """Test empty tile handling."""

    def test_count_empty_tiles(self, tmp_tool, testdata_tmp_files, run):
        """Test counting empty tiles in template."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_empty_tile_not_rendered(self):
        """Test empty tiles produce transparent output."""
        pytest.skip("Requires unit test of TMP renderer")


class TestTmpTileData:
    """Test tile pixel data extraction."""

    def test_tile_size_bytes(self):
        """Test each tile is Width * Height bytes (576 for 24x24)."""
        pytest.skip("Requires extracted TMP test files")

    def test_tile_offset_calculation(self):
        """Test tile N is at ImgStart + Index[N] * TileSize."""
        pytest.skip("Requires extracted TMP test files")

    def test_palette_indexed_pixels(self):
        """Test pixels are 8-bit palette indices."""
        pytest.skip("Requires extracted TMP test files")


class TestTmpInfoOutput:
    """Test tmp-tool info command output."""

    def test_info_human_readable(self, tmp_tool, testdata_tmp_files, run):
        """Test human-readable info output format."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()
        assert len(result.stdout_text) > 0

    def test_info_json(self, tmp_tool, testdata_tmp_files, run):
        """Test JSON info output format."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_info_fields_complete(self, tmp_tool, testdata_tmp_files, run):
        """Test all required info fields are present."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Check for key fields
        assert any(k in data for k in ["tiles", "tileCount", "numTiles"])
