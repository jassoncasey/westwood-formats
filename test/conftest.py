"""
Pytest configuration and shared fixtures for Westwood tool tests.
"""

import os
import subprocess
import tempfile
import shutil
from pathlib import Path
from typing import Optional, Tuple

import pytest


# === Path Configuration ===

ROOT_DIR = Path(__file__).parent.parent
BUILD_DIR = ROOT_DIR / "impl" / "build"
TESTDATA_DIR = ROOT_DIR / "testdata"
EXTRACTED_DIR = Path(__file__).parent / "testdata" / "extracted"
GOLDEN_DIR = Path(__file__).parent / "testdata" / "golden"


# === Tool Path Fixtures ===

@pytest.fixture
def mix_tool() -> Path:
    """Path to mix-tool executable."""
    path = BUILD_DIR / "mix-tool"
    if not path.exists():
        pytest.skip("mix-tool not built")
    return path


@pytest.fixture
def aud_tool() -> Path:
    """Path to aud-tool executable."""
    path = BUILD_DIR / "aud-tool"
    if not path.exists():
        pytest.skip("aud-tool not built")
    return path


@pytest.fixture
def shp_tool() -> Path:
    """Path to shp-tool executable."""
    path = BUILD_DIR / "shp-tool"
    if not path.exists():
        pytest.skip("shp-tool not built")
    return path


@pytest.fixture
def pal_tool() -> Path:
    """Path to pal-tool executable."""
    path = BUILD_DIR / "pal-tool"
    if not path.exists():
        pytest.skip("pal-tool not built")
    return path


@pytest.fixture
def wsa_tool() -> Path:
    """Path to wsa-tool executable."""
    path = BUILD_DIR / "wsa-tool"
    if not path.exists():
        pytest.skip("wsa-tool not built")
    return path


@pytest.fixture
def tmp_tool() -> Path:
    """Path to tmp-tool executable."""
    path = BUILD_DIR / "tmp-tool"
    if not path.exists():
        pytest.skip("tmp-tool not built")
    return path


@pytest.fixture
def fnt_tool() -> Path:
    """Path to fnt-tool executable."""
    path = BUILD_DIR / "fnt-tool"
    if not path.exists():
        pytest.skip("fnt-tool not built")
    return path


@pytest.fixture
def cps_tool() -> Path:
    """Path to cps-tool executable."""
    path = BUILD_DIR / "cps-tool"
    if not path.exists():
        pytest.skip("cps-tool not built")
    return path


@pytest.fixture
def lcw_tool() -> Path:
    """Path to lcw-tool executable."""
    path = BUILD_DIR / "lcw-tool"
    if not path.exists():
        pytest.skip("lcw-tool not built")
    return path


@pytest.fixture
def vqa_tool() -> Path:
    """Path to vqa-tool executable."""
    path = BUILD_DIR / "vqa-tool"
    if not path.exists():
        pytest.skip("vqa-tool not built")
    return path


# === Test Data Fixtures ===

@pytest.fixture
def testdata_mix_files() -> list[Path]:
    """List of MIX files in testdata."""
    mix_dir = TESTDATA_DIR / "mix"
    if not mix_dir.exists():
        pytest.skip("testdata/mix directory not found")
    return list(mix_dir.glob("*.mix"))


@pytest.fixture
def testdata_vqa_files() -> list[Path]:
    """List of VQA files in testdata."""
    vqa_files = list(TESTDATA_DIR.glob("**/*.VQA")) + list(TESTDATA_DIR.glob("**/*.vqa"))
    return vqa_files


@pytest.fixture
def testdata_aud_files() -> list[Path]:
    """List of AUD files in testdata (extracted or found)."""
    aud_files = list(EXTRACTED_DIR.glob("**/*.aud")) + list(EXTRACTED_DIR.glob("**/*.AUD"))
    aud_files += list(TESTDATA_DIR.glob("**/*.aud")) + list(TESTDATA_DIR.glob("**/*.AUD"))
    return aud_files


@pytest.fixture
def testdata_shp_files() -> list[Path]:
    """List of SHP files in testdata (extracted or found)."""
    shp_files = list(EXTRACTED_DIR.glob("**/*.shp")) + list(EXTRACTED_DIR.glob("**/*.SHP"))
    shp_files += list(TESTDATA_DIR.glob("**/*.shp")) + list(TESTDATA_DIR.glob("**/*.SHP"))
    return shp_files


@pytest.fixture
def testdata_pal_files() -> list[Path]:
    """List of PAL files in testdata (extracted or found)."""
    pal_files = list(EXTRACTED_DIR.glob("**/*.pal")) + list(EXTRACTED_DIR.glob("**/*.PAL"))
    pal_files += list(TESTDATA_DIR.glob("**/*.pal")) + list(TESTDATA_DIR.glob("**/*.PAL"))
    return pal_files


@pytest.fixture
def testdata_wsa_files() -> list[Path]:
    """List of WSA files in testdata (extracted or found)."""
    wsa_files = list(EXTRACTED_DIR.glob("**/*.wsa")) + list(EXTRACTED_DIR.glob("**/*.WSA"))
    wsa_files += list(TESTDATA_DIR.glob("**/*.wsa")) + list(TESTDATA_DIR.glob("**/*.WSA"))
    return wsa_files


@pytest.fixture
def testdata_tmp_files() -> list[Path]:
    """List of TMP files in testdata (extracted or found)."""
    tmp_files = list(EXTRACTED_DIR.glob("**/*.tmp")) + list(EXTRACTED_DIR.glob("**/*.TMP"))
    tmp_files += list(TESTDATA_DIR.glob("**/*.tmp")) + list(TESTDATA_DIR.glob("**/*.TMP"))
    return tmp_files


@pytest.fixture
def testdata_fnt_files() -> list[Path]:
    """List of FNT files in testdata (extracted or found)."""
    fnt_files = list(EXTRACTED_DIR.glob("**/*.fnt")) + list(EXTRACTED_DIR.glob("**/*.FNT"))
    fnt_files += list(TESTDATA_DIR.glob("**/*.fnt")) + list(TESTDATA_DIR.glob("**/*.FNT"))
    return fnt_files


@pytest.fixture
def testdata_cps_files() -> list[Path]:
    """List of CPS files in testdata (extracted or found)."""
    cps_files = list(EXTRACTED_DIR.glob("**/*.cps")) + list(EXTRACTED_DIR.glob("**/*.CPS"))
    cps_files += list(TESTDATA_DIR.glob("**/*.cps")) + list(TESTDATA_DIR.glob("**/*.CPS"))
    return cps_files


@pytest.fixture
def golden_dir() -> Path:
    """Path to golden files directory."""
    return GOLDEN_DIR


# === Temporary Directory Fixtures ===

@pytest.fixture
def temp_dir():
    """Provide a temporary directory that's cleaned up after the test."""
    with tempfile.TemporaryDirectory() as tmpdir:
        yield Path(tmpdir)


@pytest.fixture
def temp_file(temp_dir):
    """Factory fixture for creating temporary files."""
    def _create_temp_file(suffix: str = "", content: bytes = b"") -> Path:
        path = temp_dir / f"test{suffix}"
        if content:
            path.write_bytes(content)
        return path
    return _create_temp_file


# === Tool Execution Helpers ===

class ToolResult:
    """Result of running a CLI tool."""
    def __init__(self, returncode: int, stdout: bytes, stderr: bytes):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.stdout_text = stdout.decode("utf-8", errors="replace")
        self.stderr_text = stderr.decode("utf-8", errors="replace")
        self.stdout_bytes = stdout  # Alias for binary output

    @property
    def success(self) -> bool:
        return self.returncode == 0

    def assert_success(self):
        """Assert the tool succeeded."""
        assert self.returncode == 0, f"Tool failed with code {self.returncode}: {self.stderr_text}"

    def assert_exit_code(self, expected: int):
        """Assert specific exit code."""
        assert self.returncode == expected, \
            f"Expected exit code {expected}, got {self.returncode}: {self.stderr_text}"

    def assert_error_contains(self, text: str):
        """Assert stderr contains specific text."""
        assert text in self.stderr_text, \
            f"Expected '{text}' in stderr, got: {self.stderr_text}"


def run_tool(tool_path: Path, *args, stdin_data: Optional[bytes] = None,
             timeout: int = 30, cwd: Optional[Path] = None) -> ToolResult:
    """
    Run a CLI tool and return the result.

    Args:
        tool_path: Path to the tool executable
        *args: Command line arguments
        stdin_data: Optional bytes to send to stdin
        timeout: Timeout in seconds
        cwd: Optional working directory

    Returns:
        ToolResult with returncode, stdout, stderr
    """
    cmd = [str(tool_path)] + [str(a) for a in args]
    try:
        result = subprocess.run(
            cmd,
            input=stdin_data,
            capture_output=True,
            timeout=timeout,
            cwd=cwd
        )
        return ToolResult(result.returncode, result.stdout, result.stderr)
    except subprocess.TimeoutExpired:
        pytest.fail(f"Tool timed out after {timeout}s: {' '.join(cmd)}")


@pytest.fixture
def run(request):
    """Fixture that provides the run_tool helper."""
    return run_tool


# === File Validation Helpers ===

def is_valid_wav(path: Path) -> bool:
    """Check if file is a valid WAV file."""
    if not path.exists():
        return False
    data = path.read_bytes()
    return data[:4] == b"RIFF" and data[8:12] == b"WAVE"


def is_valid_png(path: Path) -> bool:
    """Check if file is a valid PNG file."""
    if not path.exists():
        return False
    data = path.read_bytes()
    return data[:8] == b"\x89PNG\r\n\x1a\n"


def is_valid_gif(path: Path) -> bool:
    """Check if file is a valid GIF file."""
    if not path.exists():
        return False
    data = path.read_bytes()
    return data[:6] in (b"GIF87a", b"GIF89a")


def is_valid_json(path: Path) -> bool:
    """Check if file is valid JSON."""
    import json
    try:
        json.loads(path.read_text())
        return True
    except (json.JSONDecodeError, UnicodeDecodeError):
        return False


@pytest.fixture
def validate():
    """Fixture providing file validation helpers."""
    class Validators:
        wav = staticmethod(is_valid_wav)
        png = staticmethod(is_valid_png)
        gif = staticmethod(is_valid_gif)
        json = staticmethod(is_valid_json)
    return Validators()


# === WAV Parsing Helper ===

def parse_wav_header(path: Path) -> dict:
    """Parse WAV header and return format info."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Not a valid WAV file")

    # Find fmt chunk
    pos = 12
    while pos < len(data) - 8:
        chunk_id = data[pos:pos+4]
        chunk_size = int.from_bytes(data[pos+4:pos+8], "little")
        if chunk_id == b"fmt ":
            fmt_data = data[pos+8:pos+8+chunk_size]
            audio_format = int.from_bytes(fmt_data[0:2], "little")
            num_channels = int.from_bytes(fmt_data[2:4], "little")
            sample_rate = int.from_bytes(fmt_data[4:8], "little")
            byte_rate = int.from_bytes(fmt_data[8:12], "little")
            block_align = int.from_bytes(fmt_data[12:14], "little")
            bits_per_sample = int.from_bytes(fmt_data[14:16], "little")
            return {
                "audio_format": audio_format,
                "channels": num_channels,
                "sample_rate": sample_rate,
                "byte_rate": byte_rate,
                "block_align": block_align,
                "bits_per_sample": bits_per_sample
            }
        pos += 8 + chunk_size
        if chunk_size % 2:  # WAV chunks are word-aligned
            pos += 1

    raise ValueError("No fmt chunk found in WAV file")


@pytest.fixture
def parse_wav():
    """Fixture providing WAV header parser."""
    return parse_wav_header


# === PNG Info Helper ===

def get_png_info(path: Path) -> dict:
    """Get basic PNG info (requires IHDR chunk)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not a valid PNG file")

    # IHDR is always first chunk after signature
    if data[12:16] != b"IHDR":
        raise ValueError("IHDR chunk not found")

    width = int.from_bytes(data[16:20], "big")
    height = int.from_bytes(data[20:24], "big")
    bit_depth = data[24]
    color_type = data[25]

    return {
        "width": width,
        "height": height,
        "bit_depth": bit_depth,
        "color_type": color_type  # 2=RGB, 6=RGBA
    }


@pytest.fixture
def parse_png():
    """Fixture providing PNG info parser."""
    return get_png_info


# === Golden File Comparison ===

def compare_with_golden(actual: bytes, golden_path: Path, update: bool = False) -> bool:
    """
    Compare actual output with golden file.

    If update=True and GOLDEN_UPDATE env var is set, updates the golden file.
    """
    if update and os.environ.get("GOLDEN_UPDATE"):
        golden_path.parent.mkdir(parents=True, exist_ok=True)
        golden_path.write_bytes(actual)
        return True

    if not golden_path.exists():
        pytest.skip(f"Golden file not found: {golden_path}")

    expected = golden_path.read_bytes()
    return actual == expected


@pytest.fixture
def golden():
    """Fixture for golden file comparison."""
    return compare_with_golden
