# Westwood Studios Media Formats Specification

Command & Conquer Series (1995-2003)

See also: [README.md](README.md) (project overview), [xcc.md](xcc.md) (archive formats)

```
Reference Implementations:
  - XCC Utilities by Olaf van der Spek (GPL v3)
    https://github.com/OlafvdSpek/xcc
  - FFmpeg libavformat/wsvqa.c, libavcodec/vqavideo.c
  - OpenRA (GPL v3)
    https://github.com/OpenRA/OpenRA

Document derived from source code analysis and community documentation.
```

## Table of Contents

1. [Overview](#1-overview)
2. [VQA - Vector Quantized Animation](#2-vqa---vector-quantized-animation)
3. [AUD - Westwood Audio](#3-aud---westwood-audio)
4. [SHP - Shape/Sprite Format](#4-shp---shapesprite-format)
5. [PAL - Palette Format](#5-pal---palette-format)
6. [WSA - Westwood Animation](#6-wsa---westwood-animation)
7. [TMP - Tile Template](#7-tmp---tile-template)
8. [FNT - Font Format](#8-fnt---font-format)
9. [CPS - Compressed Picture](#9-cps---compressed-picture)
10. [Standard Formats](#10-standard-formats)
11. [Compression Algorithms](#11-compression-algorithms)

---

## 1. Overview

### 1.1 Format Family

Westwood Studios developed a suite of proprietary formats for the Command & Conquer
series. These formats share common design patterns:

- **Chunked structure:** Many formats use IFF-style tagged chunks
- **Palette-indexed graphics:** 256-color with 6-bit RGB (0-63 per channel)
- **Custom compression:** LCW (Format80) and Format40 delta encoding
- **Little-endian:** All multi-byte values are little-endian

### 1.2 Format Categories

| Category | Formats | Description |
|----------|---------|-------------|
| Video | VQA, VQP | Full-motion video with audio |
| Audio | AUD | Compressed audio streams |
| Graphics | SHP, PAL, CPS, PCX | Sprites, palettes, images |
| Animation | WSA | Sprite-based animations |
| Terrain | TMP | Isometric tile graphics |
| UI | FNT | Bitmap fonts |

### 1.3 Game Compatibility

| Format | TD | RA | TS | RA2 | Notes |
|--------|:--:|:--:|:--:|:---:|-------|
| VQA | v1 | v2 | v3 | v3 | Version differences |
| AUD | ✓ | ✓ | ✓ | ✓ | Compression varies |
| SHP | TD | RA | TS | TS | Different headers |
| PAL | ✓ | ✓ | ✓ | ✓ | Consistent format |
| WSA | ✓ | ✓ | - | - | TD/RA only |
| TMP | ✓ | ✓ | ✓ | ✓ | TD/RA ortho, TS/RA2 iso |
| FNT | ✓ | ✓ | ✓ | ✓ | Minor variations |
| CPS | ✓ | ✓ | - | - | 320×200 static images |

---

## 2. VQA - Vector Quantized Animation

Westwood's full-motion video format using vector quantization compression.

### 2.1 Overview

VQA files use an IFF-style container with FORM chunk wrapper. Video frames are
compressed using vector quantization with a codebook of 4x2 or 4x4 pixel blocks.
Audio is interleaved as IMA ADPCM or raw PCM.

**File signatures:**
- VQA: `FORM....WVQA` (IFF container, type WVQA)
- VQP: Raw codebook data (no header)

### 2.2 IFF Container Structure

```
Offset  Size  Description
------  ----  -----------
0       4     "FORM" chunk ID
4       4     Chunk size (big-endian, IFF convention)
8       4     Form type: "WVQA"
12      ...   Child chunks
```

**Endianness convention:**
- IFF chunk IDs: 4 ASCII bytes (no endianness)
- IFF chunk sizes: **Big-endian** (uint32be)
- All other Westwood data: **Little-endian** (header fields, offsets, etc.)

**Chunk alignment:** Per IFF standard, chunks are aligned to even byte boundaries.
If a chunk has odd length, a null pad byte (0x00) follows. When parsing, check
if next byte is 0x00 and skip it before reading next chunk ID.

### 2.3 VQHD - Header Chunk (42 bytes)

```
Offset  Size  Type      Field        Description
------  ----  ----      -----        -----------
0       4     char[4]   ChunkID      "VQHD"
4       4     uint32be  ChunkSize    42 (big-endian, IFF convention)
8       2     uint16    Version      1, 2, or 3
10      2     uint16    Flags        Feature flags (see below)
12      2     uint16    NumFrames    Total frame count
14      2     uint16    Width        Movie width in pixels
16      2     uint16    Height       Movie height in pixels
18      1     uint8     BlockW       Block width (typically 4)
19      1     uint8     BlockH       Block height (2 for v1/v2, 4 for v3)
20      1     uint8     FrameRate    Frames per second (typically 15)
21      1     uint8     CBParts      Codebook partition count (typically 8)
22      2     uint16    Colors       Palette entries (256, or 0 for HiColor)
24      2     uint16    MaxBlocks    Maximum codebook entries
26      2     uint16    OffsetX      Overlay X position
28      2     uint16    OffsetY      Overlay Y position
30      2     uint16    MaxVPTSize   Largest VPTZ/VPRZ chunk size
32      2     uint16    Freq         Audio sample rate (22050 typical)
34      1     uint8     Channels     Audio channels (1=mono, 2=stereo)
35      1     uint8     Bits         Audio bit depth (8 or 16)
36      4     uint32    Unknown1     Reserved (always 0)
40      2     uint16    Unknown2     Version indicator (0 or 4)
```

**Flags:**
```
Bit 0: Has audio
Bit 2: Has overlay
```

**Test vector (cd1_SIZZLE.VQA):**
```
Version=2, Flags=0x0001, Frames=1624, 320x200
Block=4x2, FPS=15, CBParts=8, Colors=256, MaxBlocks=2000
Audio: 22050Hz, mono, 16-bit
```

### 2.4 FINF - Frame Index Chunk

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       4     char[4]     "FINF"
4       2     uint16      Chunk size (note: 16-bit, not 32-bit)
6       2     uint16      Unknown (typically 0)
8       4*N   uint32[N]   Encoded frame offsets (N = frame count)
```

**Frame offset decoding:**
```
raw_offset = read_uint32()       // little-endian
if raw_offset > 0x40000000:
    raw_offset -= 0x40000000     // clear flag bit
file_offset = raw_offset << 1    // multiply by 2
```

Frame offsets are relative to start of file.

### 2.5 Video Chunks

#### VQFR - Full Frame

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "VQFR"
4       4     uint32be  Chunk size (big-endian, IFF convention)
8       ...   ...       Sub-chunks (CBF, CBP, CPL, VPT)
```

#### CBF - Full Codebook

Complete codebook replacement.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CBF" + 0x00 or "CBFZ" (compressed)
4       4     uint32be  Chunk size (big-endian, IFF convention)
8       ...   uint8[]   Codebook data (or LCW compressed if CBFZ)
```

**CBFZ Decompression Mode:**

CBFZ chunks use a mode indicator in the first data byte:
- If first byte is 0x00: Skip this byte, use LCW relative mode
- If first byte is non-zero: Use LCW absolute mode

Relative mode interprets copy offsets as backwards from current output
position. Absolute mode interprets offsets from output buffer start.

```
if data[0] == 0x00:
    lcw_decompress(data[1:], output, relative=True)
else:
    lcw_decompress(data, output, relative=False)
```

Each codebook entry is `block_width * block_height` bytes (palette indices).

#### CBP - Partial Codebook

Partial codebook update. The codebook is split into CBParts segments (typically 8).
Each frame receives one segment; after CBParts frames, the entire codebook has been
updated. Segments accumulate and are decompressed together when complete.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CBP" + 0x00 or "CBPZ" (compressed)
4       4     uint32be  Chunk size (big-endian)
8       ...   uint8[]   Partial codebook data (1/CBParts of full codebook)
```

With CBParts=8 and MaxBlocks=2000, each CBP chunk contains 250 codebook entries
(2000 bytes uncompressed for 4x2 blocks).

#### CPL - Palette

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CPL" + 0x00
4       4     uint32be  Chunk size (big-endian, typically 768 = 256*3)
8       768   uint8[3]  RGB palette (6-bit values, 0-63)
```

#### VPT - Vector Pointers

Frame data referencing codebook entries. Each block position gets an index into the
codebook. The encoding differs by version.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   Chunk ID (see variants below)
4       4     uint32be  Chunk size (big-endian)
8       ...   uint8[]   Vector pointer data
```

**Chunk variants:**
- "VPT" + 0x00: Uncompressed (v1/v2)
- "VPTZ": LCW compressed, always absolute mode (v2)
- "VPTR": Uncompressed (v3 HiColor)
- "VPRZ": LCW compressed with mode detection (v3 HiColor)

**VPRZ Decompression Mode:**

Like CBFZ, VPRZ chunks use a mode indicator in the first data byte:
- If first byte is non-zero: Use LCW absolute mode
- If first byte is 0x00: Skip this byte, use LCW relative mode

```
if data[0] != 0x00:
    lcw_decompress(data, output, relative=False)
else:
    lcw_decompress(data[1:], output, relative=True)
```

**V2 Split-Byte Format (C&C, Red Alert):**

Uncompressed VPT data is split into two halves: low bytes followed by high bytes.
For a frame with W/BlockW × H/BlockH blocks (e.g., 320/4 × 200/2 = 80×100 = 8000):

```
Offset        Size    Description
------        ----    -----------
0             N       Low bytes (one per block)
N             N       High bytes (one per block)
```

For block at position (bx, by):
```
lo = data[by * blocks_x + bx]
hi = data[N + by * blocks_x + bx]
if hi == 0x0F:
    block is solid color (palette index = lo)
else:
    codebook_index = hi * 256 + lo
```

**V1 Format (Legend of Kyrandia):**

16-bit indices, where hi == 0xFF indicates solid color.

**V3 HiColor Format (Tiberian Sun):**

Detected by `Flags & 0x10` being set. Uses 16-bit RGB555 color data and
instruction-based vector encoding.

**Codebook format:** Each entry is `BlockW * BlockH * 2` bytes containing
16-bit RGB555 pixels (little-endian). After decompression, convert to RGB888:
```
packed = lo_byte | (hi_byte << 8)
R = (packed & 0x7C00) >> 7   // bits 14-10, scale to 0-248
G = (packed & 0x03E0) >> 2   // bits 9-5, scale to 0-248
B = (packed & 0x001F) << 3   // bits 4-0, scale to 0-248
```

**VPT instruction encoding:** 16-bit instructions (little-endian) with
type in bits 15-13:

| Type | Bits 12-0 | Extra | Action |
|------|-----------|-------|--------|
| 0 | count (13 bits) | none | Skip count blocks |
| 1 | B1 (8 bits) | none | Write block B1, repeat B2 times |
| 2 | B1 (8 bits) | B2 bytes | Write B1, then B2 blocks from stream |
| 3 | index (13 bits) | none | Write single block at index |
| 5 | index (13 bits) | 1 byte | Write block at index, repeat N times |

Where B2 = `(((val >> 8) & 0x1F) + 1) * 2`

### 2.6 Audio Chunks

#### SND0 - Uncompressed Audio

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND0"
4       4     uint32be  Chunk size (big-endian, IFF convention)
8       ...   uint8[]   Raw PCM samples
```

#### SND1 - Westwood ADPCM

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND1"
4       4     uint32be  Chunk size (big-endian, IFF convention)
8       2     uint16    Uncompressed size (little-endian)
10      ...   uint8[]   Westwood ADPCM data
```

#### SND2 - IMA ADPCM

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND2"
4       4     uint32be  Chunk size (big-endian, IFF convention)
8       ...   uint8[]   IMA ADPCM data
```

**Stereo Layout:**

For stereo audio (Channels=2), audio chunks are split by channel:
- First half: Left channel samples
- Second half: Right channel samples

Each channel is decoded separately, then interleaved to standard LRLRLR
format (2 bytes per sample for 16-bit output):
```
for each sample pair:
    output[i++] = left_sample_lo
    output[i++] = left_sample_hi
    output[i++] = right_sample_lo
    output[i++] = right_sample_hi
```

### 2.7 VQP Companion Files

VQP files contain **palette interpolation tables** used for video upscaling
(stretching 320×200 to 640×400). They are optional and not required for basic
playback - modern systems can upscale without them.

**Purpose:** Pre-computed 256×256 lookup tables for smooth color interpolation
when doubling video resolution.

**Note:** VQP files are NOT codebook data. The initial codebook comes from the
first CBF chunk in the VQA file, and is updated by subsequent CBP chunks.

### 2.8 Decoding Algorithm

```
1. Parse IFF container, read VQHD header
2. Read FINF frame index (optional, for seeking)
3. Initialize codebook buffer (MaxBlocks × BlockW × BlockH bytes)
4. Initialize CBP accumulator for partial codebook updates

5. For each frame (VQFR chunk):
   a. Process sub-chunks in order:
      - CPL0: Load palette (256 RGB entries, scale 6-bit to 8-bit)
      - CBF0/CBFZ: Replace entire codebook
      - CBP0/CBPZ: Accumulate partial codebook (1/CBParts segment)
        - After CBParts segments, decompress accumulated data
      - VPTZ/VPT0: Decompress and decode vector pointers
      - SND0/SND1/SND2: Decode audio samples

   b. Render frame:
      - For each block position (y=0..H/BlockH, x=0..W/BlockW):
        - Get codebook index from VPT data (handle solid color case)
        - Copy BlockW×BlockH pixels from codebook to frame buffer

   c. Queue audio samples for playback
```

### 2.9 Version Differences

| Feature | v1 (Kyrandia) | v2 (C&C, RA) | v3 (TS, RA2) |
|---------|---------------|--------------|--------------|
| Block size | 4×2 | 4×2 | 4×4 |
| Color depth | 8-bit | 8-bit | 15-bit RGB |
| Audio chunks | SND1 | SND2 | SND2 |
| VPT encoding | 16-bit | Split-byte | Instruction-based |
| Solid marker | hi=0xFF | hi=0x0F | Type field |
| CBF chunks | CBF0 | CBFZ | CBFZ (modified) |

---

## 3. AUD - Westwood Audio

Compressed audio format used for sound effects and music.

### 3.1 Overview

AUD files contain audio compressed with Westwood's ADPCM variants. The format
supports mono/stereo configurations. Data is organized as a header followed by
variable-length chunks, each marked with a signature.

**File signature:** No magic number; validated via chunk signature 0xDEAF.

### 3.2 Header Structure (12 bytes)

```
Offset  Size  Type      Field       Description
------  ----  ----      -----       -----------
0       2     uint16    SampleRate  Sample rate (8000-48000 Hz typical)
2       4     uint32    FileSize    Compressed data size
6       4     uint32    OutputSize  Uncompressed output size
10      1     uint8     Flags       Audio flags (see below)
11      1     uint8     Codec       Compression type
```

**Flags:**
```
Bit 0: Stereo (0 = mono, 1 = stereo)
Bit 1: 16-bit samples (0 = 8-bit)
Bits 2-7: Reserved (must be 0)
```

**Codec types:**
```
0x01 (1)  = Westwood ADPCM (WS-SND1, mono only)
0x63 (99) = IMA ADPCM Westwood variant (mono or stereo)
```

### 3.3 Chunk Structure

Audio data follows the header as a sequence of chunks:

```
Offset  Size  Type      Field        Description
------  ----  ----      -----        -----------
0       2     uint16    ChunkSize    Compressed chunk size
2       2     uint16    OutSize      Uncompressed output size
4       4     uint32    Signature    0x0000DEAF (little-endian)
8       N     uint8[]   Data         Compressed audio data
```

The 0xDEAF signature validates chunk integrity. Chunks are processed sequentially
until FileSize bytes consumed.

### 3.4 Westwood ADPCM (Codec 0x01)

Westwood's proprietary ADPCM with four encoding modes per chunk:

| Mode | Bits | Step Table | Description |
|------|------|------------|-------------|
| 0 | 2-bit | {-2, -1, 0, 1} | Coarse encoding |
| 1 | 4-bit | 16 values (-9..8) | Fine encoding |
| 2 | 8-bit | Direct/delta | Sparse samples |
| 3 | N/A | N/A | RLE (repeat single sample) |

Each mode processes count-prefixed data blocks with 8-bit sample clipping.
Decoder maintains sample state across chunks.

### 3.5 IMA ADPCM (Codec 0x63)

Standard IMA ADPCM with Westwood extensions:

**4-bit encoding:** Each nibble encodes one sample delta.

**Step table (89 entries):**
```c
{ 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
  143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
  494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
  1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
  4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
  27086, 29794, 32767 }
```

**Index adjustment table:**
```c
{ -1, -1, -1, -1, 2, 4, 6, 8 }
```

**Stereo layout:** For stereo, samples interleave L/R or use split channels
depending on game version.

---

## 4. SHP - Shape/Sprite Format

Sprite graphics format with multiple frames and optional compression.

### 4.1 Overview

SHP files contain one or more sprite frames, palette-indexed with 256 colors.
Two major format variants exist: TD/RA format and TS/RA2 format.

**File signatures:**
- TD/RA: No magic; identified by structure validation
- TS/RA2: First two bytes are 0x0000

### 4.2 TD/RA Format

#### File Header (14 bytes)

```
Offset  Size  Type      Field       Description
------  ----  ----      -----       -----------
0       2     uint16    Frames      Number of frames in file
2       2     uint16    XPos        X offset (ignored by game)
4       2     uint16    YPos        Y offset (ignored by game)
6       2     uint16    Width       Frame width (all frames same)
8       2     uint16    Height      Frame height (all frames same)
10      2     uint16    DeltaSize   Max decompression buffer needed
12      2     uint8[2]  Flags       Palette embedding flags
```

#### Frame Offset Table

Array of (Frames + 2) entries, each 8 bytes:

```
Offset  Size  Type      Field           Description
------  ----  ----      -----           -----------
0       3     uint24    DataOffset      File offset to frame data (24-bit)
3       1     uint8     DataFormat      Compression type (see below)
4       2     uint16    RefOffset       Reference frame's file offset
6       2     uint16    RefFormat       Reference format (as uint16)
```

**DataFormat values:**
```
0x80 = LCW compressed (Format80)
0x40 = XOR delta against LCW base frame (XORLCW)
0x20 = XOR delta against previous frame (XORPrev)
```

**RefOffset usage:**
- For Format 0x40 (XORLCW): RefOffset is the file offset of the reference
  frame. Look up the frame whose DataOffset matches RefOffset.
- For Format 0x20 (XORPrev): Reference is always previous frame (RefOffset
  ignored, use frame N-1).
- For Format 0x80 (LCW): No reference needed (RefOffset ignored).

Last two entries are sentinels: first has file length at DataOffset, second is all zeros.

### 4.3 TS/RA2 Format

#### File Header (8 bytes)

```
Offset  Size  Type      Field       Description
------  ----  ----      -----       -----------
0       2     uint16    Zero        Always 0 (format identifier)
2       2     uint16    Width       Full frame width
4       2     uint16    Height      Full frame height
6       2     uint16    Frames      Number of frames
```

#### Frame Headers (24 bytes each)

Array of Frames entries immediately after file header:

```
Offset  Size  Type      Field       Description
------  ----  ----      -----       -----------
0       2     uint16    FrameX      X alignment offset
2       2     uint16    FrameY      Y alignment offset
4       2     uint16    FrameWidth  Actual frame width
6       2     uint16    FrameHeight Actual frame height
8       4     uint32    Flags       Bit 1: transparency, Bit 2: RLE
12      4     uint8[4]  FrameColor  RGB radar color (3 bytes + pad)
16      4     uint32    Reserved    Always 0
20      4     uint32    DataOffset  Offset to frame data (0 if empty)
```

### 4.4 Compression Types

**Format80/LCW (TD/RA):** Lempel-Ziv style, see Section 10.1.

**Format40/XOR Delta (TD/RA):** XOR against previous frame, see Section 10.2.

**RLE-Zero (TS/RA2):** Per-line run-length encoding:
- Each line starts with uint16 byte count
- Value 0x00 triggers RLE: next byte is repeat count
- Other values are literal palette indices

### 4.5 Rendering Notes

- Palette index 0 is transparent
- TS/RA2 frames may be smaller than full dimensions (use FrameX/Y offsets)
- XOR delta frames must be applied in sequence from base LCW frame

---

## 5. PAL - Palette Format

256-color palette files.

### 5.1 Overview

PAL files contain 256 RGB color entries. Westwood uses 6-bit color depth
(values 0-63) which must be scaled to 8-bit for modern displays.

**File signature:** None; identified by extension and 768-byte size.

### 5.2 Structure

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       768   uint8[256][3]  RGB entries (R, G, B for each color)
```

### 5.3 Color Conversion

Westwood 6-bit to 8-bit:
```c
uint8_t convert_6to8(uint8_t value) {
    return (value << 2) | (value >> 4);
}
```

Or simply: `value * 255 / 63`

### 5.4 Special Palette Indices

| Index | Purpose |
|-------|---------|
| 0 | Transparent (in sprites) |
| 1-15 | Player colors (remappable) |
| 16-175 | General colors |
| 176-191 | Animation cycling |
| 192-223 | Shadow/translucent |
| 224-255 | Reserved/effects |

### 5.5 Common Palettes

| Name | Game | Description |
|------|------|-------------|
| TEMPERAT.PAL | TD/RA | Temperate theater |
| DESERT.PAL | TD | Desert theater |
| WINTER.PAL | TD | Winter theater |
| SNOW.PAL | RA | Snow theater |
| INTERIOR.PAL | RA | Interior maps |
| unittem.pal | TS | Unit palette (temperate) |

---

## 6. WSA - Westwood Animation

Sprite-based animation format with delta encoding.

### 6.1 Overview

WSA files store large animations (loading screens, cutscene overlays). Each frame
is XOR-delta compressed against the previous frame, then LCW compressed. This
creates cumulative frame dependencies.

**File signature:** None; identified by extension and structure validation.

### 6.2 Header Structure (C&C/RA version)

```
Offset  Size  Type        Field           Description
------  ----  ----        -----           -----------
0       2     uint16      NumFrames       Number of animation frames
2       2     uint16      XPos            Display X offset
4       2     uint16      YPos            Display Y offset
6       2     uint16      Width           Frame width
8       2     uint16      Height          Frame height
10      2     uint16      DeltaBufferSize Decompression buffer size
12      2     uint16      Flags           Bit 0: has embedded palette
14      N*4   uint32[]    FrameOffsets    (NumFrames + 2) entries
...     768   uint8[]     Palette         6-bit RGB (if Flags bit 0 set)
```

### 6.3 Frame Offset Table

The FrameOffsets array has **NumFrames + 2** entries:

- Entries 0..NumFrames-1: Offsets to frame data
- Entry NumFrames: End address OR loop frame data
- Entry NumFrames+1: Zero or end address

**Loop detection:**
- If final entry is 0: entry NumFrames is end-of-data marker
- If final entry is non-zero: entry NumFrames contains loop-back frame
  (transforms last frame to first for seamless looping)

**Missing first frame:**
- If FrameOffsets[0] is 0: animation lacks initial state
- Must chain from previous WSA file (common in multi-part animations)

### 6.4 Frame Data Compression

Each frame is doubly compressed:

1. **Format40 (XOR Delta):** Applied against previous frame buffer
2. **Format80 (LCW):** Applied to Format40 output

Decompression:
```
1. LCW decompress frame data to intermediate buffer
2. Format40 decompress, XORing against frame buffer
3. Frame buffer now contains current frame pixels
```

### 6.5 Playback Algorithm

```
1. Allocate frame buffer (Width × Height bytes, zeroed)
2. Load palette if present
3. For each frame:
   a. LCW decompress to temp buffer
   b. Format40 decode, XOR against frame buffer
   c. Display frame buffer
   d. Apply timing delay
4. For looping: use loop frame to transform back to frame 0
```

---

## 7. TMP - Tile Template

Terrain tile graphics. Two distinct formats exist: TD/RA (orthographic) and
TS/RA2 (isometric).

### 7.1 Overview

TMP files contain terrain tile graphics that form the base map layer. The format
differs significantly between game generations.

**File signature:** Magic bytes at specific offsets (see format detection).

**Tile sizes:**
- Tiberian Dawn/Red Alert: 24×24 pixels per tile (orthographic)
- Tiberian Sun: 48×24 pixels per cell (isometric)
- Red Alert 2: 60×30 pixels per cell (isometric)

### 7.2 Format Detection

**Red Alert format:**
```
Offset 20: uint32 == 0
Offset 26: uint16 == 0x2C73
```

Note: Offset 24-27 contains the full magic value 0x2C730F8A (little-endian).
OpenRA checks only the upper 16 bits at offset 26-27.

**Tiberian Dawn format:**
```
Offset 16: uint32 == 0
Offset 20: uint32 == 0x0D1AFFFF
```

**TS/RA2 format:** Lacks these magic values; detected by structure validation.

---

### 7.3 TD/RA Format (Orthographic)

#### Header (40 bytes)

```
Offset  Size  Type      Field           Description
------  ----  ----      -----           -----------
0       2     uint16    Width           Tile width in pixels (24)
2       2     uint16    Height          Tile height in pixels (24)
4       2     uint16    TileCount       Number of tiles in template
6       10    uint16[5] Reserved1       Padding (usually 0)
16      4     uint32    ImgStart        Offset to pixel data
20      8     uint32[2] Reserved2       Padding (usually 0)
28      4     uint32    IndexEnd        End of index table offset
32      4     uint32    Reserved3       Padding (usually 0)
36      4     uint32    IndexStart      Start of index table offset
```

#### Index Table

Located at IndexStart offset, contains (IndexEnd - IndexStart) bytes.
Each byte is a tile index:
- Value 0-254: Index into tile image data
- Value 255 (0xFF): Empty/null tile (no graphics)

#### Tile Data

Located at ImgStart offset. Each tile is Width × Height bytes (576 for 24×24).
Tile N is at offset: `ImgStart + TileIndex[N] × Width × Height`

**Pixel format:** 8-bit palette-indexed, no compression.

---

### 7.4 TS/RA2 Format (Isometric)

#### File Header (16 bytes)

```
Offset  Size  Type      Field           Description
------  ----  ----      -----           -----------
0       4     uint32    BlockWidth      Tile width in cells
4       4     uint32    BlockHeight     Tile height in cells
8       4     uint32    BlockImageW     Cell pixel width (48 or 60)
12      4     uint32    BlockImageH     Cell pixel height (24 or 30)
```

#### Tile Index

Following header: array of uint32 offsets to each tile cell header.
Array size = BlockWidth × BlockHeight. Offset 0 indicates empty cell.

#### Tile Cell Header

```
Offset  Size  Type      Field           Description
------  ----  ----      -----           -----------
0       4     int32     X               Cell X offset
4       4     int32     Y               Cell Y offset
8       4     uint32    ExtraDataOff    Extra graphics offset
12      4     uint32    ZDataOff        Height map offset
16      4     uint32    ExtraZDataOff   Extra height data offset
20      4     int32     ExtraX          Extra image X offset
24      4     int32     ExtraY          Extra image Y offset
28      4     uint32    ExtraWidth      Extra image width
32      4     uint32    ExtraHeight     Extra image height
36      1     uint8     Bitfield        Flags (3 LSBs, see below)
37      1     uint8     Height          Tile elevation
38      1     uint8     LandType        Terrain classification
39      1     uint8     SlopeType       Slope characteristics
40      3     uint8[3]  RadarLeft       Top-left radar color (RGB)
43      3     uint8[3]  RadarRight      Bottom-right radar color (RGB)
46      2     uint8[2]  Padding         Reserved
```

**Bitfield flags:**
```
Bit 0: HasExtraData
Bit 1: HasZData
Bit 2: HasDamagedData
```

#### Terrain Types (LandType)

| Value | Type | Passability |
|-------|------|-------------|
| 0 | Clear | Normal |
| 1-4 | Ice | Slippery |
| 5 | Tunnel | Underground |
| 6 | Railroad | Rail units |
| 7-8 | Rock | Impassable |
| 9 | Water | Naval only |
| 10 | Beach | Amphibious |
| 11-12 | Road | Speed bonus |
| 13 | Clear | Normal |
| 14 | Rough | Slowed |
| 15 | Rock | Impassable |

#### Pixel Data

Following cell headers: pixel data for each non-empty cell.
- Palette-indexed (1 byte per pixel)
- Diamond-shaped for isometric projection
- Index 0 is transparent (outside tile boundary)

---

## 8. FNT - Font Format

Bitmap font format for UI text rendering.

### 8.1 Overview

FNT files contain variable-width bitmap fonts with per-character dimensions.
Multiple format versions exist across Westwood games.

**File signature:** None; identified by extension.

**Format versions:**
- v3 (TD, RA): 4-bit per pixel, up to 256 glyphs
- v4 (TS): 8-bit per pixel (functionally 16-color)
- BitFont (RA2): 1-bit per pixel, Unicode support

### 8.2 Version 3 Header (TD/RA) - 14 bytes

```
Offset  Size  Type      Field        Description
------  ----  ----      -----        -----------
0       2     uint16    Size         Total file size
2       1     uint8     CompMethod   Always 0x00
3       1     uint8     NumBlks      Always 0x05
4       2     uint16    InfoBlk      FontInfo offset (typically 0x0E)
6       2     uint16    OffsetBlk    Glyph offset array location
8       2     uint16    WidthBlk     Width array location
10      2     uint16    DataBlk      Unused (absolute addressing)
12      2     uint16    HeightBlk    Height/Y-offset array location
```

### 8.3 FontInfo Block (6 bytes)

```
Offset  Size  Type      Field        Description
------  ----  ----      -----        -----------
0       3     uint8[3]  Reserved     Fixed: 0x12, 0x10, 0x00
3       1     uint8     NrOfChars    Last character index
4       1     uint8     MaxHeight    Maximum glyph height
5       1     uint8     MaxWidth     Maximum glyph width
```

### 8.4 Data Arrays

Following header, three arrays:

1. **Offset Array:** uint16 entries, absolute file offsets to glyph data
2. **Width Array:** uint8 per glyph, width in pixels
3. **Height Array:** 2 bytes per glyph (Y-offset, height)

### 8.5 Pixel Data

- 4 bits per pixel (16 colors from palette)
- Stride: `(width * 4 + 7) / 8` bytes per scanline
- Low nibble = leftmost pixel in each byte
- No padding between scanlines
- Identical glyphs share data (pointer reuse)

### 8.6 Known Fonts

| Filename | Size | Description |
|----------|------|-------------|
| 6POINT.FNT | 6px | Small UI text |
| 8POINT.FNT | 8px | Standard text |
| 12METFNT.FNT | 12px | Large text |
| SCOREFNT.FNT | var | Score display |
| LED.FNT | var | LED-style numbers |
| VCR.FNT | var | VCR-style display |
| game.fnt | var | RA2 Unicode font |

---

## 9. CPS - Compressed Picture

Static image format for full-screen backgrounds and title screens.

### 9.1 Overview

CPS files contain single 320×200 images with optional embedded palette. Used for
menu backgrounds, loading screens, and other static graphics.

**File signature:** None; identified by structure validation.

**Dimensions:** Always 320×200 pixels (64000 bytes uncompressed).

**Identification heuristic:** First 2 bytes + 2 = file size.

### 9.2 Header (10 bytes)

```
Offset  Size  Type      Field           Description
------  ----  ----      -----           -----------
0       2     uint16    FileSize        File size minus 2
2       2     uint16    Compression     Compression method (see below)
4       4     uint32    UncompSize      Uncompressed size (0xFA00 = 64000)
8       2     uint16    PaletteSize     Palette bytes (768 or 0)
```

### 9.3 Compression Methods

| Value | Method | Description |
|-------|--------|-------------|
| 0x0000 | None | Uncompressed raw pixels |
| 0x0001 | LZW-12 | Westwood LZW, 12-bit codes |
| 0x0002 | LZW-14 | Westwood LZW, 14-bit codes |
| 0x0003 | RLE | Run-length encoding |
| 0x0004 | LCW | Format80 compression (most common) |

### 9.4 File Layout

```
+--------+-------------------+-----------------------------------+
| Offset | Size              | Description                       |
+--------+-------------------+-----------------------------------+
| 0      | 10                | Header                            |
| 10     | PaletteSize       | Palette (if PaletteSize > 0)      |
| 10+P   | FileSize-8-P      | Compressed/raw image data         |
+--------+-------------------+-----------------------------------+

Where P = PaletteSize
```

### 9.5 Palette

When PaletteSize = 768:
- 256 RGB triplets (3 bytes each)
- 6-bit VGA values (0-63 per channel)
- Same format as standalone PAL files

When PaletteSize = 0:
- No embedded palette
- Use external palette (typically from same MIX archive)

### 9.6 Image Data

After decompression: 64000 bytes of 8-bit palette-indexed pixels.
Linear layout: row 0 first, left-to-right, then row 1, etc.

### 9.7 Decoding Algorithm

```
1. Read header (10 bytes)
2. Validate: FileSize + 2 == actual file size
3. If PaletteSize > 0: read palette
4. Decompress image data based on Compression field:
   - 0x0000: Copy raw bytes
   - 0x0004: LCW decompress (most common)
5. Result: 64000 bytes representing 320×200 indexed pixels
```

---

## 10. Standard Formats

These standard formats are used with Westwood-specific extensions or naming.

| Extension | Format | Standard | Notes |
|-----------|--------|----------|-------|
| .DAH | BMP | Windows Bitmap | Menu graphics, "High" quality variant |
| .DAL | BMP | Windows Bitmap | Menu graphics, "Low" quality variant |
| .DAW | WAV | RIFF WAVE | Menu audio, 22kHz 16-bit PCM |
| .PCX | PCX | ZSoft PCX | Some UI graphics, loading screens |
| .ICO | ICO | Windows Icon | Application icons |
| .TTF | TTF | TrueType | Modern font support (later games) |

### 10.1 DAH/DAL - Menu Graphics

Autorun/installer menu backgrounds. Standard 8-bit BMP format.
- DAH = High resolution/quality
- DAL = Low resolution/quality
- Same image content, different quality levels

### 10.2 DAW - Menu Audio

Menu music and sound effects. Standard RIFF WAVE format.
- Typically 22050 Hz sample rate
- 16-bit PCM, mono or stereo

---

## 11. Compression Algorithms

### 11.1 LCW / Format80

Lempel-Ziv style compression used in SHP, WSA, VQA, and other formats.
Provides both literal copies and back-references to repeated data.

**Two Modes:**
- **Absolute mode** (standard): Long copy offsets are from output start
- **Relative mode** (Format80r): Long copy offsets are backwards from current

VQA CBFZ and VPRZ chunks indicate mode with first data byte:
- First byte 0x00: Skip byte, use relative mode
- First byte non-zero: Use absolute mode

#### Command Byte Encoding

| Range | Command | Bytes | Description |
|-------|---------|-------|-------------|
| 0x00-0x7F | Short copy | 2 | Copy 3-10 bytes from relative offset (always relative) |
| 0x80 | End | 1 | End marker (count=0 literal) |
| 0x81-0xBF | Literal | 1+N | Copy (cmd & 0x3F) bytes from source |
| 0xC0-0xFD | Medium copy | 3 | Copy (cmd & 0x3F)+3 bytes from offset |
| 0xFE | Long fill | 4 | Fill uint16 count bytes with value |
| 0xFF | Long copy | 5 | Copy uint16 count from uint16 offset |

**Note:** The command byte layout differs from early documentation. This encoding
was confirmed via C&C source code (LCW.CPP) and tested against game files.

#### Decompression (Pseudocode)

```
// Check for relative mode indicator
if first_byte == 0x00:
    skip first byte
    use_relative = true

while not end:
    cmd = read_byte()

    if cmd < 0x80:
        // Short relative copy (always relative)
        count = ((cmd & 0x70) >> 4) + 3   // 3-10 bytes
        offset = ((cmd & 0x0F) << 8) | read_byte()
        copy count bytes from (dest - offset) to dest

    else if cmd < 0xC0:
        // Literal bytes
        count = cmd & 0x3F
        if count == 0:
            break  // End marker (0x80)
        copy count bytes from source to dest

    else if cmd < 0xFE:
        // Medium copy (absolute or relative)
        count = (cmd & 0x3F) + 3
        offset = read_uint16()
        if use_relative:
            copy count bytes from (dest - offset)
        else:
            copy count bytes from dest_start[offset]

    else if cmd == 0xFE:
        // Long fill
        count = read_uint16()
        value = read_byte()
        fill count bytes with value

    else:  // cmd == 0xFF
        // Long copy (absolute or relative)
        count = read_uint16()
        offset = read_uint16()
        if use_relative:
            copy count bytes from (dest - offset)
        else:
            copy count bytes from dest_start[offset]
```

### 11.2 Format40 - XOR Delta Encoding

Delta compression for animation frames. Applies XOR operations against
the previous frame buffer, compressing unchanged regions as skips.

Source: Original EA/Westwood XORDELTA.ASM

#### Command Encoding

| Command | Byte Pattern | Parameters | Description |
|---------|--------------|------------|-------------|
| SHORTDUMP | 0x01-0x7F | N bytes | XOR next N bytes from source |
| SHORTRUN | 0x00 | count, value | XOR next count bytes with value |
| SHORTSKIP | 0x81-0xFF | none | Skip (cmd & 0x7F) bytes |
| LONGSKIP | 0x80 + uint16 (bit15=0) | none | Skip (word & 0x7FFF) bytes |
| LONGDUMP | 0x80 + uint16 (bit15=1, bit14=0) | N bytes | XOR next (word & 0x3FFF) bytes |
| LONGRUN | 0x80 + uint16 (bits 15,14 set) | value | XOR (word & 0x3FFF) bytes with value |
| END | 0x80 0x00 0x00 | none | End of delta data |

#### Decompression (from EA source)

```
while true:
    cmd = read_byte()

    if cmd == 0x00:
        // SHORTRUN: XOR fill
        count = read_byte()
        value = read_byte()
        for i in 0..<count:
            dest[i] ^= value
        dest += count

    else if cmd < 0x80:
        // SHORTDUMP: XOR copy
        for i in 0..<cmd:
            dest[i] ^= read_byte()
        dest += cmd

    else if cmd > 0x80:
        // SHORTSKIP
        dest += (cmd & 0x7F)

    else:  // cmd == 0x80
        word = read_uint16()

        if word == 0:
            break  // END

        else if word > 0:  // bit15 == 0
            // LONGSKIP
            dest += word

        else if (word & 0x4000) == 0:  // bit15=1, bit14=0
            // LONGDUMP: XOR copy
            count = word & 0x3FFF
            for i in 0..<count:
                dest[i] ^= read_byte()
            dest += count

        else:  // bits 15,14 both set
            // LONGRUN: XOR fill
            count = word & 0x3FFF
            value = read_byte()
            for i in 0..<count:
                dest[i] ^= value
            dest += count
```

Note: "word > 0" uses signed comparison (bit 15 is sign bit).

### 11.3 Westwood ADPCM (WS-SND1)

Westwood's proprietary audio compression. Produces 8-bit unsigned PCM output.

Source: Original EA/Westwood AUDUNCMP.ASM, FFmpeg ws-snd1.c

#### Command Byte Structure

Each command byte encodes both mode and count:
```
Bits 7-6: Mode (0-3)
Bits 5-0: Count parameter
```

#### Lookup Tables

```c
int8_t step_2bit[4] = { -2, -1, 0, 1 };
int8_t step_4bit[16] = { -9, -8, -6, -5, -4, -3, -2, -1,
                          0,  1,  2,  3,  4,  5,  6,  8 };
```

#### Modes

| Mode | Name | Output Samples | Description |
|------|------|----------------|-------------|
| 0 | 2-bit ADPCM | 4 × (count+1) | Four 2-bit deltas per byte |
| 1 | 4-bit ADPCM | 2 × (count+1) | Two 4-bit deltas per byte |
| 2 | Raw/Delta | 1 or (count+1) | See below |
| 3 | Silence | count+1 | Repeat previous sample |

**Mode 2 detail:** If bit 5 of count is set, apply 5-bit signed delta
(sign-extend bits 4-0, add to previous). Otherwise, copy (count+1) literal bytes.

#### Decoding (from EA source)

```
sample = 0x80  // Initial center value (unsigned 8-bit)

while output_needed:
    cmd = read_byte()
    mode = cmd >> 6
    count = cmd & 0x3F

    if mode == 0:  // 2-bit ADPCM
        for i in 0..<(count+1):
            packed = read_byte()
            for j in 0..<4:
                delta = step_2bit[(packed >> (j*2)) & 0x03]
                sample = clamp(sample + delta, 0, 255)
                output(sample)

    elif mode == 1:  // 4-bit ADPCM
        for i in 0..<(count+1):
            packed = read_byte()
            delta = step_4bit[packed & 0x0F]
            sample = clamp(sample + delta, 0, 255)
            output(sample)
            delta = step_4bit[packed >> 4]
            sample = clamp(sample + delta, 0, 255)
            output(sample)

    elif mode == 2:  // Raw or delta
        if count & 0x20:
            // 5-bit signed delta
            delta = sign_extend_5bit(count & 0x1F)
            sample = clamp(sample + delta, 0, 255)
            output(sample)
        else:
            // Literal samples
            for i in 0..<(count+1):
                sample = read_byte()
                output(sample)

    elif mode == 3:  // Silence (RLE)
        for i in 0..<(count+1):
            output(sample)  // repeat previous
```

### 11.4 IMA ADPCM

Standard IMA ADPCM used in VQA SND2 chunks and AUD codec 0x63.

#### Algorithm

```
predictor = 0      // 16-bit signed
step_index = 0     // 0-88

for each nibble:
    step = step_table[step_index]

    // Compute delta
    delta = step >> 3
    if nibble & 0x04: delta += step
    if nibble & 0x02: delta += step >> 1
    if nibble & 0x01: delta += step >> 2
    if nibble & 0x08: delta = -delta

    predictor = clamp(predictor + delta, -32768, 32767)
    step_index = clamp(step_index + index_table[nibble & 0x07], 0, 88)

    output(predictor)
```

#### Step Table (89 entries)

```c
{ 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
  143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
  494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
  1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
  4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
  27086, 29794, 32767 }
```

#### Index Table (8 entries, mirrored)

```c
{ -1, -1, -1, -1, 2, 4, 6, 8 }
```

### 11.5 RLE-Zero (TS/RA2 SHP)

Simple run-length encoding for transparent pixels.

```
for each scanline:
    line_size = read_uint16()
    bytes_read = 2

    while bytes_read < line_size:
        value = read_byte()
        bytes_read++

        if value == 0:
            count = read_byte()
            bytes_read++
            output count transparent pixels
        else:
            output value as palette index
```

---

## References

1. XCC Utilities source code - https://github.com/OlafvdSpek/xcc
2. FFmpeg VQA/AUD decoders - libavformat/westwood_aud.c, libavcodec/vqavideo.c
3. OpenRA engine - https://github.com/OpenRA/OpenRA
4. ModEnc wiki - https://modenc.renegadeprojects.com/
5. ModdingWiki - https://moddingwiki.shikadi.net/
6. Multimedia Wiki - https://wiki.multimedia.cx/
7. EA open-source C&C code - https://github.com/electronicarts/CnC_Red_Alert
8. CnC-DDraw documentation
