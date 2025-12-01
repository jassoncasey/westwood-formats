# Westwood Studios Archive Formats Specification

Command & Conquer Series (1995-2003)

See also: [README.md](README.md) (project overview), [mix-tool.md](mix-tool.md) (tool spec)

```
Reference Implementation: XCC Utilities by Olaf van der Spek
License: GPL v3
Source: https://github.com/OlafvdSpek/xcc

Document derived from XCC source code analysis
Files: misc/mix_file.cpp, misc/cc_structures.h, misc/mix_decode.cpp,
       misc/blowfish.cpp, misc/big_file.cpp, misc/mix_rg_file.cpp
```

## Table of Contents

1. [Overview](#1-overview)
2. [Format Detection](#2-format-detection)
3. [Tiberian Dawn (TD) Format](#3-tiberian-dawn-td-format)
4. [Red Alert (RA) Format - Encrypted](#4-red-alert-ra-format---encrypted)
5. [Tiberian Sun (TS) Format](#5-tiberian-sun-ts-format)
6. [Renegade (MIX-RG) Format](#6-renegade-mix-rg-format)
7. [Generals (BIG) Format](#7-generals-big-format)
8. [Filename Hashing Algorithms](#8-filename-hashing-algorithms)
9. [Encryption](#9-encryption)
10. [Checksums](#10-checksums)
11. [Shared Structures](#11-shared-structures)
12. [Primitive Types](#12-primitive-types)
13. [Constants Reference](#13-constants-reference)
14. [Game Type Detection Heuristics](#14-game-type-detection-heuristics)
15. [Implementation Notes](#15-implementation-notes)

---

## 1. Overview

This document specifies five archive formats used in Command & Conquer games:

| Format | Games                              | Encryption | Alignment | Filenames |
|--------|------------------------------------|-----------:|----------:|-----------|
| TD     | Tiberian Dawn (1995)               | None       | None      | Hashed    |
| RA     | Red Alert (1996)                   | Blowfish   | None      | Hashed    |
| TS     | Tiberian Sun, RA2, Yuri's Revenge  | None       | 16-byte   | Hashed    |
| MIX-RG | Renegade (2002)                    | None       | None      | Stored    |
| BIG    | Generals, Zero Hour (2003)         | None       | None      | Stored    |

**Conventions:**
- All multi-byte integers are LITTLE-ENDIAN unless otherwise noted
- All structures are byte-packed (no padding between fields)
- Offsets are zero-indexed byte positions from file start

---

## 2. Format Detection

Detection MUST proceed in this exact order:

```
1. If file_size >= 16 AND bytes[0..3] == 0x4D495831:
     -> MIX-RG format (magic "MIX1")

2. If file_size >= 16 AND bytes[0..3] IN {0x46474942, 0x34474942}:
     -> BIG format (magic "BIGF" or "BIG4")

3. Read bytes[0..5] as td-header:
     c_files   = UINT16-LE at offset 0
     body_size = UINT32-LE at offset 2

     If c_files > 0
        AND c_files < 4096
        AND file_size == 6 + (c_files * 12) + body_size:
          -> TD format

4. Read bytes[0..3] as flags (UINT32-LE):
     If flags AND 0x00020000:
          -> RA format (encrypted)
     Else:
          -> TS format (unencrypted)
```

**Rationale for Step 3:**
TD format detection relies on the fact that for RA/TS formats, bytes[0..1] interpreted as `c_files` will be 0, because the low 16 bits of valid flags (0x00000000, 0x00010000, 0x00020000, 0x00030000) are always 0.

---

## 3. Tiberian Dawn (TD) Format

**Games:** Command & Conquer: Tiberian Dawn (1995)

**Features:** Unencrypted, unaligned, no checksum

### File Layout

```
+--------+-------------------+--------------------------------------+
| Offset | Size              | Description                          |
+--------+-------------------+--------------------------------------+
| 0      | 2                 | c_files (file count)                 |
| 2      | 4                 | body_size (total size of body)       |
| 6      | c_files * 12      | index (array of index entries)       |
| H      | body_size         | body (concatenated file data)        |
+--------+-------------------+--------------------------------------+

Where H = 6 + (c_files * 12)
```

### ABNF Definition

```abnf
mix-td              = td-header td-index td-body

td-header           = c-files body-size

c-files             = UINT16-LE
                      ; Range: 1 to 4095 (0x001 to 0xFFF)
                      ; Value 0 indicates RA/TS format, not TD

body-size           = UINT32-LE
                      ; Total size of body section in bytes

td-index            = *index-entry
                      ; Exactly c_files entries (12 bytes each)

td-body             = *OCTET
                      ; Exactly body_size bytes
```

### Offset Calculation

```
header_size         = 6
index_size          = c_files * 12
body_offset         = header_size + index_size = 6 + (c_files * 12)
absolute_offset(e)  = e.offset + body_offset
```

Stored offsets in index entries are RELATIVE to body start.

### Validation

```
file_size == 6 + (c_files * 12) + body_size
c_files >= 1
c_files <= 4095
```

---

## 4. Red Alert (RA) Format - Encrypted

**Games:** Command & Conquer: Red Alert (1996)

**Features:** Blowfish encryption with RSA key exchange

### File Layout

```
+--------+-------------------+--------------------------------------+
| Offset | Size              | Description                          |
+--------+-------------------+--------------------------------------+
| 0      | 4                 | flags (bit 17 set = encrypted)       |
| 4      | 80                | key_source (RSA-encrypted BF key)    |
| 84     | 8                 | encrypted_header (BF-encrypted)      |
| 92     | P                 | encrypted_index (BF-encrypted)       |
| 92+P   | body_size         | body (NOT encrypted)                 |
| EOF-20 | 20                | checksum (if flags bit 16 set)       |
+--------+-------------------+--------------------------------------+

Where P = ((c_files * 12) + 5) AND NOT 7  (rounds up to 8-byte boundary)
```

### Encrypted Header Structure

After Blowfish decryption of the 8 bytes at offset 84:

```
+--------+------+---------------------------------------------+
| Offset | Size | Description                                 |
+--------+------+---------------------------------------------+
| 0      | 2    | c_files                                     |
| 2      | 4    | body_size                                   |
| 6      | 2    | First 2 bytes of index (partial entry 0)    |
+--------+------+---------------------------------------------+
```

### Encrypted Index Structure

After decryption, contains remaining `((c_files * 12) - 2)` bytes of index data, padded with garbage to 8-byte boundary.

### Index Reconstruction Algorithm

```
1. Decrypt 8-byte header block at offset 84
2. Extract c_files (bytes 0-1) and body_size (bytes 2-5) from decrypted header
3. Calculate P = ((c_files * 12) + 5) AND NOT 7
4. Read P bytes from offset 92
5. Decrypt the P bytes using Blowfish
6. Allocate index buffer of (c_files * 12) bytes
7. Copy decrypted_header[6..7] to index[0..1]
8. Copy decrypted_index[0..((c_files*12)-2)-1] to index[2..]
9. Parse index as array of index_entry structures (12 bytes each)
```

### ABNF Definition

```abnf
mix-ra              = ra-flags key-source encrypted-header
                      encrypted-index body [checksum]

ra-flags            = UINT32-LE
                      ; Bit 16 (0x00010000): checksum present at EOF
                      ; Bit 17 (0x00020000): encrypted (MUST be set for RA)
                      ; All other bits MUST be 0

key-source          = 80OCTET
                      ; RSA-encrypted Blowfish key material
                      ; See Section 9.1 for decryption

encrypted-header    = 8OCTET
                      ; Blowfish-encrypted (ECB mode)
                      ; Contains: c_files (2) + body_size (4) + index[0..1] (2)

encrypted-index     = *OCTET
                      ; Length P = ((c_files * 12) + 5) AND NOT 7
                      ; Blowfish-encrypted (ECB mode)
                      ; Contains: index[2..] with padding

body                = *OCTET
                      ; NOT encrypted
                      ; Length: body_size bytes

checksum            = 20OCTET
                      ; SHA-1 hash (only if flags bit 16 set)
                      ; See Section 10
```

### Offset Calculation

```
P                   = ((c_files * 12) + 5) AND NOT 7
body_offset         = 92 + P
absolute_offset(e)  = e.offset + body_offset
```

### Validation

```
file_size == 92 + P + body_size + (has_checksum ? 20 : 0)
flags AND 0x00020000 != 0  (encrypted bit must be set)
(flags AND NOT 0x00030000) == 0  (no other bits set)
```

---

## 5. Tiberian Sun (TS) Format

**Games:** Tiberian Sun (1999), Red Alert 2 (2000), Yuri's Revenge (2001)

**Features:** Unencrypted, 16-byte aligned file offsets, optional checksum

### File Layout

```
+--------+-------------------+--------------------------------------+
| Offset | Size              | Description                          |
+--------+-------------------+--------------------------------------+
| 0      | 4                 | flags                                |
| 4      | 2                 | c_files                              |
| 6      | 4                 | body_size                            |
| 10     | c_files * 12      | index                                |
| H      | body_size         | body (with alignment padding)        |
| EOF-20 | 20                | checksum (if flags bit 16 set)       |
+--------+-------------------+--------------------------------------+

Where H = 10 + (c_files * 12)
```

### ABNF Definition

```abnf
mix-ts              = ts-flags ts-header ts-index ts-body [checksum]

ts-flags            = UINT32-LE
                      ; Bit 16 (0x00010000): checksum present at EOF
                      ; Bit 17 (0x00020000): MUST be 0 (else treat as RA)
                      ; Typically 0x00000000 or 0x00010000

ts-header           = c-files body-size
                      ; Same structure as TD header, at offset 4

ts-index            = *index-entry
                      ; Exactly c_files entries
                      ; All entry.offset values SHOULD be 16-byte aligned:
                      ;   (entry.offset AND 0x0F) == 0

ts-body             = *OCTET
                      ; body_size bytes
                      ; Files are padded to maintain 16-byte alignment
```

### Offset Calculation

```
header_size         = 4 + 6 = 10
index_size          = c_files * 12
body_offset         = header_size + index_size = 10 + (c_files * 12)
absolute_offset(e)  = e.offset + body_offset
```

### Validation

```
file_size == 10 + (c_files * 12) + body_size + (has_checksum ? 20 : 0)
(flags AND 0x00020000) == 0  (encryption bit must NOT be set)
```

---

## 6. Renegade (MIX-RG) Format

**Games:** Command & Conquer: Renegade (2002)

**Features:** Separate index and filename sections, plaintext filenames stored

### File Layout

```
+--------+-------------------+--------------------------------------+
| Offset | Size              | Description                          |
+--------+-------------------+--------------------------------------+
| 0      | 4                 | magic (0x4D495831 = "MIX1")          |
| 4      | 4                 | index_offset                         |
| 8      | 4                 | tailer_offset                        |
| 12     | 4                 | reserved (must be 0)                 |
| 16     | varies            | body data                            |
| I      | varies            | index section                        |
| T      | varies            | tailer section (filenames)           |
+--------+-------------------+--------------------------------------+

Where I = index_offset, T = tailer_offset
```

### ABNF Definition

```abnf
mix-rg              = rg-header rg-body rg-index rg-tailer

rg-header           = rg-magic index-offset tailer-offset rg-reserved

rg-magic            = %x4D %x49 %x58 %x31
                      ; "MIX1" in ASCII
                      ; As UINT32-LE: 0x3158494D

index-offset        = UINT32-LE
                      ; Absolute byte offset to index section

tailer-offset       = UINT32-LE
                      ; Absolute byte offset to tailer section

rg-reserved         = %x00 %x00 %x00 %x00
                      ; Must be zero

rg-body             = *OCTET
                      ; File data, between header and index section
                      ; Offsets in index are absolute from file start

rg-index            = rg-count *rg-index-entry

rg-count            = UINT32-LE
                      ; Number of files

rg-index-entry      = rg-id rg-offset rg-size

rg-id               = UINT32-LE
                      ; CRC32 hash of filename (see Section 8.3)

rg-offset           = UINT32-LE
                      ; Absolute byte offset from file start

rg-size             = UINT32-LE
                      ; File size in bytes

rg-tailer           = rg-count *rg-name-entry
                      ; Count must match index count

rg-name-entry       = name-length name-bytes

name-length         = OCTET
                      ; Length of name-bytes INCLUDING null terminator

name-bytes          = *OCTET %x00
                      ; Filename followed by null terminator
                      ; Total length equals name-length
```

### Validation

```
magic == 0x3158494D (when read as UINT32-LE)
index_offset + 4 <= tailer_offset
tailer_offset + 4 <= file_size
reserved == 0
index.count == tailer.count
```

---

## 7. Generals (BIG) Format

**Games:** C&C Generals (2003), Zero Hour (2003)

**Features:** Mixed endianness, inline plaintext filenames

**IMPORTANT:** File count and header size are BIG-ENDIAN. Index entry offsets and sizes are BIG-ENDIAN. Magic and file_size fields are LITTLE-ENDIAN.

### File Layout

```
+--------+-------------------+--------------------------------------+
| Offset | Size              | Description                          |
+--------+-------------------+--------------------------------------+
| 0      | 4                 | magic ("BIGF" or "BIG4")             |
| 4      | 4                 | file_size (LE)                       |
| 8      | 4                 | c_files (BE)                         |
| 12     | 4                 | header_size (BE)                     |
| 16     | varies            | index entries with inline filenames  |
| H      | varies            | body data                            |
+--------+-------------------+--------------------------------------+

Where H = header_size
```

### ABNF Definition

```abnf
big-file            = big-header *big-index-entry

big-header          = big-magic big-file-size big-count big-header-size

big-magic           = %x42 %x49 %x47 %x46  ; "BIGF" (0x46474942 as LE)
                    / %x42 %x49 %x47 %x34  ; "BIG4" (0x34474942 as LE)

big-file-size       = UINT32-LE
                      ; Total file size (for validation)

big-count           = UINT32-BE
                      ; Number of files (BIG-ENDIAN!)

big-header-size     = UINT32-BE
                      ; Size of header + index section (BIG-ENDIAN!)
                      ; Body data starts at this offset

big-index-entry     = big-offset big-entry-size big-filename

big-offset          = UINT32-BE
                      ; Absolute file offset (BIG-ENDIAN!)

big-entry-size      = UINT32-BE
                      ; File size in bytes (BIG-ENDIAN!)

big-filename        = *(%x01-FF) %x00
                      ; Null-terminated filename (variable length)
```

### Validation

```
magic IN {0x46474942, 0x34474942}  (as UINT32-LE)
file_size == actual file size
header_size >= 16
```

---

## 8. Filename Hashing Algorithms

All algorithms perform these preprocessing steps:

1. Convert filename to UPPERCASE
2. Replace forward slashes with backslashes: `'/'` → `'\'`
3. Apply game-specific hash function

### 8.1 TD/RA Hash (Rotate-Add)

**Used by:** game_td, game_ra

```
function hash_td_ra(name: string) -> uint32:
    name = uppercase(name)
    name = replace(name, '/', '\')

    id: uint32 = 0
    i: int = 0
    length: int = len(name)

    while i < length:
        a: uint32 = 0
        for j = 0 to 3:
            a = a >> 8
            if i < length:
                a = a | (byte(name[i]) << 24)
                i = i + 1
        id = rotate_left_32(id, 1) + a

    return id

function rotate_left_32(value: uint32, bits: int) -> uint32:
    return ((value << bits) | (value >> (32 - bits))) AND 0xFFFFFFFF
```

### 8.2 TS/RA2 Hash (CRC32 with Padding)

**Used by:** game_ts, game_ra2, game_ra2_yr

```
function hash_ts_ra2(name: string) -> uint32:
    name = uppercase(name)
    name = replace(name, '/', '\')

    length: int = len(name)
    remainder: int = length AND 3         ; length mod 4

    if remainder != 0:
        ; Pad to multiple of 4 bytes
        base: int = length AND (NOT 3)    ; round down to multiple of 4
        name = name + chr(remainder)      ; append remainder as byte value
        pad_char: char = name[base]       ; character at position 'base'
        pad_count: int = 3 - remainder
        name = name + (pad_char * pad_count)

    return crc32(name)
```

**Example:**

```
Input: "RULES.INI" (length 9)
remainder = 9 AND 3 = 1
base = 9 AND (NOT 3) = 8
pad_byte = chr(1) = '\x01'
pad_char = name[8] = 'I'
pad_count = 3 - 1 = 2
Padded: "RULES.INI\x01II" (length 12)
Return: crc32("RULES.INI\x01II")
```

### 8.3 RG/Generals Hash (Direct CRC32)

**Used by:** game_rg, game_gr, game_gr_zh

```
function hash_rg(name: string) -> uint32:
    name = uppercase(name)
    name = replace(name, '/', '\')
    return crc32(name)
```

### 8.4 CRC32 Specification

Standard CRC-32 (ISO 3309, ITU-T V.42, PKZIP, Ethernet, PNG):

| Parameter      | Value                                    |
|----------------|------------------------------------------|
| Polynomial     | 0x04C11DB7 (normal) / 0xEDB88320 (reflected) |
| Initial Value  | 0xFFFFFFFF                               |
| Reflect Input  | Yes (LSB first)                          |
| Reflect Output | Yes                                      |
| Final XOR      | 0xFFFFFFFF                               |

Also known as: CRC-32/ISO-HDLC, CRC-32/V-42, CRC-32/XZ

Reference implementations: zlib `crc32()`, Boost.CRC `crc_32_type`

---

## 9. Encryption

### 9.1 RSA Key Derivation

The 80-byte `key_source` at file offset 4 is RSA-decrypted to produce a 56-byte Blowfish key.

#### Public Key

The public key is stored as a Base64-encoded ASN.1 DER INTEGER.

**Base64 String:**
```
AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V
```

**Base64 Decoding Table (standard alphabet):**
```
'A'-'Z' -> 0-25
'a'-'z' -> 26-51
'0'-'9' -> 52-61
'+'     -> 62
'/'     -> 63
```

**Decoded Bytes (42 bytes, hex):**
```
02 28 51 BC DA 08 6D 39 FC E4 56 51 60 D6 51 71
3F A2 E8 AA 54 FA 66 82 B0 4A AB DD 0E 6A F8 B0
C1 E6 D1 FB 4F 3D AA 43 7F 15
```

**ASN.1 DER Structure:**
```
Byte 0:      0x02 = INTEGER tag
Byte 1:      0x28 = Length (40 bytes)
Bytes 2-41:  Modulus value (big-endian, 40 bytes)
```

#### RSA Parameters

**Modulus n (40 bytes, 320 bits, big-endian hex):**
```
51 BC DA 08 6D 39 FC E4 56 51 60 D6 51 71 3F A2
E8 AA 54 FA 66 82 B0 4A AB DD 0E 6A F8 B0 C1 E6
D1 FB 4F 3D AA 43 7F 15
```

**Exponent e:**
```
65537 (0x10001)
```

**Key bit length:**
```
pubkey.len = 319 (bit_length(n) - 1)
```

#### Block Sizes

```
a = (pubkey.len - 1) / 8 = (319 - 1) / 8 = 39
Input block size:  a + 1 = 40 bytes
Output block size: a     = 39 bytes
Total input:       80 bytes = 2 blocks
Total output:      78 bytes (first 56 used as Blowfish key)
```

#### Byte Order Conventions

- **File data (key_source):** byte array, index 0 = first byte in file
- **Bignum representation:** array of uint32_t, LITTLE-ENDIAN internally
  - n[0] contains least significant 32 bits
  - Byte 0 of input maps to low byte of n[0]
- **RSA modulus in ASN.1:** BIG-ENDIAN (MSB first)
  - Must be byte-reversed when loading into little-endian bignum

#### Algorithm

```
function get_blowfish_key(key_source: byte[80]) -> byte[56]:
    ; Initialize public key (once, can be cached)
    n = load_modulus()        ; See "Modulus n" above, byte-reverse to LE
    e = 65537

    ; Process in 40-byte blocks
    output: byte[78]
    out_pos = 0

    for block = 0 to 1:
        ; Read 40 bytes as little-endian bignum
        input_block = key_source[block * 40 : block * 40 + 40]
        m = bytes_to_bignum_le(input_block, 40)

        ; RSA operation: c = m^e mod n
        c = modular_exponentiation(m, e, n)

        ; Write 39 bytes as little-endian
        output[out_pos : out_pos + 39] = bignum_to_bytes_le(c, 39)
        out_pos = out_pos + 39

    ; Return first 56 bytes
    return output[0:56]


function bytes_to_bignum_le(data: byte[], length: int) -> bignum:
    ; Convert byte array to bignum, little-endian
    ; Byte 0 is LSB, byte length-1 is toward MSB
    result = 0
    for i = length - 1 down to 0:
        result = (result << 8) | data[i]
    return result


function bignum_to_bytes_le(n: bignum, length: int) -> byte[]:
    ; Convert bignum to byte array, little-endian
    result: byte[length]
    for i = 0 to length - 1:
        result[i] = n AND 0xFF
        n = n >> 8
    return result


function modular_exponentiation(base, exp, mod) -> bignum:
    ; Standard square-and-multiply algorithm
    ; result = (base ^ exp) mod mod
    result = 1
    base = base mod mod
    while exp > 0:
        if (exp AND 1) == 1:
            result = (result * base) mod mod
        exp = exp >> 1
        base = (base * base) mod mod
    return result
```

#### Implementation Notes

- The XCC implementation uses a custom bignum library with 64 uint32_t words
- Standard big-integer libraries (GMP, OpenSSL, Python int, etc.) can be used
- Ensure correct byte order when converting between bytes and bignums

### 9.2 Blowfish Cipher

Standard Blowfish cipher (Bruce Schneier, 1993).

| Parameter  | Value                                |
|------------|--------------------------------------|
| Key size   | 56 bytes (448 bits)                  |
| Block size | 8 bytes (64 bits)                    |
| Mode       | ECB (Electronic Codebook)            |
| Rounds     | 16                                   |

**S-Box and P-Array:** Standard Blowfish initialization constants.
Reference: https://www.schneier.com/academic/blowfish/

#### Byte Order Handling

The Blowfish cipher operates on 32-bit words internally. The C&C MIX format stores data in little-endian byte order, while standard Blowfish documentation assumes big-endian word representation.

The XCC implementation handles this by byte-swapping before and after cipher operations:

```
function decrypt_block(input: byte[8]) -> byte[8]:
    ; Step 1: Read as two little-endian uint32
    left  = input[0] | (input[1] << 8) | (input[2] << 16) | (input[3] << 24)
    right = input[4] | (input[5] << 8) | (input[6] << 16) | (input[7] << 24)

    ; Step 2: Byte-swap to big-endian for Blowfish core
    left  = bswap32(left)
    right = bswap32(right)

    ; Step 3: Apply standard Blowfish decryption (16 rounds)
    (left, right) = blowfish_decrypt_words(left, right)

    ; Step 4: Byte-swap back to little-endian
    left  = bswap32(left)
    right = bswap32(right)

    ; Step 5: Write as two little-endian uint32
    output: byte[8]
    output[0] = left AND 0xFF
    output[1] = (left >> 8) AND 0xFF
    output[2] = (left >> 16) AND 0xFF
    output[3] = (left >> 24) AND 0xFF
    output[4] = right AND 0xFF
    output[5] = (right >> 8) AND 0xFF
    output[6] = (right >> 16) AND 0xFF
    output[7] = (right >> 24) AND 0xFF
    return output


function bswap32(x: uint32) -> uint32:
    ; Reverse byte order: 0xAABBCCDD -> 0xDDCCBBAA
    return ((x >> 24) AND 0x000000FF) |
           ((x >> 8)  AND 0x0000FF00) |
           ((x << 8)  AND 0x00FF0000) |
           ((x << 24) AND 0xFF000000)
```

**Alternative Interpretation:**
The Blowfish cipher in MIX files operates on 32-bit words in big-endian byte order, while the surrounding file format is little-endian. The byte swaps convert between these representations.

**Platform Independence:**
The explicit byte swapping makes the format platform-independent. A correctly implemented decoder produces identical output regardless of host byte order.

#### Encrypted Regions in RA Format

**Encrypted:**
- Offset 84-91: 8 bytes (header)
- Offset 92 to 92+P-1: P bytes (index)

**NOT Encrypted:**
- Offset 0-83: Flags + key source
- Body data
- Checksum (if present)

---

## 10. Checksums

When flags bit 16 (0x00010000) is set, a 20-byte value is appended to the file (last 20 bytes before EOF).

### Important Implementation Notes

1. **The XCC reference implementation DOES NOT compute or verify checksums.**
   - When reading: checksum bytes are ignored (only used for size validation)
   - When writing: XCC always writes flags = 0 (no checksum)

2. **The checksum is believed to be SHA-1** based on:
   - Size matches SHA-1 output (160 bits = 20 bytes)
   - Common practice in late 1990s software
   - Community documentation refers to it as SHA-1

3. **The scope of the hash (what data is hashed) is UNSPECIFIED.**
   No verification code exists in XCC to determine this definitively.

   Likely candidates (in order of probability):
   - Body data only
   - Header + Index + Body (everything except checksum)
   - Index only

### Recommendations for Implementers

**For Reading:**
- Ignore the checksum entirely (follow XCC behavior)
- Use checksum presence only for file size validation:
  ```
  expected_size = header_size + index_size + body_size + (has_checksum ? 20 : 0)
  ```

**For Writing:**
- **Option A (recommended):** Do not set checksum flag, omit checksum.
  This is what XCC does and ensures maximum compatibility.

- **Option B:** If checksum is required, compute SHA-1 over body data and append. Set flags bit 16. This is speculative but reasonable.

### SHA-1 Algorithm

If implementing: Standard SHA-1 as specified in FIPS PUB 180-1.
Output: 20 bytes, written directly to file (no byte swapping).

---

## 11. Shared Structures

### Index Entry

Used by TD, RA, and TS formats:

```abnf
index-entry         = id offset entry-size

id                  = UINT32-LE
                      ; Filename hash (algorithm depends on game type)

offset              = UINT32-LE
                      ; Offset relative to body start
                      ; See format-specific sections for absolute calculation

entry-size          = UINT32-LE
                      ; Size of file in bytes
```

Total size: 12 bytes per entry.

---

## 12. Primitive Types

```abnf
UINT16-LE           = 2OCTET
                      ; 16-bit unsigned integer, little-endian
                      ; Value = byte[0] + (byte[1] << 8)

UINT32-LE           = 4OCTET
                      ; 32-bit unsigned integer, little-endian
                      ; Value = byte[0] + (byte[1] << 8) +
                      ;         (byte[2] << 16) + (byte[3] << 24)

UINT32-BE           = 4OCTET
                      ; 32-bit unsigned integer, big-endian
                      ; Value = (byte[0] << 24) + (byte[1] << 16) +
                      ;         (byte[2] << 8) + byte[3]

OCTET               = %x00-FF
                      ; Single byte (0-255)
```

---

## 13. Constants Reference

### Flags

| Name           | Value        | Description                        |
|----------------|-------------:|------------------------------------|
| mix_checksum   | 0x00010000   | Bit 16: SHA-1 checksum present     |
| mix_encrypted  | 0x00020000   | Bit 17: Blowfish encrypted (RA)    |

### Sizes

| Name               | Value | Description                      |
|--------------------|------:|----------------------------------|
| cb_mix_key         | 56    | Blowfish key size (bytes)        |
| cb_mix_key_source  | 80    | RSA key source size (bytes)      |
| cb_mix_checksum    | 20    | SHA-1 checksum size (bytes)      |

### Magic Values

| Name       | Hex Value    | ASCII  | Format  |
|------------|-------------:|--------|---------|
| mix_rg_id  | 0x3158494D   | "MIX1" | MIX-RG  |
| big_id     | 0x46474942   | "BIGF" | BIG     |
| big4_id    | 0x34474942   | "BIG4" | BIG     |

Note: Magic values shown as UINT32-LE interpretation of the ASCII bytes.

### Limits

| Name         | Value | Description                        |
|--------------|------:|------------------------------------|
| max_c_files  | 4095  | Maximum file count (12-bit limit)  |

### Special IDs

| Name           | Value        | Description                         |
|----------------|-------------:|-------------------------------------|
| ts_marker_id   | 0x763C81DD   | If present in index, indicates TS   |

---

## 14. Game Type Detection Heuristics

After determining format (TD/RA/TS), game subtype can be inferred:

**For TD Format:**
```
-> game_td (Tiberian Dawn)
```

**For RA Format (encrypted):**
```
Default: game_ra

If any index entry has id == 0x763C81DD:
    -> game_ts (despite being encrypted)

Else if all index offsets are 16-byte aligned ((offset AND 0x0F) == 0):
    -> game_ts

Else:
    -> game_ra
```

**For TS Format (unencrypted):**
```
Default: game_ts
Game subtype (TS vs RA2 vs YR) determined by context or filename database.
```

Note: game_ts, game_ra2, and game_ra2_yr all use the same file format and hash algorithm. The distinction matters only for filename database lookups.

---

## 15. Implementation Notes

### Reading a MIX File

1. Detect format using Section 2 algorithm
2. Parse header and index according to format
3. For RA format: decrypt key source, then header/index (Section 9)
4. Adjust stored offsets to absolute positions using format-specific formula
5. Files can be extracted using (absolute_offset, size) pairs from index

### Filename Recovery

Original filenames are NOT stored in TD/RA/TS formats (only hashes).

Filenames must be recovered using:
- Known filename databases (from game files or community resources)
- Brute-force hash matching
- "local mix database.dat" file (XCC convention, stored inside MIX)

MIX-RG and BIG formats include plaintext filenames in the tailer/index.

### File Ordering

- Index entries are not required to be sorted
- Files in body are not required to be in index order
- Files may have gaps between them (especially in TS format due to alignment)
- When writing, any ordering is valid

### Edge Cases

- Files with size = 0 are valid
- Empty archives (c_files = 0) are NOT valid for TD format
- Maximum file size limited by body_size field (UINT32, ~4GB)
- Nested MIX files are valid (inner MIX is just another file entry)

### Writing MIX Files

The XCC implementation writes TS-style format (unencrypted) by default:

```
Offset 0:   flags = 0x00000000 (4 bytes)
Offset 4:   c_files (2 bytes)
Offset 6:   body_size (4 bytes)
Offset 10:  index entries
Offset H:   body (concatenated files)
```

No checksum is written. The "local mix database.dat" file is automatically added to store filename mappings for XCC compatibility.

---

## Appendix: File Format Summary Table

| Format | Header Size | Index Entry Size | Has Filenames | Encrypted |
|--------|------------:|-----------------:|:-------------:|:---------:|
| TD     | 6           | 12               | No (hashed)   | No        |
| RA     | 92 + P      | 12               | No (hashed)   | Yes       |
| TS     | 10          | 12               | No (hashed)   | No        |
| MIX-RG | 16          | 12               | Yes (tailer)  | No        |
| BIG    | 16          | 8 + filename     | Yes (inline)  | No        |

Where P = ((c_files * 12) + 5) AND NOT 7

---

*Document generated from XCC Utilities source code analysis.*
*XCC Utilities Copyright (C) 2000 Olaf van der Spek, GPL v3.*
