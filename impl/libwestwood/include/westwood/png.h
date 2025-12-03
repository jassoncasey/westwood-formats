#pragma once

#include <westwood/export.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace wwd {

// PNG color types
enum class PngColorType : uint8_t {
    Grayscale = 0,
    Rgb = 2,
    Indexed = 3,
    GrayscaleAlpha = 4,
    Rgba = 6
};

// Write RGBA PNG to stream
// rgba: pointer to RGBA pixel data (4 bytes per pixel)
// width, height: image dimensions
// Returns true on success
WWD_API bool write_png_rgba(
    std::ostream& out,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height);

// Write RGBA PNG to file
WWD_API bool write_png_rgba(
    const std::string& path,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height);

// Write Grayscale+Alpha PNG to stream
// ga: pointer to GA pixel data (2 bytes per pixel)
WWD_API bool write_png_ga(
    std::ostream& out,
    const uint8_t* ga,
    uint32_t width,
    uint32_t height);

// Write Grayscale+Alpha PNG to file
WWD_API bool write_png_ga(
    const std::string& path,
    const uint8_t* ga,
    uint32_t width,
    uint32_t height);

// CRC32 (PNG polynomial)
WWD_API uint32_t crc32_png(const uint8_t* data, size_t len);

// Adler-32 checksum (for zlib)
WWD_API uint32_t adler32(const uint8_t* data, size_t len);

} // namespace wwd
