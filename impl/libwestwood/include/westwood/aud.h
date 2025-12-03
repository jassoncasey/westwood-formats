#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

enum class AudCodec {
    Unknown,
    WestwoodADPCM,  // SND1 / AUD type 1
    IMAADPCM        // SND2 / AUD type 99
};

struct AudInfo {
    uint32_t sample_rate;
    uint8_t  channels;       // 1 = mono, 2 = stereo
    uint8_t  bits;           // output bits (8 or 16)
    AudCodec codec;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    uint32_t file_size;
};

class AudReader {
public:
    static Result<std::unique_ptr<AudReader>> open(const std::string& path);
    static Result<std::unique_ptr<AudReader>> open(
        std::span<const uint8_t> data);

    ~AudReader();

    const AudInfo& info() const;

    // Derived properties
    float duration() const;
    uint32_t sample_count() const;

    // Decode audio to 16-bit signed PCM samples
    // Returns interleaved samples for stereo (L,R,L,R,...)
    Result<std::vector<int16_t>> decode() const;

private:
    AudReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
