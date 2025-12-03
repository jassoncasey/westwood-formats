# libwestwood TODO

## COMPLETED

- [x] AUD DEAF chunk validation (8-byte preamble, persistent predictor/step_index)
- [x] WSA header parsing (8-byte frame entries, LCW buffer sizing)
- [x] Stdin support for CLI tools (aud, shp, cps, wsa, pal, fnt, vqa, tmp)
- [x] TD/RA SHP format (LCW, XOR delta frames, format 0x00 handling)
- [x] TS/RA2 SHP format (RLE-Zero compression, per-frame dimensions)
- [x] TD/RA MIX format (encrypted, Blowfish)
- [x] TS MIX format (unencrypted, aligned)
- [x] VQA V2 decoding (indexed color, audio)
- [x] Westwood ADPCM and IMA ADPCM audio
- [x] PAL 6-bit and 8-bit palettes
- [x] CPS with LCW compression
- [x] TD/RA TMP orthographic tiles
- [x] BIG format (Generals/Zero Hour, mixed endianness)
- [x] VQA V3 / HiColor (detection via flag 0x10 or colors=0, VQFR/VQFL containers)
- [x] TS/RA2 TMP isometric tiles (diamond shape, 52-byte headers, extra/Z-data)
- [x] FNT v2 (1-bit monochrome, 128 chars), v3 (4-bit), v4 (8-bit), BitFont (RA2/Nox)
- [x] Unicode BitFont support (header "fonT", 65K glyphs, Unicode table)
- [x] mix-tool stdin support (read MIX from stdin with `-`)
- [x] CPS compression methods (0=none, 1=LZW12, 2=LZW14, 3=RLE, 4=LCW)
- [x] TD/RA TMP header fields documented (ID1/ID2 magic, IndexImagesInfo, IndexTilesetImages)
- [x] MIX-RG (Renegade) format (MIX1 magic, index+names tailer, CRC32 hashes)
