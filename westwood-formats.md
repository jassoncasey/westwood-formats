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
9. [Standard Formats](#9-standard-formats)
10. [Compression Algorithms](#10-compression-algorithms)

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
| Graphics | SHP, PAL, PCX | Sprites, palettes, images |
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
| TMP | - | - | ✓ | ✓ | TS/RA2 isometric |
| FNT | ✓ | ✓ | ✓ | ✓ | Minor variations |

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

**Note:** IFF uses big-endian chunk sizes, but Westwood's internal data is
little-endian.

### 2.3 VQHD - Header Chunk

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "VQHD"
4       4     uint32    Chunk size
8       2     uint16    Version (1, 2, or 3)
10      2     uint16    Flags
12      2     uint16    Frame count
14      2     uint16    Width (pixels)
16      2     uint16    Height (pixels)
18      1     uint8     Block width (typically 4)
19      1     uint8     Block height (typically 2 or 4)
20      1     uint8     Frame rate (fps)
21      1     uint8     CBParts (codebook parts)
22      2     uint16    Colors (palette entries, usually 256)
24      2     uint16    Max blocks
26      2     uint16    Unknown1
28      2     uint16    Unknown2
30      4     uint32    Audio frequency (Hz)
34      1     uint8     Audio channels (1=mono, 2=stereo)
35      1     uint8     Audio bits (8 or 16)
36      4     uint32    Unknown3 (version 2+)
40      ...   ...       Additional fields (version dependent)
```

**Flags:**
```
Bit 0: Has audio
Bit 1: Unknown
```

### 2.4 FINF - Frame Index Chunk

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       4     char[4]     "FINF"
4       4     uint32      Chunk size
8       4*N   uint32[N]   Frame offsets (N = frame count from VQHD)
```

Frame offsets are relative to start of file.

### 2.5 Video Chunks

#### VQFR - Full Frame

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "VQFR"
4       4     uint32    Chunk size
8       ...   ...       Sub-chunks (CBF, CBP, CPL, VPT)
```

#### CBF - Full Codebook

Complete codebook replacement.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CBF" + 0x00 or "CBFZ" (compressed)
4       4     uint32    Chunk size
8       ...   uint8[]   Codebook data (or LCW compressed if CBFZ)
```

Each codebook entry is `block_width * block_height` bytes (palette indices).

#### CBP - Partial Codebook

Partial codebook update.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CBP" + 0x00 or "CBPZ" (compressed)
4       4     uint32    Chunk size
8       ...   uint8[]   Partial codebook data
```

#### CPL - Palette

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "CPL" + 0x00
4       4     uint32    Chunk size (typically 768 = 256*3)
8       768   uint8[3]  RGB palette (6-bit values, 0-63)
```

#### VPT - Vector Pointers

Frame data referencing codebook entries.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "VPT" + 0x00 or "VPTZ" (compressed)
4       4     uint32    Chunk size
8       ...   uint8[]   Vector pointer data
```

TODO: Document VPT encoding (lookup table, skip codes, etc.)

### 2.6 Audio Chunks

#### SND0 - Uncompressed Audio

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND0"
4       4     uint32    Chunk size
8       ...   uint8[]   Raw PCM samples
```

#### SND1 - Westwood ADPCM

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND1"
4       4     uint32    Chunk size
8       2     uint16    Uncompressed size
10      ...   uint8[]   Westwood ADPCM data
```

#### SND2 - IMA ADPCM

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     char[4]   "SND2"
4       4     uint32    Chunk size
8       ...   uint8[]   IMA ADPCM data
```

### 2.7 VQP Companion Files

VQP files contain pre-computed codebook data loaded before playback. Format is
raw codebook entries without IFF wrapper.

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     uint32    Entry count (or derived from file size)
4       ...   uint8[]   Codebook entries
```

TODO: Verify VQP structure from test files.

### 2.8 Decoding Algorithm

```
1. Read VQHD header
2. Load initial codebook (from VQP or first CBF chunk)
3. Load palette (from CPL chunk)
4. For each frame:
   a. Apply codebook updates (CBP chunks)
   b. Read VPT data
   c. For each block position:
      - Read vector pointer (1 or 2 bytes depending on codebook size)
      - Copy block from codebook to frame buffer
   d. Decode audio chunk if present
   e. Display frame, play audio
```

### 2.9 Version Differences

| Feature | v1 (TD) | v2 (RA) | v3 (TS+) |
|---------|---------|---------|----------|
| Block size | 4x2 | 4x2 | 4x4 |
| Audio | SND1 | SND2 | SND2 |
| Hi-color | No | No | Optional |

---

## 3. AUD - Westwood Audio

Compressed audio format used for sound effects and music.

### 3.1 Overview

AUD files contain audio compressed with Westwood's IMA ADPCM variant or
uncompressed PCM. The format supports mono/stereo and 8/16-bit samples.

**File signature:** No magic number; identified by extension.

### 3.2 Header Structure

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       2     uint16    Sample rate (Hz)
2       4     uint32    Uncompressed size (bytes)
6       4     uint32    Compressed size (bytes)
10      1     uint8     Flags
11      1     uint8     Compression type
```

**Flags:**
```
Bit 0: 16-bit samples (0 = 8-bit)
Bit 1: Stereo (0 = mono)
```

**Compression types:**
```
0x00 = Uncompressed PCM
0x01 = Westwood ADPCM (8-bit)
0x63 = IMA ADPCM (99 decimal)
```

### 3.3 Chunk Structure

For compressed audio, data is stored in chunks:

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       2     uint16    Compressed chunk size
2       2     uint16    Uncompressed chunk size
4       4     uint32    ID (0xDEAF for valid chunk)
8       ...   uint8[]   Compressed data
```

### 3.4 Westwood ADPCM

TODO: Document Westwood's ADPCM variant:
- Step table differences from standard IMA
- Encoding/decoding algorithm
- Sample reconstruction

### 3.5 IMA ADPCM

Standard IMA ADPCM with:
- 4-bit samples
- Standard step table (89 entries)
- Predictor/step index state per channel

---

## 4. SHP - Shape/Sprite Format

Sprite graphics format with multiple frames and optional compression.

### 4.1 Overview

SHP files contain one or more sprite frames, typically palette-indexed.
Different games use different header variants.

**File signature:** No universal magic; some versions have identifiable headers.

### 4.2 TD/RA Format

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       2     uint16      Frame count
2       4     uint32      Unknown (0 for TD, X/Y offset for RA?)
6       4     uint32      Width (all frames)
10      4     uint32      Height (all frames)
14      4     uint32      Largest frame size
18      2     uint16      Flags
20      ...   uint32[]    Frame offsets (frame_count + 2 entries)
...     ...   ...         Frame data
```

### 4.3 TS/RA2 Format

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       4     uint32      Zero (format identifier)
4       4     uint32      Width
8       4     uint32      Height
12      2     uint16      Frame count
14      ...   ...         Frame headers and data
```

### 4.4 Frame Header

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       2     uint16    X offset
2       2     uint16    Y offset
4       2     uint16    Width
6       2     uint16    Height
8       1     uint8     Compression type
9       3     uint24    Compressed size (or other flags)
12      ...   uint8[]   Pixel data
```

**Compression types:**
```
0x00 = Uncompressed
0x01 = Unknown
0x02 = Unknown
0x03 = LCW (Format80) compressed
```

### 4.5 Pixel Data

- Palette-indexed (1 byte per pixel)
- Index 0 typically transparent
- Compression applied per-frame

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

### 5.4 Common Palettes

| Name | Game | Description |
|------|------|-------------|
| TEMPERAT.PAL | RA | Temperate theater |
| SNOW.PAL | RA | Snow theater |
| INTERIOR.PAL | RA | Interior maps |
| unittem.pal | TS | Unit palette (temperate) |

---

## 6. WSA - Westwood Animation

Sprite-based animation format with delta encoding.

### 6.1 Overview

WSA files contain frame-based animations using delta compression. Each frame
stores only the differences from the previous frame.

**File signature:** Identified by structure, no magic number.

### 6.2 Header Structure

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       2     uint16      Frame count
2       2     uint16      X position
4       2     uint16      Y position
6       2     uint16      Width
8       2     uint16      Height
10      4     uint32      Delta buffer size
14      2     uint16      Flags
16      ...   uint32[]    Frame offsets (frame_count + 2 entries)
```

**Flags:**
```
Bit 0: Has palette
Bit 1: Loop animation
```

### 6.3 Frame Data

Frames use Format40 delta encoding:

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       2     uint16    Compressed size
2       ...   uint8[]   Format40 encoded data
```

### 6.4 Embedded Palette

If flag bit 0 is set, 768 bytes of palette data follow the header.

---

## 7. TMP - Tile Template

Isometric terrain tiles for Tiberian Sun and Red Alert 2.

### 7.1 Overview

TMP files contain isometric tile graphics with height and terrain metadata.
Used for map terrain rendering.

**File signature:** None; identified by extension.

### 7.2 Header Structure

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       4     uint32      Tile width (typically 48 or 60)
4       4     uint32      Tile height (typically 24 or 30)
8       4     uint32      Tiles per row
12      4     uint32      Tile count
16      ...   uint32[]    Tile offsets
```

### 7.3 Tile Header

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       4     uint32    X position
4       4     uint32    Y position
8       4     uint32    Extra data offset
12      4     uint32    Z data offset
16      4     uint32    Extra Z data offset
20      4     uint32    X extra
24      4     uint32    Y extra
28      4     uint32    Width
32      4     uint32    Height
36      1     uint8     Terrain type
37      1     uint8     Ramp type
38      2     uint8[2]  Radar colors (left/right)
40      ...   uint8[]   Pixel data
```

### 7.4 Terrain Types

```
0x00 = Clear
0x01 = Ice
0x02 = Tunnel
0x03 = Railroad
0x04 = Rock
0x05 = Water
0x06 = Beach
0x07 = Road (clear)
0x08 = Road (rough)
0x09 = Rough
0x0A = Cliff
...
```

---

## 8. FNT - Font Format

Bitmap font format for UI text rendering.

### 8.1 Overview

FNT files contain fixed or variable-width bitmap fonts with character
width tables.

**File signature:** None; identified by extension.

### 8.2 Header Structure

```
Offset  Size  Type        Description
------  ----  ----        -----------
0       2     uint16      Font size (height in pixels)
2       1     uint8       Unknown
3       1     uint8       First character (ASCII code)
4       1     uint8       Last character (ASCII code)
5       1     uint8       Unknown
6       2     uint16      Data size
8       ...   ...         Character data
```

### 8.3 Character Data

For each character from first to last:

```
Offset  Size  Type      Description
------  ----  ----      -----------
0       1     uint8     Character width
1       1     uint8     Character height
2       ...   uint8[]   Bitmap data (1 bit per pixel or indexed)
```

### 8.4 Known Fonts

| Filename | Size | Description |
|----------|------|-------------|
| 6POINT.FNT | 6px | Small UI text |
| 8POINT.FNT | 8px | Standard text |
| 12METFNT.FNT | 12px | Large text |
| SCOREFNT.FNT | var | Score display |
| LED.FNT | var | LED-style numbers |
| VCR.FNT | var | VCR-style display |

---

## 9. Standard Formats

These standard formats are used with Westwood-specific extensions or naming.

| Extension | Format | Standard | Notes |
|-----------|--------|----------|-------|
| .DAH | BMP | Windows Bitmap | Menu graphics, "High" quality variant |
| .DAL | BMP | Windows Bitmap | Menu graphics, "Low" quality variant |
| .DAW | WAV | RIFF WAVE | Menu audio, 22kHz 16-bit PCM |
| .PCX | PCX | ZSoft PCX | Some UI graphics, loading screens |
| .ICO | ICO | Windows Icon | Application icons |
| .TTF | TTF | TrueType | Modern font support (later games) |

### 9.1 DAH/DAL - Menu Graphics

Autorun/installer menu backgrounds. Standard 8-bit BMP format.
- DAH = High resolution/quality
- DAL = Low resolution/quality
- Same image content, different quality levels

### 9.2 DAW - Menu Audio

Menu music and sound effects. Standard RIFF WAVE format.
- Typically 22050 Hz sample rate
- 16-bit PCM, mono or stereo

---

## 10. Compression Algorithms

### 10.1 LCW / Format80

Lempel-Ziv style compression used in SHP, WSA, and VQA files.

#### Command Byte Encoding

```
Byte      Command         Description
----      -------         -----------
0x00-0x3F Copy            Copy N+1 bytes from source
0x40-0x7F Copy from dest  Copy from earlier in output
0x80      End             End of compressed data
0x81-0xBF Long copy       Extended copy from destination
0xC0-0xFE Fill            Fill N bytes with value
0xFF      Long fill       Extended fill
```

#### Decompression Algorithm

```c
void lcw_decompress(uint8_t* src, uint8_t* dst) {
    while (1) {
        uint8_t cmd = *src++;

        if (cmd == 0x80) {
            break;  // End marker
        }
        else if ((cmd & 0xC0) == 0x00) {
            // Copy cmd+1 bytes from source
            int count = (cmd & 0x3F) + 1;
            memcpy(dst, src, count);
            src += count;
            dst += count;
        }
        else if ((cmd & 0xC0) == 0x40) {
            // Copy from destination (relative)
            int count = ((cmd & 0x3F) >> 4) + 3;
            int offset = ((cmd & 0x0F) << 8) | *src++;
            uint8_t* ref = dst - offset;
            while (count--) *dst++ = *ref++;
        }
        else if ((cmd & 0xC0) == 0xC0) {
            // Fill
            int count = (cmd & 0x3F) + 3;
            uint8_t value = *src++;
            memset(dst, value, count);
            dst += count;
        }
        // ... additional cases for 0x81-0xBF, 0xFF
    }
}
```

### 10.2 Format40 - Delta Encoding

Delta compression for animation frames (WSA).

#### Command Encoding

```
Byte      Command         Description
----      -------         -----------
0x00      Skip            Skip N bytes (N in next byte)
0x01-0x7F Copy            Copy N bytes from source
0x80      End             End of delta data
0x81-0xFE Skip inline     Skip (cmd - 0x80) bytes
0xFF      Long command    Extended command follows
```

### 10.3 Westwood ADPCM

Variant of IMA ADPCM with modified step table.

TODO: Document step table and algorithm differences.

### 10.4 IMA ADPCM

Standard IMA ADPCM (used in VQA SND2 chunks).

#### Step Table

```c
static const int ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
```

#### Index Table

```c
static const int ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
```

---

## References

1. XCC Utilities source code - https://github.com/OlafvdSpek/xcc
2. FFmpeg VQA decoder - libavformat/wsvqa.c, libavcodec/vqavideo.c
3. OpenRA engine - https://github.com/OpenRA/OpenRA
4. ModEnc wiki - https://modenc.renegadeprojects.com/
5. CnC-DDraw documentation
