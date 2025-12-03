#pragma once

#include <westwood/error.h>
#include <westwood/pal.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

struct WsaFrameInfo {
    uint32_t offset;      // Frame data offset (24-bit from file)
    uint32_t size;        // Frame data size
    // Frame format flags (0x80=LCW, 0x40=XOR ref, 0x20=XOR prev)
    uint8_t format;
    uint32_t ref_offset;  // Reference frame offset (for XOR delta)
    uint8_t ref_format;   // Reference frame format
};

struct WsaInfo {
    uint16_t frame_count;
    uint16_t width;
    uint16_t height;
    uint16_t delta_size;    // Decompression buffer size
    uint32_t palette_offset;
    bool     has_palette;
    bool     has_loop;      // Has loop frame at end
    uint32_t file_size;
};

class WsaReader {
public:
    static Result<std::unique_ptr<WsaReader>> open(const std::string& path);
    static Result<std::unique_ptr<WsaReader>> open(
        std::span<const uint8_t> data);

    ~WsaReader();

    const WsaInfo& info() const;
    const std::vector<WsaFrameInfo>& frames() const;

    // Get embedded palette (returns nullptr if no palette)
    const std::array<Color, 256>* palette() const;

    // Decode a single frame to palette indices
    // delta_buffer is used for frame-to-frame delta decoding
    Result<std::vector<uint8_t>> decode_frame(
        size_t frame_index,
        std::vector<uint8_t>& delta_buffer) const;

    // Decode all frames to palette indices
    Result<std::vector<std::vector<uint8_t>>> decode_all_frames() const;

private:
    WsaReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
