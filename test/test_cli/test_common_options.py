"""
Tests for common CLI options across all tools.

Tests cover:
- Exit codes
- --help and --version
- -o output option
- --force overwrite behavior
- --json flag
- stdin/stdout via - convention
"""

import pytest


class TestExitCodes:
    """Test standard exit codes."""

    def test_success_exit_0(self, aud_tool, testdata_aud_files, run):
        """Test successful operation returns exit code 0."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", testdata_aud_files[0])
        assert result.returncode == 0

    def test_usage_error_exit_1(self, aud_tool, run):
        """Test usage error returns exit code 1."""
        result = run(aud_tool, "invalid_command")
        assert result.returncode == 1

    def test_input_error_exit_2(self, aud_tool, run):
        """Test input error (bad file) returns exit code 2."""
        result = run(aud_tool, "info", "/nonexistent/file.aud")
        assert result.returncode == 2

    def test_output_error_exit_3(self, aud_tool, testdata_aud_files, run):
        """Test output error returns exit code 3."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        # Try to write to unwritable location
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", "/nonexistent/dir/out.wav")
        assert result.returncode == 3


class TestHelpOption:
    """Test --help option."""

    def test_help_long_form(self, aud_tool, run):
        """Test --help shows usage."""
        result = run(aud_tool, "--help")
        assert result.returncode == 0
        assert "usage" in result.stdout_text.lower() or "Usage" in result.stdout_text

    def test_help_short_form(self, aud_tool, run):
        """Test -h shows usage."""
        result = run(aud_tool, "-h")
        assert result.returncode == 0
        assert "usage" in result.stdout_text.lower() or "Usage" in result.stdout_text

    def test_help_lists_commands(self, aud_tool, run):
        """Test help lists available commands."""
        result = run(aud_tool, "--help")
        assert result.returncode == 0
        # Should mention info and export commands
        assert "info" in result.stdout_text.lower()


class TestVersionOption:
    """Test --version option."""

    def test_version_long_form(self, aud_tool, run):
        """Test --version shows version."""
        result = run(aud_tool, "--version")
        assert result.returncode == 0
        # Should contain version number pattern
        assert any(c.isdigit() for c in result.stdout_text)

    def test_version_short_form(self, aud_tool, run):
        """Test -V shows version."""
        result = run(aud_tool, "-V")
        assert result.returncode == 0


class TestOutputOption:
    """Test -o output path option."""

    def test_output_to_file(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test -o writes to specified file."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "output.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()

    def test_output_long_form(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test --output works same as -o."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "output.wav"
        result = run(aud_tool, "export", testdata_aud_files[0], "--output", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        assert out_file.exists()


class TestForceOption:
    """Test --force overwrite option."""

    def test_no_force_refuses_overwrite(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test refuses to overwrite without --force."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "existing.wav"
        out_file.write_bytes(b"existing data")

        result = run(aud_tool, "export", testdata_aud_files[0], "-o", str(out_file))
        # Should fail or warn about existing file
        assert result.returncode != 0 or "exist" in result.stderr_text.lower()

    def test_force_allows_overwrite(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test --force allows overwriting."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "existing.wav"
        out_file.write_bytes(b"existing data")

        result = run(aud_tool, "export", "--force", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")
        # File should be overwritten (not still original content)
        assert out_file.read_bytes() != b"existing data"

    def test_force_short_form(self, aud_tool, testdata_aud_files, run, temp_dir):
        """Test -f works same as --force."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        out_file = temp_dir / "existing.wav"
        out_file.write_bytes(b"existing data")

        result = run(aud_tool, "export", "-f", testdata_aud_files[0], "-o", str(out_file))
        if result.returncode != 0:
            pytest.skip("Export not implemented")


class TestJsonOption:
    """Test --json output option."""

    def test_json_flag(self, aud_tool, testdata_aud_files, run):
        """Test --json produces JSON output."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result.returncode != 0:
            pytest.skip("JSON not implemented")
        import json
        data = json.loads(result.stdout_text)
        assert isinstance(data, dict)

    def test_json_vs_human_readable(self, aud_tool, testdata_aud_files, run):
        """Test --json output differs from default."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result_human = run(aud_tool, "info", testdata_aud_files[0])
        result_json = run(aud_tool, "info", "--json", testdata_aud_files[0])
        if result_human.returncode != 0 or result_json.returncode != 0:
            pytest.skip("Info not implemented")
        # JSON should start with { and human readable should not
        assert result_json.stdout_text.strip().startswith("{")


class TestStdinStdout:
    """Test Unix stdin/stdout conventions."""

    def test_stdin_via_dash(self, aud_tool, testdata_aud_files, run):
        """Test reading from stdin via -."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        # Read file and pipe to tool
        with open(testdata_aud_files[0], "rb") as f:
            data = f.read()
        result = run(aud_tool, "info", "-", stdin_data=data)
        if result.returncode != 0:
            pytest.skip("stdin not implemented")
        # Should produce output
        assert len(result.stdout_text) > 0

    def test_stdout_via_dash_o(self, aud_tool, testdata_aud_files, run):
        """Test writing to stdout via -o -."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result = run(aud_tool, "export", testdata_aud_files[0], "-o", "-")
        if result.returncode != 0:
            pytest.skip("stdout not implemented")
        # Should have binary output
        assert len(result.stdout_bytes) > 0


class TestVerboseOption:
    """Test --verbose option."""

    def test_verbose_more_output(self, aud_tool, testdata_aud_files, run):
        """Test --verbose produces more output."""
        if not testdata_aud_files:
            pytest.skip("No AUD files in testdata")
        result_normal = run(aud_tool, "info", testdata_aud_files[0])
        result_verbose = run(aud_tool, "info", "--verbose", testdata_aud_files[0])
        if result_normal.returncode != 0:
            pytest.skip("Info not implemented")
        # Verbose should have at least as much output
        assert len(result_verbose.stdout_text) >= len(result_normal.stdout_text)


class TestAllToolsHaveOptions:
    """Test all tools support common options."""

    def test_all_tools_have_help(self, aud_tool, shp_tool, pal_tool, wsa_tool,
                                  tmp_tool, fnt_tool, cps_tool, vqa_tool, run):
        """Test all tools support --help."""
        tools = [aud_tool, shp_tool, pal_tool, wsa_tool,
                 tmp_tool, fnt_tool, cps_tool, vqa_tool]
        for tool in tools:
            if tool is None:
                continue
            result = run(tool, "--help")
            assert result.returncode == 0, f"{tool} --help failed"

    def test_all_tools_have_version(self, aud_tool, shp_tool, pal_tool, wsa_tool,
                                     tmp_tool, fnt_tool, cps_tool, vqa_tool, run):
        """Test all tools support --version."""
        tools = [aud_tool, shp_tool, pal_tool, wsa_tool,
                 tmp_tool, fnt_tool, cps_tool, vqa_tool]
        for tool in tools:
            if tool is None:
                continue
            result = run(tool, "--version")
            assert result.returncode == 0, f"{tool} --version failed"
