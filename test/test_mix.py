#!/usr/bin/env python3
"""Test suite for mix-tool CLI and libmix C API."""

import ctypes
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path


# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

MIX_TOOL = os.environ.get("MIX_TOOL", "mix-tool")
MIX_LIB = os.environ.get("MIX_LIB", "libmix.dylib")
TESTDATA = Path(os.environ.get("TESTDATA", "../testdata"))
FIXTURES = Path(os.environ.get("FIXTURES", "fixtures"))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run_mix(*args):
    """Run mix-tool with args, return (stdout, stderr, returncode)."""
    result = subprocess.run(
        [MIX_TOOL] + list(args),
        capture_output=True,
        text=True
    )
    return result.stdout, result.stderr, result.returncode


def load_lib():
    """Load libmix shared library, return ctypes handle or None."""
    if not os.path.exists(MIX_LIB):
        return None
    return ctypes.CDLL(MIX_LIB)


# ---------------------------------------------------------------------------
# CLI Tests
# ---------------------------------------------------------------------------

class TestCLIBasic(unittest.TestCase):
    """Basic CLI argument handling tests."""

    def test_help(self):
        out, err, code = run_mix("--help")
        self.assertEqual(code, 0)
        self.assertIn("Usage:", out + err)

    def test_version(self):
        out, err, code = run_mix("--version")
        self.assertEqual(code, 0)
        self.assertIn("0.1.0", out + err)

    def test_list_help(self):
        out, err, code = run_mix("list", "--help")
        self.assertEqual(code, 0)
        self.assertIn("--tree", out + err)

    def test_unknown_command(self):
        _, err, code = run_mix("badcmd")
        self.assertEqual(code, 2)
        self.assertIn("unknown command", err)

    def test_unknown_option(self):
        _, err, code = run_mix("list", "--badopt", "x.mix")
        self.assertEqual(code, 2)
        self.assertIn("unknown option", err)

    def test_missing_file_arg(self):
        _, err, code = run_mix("list")
        self.assertEqual(code, 2)
        self.assertIn("missing file", err)


class TestCLIFileErrors(unittest.TestCase):
    """File access error tests."""

    def test_file_not_found(self):
        _, err, code = run_mix("list", "/nonexistent.mix")
        self.assertEqual(code, 1)
        self.assertIn("Cannot open", err)


class TestCLITDFormat(unittest.TestCase):
    """TD format parsing tests."""

    @classmethod
    def setUpClass(cls):
        cls.aud = TESTDATA / "mix" / "cd1_setup_aud.mix"
        cls.setup = TESTDATA / "mix" / "cd1_setup_setup.mix"

    def test_aud_table(self):
        out, _, code = run_mix("list", str(self.aud))
        self.assertEqual(code, 0)
        self.assertIn("47 files", out)
        self.assertIn("Tiberian Dawn", out)

    def test_setup_table(self):
        out, _, code = run_mix("list", str(self.setup))
        self.assertEqual(code, 0)
        self.assertIn("71 files", out)

    def test_aud_tree(self):
        out, _, code = run_mix("list", "--tree", str(self.aud))
        self.assertEqual(code, 0)
        self.assertIn("├──", out)
        self.assertIn("└──", out)

    def test_aud_json(self):
        out, _, code = run_mix("list", "--json", str(self.aud))
        self.assertEqual(code, 0)
        data = json.loads(out)
        self.assertEqual(data["file_count"], 47)
        self.assertEqual(data["format"], "TD")
        self.assertFalse(data["encrypted"])

    def test_json_entry_count(self):
        out, _, code = run_mix("list", "--json", str(self.aud))
        data = json.loads(out)
        self.assertEqual(len(data["entries"]), data["file_count"])


class TestCLIRAFormat(unittest.TestCase):
    """RA format (encrypted) detection tests."""

    @classmethod
    def setUpClass(cls):
        cls.redalert = TESTDATA / "mix" / "cd1_install_redalert.mix"

    def test_encrypted_not_supported(self):
        _, err, code = run_mix("list", str(self.redalert))
        self.assertEqual(code, 1)
        self.assertIn("not yet supported", err)


class TestCLINameResolution(unittest.TestCase):
    """Filename resolution tests."""

    @classmethod
    def setUpClass(cls):
        cls.aud = TESTDATA / "mix" / "cd1_setup_aud.mix"
        cls.names_file = FIXTURES / "names.txt"

    def test_name_option(self):
        # Use a fake name - won't match but shouldn't error
        out, _, code = run_mix(
            "list", "--name=TEST.AUD", str(self.aud)
        )
        self.assertEqual(code, 0)

    def test_names_file_missing(self):
        out, err, code = run_mix(
            "list", "-F", "/nonexistent.txt", str(self.aud)
        )
        self.assertEqual(code, 0)  # warning only
        self.assertIn("warning", err)


# ---------------------------------------------------------------------------
# C API Tests
# ---------------------------------------------------------------------------

class TestCAPI(unittest.TestCase):
    """C API (FFI) tests via ctypes."""

    @classmethod
    def setUpClass(cls):
        cls.lib = load_lib()
        if cls.lib is None:
            raise unittest.SkipTest("libmix.dylib not found")
        cls._setup_ctypes()
        cls.aud = TESTDATA / "mix" / "cd1_setup_aud.mix"

    @classmethod
    def _setup_ctypes(cls):
        lib = cls.lib
        lib.mix_reader_open.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p)
        ]
        lib.mix_reader_open.restype = ctypes.c_uint32
        lib.mix_reader_free.argtypes = [ctypes.c_void_p]
        lib.mix_reader_count.argtypes = [ctypes.c_void_p]
        lib.mix_reader_count.restype = ctypes.c_uint32
        lib.mix_error_string.argtypes = [ctypes.c_uint32]
        lib.mix_error_string.restype = ctypes.c_char_p
        lib.mix_version.restype = ctypes.c_char_p
        lib.mix_hash_td.argtypes = [ctypes.c_char_p]
        lib.mix_hash_td.restype = ctypes.c_uint32

    def test_version(self):
        ver = self.lib.mix_version().decode()
        self.assertEqual(ver, "0.1.0")

    def test_open_close(self):
        reader = ctypes.c_void_p()
        err = self.lib.mix_reader_open(
            str(self.aud).encode(),
            ctypes.byref(reader)
        )
        self.assertEqual(err, 0)
        self.assertIsNotNone(reader.value)
        self.lib.mix_reader_free(reader)

    def test_count(self):
        reader = ctypes.c_void_p()
        self.lib.mix_reader_open(
            str(self.aud).encode(),
            ctypes.byref(reader)
        )
        count = self.lib.mix_reader_count(reader)
        self.assertEqual(count, 47)
        self.lib.mix_reader_free(reader)

    def test_error_string(self):
        msg = self.lib.mix_error_string(1).decode()
        self.assertIn("not found", msg.lower())

    def test_hash_td(self):
        # Known hash for "TEST.MIX" using TD algorithm
        h = self.lib.mix_hash_td(b"TEST.MIX")
        self.assertIsInstance(h, int)
        self.assertGreater(h, 0)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
