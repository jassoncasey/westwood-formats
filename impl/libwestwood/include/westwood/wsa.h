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
    uint32_t offset;
    uint32_t size;
};

struct WsaInfo {
    uint16_t frame_count;
    uint16_t width;
    uint16_t height;
    uint32_t delta_size;    // largest delta frame
    uint32_t palette_offset;
    bool     has_palette;
    bool     has_loop;      // frame 0 has delta (looping animation)
    uint32_t file_size;
};

class WsaReader {
public:
    static Result<std::unique_ptr<WsaReader>> open(const std::string& path);
    static Result<std::unique_ptr<WsaReader>> open(std::span<const uint8_t> data);

    ~WsaReader();

    const WsaInfo& info() const;
    const std::vector<WsaFrameInfo>& frames() const;

    // Get embedded palette (returns nullptr if no palette)
    const std::array<Color, 256>* palette() const;

    // Decode a single frame to palette indices
    // delta_buffer is used for frame-to-frame delta decoding
    Result<std::vector<uint8_t>> decode_frame(size_t frame_index,
                                               std::vector<uint8_t>& delta_buffer) const;

    // Decode all frames to palette indices
    Result<std::vector<std::vector<uint8_t>>> decode_all_frames() const;

private:
    WsaReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
