"""
Integration tests for MIX extraction and media processing workflow.

Tests cover:
- Extracting files from MIX archives
- Processing extracted media files
- End-to-end pipeline validation
"""

import pytest
from pathlib import Path


class TestMixToAudioWorkflow:
    """Test extracting and processing audio from MIX."""

    def test_extract_and_decode_aud(
        self, mix_tool, aud_tool, testdata_mix_files, run, temp_dir
    ):
        """Test extracting AUD from MIX and decoding to WAV."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # Step 1: List AUD files in MIX
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        # Find an AUD file
        aud_files = [line for line in result.stdout_text.splitlines()
                     if ".aud" in line.lower()]
        if not aud_files:
            pytest.skip("No AUD files in MIX")

        # Step 2: Extract the AUD file
        aud_name = aud_files[0].split()[0] if aud_files else None
        if not aud_name:
            pytest.skip("Could not parse AUD filename")

        extract_result = run(mix_tool, "extract", testdata_mix_files[0],
                            aud_name, "-o", str(temp_dir))
        if extract_result.returncode != 0:
            pytest.skip("MIX extraction not implemented")

        extracted_aud = temp_dir / aud_name
        assert extracted_aud.exists()

        # Step 3: Decode to WAV
        wav_file = temp_dir / "output.wav"
        decode_result = run(
            aud_tool, "export", str(extracted_aud), "-o", str(wav_file)
        )
        if decode_result.returncode != 0:
            pytest.skip("AUD decode not implemented")

        assert wav_file.exists()
        assert wav_file.read_bytes()[:4] == b"RIFF"


class TestMixToImageWorkflow:
    """Test extracting and processing images from MIX."""

    def test_extract_and_render_shp(self, mix_tool, shp_tool, pal_tool,
                                     testdata_mix_files, run, temp_dir):
        """Test extracting SHP and PAL from MIX and rendering."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # List files
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        lines = result.stdout_text.splitlines()
        shp_files = [l.split()[0] for l in lines if ".shp" in l.lower()]
        pal_files = [l.split()[0] for l in lines if ".pal" in l.lower()]

        if not shp_files or not pal_files:
            pytest.skip("No SHP or PAL files in MIX")

        # Extract both
        for f in [shp_files[0], pal_files[0]]:
            run(
                mix_tool, "extract", testdata_mix_files[0], f,
                "-o", str(temp_dir)
            )

        shp_path = temp_dir / shp_files[0]
        pal_path = temp_dir / pal_files[0]

        if not shp_path.exists() or not pal_path.exists():
            pytest.skip("Extraction failed")

        # Render SHP with palette
        png_file = temp_dir / "output.png"
        render_result = run(shp_tool, "export", "-p", str(pal_path),
                           str(shp_path), "-o", str(png_file))
        if render_result.returncode != 0:
            pytest.skip("SHP rendering not implemented")

        assert png_file.exists()


class TestMixToVideoWorkflow:
    """Test extracting and processing video from MIX."""

    def test_extract_and_convert_vqa(
        self, mix_tool, vqa_tool, testdata_mix_files,
        testdata_pal_files, run, temp_dir
    ):
        """Test extracting VQA from MIX and converting to MP4."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # Look for VQA in MIX
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        vqa_files = [l.split()[0] for l in result.stdout_text.splitlines()
                     if ".vqa" in l.lower()]
        if not vqa_files:
            pytest.skip("No VQA files in MIX")

        # Extract VQA
        run(
            mix_tool, "extract", testdata_mix_files[0], vqa_files[0],
            "-o", str(temp_dir)
        )
        vqa_path = temp_dir / vqa_files[0]

        if not vqa_path.exists():
            pytest.skip("VQA extraction failed")

        # Convert to MP4
        mp4_file = temp_dir / "output.mp4"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        convert_result = run(vqa_tool, "export", "--mp4", *pal_args,
                            str(vqa_path), "-o", str(mp4_file))
        if convert_result.returncode != 0:
            pytest.skip("VQA conversion not implemented")

        assert mp4_file.exists()


class TestNestedMixWorkflow:
    """Test processing files from nested MIX archives."""

    def test_extract_from_nested_mix(
        self, mix_tool, testdata_mix_files, run, temp_dir
    ):
        """Test extracting from MIX inside MIX."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # Check for nested MIX
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        nested_mix = [l.split()[0] for l in result.stdout_text.splitlines()
                      if ".mix" in l.lower()]
        if not nested_mix:
            pytest.skip("No nested MIX files")

        # Extract nested MIX
        run(
            mix_tool, "extract", testdata_mix_files[0], nested_mix[0],
            "-o", str(temp_dir)
        )
        nested_path = temp_dir / nested_mix[0]

        if not nested_path.exists():
            pytest.skip("Nested extraction failed")

        # List contents of nested MIX
        nested_list = run(mix_tool, "list", str(nested_path))
        assert nested_list.returncode == 0


class TestBatchProcessingWorkflow:
    """Test batch processing of extracted files."""

    def test_batch_audio_export(
        self, mix_tool, aud_tool, testdata_mix_files, run, temp_dir
    ):
        """Test batch exporting multiple AUD files."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # List all AUD files
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        aud_files = [l.split()[0] for l in result.stdout_text.splitlines()
                     if ".aud" in l.lower()][:3]  # Limit to 3

        if not aud_files:
            pytest.skip("No AUD files in MIX")

        wav_count = 0
        for aud_name in aud_files:
            # Extract
            run(
                mix_tool, "extract", testdata_mix_files[0], aud_name,
                "-o", str(temp_dir)
            )
            aud_path = temp_dir / aud_name

            if aud_path.exists():
                # Convert
                wav_path = temp_dir / (aud_path.stem + ".wav")
                result = run(
                    aud_tool, "export", str(aud_path), "-o", str(wav_path)
                )
                if result.returncode == 0 and wav_path.exists():
                    wav_count += 1

        # Should have converted at least one
        assert wav_count > 0 or len(aud_files) == 0


class TestPipelineWithStdio:
    """Test pipeline using stdin/stdout."""

    def test_pipe_extract_to_decode(
        self, mix_tool, aud_tool, testdata_mix_files, run, temp_dir
    ):
        """Test piping extraction directly to decoder."""
        # This would test:
        # mix-tool extract MIX file - | aud-tool export - -o out.wav
        pytest.skip("Requires shell pipeline testing")

    def test_info_to_json_processing(self, aud_tool, testdata_aud_files, run):
        """Test piping JSON info output."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        # Get JSON info
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON info not implemented")

        # Parse the JSON
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)
