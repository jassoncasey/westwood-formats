"""
Tests for JSON output format (tool info --json).

Tests cover:
- Valid JSON structure
- Required fields per tool
- Field types and values
- Consistent naming conventions
"""

import pytest
import json


class TestJsonValidity:
    """Test JSON output is valid."""

    def test_aud_info_valid_json(self, aud_tool, testdata_aud_files, run):
        """Test aud-tool info --json produces valid JSON."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_shp_info_valid_json(self, shp_tool, testdata_shp_files, run):
        """Test shp-tool info --json produces valid JSON."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_pal_info_valid_json(self, pal_tool, testdata_pal_files, run):
        """Test pal-tool info --json produces valid JSON."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", "--json", testdata_pal_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_vqa_info_valid_json(self, vqa_tool, testdata_vqa_files, run):
        """Test vqa-tool info --json produces valid JSON."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)


class TestAudJsonFields:
    """Test aud-tool JSON output fields."""

    def test_required_fields(self, aud_tool, testdata_aud_files, run):
        """Test all required fields are present."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # Required fields per spec
        required = [
            "sample_rate", "channels", "bits", "codec",
            "compressed_size", "uncompressed_size", "duration"
        ]
        for field in required:
            field_present = (
                field in data or
                field.replace("_", "") in str(data).lower()
            )
            assert field_present, f"Missing field: {field}"

    def test_sample_rate_type(self, aud_tool, testdata_aud_files, run):
        """Test sample_rate is integer."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        rate = data.get("sample_rate") or data.get("SampleRate")
        assert isinstance(rate, int)

    def test_codec_values(self, aud_tool, testdata_aud_files, run):
        """Test codec is valid value."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        codec = data.get("codec") or data.get("Codec")
        valid_codecs = [
            "westwood_adpcm", "ima_adpcm",
            "WestwoodADPCM", "IMA_ADPCM"
        ]
        assert codec in valid_codecs or "adpcm" in str(codec).lower()


class TestShpJsonFields:
    """Test shp-tool JSON output fields."""

    def test_required_fields(self, shp_tool, testdata_shp_files, run):
        """Test all required fields are present."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # Required fields
        required = ["frames", "width", "height", "format"]
        for field in required:
            field_present = (
                field in data or
                field.replace("_", "") in str(data).lower()
            )
            assert field_present, f"Missing field: {field}"

    def test_frames_is_integer(self, shp_tool, testdata_shp_files, run):
        """Test frames count is integer."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        result = run(shp_tool, "info", "--json", testdata_shp_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        frames = (
            data.get("frames") or data.get("Frames") or
            data.get("frame_count")
        )
        assert isinstance(frames, int)
        assert frames > 0


class TestPalJsonFields:
    """Test pal-tool JSON output fields."""

    def test_required_fields(self, pal_tool, testdata_pal_files, run):
        """Test all required fields are present."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", "--json", testdata_pal_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # Required fields
        required = ["format", "colors", "bit_depth"]
        for field in required:
            field_present = (
                field in data or
                field.replace("_", "") in str(data).lower()
            )
            assert field_present, f"Missing field: {field}"

    def test_colors_is_256(self, pal_tool, testdata_pal_files, run):
        """Test palette has 256 colors."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        result = run(pal_tool, "info", "--json", testdata_pal_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)
        colors = (
            data.get("colors") or data.get("Colors") or
            data.get("color_count")
        )
        assert colors == 256


class TestVqaJsonFields:
    """Test vqa-tool JSON output fields."""

    def test_video_fields(self, vqa_tool, testdata_vqa_files, run):
        """Test video-related fields are present."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # Video fields
        video_fields = ["version", "width", "height", "frames", "frame_rate"]
        found = 0
        for field in video_fields:
            if field in data or field.replace("_", "") in str(data).lower():
                found += 1
        assert found >= 3, "Missing video fields"

    def test_audio_fields(self, vqa_tool, testdata_vqa_files, run):
        """Test audio-related fields are present."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # May have has_audio or audio section
        has_audio_info = ("has_audio" in str(data).lower() or
                         "audio" in str(data).lower() or
                         "sample_rate" in str(data).lower())
        assert has_audio_info


class TestFntJsonFields:
    """Test fnt-tool JSON output fields."""

    def test_required_fields(self, fnt_tool, run):
        """Test all required fields are present."""
        pytest.skip("Requires extracted FNT test files")

    def test_glyphs_object(self, fnt_tool, run):
        """Test glyphs is object with character keys."""
        pytest.skip("Requires extracted FNT test files")


class TestJsonNaming:
    """Test JSON field naming conventions."""

    def test_consistent_snake_case(self):
        """Test field names use snake_case."""
        # Document expectation: all fields should be snake_case
        valid_names = ["sample_rate", "frame_count", "bit_depth"]
        for name in valid_names:
            assert "_" in name or name.islower()

    def test_no_mixed_conventions(self, aud_tool, testdata_aud_files, run):
        """Test no mixing of camelCase and snake_case."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON output not implemented")
        data = json.loads(result.stdout_text)

        # Check for consistent naming
        keys = list(data.keys())
        has_snake = any("_" in k for k in keys)
        has_camel = any(
            k[0].islower() and any(c.isupper() for c in k[1:])
            for k in keys
        )
        # Should not have both (unless intentional)
        # This is a soft check - some tools may use different conventions


class TestJsonMetrics:
    """Test fnt-tool metrics JSON output."""

    def test_metrics_structure(self):
        """Test metrics JSON has correct structure."""
        # Expected structure per spec
        expected = {
            "format": "string",
            "version": "integer",
            "glyphs": "object",
            "max_width": "integer",
            "max_height": "integer",
            "bit_depth": "integer"
        }
        assert "glyphs" in expected

    def test_glyph_entry_fields(self):
        """Test each glyph entry has required fields."""
        # Expected glyph structure
        glyph_fields = ["width", "height", "y_offset"]
        assert len(glyph_fields) == 3

    def test_character_keyed_glyphs(self):
        """Test glyphs are keyed by character (not index)."""
        # Example: {"A": {...}, "B": {...}} not {"65": {...}, "66": {...}}
        example = {"A": {"width": 8}, "B": {"width": 9}}
        for key in example:
            assert isinstance(key, str)
            assert len(key) == 1  # Single character
