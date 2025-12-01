# mix-tool Test Suite

## Overview

Tests for mix-tool CLI and libmix C API using Python unittest.

## Requirements

- Python 3.8+
- Built mix-tool and libmix (run cmake build first)

## Running Tests

```bash
cd test
./run_tests.sh
```

Or with verbose output:

```bash
./run_tests.sh -v
```

## Test Structure

```
test/
├── run_tests.sh      # Entry point, sets up environment
├── test_mix.py       # Python test suite
├── test.md           # This file
└── fixtures/
    └── names.txt     # Known filenames for resolution tests
```

## Test Categories

### CLI Basic (`TestCLIBasic`)

| Test                 | Description                          |
|----------------------|--------------------------------------|
| test_help            | --help shows usage, exits 0          |
| test_version         | --version shows version, exits 0     |
| test_list_help       | list --help shows options, exits 0   |
| test_unknown_command | Unknown command exits 2              |
| test_unknown_option  | Unknown option exits 2               |
| test_missing_file_arg| Missing file argument exits 2        |

### CLI File Errors (`TestCLIFileErrors`)

| Test               | Description                           |
|--------------------|---------------------------------------|
| test_file_not_found| Non-existent file exits 1 with error  |

### CLI TD Format (`TestCLITDFormat`)

| Test               | Description                           |
|--------------------|---------------------------------------|
| test_aud_table     | AUD.MIX table output, 47 files        |
| test_setup_table   | SETUP.MIX table output, 71 files      |
| test_aud_tree      | Tree output has box-drawing chars     |
| test_aud_json      | JSON output is valid, correct fields  |
| test_json_entry_count | Entry count matches file_count     |

### CLI RA Format (`TestCLIRAFormat`)

| Test                      | Description                      |
|---------------------------|----------------------------------|
| test_encrypted_not_supported | Encrypted file exits 1        |

### CLI Name Resolution (`TestCLINameResolution`)

| Test                 | Description                          |
|----------------------|--------------------------------------|
| test_name_option     | --name option accepted               |
| test_names_file_missing | Missing names file warns only     |

### C API (`TestCAPI`)

| Test              | Description                            |
|-------------------|----------------------------------------|
| test_version      | mix_version() returns "0.1.0"          |
| test_open_close   | Open/close reader without error        |
| test_count        | mix_reader_count() returns 47          |
| test_error_string | Error codes have string messages       |
| test_hash_td      | mix_hash_td() returns nonzero hash     |

## Exit Codes

| Code | Meaning                                      |
|-----:|----------------------------------------------|
| 0    | All tests passed                             |
| 1    | One or more tests failed                     |

## Environment Variables

| Variable   | Default            | Description               |
|------------|--------------------| --------------------------|
| MIX_TOOL   | impl/build/mix-tool| Path to CLI binary        |
| MIX_LIB    | impl/build/libmix.dylib | Path to shared lib   |
| TESTDATA   | ../testdata        | Test MIX files directory  |
| FIXTURES   | fixtures           | Test fixtures directory   |

## Adding Tests

1. Add test method to appropriate class in `test_mix.py`
2. Follow naming: `test_<description>`
3. Use `run_mix()` helper for CLI tests
4. Use `self.lib` for C API tests
5. Keep tests under 10 lines when possible

## Test Data

Test files in `testdata/`:

| File                     | Format | Files | Use Case          |
|--------------------------|--------|------:|-------------------|
| cd1_setup_aud.mix        | TD     |    47 | Primary TD tests  |
| cd1_setup_setup.mix      | TD     |    71 | Secondary TD test |
| cd1_install_redalert.mix | RA     |     ? | Encrypted error   |
