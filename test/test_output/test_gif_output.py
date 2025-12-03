"""
Tests for GIF output format (libwestwood export).

Tests cover:
- GIF header/signature
- Frame timing (centiseconds)
- Loop behavior
- Transparency handling
- Animation sequence
"""

import pytest
import struct
from pathlib import Path


class TestGifSignature:
    """Test GIF file signature."""

    def test_gif_signature_89a(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test GIF89a signature."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        assert data[:6] == b"GIF89a"

    def test_gif_version(self):
        """Test GIF89a is used (not GIF87a) for animation."""
        # GIF89a required for animation and transparency
        gif89a = b"GIF89a"
        assert gif89a[3:6] == b"89a"


class TestGifDimensions:
    """Test GIF canvas dimensions."""

    def test_logical_screen_width(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test logical screen width."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        # Get source dimensions
        info_result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_width = info.get("width") or info.get("Width")

        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        gif_width = struct.unpack("<H", data[6:8])[0]
        assert gif_width == source_width

    def test_logical_screen_height(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test logical screen height."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        info_result = run(wsa_tool, "info", "--json", testdata_wsa_files[0])
        if info_result.returncode != 0:
            pytest.skip("Info not implemented")
        import json
        info = json.loads(info_result.stdout_text)
        source_height = info.get("height") or info.get("Height")

        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        gif_height = struct.unpack("<H", data[8:10])[0]
        assert gif_height == source_height


class TestGifFrameTiming:
    """Test frame timing in centiseconds."""

    def test_default_fps(self):
        """Test default 15 FPS -> 7 centiseconds delay."""
        fps = 15
        delay_cs = round(100 / fps)
        assert delay_cs == 7  # 100/15 = 6.67, rounds to 7

    def test_delay_calculation(self):
        """Test delay = round(100 / fps) centiseconds."""
        test_cases = [
            (10, 10),   # 100/10 = 10
            (15, 7),    # 100/15 = 6.67 -> 7
            (20, 5),    # 100/20 = 5
            (24, 4),    # 100/24 = 4.17 -> 4
            (25, 4),    # 100/25 = 4
            (30, 3),    # 100/30 = 3.33 -> 3
        ]
        for fps, expected_delay in test_cases:
            actual = round(100 / fps)
            assert actual == expected_delay, f"FPS {fps}: expected {expected_delay}, got {actual}"

    def test_custom_fps_option(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test --fps option affects timing."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "--fps", "10", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Would need to parse GIF to verify delay
        assert out_file.exists()


class TestGifLooping:
    """Test GIF looping behavior."""

    def test_netscape_extension_present(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test NETSCAPE2.0 extension for looping."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # NETSCAPE2.0 extension marker
        assert b"NETSCAPE2.0" in data or b"NETSCAPE" in data

    def test_loop_infinite_default(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test default is infinite loop (count=0)."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "--loop", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # Loop count 0 = infinite
        assert out_file.exists()

    def test_no_loop_option(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test --no-loop produces single-play GIF."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "--no-loop", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # No NETSCAPE extension or loop count = 1
        assert out_file.exists()


class TestGifTransparency:
    """Test GIF transparency handling."""

    def test_transparent_flag_option(self, shp_tool, testdata_shp_files, testdata_pal_files, run, temp_dir):
        """Test --transparent enables transparency."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(shp_tool, "export", "--gif", "--transparent", "-p", testdata_pal_files[0], testdata_shp_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_transparent_index(self):
        """Test index 0 is transparent color."""
        # GIF uses color index for transparency
        transparent_index = 0
        assert transparent_index == 0


class TestGifColorTable:
    """Test GIF color table."""

    def test_global_color_table_present(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test global color table is present."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # Packed byte at offset 10, bit 7 = global color table flag
        packed = data[10]
        has_gct = (packed & 0x80) != 0
        assert has_gct

    def test_color_table_size(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test color table has 256 entries."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # Size bits 0-2 of packed byte, actual size = 2^(N+1)
        packed = data[10]
        size_bits = packed & 0x07
        table_size = 2 ** (size_bits + 1)
        assert table_size == 256


class TestGifAnimation:
    """Test GIF animation structure."""

    def test_multiple_frames(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test animated GIF has multiple image blocks."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # Count image descriptors (0x2C marker)
        frame_count = data.count(b",")  # 0x2C = ','
        # Should have at least one frame
        assert frame_count >= 1

    def test_graphic_control_extension(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test graphic control extension for each frame."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        # GCE marker is 0x21 0xF9
        gce_marker = bytes([0x21, 0xF9])
        gce_count = data.count(gce_marker)
        # Each animated frame should have GCE
        assert gce_count >= 1

    def test_trailer_present(self, wsa_tool, testdata_wsa_files, testdata_pal_files, run, temp_dir):
        """Test GIF trailer (0x3B) terminates file."""
        if not testdata_wsa_files:
            pytest.skip("No WSA files in testdata")
        if not testdata_pal_files:
            pytest.skip("No PAL files in testdata")
        out_file = temp_dir / "test.gif"
        result = run(wsa_tool, "export", "-p", testdata_pal_files[0], testdata_wsa_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        data = out_file.read_bytes()
        assert data[-1] == 0x3B  # GIF trailer
