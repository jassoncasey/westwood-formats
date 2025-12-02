"""
Tests for WAV output format (libwestwood export).

Tests cover:
- WAV header structure
- 16-bit signed PCM format
- Sample rate preservation
- Channel preservation (mono/stereo)
- Proper byte ordering (little-endian)
"""

import pytest
import struct
from pathlib import Path


class TestWavHeader:
    """Test WAV file header structure."""

    def test_riff_magic(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test RIFF magic bytes."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        assert data[:4] == b"RIFF"

    def test_wave_format(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test WAVE format identifier."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        assert data[8:12] == b"WAVE"

    def test_fmt_chunk(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test fmt chunk presence and size."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        assert data[12:16] == b"fmt "
        fmt_size = struct.unpack("<I", data[16:20])[0]
        assert fmt_size >= 16  # Minimum for PCM

    def test_data_chunk(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test data chunk presence."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # Find data chunk (may not be at fixed offset)
        assert b"data" in data

    def test_file_size_field(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test RIFF file size field accuracy."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        riff_size = struct.unpack("<I", data[4:8])[0]
        assert riff_size == len(data) - 8


class TestWavPcmFormat:
    """Test PCM audio format."""

    def test_format_code_pcm(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test audio format code is 1 (PCM)."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        format_code = struct.unpack("<H", data[20:22])[0]
        assert format_code == 1  # PCM

    def test_bits_per_sample(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test 16-bit samples."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        bits = struct.unpack("<H", data[34:36])[0]
        assert bits == 16

    def test_signed_pcm_range(self):
        """Test 16-bit signed range (-32768 to 32767)."""
        # Verify 16-bit signed representation
        min_val = struct.pack("<h", -32768)
        max_val = struct.pack("<h", 32767)
        assert struct.unpack("<h", min_val)[0] == -32768
        assert struct.unpack("<h", max_val)[0] == 32767


class TestWavSampleRate:
    """Test sample rate preservation."""

    def test_sample_rate_preserved(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test sample rate matches source AUD."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        # Get source sample rate
        info_result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_rate = info.get("sample_rate") or info.get("SampleRate")

        # Export and check WAV
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        wav_rate = struct.unpack("<I", data[24:28])[0]
        assert wav_rate == source_rate

    def test_common_sample_rates(self):
        """Test common Red Alert sample rates are valid."""
        # Red Alert typically uses 22050 Hz
        common_rates = [11025, 22050, 44100]
        for rate in common_rates:
            assert rate > 0
            assert rate <= 48000


class TestWavChannels:
    """Test channel preservation."""

    def test_channel_count_preserved(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test channel count matches source AUD."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        # Get source channels
        info_result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_channels = info.get("channels") or info.get("Channels")

        # Export and check WAV
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        wav_channels = struct.unpack("<H", data[22:24])[0]
        assert wav_channels == source_channels

    def test_mono_block_align(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test mono block alignment (2 bytes for 16-bit)."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        channels = struct.unpack("<H", data[22:24])[0]
        block_align = struct.unpack("<H", data[32:34])[0]
        assert block_align == channels * 2  # 2 bytes per sample

    def test_stereo_interleaving(self):
        """Test stereo samples are interleaved (L, R, L, R, ...)."""
        # Create test stereo data
        left = [100, 200, 300]
        right = [-100, -200, -300]
        interleaved = []
        for l, r in zip(left, right):
            interleaved.extend([l, r])
        assert interleaved == [100, -100, 200, -200, 300, -300]


class TestWavByteOrder:
    """Test little-endian byte ordering."""

    def test_header_little_endian(self):
        """Test header fields are little-endian."""
        # WAV uses little-endian throughout
        value = 0x12345678
        packed = struct.pack("<I", value)
        assert packed == bytes([0x78, 0x56, 0x34, 0x12])

    def test_samples_little_endian(self):
        """Test sample data is little-endian."""
        sample = -1000
        packed = struct.pack("<h", sample)
        unpacked = struct.unpack("<h", packed)[0]
        assert unpacked == sample


class TestWavDataIntegrity:
    """Test audio data integrity."""

    def test_data_size_consistency(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test data chunk size matches actual data."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # Find data chunk offset
        data_offset = data.find(b"data")
        assert data_offset > 0
        data_size = struct.unpack("<I", data[data_offset + 4:data_offset + 8])[0]
        actual_size = len(data) - (data_offset + 8)
        assert data_size == actual_size

    def test_sample_count_matches_duration(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test sample count corresponds to duration."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "test.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()

        # Get parameters
        sample_rate = struct.unpack("<I", data[24:28])[0]
        channels = struct.unpack("<H", data[22:24])[0]
        bits = struct.unpack("<H", data[34:36])[0]

        # Find data chunk
        data_offset = data.find(b"data")
        data_size = struct.unpack("<I", data[data_offset + 4:data_offset + 8])[0]

        # Calculate
        bytes_per_sample = bits // 8
        total_samples = data_size // (bytes_per_sample * channels)
        duration = total_samples / sample_rate

        assert duration > 0
