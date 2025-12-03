# Westwood Studios Format Tools

A C++ library and CLI toolset for reading and exporting Westwood Studios
game formats from the Command & Conquer series (1995-2003).

## About This Project

This project is an experiment using [Claude Code](https://claude.com/claude-code)
to explore AI-assisted software development. The goal: how fast and maintainable
can a project be when the developer has no prior knowledge of the domain?

Before starting, I knew nothing about Westwood's file formats (MIX archives,
VQA video, IMA ADPCM audio, LCW compression, etc.). All format knowledge was
researched and implemented through Claude Code, using publicly available
documentation and reverse-engineering resources.

**What worked well:**
- Rapid prototyping of file format parsers
- Consistent code style across 11 CLI tools
- Comprehensive test coverage (439 tests)
- Unix-philosophy CLI design patterns
- Iterative refinement based on test failures

**Lessons learned:**
- Complex algorithms (LCW compression, ADPCM decoding) required careful
  verification against reference implementations
- Format edge cases often needed multiple research/implement/test cycles
- Domain expertise accelerates debugging but isn't strictly required

## Prerequisites

**Required:**
- CMake 3.20+
- C++23 compiler:
  - Clang 16+ (recommended)
  - GCC 13+
  - MSVC 2022+
- Python 3.10+ (for tests)

**Optional:**
- ffmpeg (for VQA to MP4 export)

### macOS

```bash
brew install cmake python3
# ffmpeg optional: brew install ffmpeg
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install cmake g++-13 python3 python3-pip
# ffmpeg optional: sudo apt install ffmpeg
```

### Windows

Install Visual Studio 2022 with C++ workload, CMake, and Python from python.org.

## Building

```bash
cd impl
cmake -B build
cmake --build build

# Or with specific build type
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Build options:**
```bash
cmake -B build \
  -DWWD_BUILD_STATIC=ON \   # Build static library (default)
  -DWWD_BUILD_SHARED=OFF \  # Build shared library
  -DWWD_BUILD_CLI=ON \      # Build CLI tools (default)
  -DWWD_BUILD_TESTS=OFF     # Build C++ tests
```

**Outputs:**
- `build/libwestwood.a` - Static library
- `build/*-tool` - CLI executables (11 tools)

## Running

```bash
cd impl/build

# Show help
./mix-tool --help
./vqa-tool --help

# List contents of a MIX archive
./mix-tool list ../../testdata/mix/cd1_setup_aud.mix

# Get video info
./vqa-tool info ../../testdata/vqa/intro.vqa

# Export audio to WAV
./aud-tool export sound.aud -o sound.wav

# Export video to MP4 (requires ffmpeg)
./vqa-tool export movie.vqa -o movie.mp4
```

## Testing

### Setup Test Data

Test assets are extracted from Red Alert CD ISOs (freeware release by EA).
Not stored in git due to size.

```bash
./scripts/setup_testdata.sh
```

Source: [Internet Archive](https://archive.org/details/command-conquer-red-alert_202309)

### Run Tests

```bash
cd test
MIX_TOOL=../impl/build/mix-tool python3 -m pytest

# Verbose output
MIX_TOOL=../impl/build/mix-tool python3 -m pytest -v

# Run specific test file
MIX_TOOL=../impl/build/mix-tool python3 -m pytest test_cli/test_aud_tool.py
```

**Test coverage:** 439 tests across CLI, library, output formats, and integration.

## Using the Library

### Linking

**CMake:**
```cmake
add_subdirectory(path/to/impl)
target_link_libraries(myapp PRIVATE westwood::static)
```

**Manual:**
```bash
g++ -std=c++23 myapp.cpp -I impl/libwestwood/include -L impl/build -lwestwood
```

### API Example

```cpp
#include <westwood/vqa.h>
#include <westwood/aud.h>
#include <westwood/mix.h>
#include <iostream>

int main() {
    // Open a VQA video file
    auto result = wwd::VqaReader::open("intro.vqa");
    if (!result) {
        std::cerr << "Error: " << result.error().message() << "\n";
        return 1;
    }

    auto& reader = *result.value();
    const auto& info = reader.info();

    std::cout << "Video: " << info.header.width << "x"
              << info.header.height << "\n";
    std::cout << "Frames: " << info.header.frame_count << "\n";
    std::cout << "Duration: " << reader.duration() << "s\n";

    // Decode all video frames to RGB
    auto frames = reader.decode_video();
    if (frames) {
        for (const auto& frame : *frames) {
            // frame.rgb is std::vector<uint8_t> with RGB data
            // frame.width, frame.height are dimensions
        }
    }

    // Decode audio to PCM samples
    auto audio = reader.decode_audio();
    if (audio) {
        // *audio is std::vector<int16_t> with PCM samples
    }

    return 0;
}
```

### Reader Classes

| Class | Header | Description |
|-------|--------|-------------|
| `wwd::MixReader` | mix.h | MIX archive parsing and extraction |
| `wwd::VqaReader` | vqa.h | VQA video decoding |
| `wwd::AudReader` | aud.h | AUD audio decoding |
| `wwd::ShpReader` | shp.h | SHP sprite decoding |
| `wwd::WsaReader` | wsa.h | WSA animation decoding |
| `wwd::TmpReader` | tmp.h | TMP terrain tile decoding |
| `wwd::FntReader` | fnt.h | FNT font decoding |
| `wwd::CpsReader` | cps.h | CPS image decoding |
| `wwd::PalReader` | pal.h | PAL palette parsing |

### Error Handling

All reader methods return `wwd::Result<T>` (alias for `std::expected<T, wwd::Error>`):

```cpp
auto result = wwd::ShpReader::open("sprite.shp");

// Check for error
if (!result) {
    wwd::Error err = result.error();
    std::cerr << err.message() << "\n";  // Human-readable message
    std::cerr << "Code: " << static_cast<int>(err.code()) << "\n";
    return 1;
}

// Access value
auto& reader = *result.value();
```

**Error codes:**
- `wwd::ErrorCode::InvalidFormat` - File format not recognized
- `wwd::ErrorCode::IoError` - File read/write failure
- `wwd::ErrorCode::OutOfBounds` - Index out of range
- `wwd::ErrorCode::DecompressionFailed` - LCW/LZW decompression error

## Supported Formats

| Format | Tool | Description | Export |
|--------|------|-------------|--------|
| MIX | mix-tool | Archive containers (TD, RA, TS, RA2, RG, BIG) | extract |
| VQA | vqa-tool | Full-motion video (v1, v2, v3, HiColor) | MP4, PNG+WAV |
| AUD | aud-tool | Compressed audio (IMA/Westwood ADPCM) | WAV |
| SHP | shp-tool | Sprites (TD/RA and TS/RA2 variants) | PNG |
| PAL | pal-tool | Color palettes (6-bit and 8-bit) | PNG swatch |
| WSA | wsa-tool | Sprite animations | GIF, PNG |
| TMP | tmp-tool | Terrain tiles (orthographic + isometric) | PNG |
| FNT | fnt-tool | Bitmap fonts (v2-v4, BitFont, Unicode) | PNG |
| CPS | cps-tool | Compressed pictures (320x200) | PNG |
| LCW | lcw-tool | Compression algorithm (Format80/40) | raw |
| Blowfish | blowfish-tool | Encryption/key derivation | raw |

## CLI Reference

All tools follow Unix conventions:

```bash
# Common options
<tool> --help          # Show usage
<tool> --version       # Show version
<tool> -v, --verbose   # Verbose output
<tool> -q, --quiet     # Suppress non-essential output
<tool> -o <path>       # Output path
<tool> -f, --force     # Overwrite existing files

# Stdin/stdout support (use - for path)
cat file.aud | aud-tool export - -o - > output.wav
mix-tool list - < archive.mix

# Info commands output JSON
vqa-tool info --json movie.vqa | jq '.frames'
```

**Exit codes:** 0=success, 1=invalid args, 2=format error, 3=io error

### Examples

```bash
# Extract all files from a MIX archive
mix-tool extract redalert.mix -o extracted/

# Convert VQA video to MP4 (requires ffmpeg)
vqa-tool export intro.vqa -o intro.mp4

# Export sprite sheet with palette
shp-tool export units.shp -p temperat.pal -o units.png

# Export animation as GIF
wsa-tool export menu.wsa -p redalert.pal -o menu.gif

# Export individual frames
shp-tool export tank.shp -p pal.pal --frames -o frames/tank
```

## Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| macOS (arm64) | Tested | Primary development |
| macOS (x86_64) | Should work | Not regularly tested |
| Linux (x86_64) | Should work | Requires C++23 compiler |
| Windows | Should work | MSVC 2022+ or Clang |

## Project Organization

```
.
├── README.md                 # This file
├── LICENSE                   # Apache 2.0
├── impl/                     # C++ implementation
│   ├── CMakeLists.txt        # Build configuration
│   ├── libwestwood/          # Core library
│   │   ├── include/westwood/ # Public headers
│   │   └── src/              # Implementation
│   ├── *-tool/               # CLI tools (11 directories)
│   │   └── main.cpp          # Tool entry point
│   └── build/                # Build output (generated)
├── test/                     # Python test suite
│   ├── conftest.py           # Pytest fixtures and helpers
│   ├── test_cli/             # CLI tool tests
│   ├── test_library/         # Library algorithm tests
│   ├── test_output/          # Export format validation
│   ├── test_integration/     # End-to-end tests
│   └── testdata/             # Test-specific fixtures
├── testdata/                 # Extracted test assets (generated)
├── scripts/                  # Setup and utility scripts
└── data/                     # Static data (hash tables, etc.)
```

### Documentation Files

| File | Description |
|------|-------------|
| [tool-specs.md](tool-specs.md) | CLI interface specification (flags, exit codes, I/O) |
| [westwood-formats.md](westwood-formats.md) | Media format documentation (VQA, AUD, SHP, WSA, etc.) |
| [xcc.md](xcc.md) | Archive format details (MIX variants, BIG, Blowfish) |
| [test/test.md](test/test.md) | Test suite organization and coverage |

### Build Outputs

After building, `impl/build/` contains:
- `libwestwood.a` - Static library
- `*-tool` - 11 CLI executables

### Test Organization

| Directory | Purpose |
|-----------|---------|
| `test_cli/` | Tests each CLI tool's flags, inputs, outputs |
| `test_library/` | Tests core algorithms (LCW, Format40, ADPCM) |
| `test_output/` | Validates export formats (PNG, WAV, GIF, MP4) |
| `test_integration/` | End-to-end workflows (extract, convert, verify) |

## TODO

### Infrastructure
- [ ] CI/CD pipeline (GitHub Actions)
- [ ] Man page generation
- [ ] Distribution packages (Homebrew, deb, rpm)
- [ ] Valgrind/ASan memory testing

### Feature Expansion
- [ ] Write support (create MIX archives)
- [ ] Encoding support (PNG to SHP, WAV to AUD)
- [ ] GUI asset browser
- [ ] OpenRA integration

## License

Apache License 2.0
