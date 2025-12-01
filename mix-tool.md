# mix-tool Specification

Command-line utility and library for working with Westwood Studios MIX archive files.

See also: [README.md](README.md) (project overview), [xcc.md](xcc.md) (format spec)

## Overview

`mix-tool` is a CLI utility backed by `libmix`, a static C++ library. The tool
provides read operations for MIX archive files from Command & Conquer games.

## Project Structure

```
cnc_redalert/
├── xcc.md                  # Format specification
├── mix-tool.md             # This file - tool specification
├── impl/
│   ├── CMakeLists.txt
│   ├── cmake/
│   │   └── mix-config.cmake.in
│   ├── build/              # Build output
│   │   ├── mix-tool        # CLI binary
│   │   ├── libmix.a        # Static library
│   │   └── libmix.dylib    # Shared library (if enabled)
│   ├── libmix/
│   │   ├── include/mix/
│   │   │   ├── export.h    # Symbol visibility macros
│   │   │   ├── types.h     # Constants and types
│   │   │   ├── error.h     # Error handling
│   │   │   ├── mix.h       # C++ API
│   │   │   └── mix_c.h     # C API (FFI)
│   │   └── src/
│   │       ├── error.cpp
│   │       ├── hash.cpp    # Filename hashing algorithms
│   │       ├── mix_reader.cpp
│   │       └── mix_c.cpp   # C API implementation
│   └── mix-tool/
│       ├── main.cpp
│       ├── cmd_list.cpp
│       ├── format.h
│       └── format.cpp
├── scripts/
│   └── setup_testdata.sh   # Download ISOs and extract testdata
├── testdata/               # Test assets (organized by type, not in git)
│   ├── mix/                # MIX archive files
│   ├── vqa/                # VQA video files and VQP palettes
│   ├── ard/                # Menu graphics (BMP) and audio (WAV)
│   └── ico/                # Windows icon files
└── reference/
    └── xcc/                # XCC Utilities source reference
```

## Command Line Interface

### Synopsis

```
mix-tool <command> [options] <file>

Commands:
    list        List contents of a MIX archive

Global Options:
    -h, --help      Show help message
    -v, --version   Show version information
```

### list Command

List the contents of a MIX archive file.

```
mix-tool list [options] <file>

Options:
    -t, --tree              Display as tree view (default: table)
    -j, --json              Output as JSON
    -r, --recursive         Recurse into nested MIX files
    -F, --names-file=FILE   Load filename mappings from FILE (one per line)
                            Can be specified multiple times
    -n, --name=NAME         Add a known filename mapping
                            Can be specified multiple times

Output Modes:
    table (default)     Tabular format with columns
    tree (-t)           Tree view with indentation
    json (-j)           JSON array of entries
```

### Output Formats

#### Table Format (default)

```
cd1_setup_aud.mix (Tiberian Dawn, 47 files, 1,456,342 bytes)

Name                Hash          Offset                Size
------------------------------------------------------------
<unknown>           0x81c01b16    0x0000023a          65,148
<unknown>           0x81c21b16    0x000100b6          65,249
SOUNDS.AUD          0x8fa5ac1a    0x0001ff97         166,932
```

Header shows: filename, game type, file count, total archive size.
Entries sorted by offset, showing name (or `<unknown>`), hash, hex offset, decimal size.

#### Tree Format (`--tree`)

```
cd1_setup_aud.mix (Tiberian Dawn, 47 files, 1,456,342 bytes)
├── <unknown> (0x81c01b16, offset=0x0000023a, 65,148 bytes)
├── <unknown> (0x81c21b16, offset=0x000100b6, 65,249 bytes)
├── SOUNDS.AUD (0x8fa5ac1a, offset=0x0001ff97, 166,932 bytes)
└── LAST.DAT (0x7a2208d6, offset=0x00152f32, 68,004 bytes)
```

With `--recursive` (not yet implemented):

```
MAIN.MIX (Red Alert, encrypted, 100 files, 454,605,294 bytes)
├── CONQUER.MIX (0xaabbccdd, offset=0x00010000, 524,288 bytes)
│   ├── RULES.INI (0x11223344, offset=0x00000000, 1,024 bytes)
│   └── ART.INI (0x55667788, offset=0x00000400, 2,048 bytes)
└── SOUNDS.MIX (0xccddee00, offset=0x00090000, 1,048,576 bytes)
```

#### JSON Format (`--json`)

```json
{
  "file": "cd1_setup_aud.mix",
  "format": "TD",
  "encrypted": false,
  "checksum": false,
  "file_count": 47,
  "file_size": 1456342,
  "entries": [
    {
      "name": null,
      "hash": "0x81c01b16",
      "offset": 570,
      "size": 65148
    },
    {
      "name": "SOUNDS.AUD",
      "hash": "0x8fa5ac1a",
      "offset": 130967,
      "size": 166932
    }
  ]
}
```

With `--recursive`, nested MIX files include a `children` array (not yet implemented).

### Exit Codes

| Code | Meaning                                      |
|-----:|----------------------------------------------|
| 0    | Success                                      |
| 1    | General error (corrupt file, parse failure)  |
| 2    | Usage error (bad arguments, file not found)  |

### Filename Resolution

Filenames are resolved by computing hashes and matching against the index:

1. Load filenames from `--names-file` files (if specified)
2. Add filenames from `--name` arguments (if specified)
3. For each filename, compute hash using detected game's algorithm
4. Match computed hashes against index entry hashes
5. Unmatched entries display as `<unknown>`

Filename file format (one filename per line):
```
RULES.INI
TEMPERAT.PAL
CONQUER.MIX
# Lines starting with # are comments
# Empty lines are ignored
```

---

## Library API (libmix)

### Public Header: `mix/mix.h`

```cpp
#pragma once

#include <mix/types.h>
#include <mix/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mix {

// Forward declarations
class MixReader;

// Game type enumeration
enum class GameType {
    Unknown,
    TiberianDawn,   // TD format
    RedAlert,       // RA format (encrypted)
    TiberianSun,    // TS format
    RedAlert2,      // TS format
    YurisRevenge,   // TS format
    Renegade,       // MIX-RG format
    Generals,       // BIG format
    ZeroHour        // BIG format
};

// Format type (structural, independent of game)
enum class FormatType {
    Unknown,
    TD,         // Tiberian Dawn: simple header
    RA,         // Red Alert: encrypted
    TS,         // Tiberian Sun: unencrypted, aligned
    MixRG,      // Renegade: separate index/tailer
    BIG         // Generals: big-endian counts
};

// Single index entry
struct Entry {
    uint32_t    hash;       // Filename hash (ID)
    uint32_t    offset;     // Absolute byte offset in file
    uint32_t    size;       // Size in bytes
    std::string name;       // Resolved name (empty if unknown)
};

// Archive metadata
struct ArchiveInfo {
    FormatType  format;
    GameType    game;
    bool        encrypted;
    bool        has_checksum;
    uint32_t    file_count;
    uint64_t    file_size;
};

// Result type for operations that can fail
template<typename T>
using Result = std::expected<T, Error>;

// Main reader class
class MixReader {
public:
    // Open a MIX file from path
    static Result<std::unique_ptr<MixReader>> open(const std::string& path);

    // Open from memory buffer
    static Result<std::unique_ptr<MixReader>> open(std::span<const uint8_t> data);

    ~MixReader();

    // Archive information
    const ArchiveInfo& info() const;

    // Get all entries
    const std::vector<Entry>& entries() const;

    // Get entry by hash
    const Entry* find(uint32_t hash) const;

    // Get entry by name (requires name to be resolved)
    const Entry* find(std::string_view name) const;

    // Read file data by entry
    Result<std::vector<uint8_t>> read(const Entry& entry) const;

    // Resolve filenames from a list
    // Computes hash for each name and matches against index
    void resolve_names(const std::vector<std::string>& names);

private:
    MixReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Utility: compute filename hash for a given game type
uint32_t compute_hash(GameType game, std::string_view filename);

// Utility: detect game type from format and heuristics
GameType detect_game(FormatType format, const std::vector<Entry>& entries);

} // namespace mix
```

### Public Header: `mix/error.h`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mix {

enum class ErrorCode : uint32_t {
    None = 0,

    // I/O errors
    FileNotFound,
    ReadError,

    // Format errors
    InvalidFormat,
    UnsupportedFormat,
    CorruptHeader,
    CorruptIndex,

    // Crypto errors (for future use)
    DecryptionFailed,
    InvalidKey,
};

class Error {
public:
    Error(ErrorCode code, std::string message = {});

    ErrorCode code() const;
    std::string_view message() const;

    explicit operator bool() const;  // true if error

private:
    ErrorCode code_;
    std::string message_;
};

} // namespace mix
```

### Public Header: `mix/types.h`

```cpp
#pragma once

#include <cstdint>

namespace mix {

// Constants from format spec
constexpr uint32_t FLAG_CHECKSUM  = 0x00010000;
constexpr uint32_t FLAG_ENCRYPTED = 0x00020000;

constexpr size_t MIX_KEY_SIZE        = 56;   // Blowfish key
constexpr size_t MIX_KEY_SOURCE_SIZE = 80;   // RSA-encrypted key
constexpr size_t MIX_CHECKSUM_SIZE   = 20;   // SHA-1

constexpr uint32_t MIX_RG_MAGIC = 0x3158494D;  // "MIX1"
constexpr uint32_t BIG_MAGIC    = 0x46474942;  // "BIGF"
constexpr uint32_t BIG4_MAGIC   = 0x34474942;  // "BIG4"

constexpr uint32_t TS_MARKER_ID = 0x763C81DD;  // Indicates TS game

constexpr uint32_t MAX_FILE_COUNT = 4095;

} // namespace mix
```

---

## C API (FFI)

The library provides a pure C API for FFI compatibility with C, Rust, Python, and other languages.
Header: `mix/mix_c.h`

### C Example

```c
#include <mix/mix_c.h>
#include <stdio.h>

int main(void) {
    mix_reader_t* reader = NULL;
    mix_error_t err = mix_reader_open("game.mix", &reader);
    if (err != MIX_OK) {
        fprintf(stderr, "Error: %s\n", mix_error_string(err));
        return 1;
    }

    mix_info_t info;
    mix_reader_info(reader, &info);
    printf("Files: %u, Size: %llu\n", info.file_count, info.file_size);

    for (uint32_t i = 0; i < mix_reader_count(reader); i++) {
        mix_entry_t entry;
        mix_reader_entry(reader, i, &entry);
        printf("  %s (0x%08x, %u bytes)\n",
               entry.name ? entry.name : "<unknown>",
               entry.hash, entry.size);
    }

    mix_reader_free(reader);
    return 0;
}
```

### Rust FFI Example

```rust
// build.rs: link with -lmix
// Generate bindings with bindgen from mix/mix_c.h

use std::ffi::{CStr, CString};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = CString::new("game.mix")?;
    let mut reader: *mut mix_reader_t = std::ptr::null_mut();

    let err = unsafe { mix_reader_open(path.as_ptr(), &mut reader) };
    if err != MIX_OK {
        let msg = unsafe { CStr::from_ptr(mix_error_string(err)) };
        return Err(msg.to_str()?.into());
    }

    let count = unsafe { mix_reader_count(reader) };
    println!("Found {} files", count);

    unsafe { mix_reader_free(reader) };
    Ok(())
}
```

### Python ctypes Example

```python
import ctypes
from ctypes import c_char_p, c_uint32, c_uint64, c_void_p, POINTER, byref

# Load library
lib = ctypes.CDLL("libmix.dylib")  # or .so / .dll

# Define types
lib.mix_reader_open.argtypes = [c_char_p, POINTER(c_void_p)]
lib.mix_reader_open.restype = c_uint32
lib.mix_reader_count.argtypes = [c_void_p]
lib.mix_reader_count.restype = c_uint32
lib.mix_reader_free.argtypes = [c_void_p]
lib.mix_error_string.argtypes = [c_uint32]
lib.mix_error_string.restype = c_char_p

# Use library
reader = c_void_p()
err = lib.mix_reader_open(b"game.mix", byref(reader))
if err != 0:
    print(f"Error: {lib.mix_error_string(err).decode()}")
else:
    count = lib.mix_reader_count(reader)
    print(f"Found {count} files")
    lib.mix_reader_free(reader)
```

### Memory Ownership

- `mix_reader_open*()` returns ownership; caller must call `mix_reader_free()`
- `mix_entry_t.name` points to internal storage; valid until reader is freed
- `mix_reader_read()` allocates new buffer; caller must call `mix_free()`

---

## Build System

### Requirements

- CMake 3.20+
- C++23 compiler (Clang 16+, GCC 13+, MSVC 2022+)

### Supported Platforms

| Platform       | Architectures  | Status   |
|----------------|----------------|----------|
| macOS          | ARM64, x86_64  | Primary  |
| Linux          | ARM64, x86_64  | Primary  |
| Windows        | x64, ARM64     | Primary  |

### Build Options

```bash
cmake -B build [options] .

Options:
  -DMIX_BUILD_STATIC=ON|OFF    Build static library (default: ON)
  -DMIX_BUILD_SHARED=ON|OFF    Build shared library (default: OFF)
  -DMIX_BUILD_CLI=ON|OFF       Build command-line tool (default: ON)
  -DCMAKE_BUILD_TYPE=Release   Build type (Release, Debug, RelWithDebInfo)
```

### Quick Start

```bash
cd impl
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mix-tool --version
```

### Build Both Libraries

```bash
cmake -B build -DMIX_BUILD_STATIC=ON -DMIX_BUILD_SHARED=ON
cmake --build build
# Produces: libmix.a (static) and libmix.dylib/.so/.dll (shared)
```

### Install

```bash
cmake --install build --prefix /usr/local
# Installs: bin/mix-tool, lib/libmix.*, include/mix/*.h
```

### Using libmix in Your Project

**CMake (recommended):**

```cmake
find_package(mix REQUIRED)
target_link_libraries(your_app PRIVATE mix::static)  # or mix::shared
```

**Manual linking:**

```bash
# Static
g++ -I/path/to/include your_code.cpp -L/path/to/lib -lmix -o your_app

# Shared
g++ -I/path/to/include your_code.cpp -L/path/to/lib -lmix -o your_app
```

---

## Implementation Phases

### Phase 1: Basic List (TD Format) - COMPLETE

- [x] Parse TD format header and index
- [x] Table output format
- [x] Tree view formatting
- [x] JSON output
- [x] Exit codes
- [x] `--names-file` and `--name` options

### Phase 2: RA Format (Encrypted)

- [ ] RSA key derivation (bignum math per xcc.md spec)
- [ ] Blowfish decryption (ECB mode, 8-byte blocks)
- [ ] Decrypt and parse encrypted index
- [ ] Test with cd1_install_redalert.mix and cd1_main.mix

### Phase 3: Recursive Listing

- [ ] Detect nested MIX files by extension or magic
- [ ] `--recursive` flag to descend into nested archives
- [ ] Proper tree indentation for nested content

### Phase 4: Additional Formats

- [ ] TS format (unencrypted, 4-byte aligned)
- [ ] MIX-RG format (Renegade)
- [ ] BIG format (Generals)

### Phase 5: Polish

- [ ] Comprehensive error messages
- [ ] Performance optimization for large files
- [ ] Memory-mapped I/O option

---

## Testing

### Running Tests

```bash
cd test
./run_tests.sh      # Run all tests
./run_tests.sh -v   # Verbose output
```

### Test Structure

```
test/
├── run_tests.sh      # Entry point, sets up environment
├── test_mix.py       # Python test suite (unittest)
├── test.md           # Test documentation
└── fixtures/
    └── names.txt     # Known filenames for resolution tests
```

### Test Categories

| Category          | Tests | Description                    |
|-------------------|------:|--------------------------------|
| CLI Basic         |     6 | Help, version, error handling  |
| CLI File Errors   |     1 | Missing file detection         |
| CLI TD Format     |     5 | Parsing, output formats        |
| CLI RA Format     |     1 | Encrypted file detection       |
| CLI Name Resolution |   2 | --name and --names-file        |
| C API             |     5 | FFI via Python ctypes          |

### Test Data Setup

Test assets are not stored in git (large binaries). Run the setup script to download
and extract them from the official freeware release:

```bash
./scripts/setup_testdata.sh          # Download, extract, delete ISOs
./scripts/setup_testdata.sh --keep-iso  # Keep ISOs in downloads/
```

The script:
1. Downloads Red Alert CD ISOs from [Internet Archive](https://archive.org/details/command-conquer-red-alert_202309)
2. Mounts each ISO and extracts required assets to `testdata/`
3. Deletes the ISOs (unless `--keep-iso` specified)

**Requirements:** macOS (uses `hdiutil` for ISO mounting), `curl`

**Note:** Red Alert was released as freeware by EA in 2008. The ISOs are legally
available from Internet Archive.

### Test Data

All test assets extracted from Red Alert CD ISOs.

**Naming convention:** `{disc}_{path}_{filename}.ext` where path separators become underscores.

#### testdata/mix/ - MIX Archive Files

| File                       | Format | Encrypted |  Files | Size       | Notes                     |
|----------------------------|--------|-----------|-------:|------------|---------------------------|
| cd1_setup_aud.mix          | TD     | No        |     47 | 1.4 MB     | Audio files               |
| cd1_setup_setup.mix        | TD     | No        |     71 | 11.4 MB    | Setup/installer files     |
| cd1_install_redalert.mix   | RA     | Yes       |      ? | 23.9 MB    | Installer archive         |
| cd1_main.mix               | RA     | Yes       |      ? | 433.5 MB   | Main game archive (CD1)   |
| cd2_setup_aud.mix          | TD     | No        |     47 | 1.4 MB     | Audio files (identical)   |
| cd2_setup_setup.mix        | TD     | No        |     71 | 11.4 MB    | Setup files (identical)   |
| cd2_install_redalert.mix   | RA     | Yes       |      ? | 23.9 MB    | Installer (identical)     |
| cd2_main.mix               | RA     | Yes       |      ? | 477.4 MB   | Main game archive (CD2)   |

Source: `/Volumes/CD{1,2}/MAIN.MIX`, `INSTALL/REDALERT.MIX`, `SETUP/*.MIX`

#### testdata/vqa/ - Video Files

| File              | Format | Size   | Notes                          |
|-------------------|--------|--------|--------------------------------|
| cd1_SIZZLE.VQA    | VQA    | 20 MB  | Promo/intro video              |
| cd1_SIZZLE.VQP    | VQP    | 1.6 MB | Palette/lookup for SIZZLE.VQA  |
| cd1_SIZZLE2.VQA   | VQA    | 14 MB  | Secondary promo video          |
| cd1_SIZZLE2.VQP   | VQP    | 225 KB | Palette/lookup for SIZZLE2.VQA |
| cd2_SIZZLE*.VQ*   | -      | -      | Identical copies on CD2        |

Source: `/Volumes/CD{1,2}/*.VQA`, `*.VQP`

**VQA Format:** Westwood's Vector Quantized Animation format. IFF-style container
with "FORM....WVQAVQHD" header. VQP files contain palette/codebook data.

#### testdata/ard/ - Menu Graphics and Audio (183 files)

| Extension | Format | Count | Description                        |
|-----------|--------|------:|------------------------------------|
| .DAH      | BMP    |    81 | Menu graphics - High resolution    |
| .DAL      | BMP    |    81 | Menu graphics - Low resolution     |
| .DAW      | WAV    |    20 | Menu audio (22kHz 16-bit PCM)      |
| .TTF      | TTF    |     1 | TrueType font (CD2 only)           |

Source: `/Volumes/CD{1,2}/ard/MENU*.DA{H,L,W}`, `MENU1.TTF`

**Note:** DAH/DAL files are standard BMP images (8-bit indexed, 640x480).
H=High, L=Low quality variants of same graphics. DAW are standard WAV files.

#### testdata/ico/ - Windows Icons (3 files)

| File                     | Size   | Description          |
|--------------------------|--------|----------------------|
| cd1_install_RAEDIT.ICO   | 1 KB   | Map editor icon      |
| cd1_install_RANOTES.ICO  | 1 KB   | Release notes icon   |
| cd1_internet_README.ICO  | 1 KB   | Readme icon          |

Source: `/Volumes/CD1/INSTALL/*.ICO`, `INTERNET/*.ICO`

#### Other Files on ISO (Not Extracted)

| Path           | Contents                                      |
|----------------|-----------------------------------------------|
| DXSETUP/       | DirectX 5 installer (~500 DLLs, drivers)      |
| eCat/          | EA electronic catalog (21 MB exe)             |
| eDocs/         | PDF manuals, Acrobat Reader installer         |
| MPLAYER/       | MPlayer online service installer              |
| TEN/           | TEN online service installer                  |
| INSTALL/       | Game executables (RA95.EXE, etc.)             |

**RESLIB.DLL:** 14 KB code-only library for resource loading functions. No
embedded assets - too small, contains only WATCOM runtime and API stubs.

Test cases:

1. **TD format parsing** - cd1_setup_aud.mix, cd1_setup_setup.mix
2. **RA format parsing** - cd1_install_redalert.mix, cd1_main.mix (requires decryption)
3. **Name resolution** - provide known filenames, verify matches
4. **Error handling** - truncated file, invalid header, missing file
5. **Output formats** - table, tree, JSON correctness
6. **Recursive listing** - nested MIX within MIX

---

## References

- [xcc.md](xcc.md) - Complete format specification
- [reference/xcc/](reference/xcc/) - XCC Utilities source code
