#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace wwd {

struct VqaHeader {
    uint16_t version;
    uint16_t flags;
    uint16_t frame_count;
    uint16_t width;
    uint16_t height;
    uint8_t  block_w;
    uint8_t  block_h;
    uint8_t  frame_rate;
    uint8_t  cb_parts;      // codebook partition count
    uint16_t colors;        // palette entries (0 for HiColor)
    uint16_t max_blocks;    // max codebook entries
    uint16_t offset_x;
    uint16_t offset_y;
    uint16_t max_vpt_size;
    uint16_t sample_rate;
    uint8_t  channels;
    uint8_t  bits;
};

struct VqaAudioInfo {
    uint32_t sample_rate;
    uint8_t  channels;
    uint8_t  bits;
    bool     has_audio;
    bool     compressed;    // SND2 (IMA ADPCM) vs SND0
};

struct VqaInfo {
    VqaHeader header;
    VqaAudioInfo audio;
    uint64_t file_size;
};

class VqaReader {
public:
    static Result<std::unique_ptr<VqaReader>> open(const std::string& path);
    static Result<std::unique_ptr<VqaReader>> open(std::span<const uint8_t> data);

    ~VqaReader();

    const VqaInfo& info() const;

    // Derived properties
    float duration() const;
    bool is_hicolor() const;
    uint32_t block_count() const;

private:
    VqaReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
