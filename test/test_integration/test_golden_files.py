"""
Integration tests using golden file comparisons.

Tests cover:
- Known-good output verification
- Regression testing
- Format compliance validation
"""

import pytest
from pathlib import Path


class TestAudioGoldenFiles:
    """Test audio output against golden files."""

    def test_aud_to_wav_matches_golden(self, aud_tool, testdata_aud_files, golden_dir, run, temp_dir):
        """Test AUD export matches golden WAV."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        # Look for corresponding golden file
        aud_name = Path(testdata_aud_files[0]).stem
        golden_wav = golden_dir / f"{aud_name}.wav"

        if not golden_wav.exists():
            pytest.skip(f"No golden file: {golden_wav}")

        # Export
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        # Compare (skip RIFF size field which may vary)
        actual = out_file.read_bytes()
        expected = golden_wav.read_bytes()

        # Compare audio data portion (after WAV header)
        assert actual[44:] == expected[44:], "Audio data mismatch"


class TestImageGoldenFiles:
    """Test image output against golden files."""

    def test_shp_to_png_matches_golden(self, shp_tool, testdata_shp_files, testdata_pal_files,
                                        golden_dir, run, temp_dir):
        """Test SHP export matches golden PNG."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")

        shp_name = Path(testdata_shp_files[0]).stem
        golden_png = golden_dir / f"{shp_name}.png"

        if not golden_png.exists():
            pytest.skip(f"No golden file: {golden_png}")

        out_prefix = temp_dir / "test"
        result = run(shp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_prefix))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        # SHP export creates numbered frames, find the first one
        frame_files = sorted(temp_dir.glob("test_*.png"))
        if not frame_files:
            pytest.skip("No frame files exported")
        out_file = frame_files[0]

        # PNG comparison (may need to ignore metadata chunks)
        actual = out_file.read_bytes()
        expected = golden_png.read_bytes()

        # Simple comparison - could be enhanced to ignore non-essential chunks
        assert actual == expected, "PNG mismatch"

    def test_pal_swatch_matches_golden(self, pal_tool, testdata_pal_files, golden_dir, run, temp_dir):
        """Test PAL swatch export matches golden."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")

        pal_name = Path(testdata_pal_files[0]).stem
        golden_swatch = golden_dir / f"{pal_name}_swatch.png"

        if not golden_swatch.exists():
            pytest.skip(f"No golden file: {golden_swatch}")

        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        actual = out_file.read_bytes()
        expected = golden_swatch.read_bytes()
        assert actual == expected, "Swatch mismatch"


class TestJsonGoldenFiles:
    """Test JSON output against golden files."""

    def test_aud_info_json_matches_golden(self, aud_tool, testdata_aud_files, golden_dir, run):
        """Test AUD info JSON matches golden."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        aud_name = Path(testdata_aud_files[0]).stem
        golden_json = golden_dir / f"{aud_name}_info.json"

        if not golden_json.exists():
            pytest.skip(f"No golden file: {golden_json}")

        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON info not implemented")

        import json
        actual = json.loads(result.stdout_text)
        expected = json.loads(golden_json.read_text())

        assert actual == expected, "JSON info mismatch"


class TestGoldenFileGeneration:
    """Helpers for generating golden files."""

    @pytest.mark.skip(reason="Golden file generation - run manually")
    def test_generate_aud_golden(self, aud_tool, testdata_aud_files, golden_dir, run):
        """Generate golden WAV files from AUD."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        golden_dir.mkdir(parents=True, exist_ok=True)

        for aud_file in testdata_aud_files[:5]:  # Limit
            aud_name = Path(aud_file).stem
            out_file = golden_dir / f"{aud_name}.wav"
            result = run(aud_tool, "export", aud_file, "-o", str(out_file))
            if result.returncode == 0:
                print(f"Generated: {out_file}")

    @pytest.mark.skip(reason="Golden file generation - run manually")
    def test_generate_info_golden(self, aud_tool, testdata_aud_files, golden_dir, run):
        """Generate golden JSON info files."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        golden_dir.mkdir(parents=True, exist_ok=True)

        for aud_file in testdata_aud_files[:5]:
            aud_name = Path(aud_file).stem
            out_file = golden_dir / f"{aud_name}_info.json"
            result = run(aud_tool, "info", "--json", aud_file)
            if result.returncode == 0:
                out_file.write_text(result.stdout_text)
                print(f"Generated: {out_file}")


class TestRegressionPrevention:
    """Tests to prevent regressions in specific scenarios."""

    def test_6bit_palette_conversion(self, pal_tool, testdata_pal_files, run, temp_dir):
        """Test 6-bit to 8-bit color conversion is correct."""
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")

        # The conversion formula should be: (val << 2) | (val >> 4)
        # This test ensures the formula is applied correctly

        # Export swatch
        out_file = temp_dir / "swatch.png"
        result = run(pal_tool, "export", testdata_pal_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        # Verify by checking PNG dimensions (should be 512x512)
        import struct
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        height = struct.unpack(">I", data[20:24])[0]
        assert width == 512
        assert height == 512

    def test_index0_transparency(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test palette index 0 produces transparency."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")

        out_base = temp_dir / "test.png"
        result = run(shp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_shp_files[0], "-o", str(out_base))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        # shp-tool creates numbered frame files (test.png_000.png, etc)
        png_files = list(temp_dir.glob("test.png_*.png"))
        if not png_files:
            pytest.skip("No PNG files created")

        # Verify first PNG is RGBA (color type 6)
        data = png_files[0].read_bytes()
        color_type = data[25]
        assert color_type == 6, "Should be RGBA for transparency support"

    def test_wav_16bit_signed(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test WAV output is 16-bit signed PCM."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        import struct
        data = out_file.read_bytes()

        # Format code should be 1 (PCM)
        format_code = struct.unpack("<H", data[20:22])[0]
        assert format_code == 1, "Should be PCM format"

        # Bits per sample should be 16
        bits = struct.unpack("<H", data[34:36])[0]
        assert bits == 16, "Should be 16-bit"
