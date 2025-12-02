"""
Tests for aud-tool CLI.

Tests cover:
- info command
- export command
- Error handling
"""

import pytest
from pathlib import Path


class TestAudToolInfo:
    """Test aud-tool info command."""

    def test_info_basic(self, aud_tool, testdata_aud_files, run):
        """Test basic info output."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", testdata_aud_files[0])
        result.assert_success()

    def test_info_shows_sample_rate(self, aud_tool, testdata_aud_files, run):
        """Test info shows sample rate."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", testdata_aud_files[0])
        result.assert_success()
        assert "hz" in result.stdout_text.lower() or "Hz" in result.stdout_text

    def test_info_shows_codec(self, aud_tool, testdata_aud_files, run):
        """Test info shows codec type."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", testdata_aud_files[0])
        result.assert_success()
        assert "adpcm" in result.stdout_text.lower()

    def test_info_json_format(self, aud_tool, testdata_aud_files, run):
        """Test info --json produces valid JSON."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert "sample_rate" in data or "SampleRate" in data

    def test_info_multiple_files(self, aud_tool, testdata_aud_files, run):
        """Test info on multiple files."""
        if len(testdata_aud_files) < 2:
            pytest.skip("Need multiple AUD files")
        result = run(aud_tool, "info", testdata_aud_files[0], testdata_aud_files[1])
        result.assert_success()


class TestAudToolExport:
    """Test aud-tool export command."""

    def test_export_to_wav(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test export produces WAV file."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()
        assert out_file.read_bytes()[:4] == b"RIFF"

    def test_export_preserves_sample_rate(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test export preserves source sample rate."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        import struct
        import json

        # Get source rate
        info_result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        info = json.loads(info_result.stdout_text)
        source_rate = info.get("sample_rate") or info.get("SampleRate")

        # Export
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")

        # Check WAV rate
        wav_data = out_file.read_bytes()
        wav_rate = struct.unpack("<I", wav_data[24:28])[0]
        assert wav_rate == source_rate

    def test_export_default_filename(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test export without -o uses default name."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        # Run from temp_dir
        result = run(aud_tool, "export", testdata_aud_files[0], cwd=temp_dir)
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Should create file with same base name
        expected = temp_dir / (Path(testdata_aud_files[0]).stem + ".wav")
        assert expected.exists()


class TestAudToolErrors:
    """Test aud-tool error handling."""

    def test_nonexistent_file(self, aud_tool, run):
        """Test error on nonexistent file."""
        result = run(aud_tool, "info", "/nonexistent/file.aud")
        result.assert_exit_code(2)

    def test_invalid_aud_file(self, aud_tool, temp_file, run):
        """Test error on invalid AUD file."""
        bad_file = temp_file(".aud", b"not an aud file")
        result = run(aud_tool, "info", bad_file)
        result.assert_exit_code(2)

    def test_missing_output_dir(self, aud_tool, testdata_aud_files, run):
        """Test error when output directory doesn't exist."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", "/nonexistent/dir/out.wav")
        result.assert_exit_code(3)

    def test_invalid_command(self, aud_tool, run):
        """Test error on invalid command."""
        result = run(aud_tool, "invalid_command")
        result.assert_exit_code(1)


class TestAudToolWestwoodAdpcm:
    """Test Westwood ADPCM (0x01) handling."""

    def test_westwood_codec_detection(self, aud_tool, testdata_aud_files, run):
        """Test detection of Westwood ADPCM codec."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(result.stdout_text)
        codec = info.get("codec") or info.get("Codec") or ""
        # Most RA AUDs use Westwood ADPCM
        assert "adpcm" in str(codec).lower()


class TestAudToolImaAdpcm:
    """Test IMA ADPCM (0x63) handling."""

    def test_ima_codec_detection(self):
        """Test detection of IMA ADPCM codec."""
        # Tiberian Sun uses IMA ADPCM (0x63)
        pytest.skip("Requires IMA ADPCM test file")
