#include <westwood/aud.h>
#include <westwood/io.h>

#include <algorithm>
#include <cstring>

namespace wwd {

// IMA ADPCM step table (standard)
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

// IMA ADPCM index adjustment table
static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

// Westwood ADPCM (WS-SND1) lookup tables
static const int8_t ws_step_2bit[4] = { -2, -1, 0, 1 };
static const int8_t ws_step_4bit[16] = {
    -9, -8, -6, -5, -4, -3, -2, -1,
     0,  1,  2,  3,  4,  5,  6,  8
};

struct AudReaderImpl {
    AudInfo info{};
    std::vector<uint8_t> data;  // Full file data for decoding
};

struct AudReader::Impl : AudReaderImpl {};

AudReader::AudReader() : impl_(std::make_unique<Impl>()) {}
AudReader::~AudReader() = default;

const AudInfo& AudReader::info() const { return impl_->info; }

float AudReader::duration() const {
    if (impl_->info.sample_rate == 0) return 0.0f;
    return float(sample_count()) / float(impl_->info.sample_rate);
}

uint32_t AudReader::sample_count() const {
    // Output is always 16-bit
    uint32_t bytes_per_sample = 2 * impl_->info.channels;
    if (bytes_per_sample == 0) return 0;
    return impl_->info.uncompressed_size / bytes_per_sample;
}

// Decode IMA ADPCM
static void decode_ima_adpcm(const uint8_t* src, size_t src_size,
                             int16_t* dst, size_t& dst_samples,
                             int& predictor, int& step_index) {
    dst_samples = 0;

    for (size_t i = 0; i < src_size; ++i) {
        uint8_t byte = src[i];

        // Process low nibble then high nibble
        for (int nibble_idx = 0; nibble_idx < 2; ++nibble_idx) {
            int nibble = (nibble_idx == 0) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);

            int step = ima_step_table[step_index];
            int diff = step >> 3;

            if (nibble & 1) diff += step >> 2;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 4) diff += step;
            if (nibble & 8) diff = -diff;

            predictor += diff;
            predictor = std::clamp(predictor, -32768, 32767);

            dst[dst_samples++] = static_cast<int16_t>(predictor);

            step_index += ima_index_table[nibble];
            step_index = std::clamp(step_index, 0, 88);
        }
    }
}

// Decode Westwood ADPCM (WS-SND1) chunk
// Produces 8-bit unsigned PCM, which we convert to 16-bit signed for output
static void decode_westwood_adpcm(const uint8_t* src, size_t src_size,
                                   int16_t* dst, size_t& dst_samples) {
    dst_samples = 0;
    if (src_size == 0) return;

    size_t pos = 0;
    int sample = 0x80;  // Initial center value for 8-bit unsigned

    // Helper to output sample (convert 8-bit unsigned to 16-bit signed)
    auto output = [&](int s) {
        s = std::clamp(s, 0, 255);
        sample = s;
        // Convert 8-bit unsigned (0-255) to 16-bit signed (-32768 to 32767)
        dst[dst_samples++] = static_cast<int16_t>((s - 128) << 8);
    };

    while (pos < src_size) {
        uint8_t cmd = src[pos++];
        int mode = (cmd >> 6) & 0x03;
        int count = cmd & 0x3F;

        switch (mode) {
            case 0:  // Mode 0: 2-bit ADPCM
                for (int i = 0; i <= count && pos < src_size; ++i) {
                    uint8_t packed = src[pos++];
                    // 4 samples per byte, 2 bits each, LSB first
                    for (int j = 0; j < 4; ++j) {
                        int delta_idx = (packed >> (j * 2)) & 0x03;
                        int delta = ws_step_2bit[delta_idx];
                        output(sample + delta);
                    }
                }
                break;

            case 1:  // Mode 1: 4-bit ADPCM
                for (int i = 0; i <= count && pos < src_size; ++i) {
                    uint8_t packed = src[pos++];
                    // 2 samples per byte, 4 bits each
                    // Low nibble first
                    int delta = ws_step_4bit[packed & 0x0F];
                    output(sample + delta);
                    // High nibble second
                    delta = ws_step_4bit[packed >> 4];
                    output(sample + delta);
                }
                break;

            case 2:  // Mode 2: Raw bytes or 5-bit signed delta
                if (count & 0x20) {
                    // Bit 5 set: 5-bit signed delta (bits 4-0)
                    int delta = count & 0x1F;
                    // Sign-extend 5-bit to full int
                    if (delta & 0x10) {
                        delta |= ~0x1F;  // Sign extend
                    }
                    output(sample + delta);
                } else {
                    // Bit 5 clear: copy (count+1) literal bytes
                    for (int i = 0; i <= count && pos < src_size; ++i) {
                        output(src[pos++]);
                    }
                }
                break;

            case 3:  // Mode 3: Silence (RLE) - repeat previous sample
                for (int i = 0; i <= count; ++i) {
                    output(sample);
                }
                break;
        }
    }
}

Result<std::vector<int16_t>> AudReader::decode() const {
    const auto& info = impl_->info;

    if (info.codec == AudCodec::Unknown) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "Unknown AUD codec"));
    }

    // Audio data starts after 12-byte header
    if (impl_->data.size() < 12) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "AUD file too small"));
    }

    const uint8_t* audio_data = impl_->data.data() + 12;
    size_t audio_size = impl_->data.size() - 12;

    // Estimate output size (may be slightly larger than actual)
    size_t max_samples = info.uncompressed_size / (info.bits / 8);
    std::vector<int16_t> output;
    output.reserve(max_samples);

    if (info.codec == AudCodec::IMAADPCM) {
        // IMA ADPCM - process chunks
        // Each chunk has: 2 bytes compressed size, 2 bytes output size, 4 bytes state, data
        size_t pos = 0;

        while (pos + 8 < audio_size) {
            uint16_t comp_size = audio_data[pos] | (audio_data[pos + 1] << 8);
            uint16_t out_size = audio_data[pos + 2] | (audio_data[pos + 3] << 8);

            // Initial predictor and step index
            int predictor = static_cast<int16_t>(audio_data[pos + 4] | (audio_data[pos + 5] << 8));
            int step_index = audio_data[pos + 6];
            step_index = std::clamp(step_index, 0, 88);

            pos += 8;

            if (pos + comp_size - 4 > audio_size) break;

            // Decode chunk
            size_t chunk_samples = 0;
            std::vector<int16_t> chunk_out(out_size);

            decode_ima_adpcm(audio_data + pos, comp_size - 4,
                           chunk_out.data(), chunk_samples,
                           predictor, step_index);

            output.insert(output.end(), chunk_out.begin(),
                         chunk_out.begin() + std::min(chunk_samples, static_cast<size_t>(out_size)));

            pos += comp_size - 4;
        }
    } else if (info.codec == AudCodec::WestwoodADPCM) {
        // Westwood ADPCM - process chunks
        size_t pos = 0;

        while (pos + 4 < audio_size) {
            uint16_t comp_size = audio_data[pos] | (audio_data[pos + 1] << 8);
            uint16_t out_size = audio_data[pos + 2] | (audio_data[pos + 3] << 8);

            pos += 4;

            if (pos + comp_size > audio_size) break;

            // Decode chunk
            size_t chunk_samples = 0;
            std::vector<int16_t> chunk_out(out_size * 2);  // Extra space for safety

            decode_westwood_adpcm(audio_data + pos, comp_size,
                                 chunk_out.data(), chunk_samples);

            output.insert(output.end(), chunk_out.begin(),
                         chunk_out.begin() + std::min(chunk_samples, static_cast<size_t>(out_size)));

            pos += comp_size;
        }
    }

    return output;
}

static Result<void> parse_aud(AudReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // AUD header: 12 bytes
    // Offset  Size  Description
    // 0       2     Sample rate
    // 2       4     Uncompressed size
    // 6       4     Compressed size (output size for old format)
    // 10      1     Flags (bit 0 = stereo, bit 1 = 16-bit)
    // 11      1     Compression type (1 = WW ADPCM, 99 = IMA ADPCM)

    if (data.size() < 12) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "AUD file too small"));
    }

    const uint8_t* p = data.data();

    impl.info.sample_rate = read_u16(p);
    impl.info.uncompressed_size = read_u32(p + 2);
    impl.info.compressed_size = read_u32(p + 6);

    uint8_t flags = p[10];
    impl.info.channels = (flags & 0x01) ? 2 : 1;
    impl.info.bits = (flags & 0x02) ? 16 : 8;

    uint8_t comp_type = p[11];
    switch (comp_type) {
        case 1:
            impl.info.codec = AudCodec::WestwoodADPCM;
            break;
        case 99:
            impl.info.codec = AudCodec::IMAADPCM;
            break;
        default:
            impl.info.codec = AudCodec::Unknown;
            break;
    }

    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Store data for later decoding
    impl.data.assign(data.begin(), data.end());

    return {};
}

Result<std::unique_ptr<AudReader>> AudReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());
    return open(std::span(*data));
}

Result<std::unique_ptr<AudReader>> AudReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<AudReader>(new AudReader());
    auto result = parse_aud(*reader->impl_, data);
    if (!result) return std::unexpected(result.error());
    return reader;
}

} // namespace wwd
