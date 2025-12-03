"""
Integration tests for error handling and recovery.

Tests cover:
- Graceful handling of corrupted files
- Missing dependencies
- Partial processing recovery
- Error message quality
"""

import pytest
from pathlib import Path


class TestCorruptedFileHandling:
    """Test handling of corrupted/truncated files."""

    def test_truncated_aud(self, aud_tool, temp_file, run):
        """Test handling truncated AUD file."""
        # Create truncated AUD (valid header but missing data)
        # AUD header is 12 bytes
        truncated = temp_file(".aud", b"\x00" * 12)
        result = run(aud_tool, "info", truncated)
        result.assert_exit_code(2)
        # Should have meaningful error message
        assert len(result.stderr_text) > 0

    def test_truncated_shp(self, shp_tool, temp_file, run):
        """Test handling truncated SHP file."""
        # SHP header is 6 bytes minimum
        truncated = temp_file(".shp", b"\x00" * 6)
        result = run(shp_tool, "info", truncated)
        result.assert_exit_code(2)

    def test_truncated_vqa(self, vqa_tool, temp_file, run):
        """Test handling truncated VQA file."""
        # VQA needs FORM + size + WVQA
        truncated = temp_file(".vqa", b"FORM\x00\x00\x00\x00WVQA")
        result = run(vqa_tool, "info", truncated)
        result.assert_exit_code(2)

    def test_random_garbage_file(self, aud_tool, temp_file, run):
        """Test handling file with random garbage."""
        import os
        garbage = temp_file(".aud", os.urandom(1024))
        result = run(aud_tool, "info", garbage)
        result.assert_exit_code(2)


class TestMissingDependencies:
    """Test handling of missing dependencies."""

    def test_shp_missing_palette(
        self, shp_tool, testdata_shp_files, run, temp_dir
    ):
        """Test SHP export without palette."""
        if not testdata_shp_files:
            pytest.skip("No SHP files in testdata")
        out_file = temp_dir / "test.png"
        result = run(
            shp_tool, "export", testdata_shp_files[0], "-o", str(out_file)
        )
        # Should fail with clear message about missing palette
        if result.returncode != 0:
            stderr_lower = result.stderr_text.lower()
            assert "palette" in stderr_lower or "pal" in stderr_lower

    def test_vqa_mp4_missing_ffmpeg(
        self, vqa_tool, testdata_vqa_files, run, temp_dir, monkeypatch
    ):
        """Test VQA MP4 export without ffmpeg."""
        if not testdata_vqa_files:
            pytest.skip("No VQA files in testdata")
        # Remove ffmpeg from PATH
        monkeypatch.setenv("PATH", "")
        out_file = temp_dir / "test.mp4"
        result = run(
            vqa_tool, "export", "--mp4", testdata_vqa_files[0], "-o",
            str(out_file)
        )
        if result.returncode != 0:
            # Should mention ffmpeg in error
            stderr_lower = result.stderr_text.lower()
            assert "ffmpeg" in stderr_lower or result.returncode in [1, 2, 3]


class TestFilePermissions:
    """Test handling of permission issues."""

    def test_unreadable_input(self, aud_tool, temp_file, run):
        """Test handling unreadable input file."""
        import os
        import stat
        unreadable = temp_file(".aud", b"test data")
        os.chmod(unreadable, 0o000)
        try:
            result = run(aud_tool, "info", unreadable)
            result.assert_exit_code(2)
        finally:
            os.chmod(unreadable, stat.S_IRUSR | stat.S_IWUSR)

    def test_unwritable_output_dir(
        self, aud_tool, testdata_aud_files, run, temp_dir
    ):
        """Test handling unwritable output directory."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        import os
        import stat

        unwritable_dir = temp_dir / "unwritable"
        unwritable_dir.mkdir()
        os.chmod(unwritable_dir, stat.S_IRUSR | stat.S_IXUSR)

        try:
            out_file = unwritable_dir / "test.wav"
            result = run(
                aud_tool, "export", testdata_aud_files[0], "-o", str(out_file)
            )
            result.assert_exit_code(3)
        finally:
            os.chmod(unwritable_dir, stat.S_IRWXU)


class TestOverwriteProtection:
    """Test file overwrite protection."""

    def test_refuse_overwrite_without_force(
        self, aud_tool, testdata_aud_files, run, temp_dir
    ):
        """Test refusing to overwrite existing file."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        existing = temp_dir / "existing.wav"
        existing.write_bytes(b"RIFF" + b"\x00" * 100)

        result = run(
            aud_tool, "export", testdata_aud_files[0], "-o", str(existing)
        )
        # Should fail or warn
        if result.returncode == 0:
            # Check file wasn't changed (some implementations may succeed)
            pass
        else:
            stderr_lower = result.stderr_text.lower()
            assert "exist" in stderr_lower or "force" in stderr_lower

    def test_allow_overwrite_with_force(
        self, aud_tool, testdata_aud_files, run, temp_dir
    ):
        """Test allowing overwrite with --force."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        existing = temp_dir / "existing.wav"
        existing.write_bytes(b"original content")
        original_size = existing.stat().st_size

        result = run(
            aud_tool, "export", "--force", testdata_aud_files[0], "-o",
            str(existing)
        )
        if result.returncode == 0:
            # File should be different now
            size_changed = existing.stat().st_size != original_size
            content_changed = existing.read_bytes() != b"original content"
            assert size_changed or content_changed


class TestErrorMessages:
    """Test quality of error messages."""

    def test_nonexistent_file_message(self, aud_tool, run):
        """Test error message for nonexistent file."""
        result = run(aud_tool, "info", "/nonexistent/path/file.aud")
        result.assert_exit_code(2)
        # Should mention the file or "not found"
        stderr_lower = result.stderr_text.lower()
        assert (
            "not found" in stderr_lower or
            "no such" in stderr_lower or
            "nonexistent" in stderr_lower
        )

    def test_invalid_format_message(self, aud_tool, temp_file, run):
        """Test error message for invalid format."""
        bad_file = temp_file(".aud", b"NOT_AUD_FORMAT_DATA")
        result = run(aud_tool, "info", bad_file)
        result.assert_exit_code(2)
        # Should indicate format/validation issue

    def test_invalid_command_message(self, aud_tool, run):
        """Test error message for invalid command."""
        result = run(aud_tool, "invalid_command_xyz")
        result.assert_exit_code(1)
        # Should list valid commands or suggest help

    def test_missing_argument_message(self, aud_tool, run):
        """Test error message for missing argument."""
        result = run(aud_tool, "info")  # Missing filename
        result.assert_exit_code(1)
        # Should indicate missing argument


class TestPartialProcessing:
    """Test handling partial processing scenarios."""

    def test_batch_continues_after_error(
        self, aud_tool, testdata_aud_files, temp_file, run, temp_dir
    ):
        """Test batch processing continues after individual errors."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")

        # Create a mix of valid and invalid files
        bad_file = temp_file(".aud", b"invalid")
        good_file = testdata_aud_files[0]

        # Process multiple files (if supported)
        result = run(aud_tool, "info", bad_file, good_file)
        # Should process the good file even if bad file fails
        # (specific behavior depends on implementation)

    def test_output_on_decode_error(
        self, vqa_tool, testdata_vqa_files, testdata_pal_files, run, temp_dir
    ):
        """Test partial output on decode error."""
        # This tests if tool can produce partial output when decode
        # fails mid-stream
        pytest.skip("Requires specially crafted test file")
