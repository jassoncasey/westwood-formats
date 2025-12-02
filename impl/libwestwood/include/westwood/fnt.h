#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

struct FntGlyphInfo {
    uint8_t  width;
    uint8_t  height;
    uint16_t offset;   // offset into data block
};

struct FntInfo {
    uint16_t glyph_count;
    uint8_t  height;       // max glyph height
    uint8_t  max_width;    // max glyph width
    uint8_t  first_char;   // first ASCII character (usually 32 = space)
    uint8_t  last_char;    // last ASCII character
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

private:
    FntReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
