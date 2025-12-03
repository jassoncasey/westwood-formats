#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

// Font format versions
enum class FntFormat {
    V2,            // 1-bit monochrome, 128 chars max (BattleTech, Eye of the Beholder)
    V3,            // 4-bit grayscale, variable chars (TD, RA, Kyrandia, Lands of Lore)
    V4,            // 8-bit grayscale, variable chars (TS, RA2, Lands of Lore 3)
    BitFont,       // 1-bit monochrome, RA2/Nox style (header "FoNt" or "tNoF")
    UnicodeBitFont // 1-bit monochrome, Unicode (header "fonT", up to 65K glyphs)
};

struct FntGlyphInfo {
    uint8_t  width;
    uint8_t  height;
    uint8_t  y_offset;  // vertical offset from baseline
    uint32_t offset;    // offset into data block (32-bit for Unicode fonts)
};

struct FntInfo {
    FntFormat format;
    uint16_t glyph_count;
    uint8_t  height;       // max glyph height
    uint8_t  max_width;    // max glyph width
    uint16_t first_char;   // first character (usually 0 or 32)
    uint16_t last_char;    // last character (up to 65535 for Unicode)
    uint8_t  bits_per_pixel; // 1, 4, or 8
    uint32_t stride;       // BitFont: bytes per scanline
    uint32_t data_size;
    uint32_t file_size;
};

class FntReader {
public:
    static Result<std::unique_ptr<FntReader>> open(const std::string& path);
    static Result<std::unique_ptr<FntReader>> open(std::span<const uint8_t> data);

    ~FntReader();

    const FntInfo& info() const;
    const std::vector<FntGlyphInfo>& glyphs() const;

    // Decode a single glyph by index to grayscale values (0-255)
    // Returns empty vector for glyphs with width 0
    std::vector<uint8_t> decode_glyph(size_t glyph_index) const;

    // Decode a glyph by Unicode code point (for UnicodeBitFont format)
    // For other formats, treats code_point as glyph index
    // Returns empty vector if code point not found
    std::vector<uint8_t> decode_char(uint16_t code_point) const;

    // Get glyph index for a Unicode code point (UnicodeBitFont only)
    // Returns -1 if not found
    int32_t glyph_index_for_char(uint16_t code_point) const;

    // Get the source filename (if available)
    const std::string& source_filename() const;

private:
    FntReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
