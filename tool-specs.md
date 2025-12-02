# Tool Specifications

Command-line interface and export format specifications for Westwood media tools.

See also: [westwood-formats.md](westwood-formats.md) (input format encoding details)

---

## 0. Architecture

### Library vs CLI Separation

**libwestwood** (decoding library):
- Decodes Westwood formats to raw data (RGB pixels, PCM samples)
- No external dependencies for encoding
- Suitable for linking into games, editors, and other tools

**CLI tools** (this document):
- Use libwestwood for decoding
- Handle encoding to output formats (WAV, PNG, GIF, MP4)
- May have external dependencies (e.g., ffmpeg for MP4)

| Component | Decoding | Encoding | Dependencies |
|-----------|----------|----------|--------------|
| libwestwood | All formats | None | None |
| CLI tools | Via library | WAV, PNG, GIF, MP4 | ffmpeg (MP4 only) |

---

## 1. Common Conventions

### 1.1 Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error (invalid arguments, file not found) |
| 2 | Format error (corrupted/unsupported input) |

### 1.2 Output Behavior

- **Errors to stderr**, results to stdout
- **No color output** - plain text only
- **No interactive prompts** - all input via arguments
- **Quiet by default** - only output what's necessary
- **--verbose flag** - detailed output when debugging
- **No overwrite by default** - error if output file exists, use `--force` to overwrite
- **Error message format:** `<tool>: error: <message>` (e.g., `aud-tool: error: file not found: sound.aud`)

### 1.3 Unix Composability

Tools support stdin/stdout via the `-` convention (standard Unix idiom):

- **`-` as input filename:** Read from stdin
- **`-o -` for output:** Write to stdout

**Single-output tools** (WAV, single PNG, GIF, MP4):
```bash
# File to file
aud-tool export sound.aud -o sound.wav

# Stdin to file
cat sound.aud | aud-tool export - -o sound.wav

# File to stdout
aud-tool export sound.aud -o - > sound.wav

# Full pipeline
cat sound.aud | aud-tool export - -o - > sound.wav
```

**Multi-output tools** (`--frames` mode): Cannot use stdout; require `-o prefix`:
```bash
shp-tool export sprite.shp -p pal.pal --frames -o out/sprite  # → out/sprite_000.png, ...
```

**Info commands:** Always output to stdout (text or JSON):
```bash
vqa-tool info --json movie.vqa | jq '.frames'
```

**Composability matrix:**

| Tool | Stdin | Stdout | Notes |
|------|:-----:|:------:|-------|
| aud-tool export | ✅ | ✅ | WAV output |
| shp-tool export --sheet | ✅ | ✅ | Single PNG |
| shp-tool export --frames | ✅ | ❌ | Multi-file |
| pal-tool export | ✅ | ✅ | PNG output |
| wsa-tool export (GIF) | ✅ | ✅ | GIF output |
| wsa-tool export --frames | ✅ | ❌ | Multi-file |
| cps-tool export | ✅ | ✅ | PNG output |
| fnt-tool export | ✅ | ❌ | PNG + JSON (two files) |
| tmp-tool export | ✅ | ✅ | PNG output |
| vqa-tool export (MP4) | ✅ | ✅ | MP4 output |
| vqa-tool export --frames | ✅ | ❌ | Multi-file |
| All info commands | ✅ | ✅ | Text/JSON |

**Example pipelines:**
```bash
# Extract from MIX and convert
mix-tool extract archive.mix SOUND.AUD - | aud-tool export - -o sound.wav

# Stream VQA to player
vqa-tool export movie.vqa -o - | ffplay -i -

# JSON metadata processing
vqa-tool info --json movie.vqa | jq '{frames, duration}'
```

### 1.4 Common Options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show usage |
| `--version`, `-V` | Show version |
| `--verbose`, `-v` | Verbose output |
| `--output`, `-o` | Output path (default: derive from input; `-` for stdout) |
| `--force`, `-f` | Overwrite existing output files |
| `--json` | Output info in JSON format (for `info` commands) |

### 1.5 Info Command Output

All tools support an `info` command with two output modes:

- **Default:** Human-readable key-value format
- **JSON:** Machine-readable with `--json` flag

Info output includes all fields an audio/visual engineer needs to implement
a player or converter. See per-tool sections for specific fields.

---

## 2. Tool Specifications

### 2.1 mix-tool

**Status:** Implemented

```
mix-tool list [-r] <file.mix>
mix-tool extract <file.mix> <entry> [-o output]
mix-tool extract-all <file.mix> [-o output_dir]
mix-tool info <file.mix>
```

See implementation for details.

---

### 2.2 aud-tool

**Commands:**
```
aud-tool info [--json] <file.aud>
aud-tool export <file.aud> [-o output.wav]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-o, --output <path>` | Output path (default: input name + .wav) |

**Input:** AUD files (Westwood ADPCM codec 0x01, IMA ADPCM codec 0x63)

**Output:** WAV format (see Section 3.1)

**Info output fields:**
```
Format: Westwood AUD
Codec: IMA ADPCM (0x63) | Westwood ADPCM (0x01)
Sample rate: <N> Hz
Channels: <N> (mono|stereo)
Output bit depth: 16-bit signed
Samples: <N>
Duration: <N.N> seconds
Compressed size: <N> bytes
Uncompressed size: <N> bytes
Compression ratio: <N.N>:1
```

---

### 2.3 shp-tool

**Commands:**
```
shp-tool info [--json] <file.shp>
shp-tool export <file.shp> -p <palette> [--frames|--sheet] [-o output]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-p, --palette <file>` | PAL file for color lookup (required for export) |
| `--frames` | Output one PNG per frame (default) |
| `--sheet` | Output single sprite sheet PNG |
| `-o, --output <path>` | Output path (default: input name + .png) |

**Input:** SHP files (TD/RA format), PAL file required for export

**Output:** PNG format (see Section 3.2)

**Info output fields:**
```
Format: TD/RA SHP
Frames: <N>
Dimensions: <W>x<H>
Delta buffer: <N> bytes
Compression: LCW + XOR delta
Frame breakdown:
  LCW base frames: <N>
  XOR delta frames: <N>
```

---

### 2.4 pal-tool

**Commands:**
```
pal-tool info [--json] <file.pal>
pal-tool export <file.pal> [-o output.png]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-o, --output <path>` | Output path (default: input name + .png) |

**Input:** PAL files (768 bytes, 256 RGB triplets, 6-bit per channel)

**Output:** PNG swatch image (see Section 3.3)

**Info output fields:**
```
Format: Westwood PAL
Colors: 256
Bit depth: 6-bit per channel (18-bit color)
File size: 768 bytes
```

---

### 2.5 wsa-tool

**Commands:**
```
wsa-tool info [--json] <file.wsa>
wsa-tool export <file.wsa> [-p palette] [--fps N] [--loop|--no-loop] [--transparent] [-o output.gif]
wsa-tool export <file.wsa> [-p palette] --frames [-o output_prefix]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-p, --palette <file>` | External PAL file (if no embedded palette) |
| `--frames` | Export as PNG sequence instead of GIF |
| `--fps <N>` | Frame rate for GIF (default: 15) |
| `--loop` | Loop forever in GIF (default) |
| `--no-loop` | Play once in GIF |
| `--transparent` | Treat index 0 as transparent |
| `-o, --output <path>` | Output path (default: input name + .gif or _NNN.png) |

**Input:** WSA files (Format40 + LCW delta animation)

**Output:**
- Default: Animated GIF (see Section 3.4)
- With `--frames`: PNG sequence (see Section 3.2)

**Palette handling:** Use embedded palette if present, otherwise require `-p` option.
External palette (`-p`) overrides embedded when both are available.

**Info output fields:**
```
Format: Westwood WSA
Frames: <N>
Dimensions: <W>x<H>
Delta buffer: <N> bytes
Has embedded palette: yes|no
Has loop frame: yes|no
Compression: Format40 + LCW
```

---

### 2.6 tmp-tool

**Commands:**
```
tmp-tool info [--json] <file.tmp>
tmp-tool export <file.tmp> -p <palette> [-o output.png]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-p, --palette <file>` | PAL file for color lookup (required for export) |
| `-o, --output <path>` | Output path (default: input name + .png) |

**Input:** TMP files (TD/RA orthographic tiles, 24×24)

**Output:** PNG tileset image (see Section 3.2)

**Tileset layout:** Tiles arranged in a grid. Empty tiles (index 0xFF) rendered as
transparent. Grid dimensions based on tile count (e.g., 4×4 for 16 tiles).

**Info output fields:**
```
Format: TD/RA TMP
Tiles: <N> total (<M> empty)
Tile dimensions: <W>x<H>
Image data offset: <N>
Index table offset: <N>
```

---

### 2.7 fnt-tool

**Commands:**
```
fnt-tool info [--json] <file.fnt>
fnt-tool export <file.fnt> [-o output.png] [-m metrics.json]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-o, --output <path>` | PNG atlas path (default: input name + .png) |
| `-m, --metrics <path>` | JSON metrics path (default: input name + .json) |

**Input:** FNT files (4-bit bitmap fonts)

**Output:** PNG glyph atlas + JSON metrics (see Section 3.6)

**Palette handling:** No palette required. FNT 4-bit values (0-15) are converted to
grayscale intensity: `intensity = value * 17` (0→0, 15→255). Output is white glyphs
on transparent background, suitable for GPU shader tinting at runtime.

**Info output fields:**
```
Format: Westwood FNT v<N>
Glyphs: <N>
Character range: <first>-<last>
Max dimensions: <W>x<H>
Bit depth: 4-bit (16 colors)
```

---

### 2.8 cps-tool

**Commands:**
```
cps-tool info [--json] <file.cps>
cps-tool export <file.cps> [-p palette] [-o output.png]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `-p, --palette <file>` | External PAL file (if no embedded palette) |
| `-o, --output <path>` | Output path (default: input name + .png) |

**Input:** CPS files (320×200 LCW compressed images)

**Output:** PNG image (see Section 3.2)

**Palette handling:** Use embedded palette if present, otherwise require `-p` option.
External palette (`-p`) overrides embedded when both are available.

**Info output fields:**
```
Format: Westwood CPS
Dimensions: 320x200
Compression: LCW | LZW-12 | LZW-14 | RLE | none
Has embedded palette: yes|no
Compressed size: <N> bytes
Uncompressed size: 64000 bytes
```

---

### 2.9 vqa-tool

**Commands:**
```
vqa-tool info [--json] <file.vqa>
vqa-tool export <file.vqa> [--quality N] [-o output.mp4]
vqa-tool export <file.vqa> --frames [-o output_prefix]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--json` | Output info as JSON |
| `--quality <N>` | H.264 CRF value, 0-51 (default: 18, lower = better) |
| `--frames` | Export as PNG sequence + WAV instead of MP4 |
| `-o, --output <path>` | Output path (default: input name + .mp4; prefix for --frames) |

**Input:** VQA files (vector quantized video + IMA ADPCM audio)

**Supported versions:**
| Version | Games | Color | Block | Audio |
|---------|-------|-------|-------|-------|
| v1 | Kyrandia, Lands of Lore | 8-bit indexed | 4×2 | SND1 (Westwood ADPCM) |
| v2 | C&C, Red Alert | 8-bit indexed | 4×2 | SND2 (IMA ADPCM) |
| v3 | Tiberian Sun, RA2 | 15-bit RGB555 | 4×4 | SND2 (IMA ADPCM) |

**V3 HiColor conversion:** RGB555 (15-bit) to RGB888 (24-bit):
```
R = ((pixel >> 10) & 0x1F) << 3  // bits 14-10 → 0-248
G = ((pixel >> 5) & 0x1F) << 3   // bits 9-5 → 0-248
B = (pixel & 0x1F) << 3          // bits 4-0 → 0-248
```

**Output:**
- Default: MP4 video (see Section 3.5)
- With `--frames`: PNG sequence + WAV (see Sections 3.1, 3.2)

**VQA without audio:** If VQA has no audio track (some cutscenes are video-only),
WAV is not generated in `--frames` mode, and MP4 is created without audio.

**Dependencies:** MP4 export requires ffmpeg in PATH. If ffmpeg is not found, MP4
export fails with an error; use `--frames` for PNG+WAV output (no external dependency).
The underlying library (libwestwood) has no ffmpeg dependency - it decodes to raw
frames and audio samples. Encoding is handled by the CLI tool only.

**Info output fields:**
```
Format: Westwood VQA v<N>
Video:
  Dimensions: <W>x<H>
  Block size: <W>x<H>
  Frame rate: <N> fps
  Frames: <N>
  Duration: <N.N> seconds
  Codebook: <N> parts, max <N> blocks
  Colors: 256 (indexed) | 32768 (hicolor)
Audio:
  Present: yes|no
  Codec: PCM (SND0) | Westwood ADPCM (SND1) | IMA ADPCM (SND2)
  Sample rate: <N> Hz
  Channels: <N> (mono|stereo)
  Bit depth: <N>-bit
```

---

## 3. Output Format Specifications

### 3.1 WAV Output

Used by: `aud-tool export`, `vqa-tool export`

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Format | RIFF WAVE | Universal compatibility |
| Sample rate | Preserve original | No resampling artifacts |
| Bit depth | 16-bit signed PCM | Standard format, lossless upconversion from 8-bit |
| Channels | Preserve original | Mono stays mono, stereo stays stereo |
| Byte order | Little-endian | WAV standard |

**Notes:**
- Westwood ADPCM (codec 0x01) decodes to 8-bit unsigned, which is upconverted
  to 16-bit signed via `(sample - 128) << 8`. This is mathematically lossless.
- Stereo samples are interleaved LRLRLR format (left sample, right sample, ...).
  Each sample is 2 bytes (16-bit), so stereo frame = 4 bytes.

### 3.2 PNG Output

Used by: `shp-tool export`, `cps-tool export`, `tmp-tool export`, `fnt-tool export`

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Color mode | RGBA (truecolor + alpha) | Universal compatibility, proper transparency |
| Bit depth | 8 bits per channel (32-bit) | Standard, no precision loss |
| Transparency | Index 0 → alpha 0, others → alpha 255 | Clean alpha channel |
| Color conversion | `(val << 2) \| (val >> 4)` | Accurate 6-bit to 8-bit scaling |

**Multi-frame output (shp-tool, wsa-tool frames, vqa-tool frames):**

| Option | Output | Description |
|--------|--------|-------------|
| `--frames` | `name_000.png`, `name_001.png`, ... | One PNG per frame (default for shp-tool) |
| `--sheet` | `name.png` | Single sprite sheet, horizontal layout |

**Frame numbering:** Zero-padded, minimum 3 digits, expanded as needed for larger
frame counts. Formula: `digits = max(3, ceil(log10(frame_count + 1)))`.

| Frame count | Format | Example |
|-------------|--------|---------|
| 1-999 | `_NNN` | `name_000.png` to `name_999.png` |
| 1000-9999 | `_NNNN` | `name_0000.png` to `name_9999.png` |

**Output path with `--frames`:** The `-o` option specifies a prefix (not a directory):
```
tool export file.shp --frames              # → file_000.png, file_001.png, ...
tool export file.shp --frames -o bar       # → bar_000.png, bar_001.png, ...
tool export file.shp --frames -o out/bar   # → out/bar_000.png, out/bar_001.png, ...
```

For vqa-tool, the prefix applies to both PNG frames and WAV audio:
```
vqa-tool export movie.vqa --frames -o out/movie   # → out/movie_000.png, ..., out/movie.wav
```

Sprite sheet layout: frames arranged left-to-right, all frames same dimensions
(padded to largest frame size if variable).

### 3.3 Palette Swatch PNG (pal-tool)

| Parameter | Value |
|-----------|-------|
| Dimensions | 512×512 px |
| Layout | 16×16 grid (32px per swatch) |
| Color mode | RGB (no alpha needed) |
| Bit depth | 8 bits per channel (24-bit) |

**Layout:** Row-major order, index 0 at top-left, index 255 at bottom-right.
- Row 0: indices 0-15
- Row 1: indices 16-31
- ...
- Row 15: indices 240-255

Each swatch is a 32×32 px solid color square.

### 3.4 GIF Output (wsa-tool)

| Parameter | Value |
|-----------|-------|
| Frame rate | User-specified, default 15 FPS |
| Frame delay | `round(100 / fps)` centiseconds (GIF delay units = 1/100 sec) |
| Looping | User choice: `--loop` (default) or `--no-loop` |
| Palette | 256 colors from WSA or external PAL |
| Transparency | Off by default, `--transparent` for index 0 |
| Color conversion | 6-bit to 8-bit: `(val << 2) \| (val >> 4)` |

### 3.5 MP4 Output (vqa-tool)

| Parameter | Value |
|-----------|-------|
| Container | MP4 |
| Video codec | H.264 |
| Video quality | CRF 18 default, `--quality` to override (0-51, lower = better) |
| Video dimensions | Preserve original (e.g., 320×200) |
| Frame rate | Preserve original (from VQHD header, typically 15 FPS) |
| Pixel format | yuv420p (H.264 standard) |
| Audio codec | AAC |
| Audio bitrate | 192 kbps |
| Audio sample rate | Preserve original (typically 22050 Hz) |
| Audio channels | Preserve original (mono or stereo) |

**Frame export mode (`--frames`):**
- Video frames: PNG sequence per Section 3.2 (`name_000.png`, `name_001.png`, ...)
- Audio track: WAV per Section 3.1 (`name.wav`)

### 3.6 JSON Font Metrics (fnt-tool)

**Schema:**
```json
{
  "source": "6POINT.FNT",
  "atlasWidth": 128,
  "atlasHeight": 64,
  "maxHeight": 6,
  "maxWidth": 5,
  "glyphs": {
    "32": { "x": 0, "y": 0, "width": 3, "height": 6, "yOffset": 0 },
    "33": { "x": 3, "y": 0, "width": 2, "height": 6, "yOffset": 0 },
    "...": "..."
  }
}
```

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `source` | string | Original FNT filename |
| `atlasWidth` | int | PNG atlas width in pixels |
| `atlasHeight` | int | PNG atlas height in pixels |
| `maxHeight` | int | Maximum glyph height |
| `maxWidth` | int | Maximum glyph width |
| `glyphs` | object | Map of character index (string) to glyph data |

**Glyph fields:**
| Field | Type | Description |
|-------|------|-------------|
| `x` | int | X position in atlas |
| `y` | int | Y position in atlas |
| `width` | int | Glyph width in pixels |
| `height` | int | Glyph height in pixels |
| `yOffset` | int | Vertical offset from baseline |

**PNG atlas:**
- RGBA format per Section 3.2
- Glyphs packed row by row (minimal wasted space)
- Background: transparent (alpha 0)
- Glyph pixels: palette colors (typically grayscale)
