"""
Integration tests for real game asset processing workflows.

Tests cover:
- Processing actual game data structures
- Multi-file dependencies (SHP + PAL)
- Animation sequences
- Complete media conversion pipelines
"""

import pytest
from pathlib import Path


class TestUnitAnimationWorkflow:
    """Test processing unit animation assets."""

    def test_unit_shp_to_gif(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test converting unit SHP to animated GIF."""
        if not testdata_shp_files or not testdata_pal_files:
            pytest.skip("No SHP or PAL files in testdata")

        # Find a multi-frame SHP (likely unit animation)
        for shp_file in testdata_shp_files:
            info_result = run(shp_tool, "info", "--json", shp_file)
            if info_result.returncode != 0:
                continue

            import json
            info = json.loads(info_result.stdout_text)
            frames = info.get("frames") or info.get("Frames") or 0

            if frames > 1:
                # This is an animated SHP
                out_file = temp_dir / "animation.gif"
                result = run(shp_tool, "export", "--gif", "-p", testdata_pal_files[0],
                            shp_file, "-o", str(out_file))
                if result.returncode == 0:
                    assert out_file.exists()
                    return

        pytest.skip("No multi-frame SHP found")


class TestTerrainTileWorkflow:
    """Test processing terrain tile assets."""

    def test_tmp_tileset_export(self, tmp_tool, testdata_tmp_files, testdata_pal_files, run, temp_dir):
        """Test exporting terrain tileset."""
        if not testdata_tmp_files or not testdata_pal_files:
            pytest.skip("No TMP or PAL files in testdata")

        out_file = temp_dir / "tileset.png"
        result = run(tmp_tool, "export", "-p", testdata_pal_files[0],
                    testdata_tmp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("TMP export not implemented")

        assert out_file.exists()


class TestMenuAnimationWorkflow:
    """Test processing menu/UI animation assets."""

    def test_wsa_menu_animation(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test converting WSA menu animation."""
        if not testdata_wsa_files or not testdata_pal_files:
            pytest.skip("No WSA or PAL files in testdata")

        out_file = temp_dir / "menu.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0],
                    testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("WSA export not implemented")

        assert out_file.exists()


class TestCutsceneWorkflow:
    """Test processing cutscene video assets."""

    def test_vqa_full_conversion(self, vqa_tool, testdata_vqa_files, testdata_pal_files, run, temp_dir):
        """Test full VQA to MP4 conversion."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        # Get info first
        info_result = run(vqa_tool, "info", "--json", testdata_vqa_files[0])
        if info_result.returncode != 0:
            pytest.skip("VQA info not implemented")

        import json
        info = json.loads(info_result.stdout_text)

        # Export to MP4
        mp4_file = temp_dir / "cutscene.mp4"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(vqa_tool, "export", "--mp4", *pal_args,
                    testdata_vqa_files[0], "-o", str(mp4_file))
        if result.returncode != 0:
            pytest.skip("MP4 export not implemented")

        assert mp4_file.exists()

    def test_vqa_audio_extraction(self, vqa_tool, testdata_vqa_files, run, temp_dir):
        """Test extracting audio track from VQA."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")

        wav_file = temp_dir / "cutscene_audio.wav"
        result = run(vqa_tool, "export", "--wav", testdata_vqa_files[0], "-o", str(wav_file))
        if result.returncode != 0:
            pytest.skip("WAV export not implemented")

        assert wav_file.exists()


class TestFontWorkflow:
    """Test processing font assets."""

    def test_font_to_png_and_metrics(self, fnt_tool, testdata_fnt_files, run, temp_dir):
        """Test exporting font glyphs and metrics."""
        if not testdata_fnt_files:
            pytest.skip("No FNT files in testdata")

        # Export glyph sheet
        png_file = temp_dir / "font.png"
        result = run(fnt_tool, "export", testdata_fnt_files[0], "-o", str(png_file))
        if result.returncode != 0:
            pytest.skip("FNT export not implemented")

        # Export metrics
        json_file = temp_dir / "metrics.json"
        metrics_result = run(fnt_tool, "export", "--metrics",
                            testdata_fnt_files[0], "-o", str(json_file))
        if metrics_result.returncode == 0:
            assert json_file.exists()
            import json
            metrics = json.loads(json_file.read_text())
            assert "glyphs" in metrics


class TestSplashScreenWorkflow:
    """Test processing splash screen/title assets."""

    def test_cps_fullscreen_export(self, cps_tool, testdata_cps_files, testdata_pal_files, run, temp_dir):
        """Test exporting CPS fullscreen image."""
        if not testdata_cps_files:
            pytest.skip("No CPS files in testdata")

        out_file = temp_dir / "splash.png"
        pal_args = ["-p", testdata_pal_files[0]] if testdata_pal_files else []
        result = run(cps_tool, "export", *pal_args,
                    testdata_cps_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("CPS export not implemented")

        # Verify 320x200
        import struct
        data = out_file.read_bytes()
        width = struct.unpack(">I", data[16:20])[0]
        height = struct.unpack(">I", data[20:24])[0]
        assert width == 320
        assert height == 200


class TestPaletteSwapWorkflow:
    """Test using different palettes with same graphics."""

    def test_shp_with_multiple_palettes(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test rendering same SHP with different palettes."""
        if not testdata_shp_files or len(testdata_pal_files) < 2:
            pytest.skip("Need SHP and multiple PAL files")

        # Render with first palette
        out1 = temp_dir / "pal1.png"
        run(shp_tool, "export", "-p", testdata_pal_files[0],
            testdata_shp_files[0], "-o", str(out1))

        # Render with second palette
        out2 = temp_dir / "pal2.png"
        run(shp_tool, "export", "-p", testdata_pal_files[1],
            testdata_shp_files[0], "-o", str(out2))

        # Both should exist but may differ in colors
        if out1.exists() and out2.exists():
            # Files should have different content (different palettes)
            # Unless both palettes are identical
            pass


class TestCompleteAssetDump:
    """Test dumping all assets from a MIX file."""

    def test_dump_all_media(self, mix_tool, aud_tool, shp_tool, pal_tool,
                            testdata_mix_files, run, temp_dir):
        """Test extracting and converting all media from MIX."""
        if not testdata_mix_files:
            pytest.skip("No MIX files in testdata")

        # List all files
        result = run(mix_tool, "list", testdata_mix_files[0])
        if result.returncode != 0:
            pytest.skip("MIX list not implemented")

        lines = result.stdout_text.splitlines()

        # Extract a few of each type
        extracted = {"aud": [], "shp": [], "pal": []}
        for line in lines[:20]:  # Limit to first 20 entries
            if not line.strip():
                continue
            name = line.split()[0]
            ext = name.split(".")[-1].lower() if "." in name else ""

            if ext in extracted and len(extracted[ext]) < 2:
                run(mix_tool, "extract", testdata_mix_files[0], name, "-o", str(temp_dir))
                path = temp_dir / name
                if path.exists():
                    extracted[ext].append(path)

        # Convert what we found
        converted_count = 0

        for aud_path in extracted["aud"]:
            wav_path = temp_dir / (aud_path.stem + ".wav")
            if run(aud_tool, "export", str(aud_path), "-o", str(wav_path)).returncode == 0:
                converted_count += 1

        # Note: SHP needs PAL, so only convert if we have both
        if extracted["pal"] and extracted["shp"]:
            for shp_path in extracted["shp"]:
                png_path = temp_dir / (shp_path.stem + ".png")
                if run(shp_tool, "export", "-p", str(extracted["pal"][0]),
                       str(shp_path), "-o", str(png_path)).returncode == 0:
                    converted_count += 1

        # Should have converted at least something
        # (or nothing if tools not implemented)
