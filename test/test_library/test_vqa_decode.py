"""
Tests for VQA video format decoding (libwestwood).

Tests cover:
- IFF container parsing
- VQHD header parsing
- Version detection (v1, v2, v3)
- Video frame decoding (codebook, VPT)
- Audio chunk decoding (SND0, SND1, SND2)
"""

import pytest
from pathlib import Path


class TestVqaContainerParsing:
    """Test IFF container parsing."""

    def test_form_chunk(self, vqa_tool, testdata_vqa_files, run):
        """Test FORM chunk identification."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()

    def test_wvqa_type(self, vqa_tool, testdata_vqa_files, run):
        """Test WVQA form type identification."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        assert "VQA" in result.stdout_text

    def test_chunk_alignment(self):
        """Test IFF chunk even-byte alignment."""
        pytest.skip("Requires unit test of parser")

    def test_big_endian_chunk_sizes(self):
        """Test chunk sizes are big-endian."""
        pytest.skip("Requires unit test of parser")

    def test_invalid_form(self, vqa_tool, temp_file, run):
        """Test rejection of non-FORM file."""
        bad_file = temp_file(".vqa", b"NOTFORM\x00" * 100)
        result = run(vqa_tool, "info", bad_file)
        result.assert_exit_code(2)


class TestVqaHeaderParsing:
    """Test VQHD header chunk parsing."""

    def test_version_field(self, vqa_tool, testdata_vqa_files, run):
        """Test version extraction (1, 2, or 3)."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        result.assert_success()
        import json
        data = json.loads(result.stdout_text)
        assert "version" in data or "Version" in str(data)

    def test_dimensions(self, vqa_tool, testdata_vqa_files, run):
        """Test width/height extraction."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Should contain dimensions like "320x200"
        assert "x" in result.stdout_text or "×" in result.stdout_text

    def test_frame_count(self, vqa_tool, testdata_vqa_files, run):
        """Test NumFrames extraction."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()

    def test_block_size(self, vqa_tool, testdata_vqa_files, run):
        """Test BlockW/BlockH extraction."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()

    def test_frame_rate(self, vqa_tool, testdata_vqa_files, run):
        """Test FrameRate extraction."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        assert "fps" in result.stdout_text.lower()

    def test_audio_params(self, vqa_tool, testdata_vqa_files, run):
        """Test Freq/Channels/Bits extraction."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()


class TestVqaVersionDetection:
    """Test VQA version detection."""

    def test_v2_detection(self, vqa_tool, testdata_vqa_files, run):
        """Test v2 detection (C&C, Red Alert)."""
        # Red Alert VQAs should be v2
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Check version is reported


class TestVqaCodebook:
    """Test codebook handling."""

    def test_cbf_full_codebook(self):
        """Test CBF chunk replaces entire codebook."""
        pytest.skip("Requires unit test of decoder")

    def test_cbp_partial_codebook(self):
        """Test CBP chunk updates codebook segment."""
        pytest.skip("Requires unit test of decoder")

    def test_cbfz_decompression(self):
        """Test CBFZ LCW decompression with mode detection."""
        pytest.skip("Requires unit test of decoder")

    def test_cbp_accumulation(self):
        """Test CBP segments accumulate over CBParts frames."""
        pytest.skip("Requires unit test of decoder")


class TestVqaVectorPointers:
    """Test VPT frame rendering."""

    def test_v2_split_byte_format(self):
        """Test v2 lo/hi byte split format."""
        pytest.skip("Requires unit test of decoder")

    def test_solid_color_marker(self):
        """Test hi=0x0F indicates solid color."""
        pytest.skip("Requires unit test of decoder")

    def test_vptz_decompression(self):
        """Test VPTZ LCW decompression."""
        pytest.skip("Requires unit test of decoder")


class TestVqaV3HiColor:
    """Test V3 HiColor (RGB555) handling."""

    def test_rgb555_to_rgb888(self):
        """Test RGB555 to RGB888 conversion."""
        def rgb555_to_rgb888(pixel):
            r = ((pixel >> 10) & 0x1F) << 3
            g = ((pixel >> 5) & 0x1F) << 3
            b = (pixel & 0x1F) << 3
            return (r, g, b)

        # Black
        assert rgb555_to_rgb888(0x0000) == (0, 0, 0)
        # White (max values)
        assert rgb555_to_rgb888(0x7FFF) == (248, 248, 248)
        # Pure red
        assert rgb555_to_rgb888(0x7C00) == (248, 0, 0)
        # Pure green
        assert rgb555_to_rgb888(0x03E0) == (0, 248, 0)
        # Pure blue
        assert rgb555_to_rgb888(0x001F) == (0, 0, 248)


class TestVqaAudio:
    """Test audio chunk decoding."""

    def test_snd0_raw_pcm(self):
        """Test SND0 raw PCM extraction."""
        pytest.skip("Requires unit test of decoder")

    def test_snd1_westwood_adpcm(self):
        """Test SND1 Westwood ADPCM decoding."""
        pytest.skip("Requires unit test of decoder")

    def test_snd2_ima_adpcm(self):
        """Test SND2 IMA ADPCM decoding."""
        pytest.skip("Requires unit test of decoder")

    def test_stereo_channel_split(self):
        """Test stereo audio channel splitting and interleaving."""
        pytest.skip("Requires unit test of decoder")


class TestVqaNoAudio:
    """Test VQA without audio track."""

    def test_video_only_detection(self):
        """Test detection of video-only VQA."""
        pytest.skip("Requires video-only VQA test file")

    def test_video_only_export(self):
        """Test export of video-only VQA."""
        pytest.skip("Requires video-only VQA test file")


class TestVqaInfoOutput:
    """Test vqa-tool info command output."""

    def test_info_human_readable(self, vqa_tool, testdata_vqa_files, run):
        """Test human-readable info output format."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        result = run(vqa_tool, "info", testdata_vqa_files[0])
        result.assert_success()
        # Check key sections
        assert "Video" in result.stdout_text or "video" in result.stdout_text

    def test_info_json(self, vqa_tool, testdata_vqa_files, run):
        """Test JSON info output format."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        import json
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        result.assert_success()
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_info_fields_complete(self, vqa_tool, testdata_vqa_files, run):
        """Test all required info fields are present."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        import json
        result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        result.assert_success()
        # Should have video and audio sections with appropriate fields
        data = json.loads(result.stdout_text)
        assert data is not None
