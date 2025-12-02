#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

enum class ShpFormat { TD, TS };  // TD/RA vs TS/RA2

enum class ShpFrameFormat : uint8_t {
    LCW     = 0x80,  // LCW compressed
    XORPrev = 0x40,  // XOR with previous frame
    XORLCW  = 0x20   // XOR with reference frame, LCW compressed
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
    uint32_t  file_size;
};

class ShpReader {
public:
    static Result<std::unique_ptr<ShpReader>> open(const std::string& path);
    static Result<std::unique_ptr<ShpReader>> open(std::span<const uint8_t> data);

    ~ShpReader();

    const ShpInfo& info() const;
    const std::vector<ShpFrameInfo>& frames() const;

private:
    ShpReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
