#pragma once

#include <westwood/error.h>
#include <westwood/pal.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

// CPS - Compressed Picture (320x200 indexed)
//
// Simple container for a 320x200 8-bit image with optional embedded palette.
// Image data may be LCW compressed (Format80) or uncompressed.

struct CpsInfo {
    uint16_t file_size;        // from header (actual size - 2)
    uint16_t compression;      // 0=none, 4=LCW
    uint32_t uncomp_size;      // decompressed size (0xFA00 = 64000 for 320x200)
    uint16_t palette_size;     // 768 if embedded palette, 0 otherwise
    uint16_t width;            // always 320
    uint16_t height;           // always 200
    bool     has_palette;
};

class CpsReader {
public:
    static Result<std::unique_ptr<CpsReader>> open(const std::string& path);
    static Result<std::unique_ptr<CpsReader>> open(std::span<const uint8_t> data);

    ~CpsReader();

    const CpsInfo& info() const;

    // Get decompressed pixel data (320x200 = 64000 bytes)
    const std::vector<uint8_t>& pixels() const;

    // Get embedded palette (if present)
    const std::array<Color, 256>* palette() const;

private:
    CpsReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
