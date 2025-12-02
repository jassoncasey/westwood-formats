#pragma once

#include <westwood/error.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace wwd {

struct Color {
    uint8_t r, g, b;
};

struct PalInfo {
    uint16_t entries;      // 256
    uint8_t  bit_depth;    // 6 (0-63) or 8 (0-255)
    uint32_t file_size;
};

class PalReader {
public:
    static Result<std::unique_ptr<PalReader>> open(const std::string& path);
    static Result<std::unique_ptr<PalReader>> open(std::span<const uint8_t> data);

    ~PalReader();

    const PalInfo& info() const;
    const std::array<Color, 256>& colors() const;

    // Get color scaled to 8-bit (0-255)
    Color color_8bit(uint8_t index) const;

private:
    PalReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
