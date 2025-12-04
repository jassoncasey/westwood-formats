#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

enum class ShpFormat { TD, TS, D2 };  // TD/RA, TS/RA2, Dune 2

enum class ShpFrameFormat : uint8_t {
    LCW     = 0x80,  // LCW compressed
    XORPrev = 0x40,  // XOR with previous frame
    XORLCW  = 0x20   // XOR with reference frame, LCW compressed
};

// D2 format flags (per-frame)
enum class D2FormatFlags : uint16_t {
    PaletteTable        = 1,  // Has color lookup table
    NotLCWCompressed    = 2,  // Data is RLE-only, not LCW+RLE
    VariableLengthTable = 4   // Palette table size is variable
};

struct ShpFrameInfo {
    uint16_t width;
    uint16_t height;
    int16_t  offset_x;      // anchor X (signed)
    int16_t  offset_y;      // anchor Y (signed)
    uint8_t  format;        // ShpFrameFormat flags
    uint32_t data_offset;   // file offset to frame data
    uint32_t data_size;     // compressed size
    uint32_t ref_offset;    // reference frame offset (for XORLCW)
};

struct ShpInfo {
    ShpFormat format;
    uint16_t  frame_count;
    uint16_t  max_width;
    uint16_t  max_height;
    uint16_t  delta_buffer_size;  // largest frame uncompressed size
    uint32_t  file_size;
    uint32_t  lcw_frames;   // count of LCW base frames
    uint32_t  xor_frames;   // count of XOR delta frames
    uint8_t   offset_size;  // D2: 2 or 4 byte offsets (0 for non-D2)
};

class ShpReader {
public:
    static Result<std::unique_ptr<ShpReader>> open(const std::string& path);
    static Result<std::unique_ptr<ShpReader>> open(
        std::span<const uint8_t> data);

    ~ShpReader();

    const ShpInfo& info() const;
    const std::vector<ShpFrameInfo>& frames() const;

    // Decode a frame to palette indices (8-bit per pixel)
    // The delta buffer maintains state across frames for XOR delta decoding
    Result<std::vector<uint8_t>> decode_frame(size_t frame_index,
        std::vector<uint8_t>& delta_buffer) const;

    // Convenience: decode all frames
    Result<std::vector<std::vector<uint8_t>>> decode_all_frames() const;

private:
    ShpReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
