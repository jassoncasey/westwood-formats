#pragma once

#include <westwood/error.h>

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

private:
    WsaReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
