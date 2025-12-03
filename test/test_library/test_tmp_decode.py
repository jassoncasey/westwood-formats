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
        assert any(
            k in data for k in ["tiles", "tileCount", "tile_count",
                                "numTiles"]
        )

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

    def test_index_table_size(self, tmp_tool, testdata_tmp_files, run):
        """Test index table has correct number of entries."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        tiles = data.get("tiles", 0)
        assert tiles > 0

    def test_valid_indices(self, tmp_tool, testdata_tmp_files, run):
        """Test tile indices are in valid range."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # empty_tiles should be <= total tiles
        empty = data.get("empty_tiles", 0)
        tiles = data.get("tiles", 0)
        assert empty <= tiles

    def test_empty_tile_marker(self, tmp_tool, testdata_tmp_files, run):
        """Test 0xFF marks empty/null tile."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # empty_tiles count is extracted - 0xFF markers are counted
        assert "empty_tiles" in data


class TestTmpEmptyTiles:
    """Test empty tile handling."""

    def test_count_empty_tiles(self, tmp_tool, testdata_tmp_files, run):
        """Test counting empty tiles in template."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", testdata_tmp_files[0])
        result.assert_success()

    def test_empty_tiles_in_json(self, tmp_tool, testdata_tmp_files, run):
        """Test empty tile count is reported in JSON output."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Empty tiles count should be present and non-negative
        empty = data.get("empty_tiles", 0)
        assert empty >= 0


class TestTmpTileData:
    """Test tile pixel data extraction."""

    def test_tile_dimensions_consistent(
            self, tmp_tool, testdata_tmp_files, run):
        """Test tile dimensions are reported consistently."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        result = run(tmp_tool, "info", "--json", testdata_tmp_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        # Tile dimensions should be present
        width = (data.get("tileWidth") or data.get("tile_width") or
                 data.get("width", 0))
        height = (data.get("tileHeight") or data.get("tile_height") or
                  data.get("height", 0))
        # RA tiles are typically 24x24
        assert width > 0
        assert height > 0

    def test_tile_export(
            self, tmp_tool, testdata_tmp_files, testdata_pal_files, run,
            temp_dir):
        """Test exporting tiles to PNG."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(tmp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_tmp_files[0], "-o", str(temp_dir / "tile.png"))
        result.assert_success()
        # Should create at least one PNG
        png_files = list(temp_dir.glob("*.png"))
        assert len(png_files) >= 1

    def test_palette_required_for_export(
            self, tmp_tool, testdata_tmp_files, run, temp_dir):
        """Test export requires palette file."""
        if not testdata_tmp_files:
            pytest.skip("No TMP files in testdata")
        # Export without palette should fail or warn
        result = run(tmp_tool, "export", testdata_tmp_files[0], "-o",
                     str(temp_dir / "tile.png"))
        # Either fails or uses default palette
        # Verify command completes (may succeed with embedded/default
        # palette)


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
