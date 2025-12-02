# C&C Red Alert Archive Tools

Tools for reading Westwood Studios MIX archive files from Command & Conquer games.

## Project Overview

This project provides `mix-tool`, a CLI utility, and `libmix`, a C/C++ library for
parsing MIX archive files used in Command & Conquer: Red Alert and related games.

**Status:** Phase 1 complete (TD format). Phase 2 (encrypted RA format) in progress.

## Quick Start

```bash
# Setup test data (downloads from Internet Archive)
./scripts/setup_testdata.sh

# Build
cd impl && cmake -B build && cmake --build build

# Run
./build/mix-tool list ../testdata/mix/cd1_setup_aud.mix

# Test
cd ../test && ./run_tests.sh
```

## Project Structure

```
cnc_redalert/
├── README.md           # This file - project overview
├── mix-tool.md         # Tool specification, API docs, build instructions
├── xcc.md              # MIX file format specification (reverse-engineered)
├── impl/               # C++ implementation
│   ├── libmix/         #   Core library (C++ with C API for FFI)
│   └── mix-tool/       #   CLI application
├── scripts/
│   └── setup_testdata.sh   # Download and extract test assets
├── test/               # Python test suite
│   ├── run_tests.sh    #   Test runner
│   └── test_mix.py     #   Unit tests (CLI + C API via ctypes)
├── testdata/           # Test assets (not in git, see scripts/)
│   ├── mix/            #   MIX archive files
│   ├── vqa/            #   VQA video files
│   ├── ard/            #   Menu graphics (BMP) and audio (WAV)
│   └── ico/            #   Windows icons
└── reference/
    └── xcc/            # XCC Utilities source (reference implementation)
```

## Documentation Index

| File | Purpose |
|------|---------|
| [mix-tool.md](mix-tool.md) | CLI usage, library API, build system, test data catalog |
| [xcc.md](xcc.md) | Archive formats: MIX (TD, RA, TS), BIG containers |
| [westwood-formats.md](westwood-formats.md) | Media formats: VQA, AUD, SHP, PAL, WSA, TMP, FNT |
| [test/test.md](test/test.md) | Test strategy and coverage |

## Key Concepts

### MIX Format Variants

| Format | Games | Encryption | Notes |
|--------|-------|------------|-------|
| TD | Tiberian Dawn | None | Simple header + index |
| RA | Red Alert | RSA+Blowfish | Encrypted index |
| TS | Tiberian Sun, RA2 | None | 4-byte aligned |

### Filename Hashing

MIX files store entries by 32-bit hash, not filename. Two algorithms exist:
- **TD/RA:** Rotate-add (ROL 1 + add each char)
- **TS+:** CRC32 with null-padding to 4-byte boundary

See `xcc.md` for algorithm details and test vectors.

## Implementation Status

- [x] TD format parsing (unencrypted)
- [x] Table/tree/JSON output
- [x] Filename resolution via hash matching
- [x] C API for FFI (Python, Rust, etc.)
- [ ] RA format decryption (RSA + Blowfish)
- [ ] Recursive listing of nested MIX files
- [ ] TS/RA2 format support

## Test Data

Test assets are extracted from Red Alert CD ISOs (freeware release by EA, 2008).
Not stored in git due to size (~1 GB). Run setup script to download:

```bash
./scripts/setup_testdata.sh          # Extract then delete ISOs
./scripts/setup_testdata.sh --keep-iso  # Keep ISOs in downloads/
```

Source: [Internet Archive](https://archive.org/details/command-conquer-red-alert_202309)

## Architecture Notes

### Library Design

- **C++ core** with modern features (C++23, std::expected, std::span)
- **Pure C API** (`mix/mix_c.h`) for FFI compatibility
- **Pimpl pattern** hides implementation details
- **Cross-platform** build via CMake (macOS, Linux, Windows)

### Error Handling

- C++ API uses `std::expected<T, Error>` for fallible operations
- C API uses error codes with `mix_error_string()` for messages
- CLI uses exit codes: 0=success, 1=runtime error, 2=usage error

### File Organization in impl/

```
impl/
├── CMakeLists.txt          # Build configuration
├── libmix/
│   ├── include/mix/
│   │   ├── export.h        # Symbol visibility macros
│   │   ├── types.h         # Constants (magic numbers, limits)
│   │   ├── error.h         # Error codes and Error class
│   │   ├── mix.h           # C++ API (MixReader, Entry, etc.)
│   │   └── mix_c.h         # C API (opaque handles, C types)
│   └── src/
│       ├── error.cpp       # Error implementation
│       ├── hash.cpp        # TD and TS hash algorithms
│       ├── mix_reader.cpp  # Format parsing, file reading
│       └── mix_c.cpp       # C API wrapper
└── mix-tool/
    ├── main.cpp            # CLI entry point, argument parsing
    ├── cmd_list.cpp        # 'list' command implementation
    └── format.cpp          # Output formatting (table, tree, JSON)
```
