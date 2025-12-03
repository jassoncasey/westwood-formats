#include <westwood/aud.h>
#include <westwood/io.h>

#include <algorithm>
#include <cstring>

namespace wwd {

// IMA ADPCM step table (standard)
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767
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
            int nibble = (nibble_idx == 0)
                ? (byte & 0x0F) : ((byte >> 4) & 0x0F);

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

// Westwood ADPCM decode state
struct WsAdpcmState {
    const uint8_t* src;
    size_t src_size;
    size_t pos = 0;
    int sample = 0x80;  // Center value for 8-bit unsigned
    int16_t* dst;
    size_t dst_samples = 0;
};

// Convert 8-bit unsigned to 16-bit signed and output
static void ws_output(WsAdpcmState& st, int s) {
    s = std::clamp(s, 0, 255);
    st.sample = s;
    st.dst[st.dst_samples++] = static_cast<int16_t>((s - 128) << 8);
}

// Mode 0: 2-bit ADPCM (4 samples per byte)
static void ws_mode_2bit(WsAdpcmState& st, int count) {
    for (int i = 0; i <= count && st.pos < st.src_size; ++i) {
        uint8_t packed = st.src[st.pos++];
        for (int j = 0; j < 4; ++j) {
            int delta = ws_step_2bit[(packed >> (j * 2)) & 0x03];
            ws_output(st, st.sample + delta);
        }
    }
}

// Mode 1: 4-bit ADPCM (2 samples per byte)
static void ws_mode_4bit(WsAdpcmState& st, int count) {
    for (int i = 0; i <= count && st.pos < st.src_size; ++i) {
        uint8_t packed = st.src[st.pos++];
        ws_output(st, st.sample + ws_step_4bit[packed & 0x0F]);
        ws_output(st, st.sample + ws_step_4bit[packed >> 4]);
    }
}

// Mode 2: Raw bytes or 5-bit signed delta
static void ws_mode_raw(WsAdpcmState& st, int count) {
    if (count & 0x20) {
        int delta = count & 0x1F;
        if (delta & 0x10) delta |= ~0x1F;
        ws_output(st, st.sample + delta);
    } else {
        for (int i = 0; i <= count && st.pos < st.src_size; ++i)
            ws_output(st, st.src[st.pos++]);
    }
}

// Mode 3: Silence (RLE)
static void ws_mode_rle(WsAdpcmState& st, int count) {
    for (int i = 0; i <= count; ++i) ws_output(st, st.sample);
}

// Decode Westwood ADPCM chunk
static void decode_westwood_adpcm(const uint8_t* src, size_t src_size,
                                   int16_t* dst, size_t& dst_samples) {
    dst_samples = 0;
    if (src_size == 0) return;

    WsAdpcmState st{src, src_size, 0, 0x80, dst, 0};

    while (st.pos < st.src_size) {
        uint8_t cmd = st.src[st.pos++];
        int mode = (cmd >> 6) & 0x03;
        int count = cmd & 0x3F;

        switch (mode) {
            case 0: ws_mode_2bit(st, count); break;
            case 1: ws_mode_4bit(st, count); break;
            case 2: ws_mode_raw(st, count); break;
            case 3: ws_mode_rle(st, count); break;
        }
    }
    dst_samples = st.dst_samples;
}

// DEAF chunk signature (0x0000DEAF in little-endian)
constexpr uint32_t DEAF_SIGNATURE = 0x0000DEAF;

// AUD chunk header (8 bytes)
struct AudChunkHeader {
    uint16_t comp_size;
    uint16_t out_size;
    uint32_t signature;
};

// Read and validate chunk header
static Result<AudChunkHeader> read_chunk_header(
    const uint8_t* data, size_t pos, size_t size)
{
    if (pos + 8 > size)
        return std::unexpected(make_error(ErrorCode::UnexpectedEof, "chunk"));
    AudChunkHeader h;
    h.comp_size = data[pos] | (data[pos + 1] << 8);
    h.out_size = data[pos + 2] | (data[pos + 3] << 8);
    h.signature = data[pos + 4] | (data[pos + 5] << 8) |
                  (data[pos + 6] << 16) | (data[pos + 7] << 24);
    if (h.signature != DEAF_SIGNATURE)
        return std::unexpected(make_error(ErrorCode::CorruptData, "DEAF"));
    return h;
}

// Decode IMA ADPCM chunks
static Result<void> decode_ima_chunks(
    const uint8_t* data, size_t size, std::vector<int16_t>& out)
{
    size_t pos = 0;
    int predictor = 0, step_index = 0;

    while (pos + 8 < size) {
        auto h = read_chunk_header(data, pos, size);
        if (!h) return std::unexpected(h.error());
        pos += 8;
        if (pos + h->comp_size > size) break;

        size_t chunk_samples = 0;
        std::vector<int16_t> chunk_out(h->out_size);
        decode_ima_adpcm(data + pos, h->comp_size, chunk_out.data(),
                         chunk_samples, predictor, step_index);
        auto n = std::min(chunk_samples, static_cast<size_t>(h->out_size));
        out.insert(out.end(), chunk_out.begin(), chunk_out.begin() + n);
        pos += h->comp_size;
    }
    return {};
}

// Decode Westwood ADPCM chunks
static Result<void> decode_ws_chunks(
    const uint8_t* data, size_t size, std::vector<int16_t>& out)
{
    size_t pos = 0;

    while (pos + 8 < size) {
        auto h = read_chunk_header(data, pos, size);
        if (!h) return std::unexpected(h.error());
        pos += 8;
        if (pos + h->comp_size > size) break;

        size_t chunk_samples = 0;
        std::vector<int16_t> chunk_out(h->out_size * 2);
        decode_westwood_adpcm(data + pos, h->comp_size, chunk_out.data(),
                              chunk_samples);
        auto n = std::min(chunk_samples, static_cast<size_t>(h->out_size));
        out.insert(out.end(), chunk_out.begin(), chunk_out.begin() + n);
        pos += h->comp_size;
    }
    return {};
}

Result<std::vector<int16_t>> AudReader::decode() const {
    const auto& info = impl_->info;

    if (info.codec == AudCodec::Unknown)
        return std::unexpected(make_error(ErrorCode::UnsupportedFormat, "AUD"));
    if (impl_->data.size() < 12)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "AUD"));

    const uint8_t* audio_data = impl_->data.data() + 12;
    size_t audio_size = impl_->data.size() - 12;

    std::vector<int16_t> output;
    output.reserve(info.uncompressed_size / (info.bits / 8));

    Result<void> r;
    if (info.codec == AudCodec::IMAADPCM)
        r = decode_ima_chunks(audio_data, audio_size, output);
    else if (info.codec == AudCodec::WestwoodADPCM)
        r = decode_ws_chunks(audio_data, audio_size, output);
    if (!r) return std::unexpected(r.error());

    return output;
}

// Decode compression type byte to codec enum
static Result<AudCodec> decode_codec(uint8_t comp_type) {
    switch (comp_type) {
        case 1:  return AudCodec::WestwoodADPCM;
        case 99: return AudCodec::IMAADPCM;
        default:
            return std::unexpected(
                make_error(ErrorCode::UnsupportedFormat, "AUD"));
    }
}

// Validate AUD header values
static Result<void> validate_aud_header(const AudInfo& info) {
    if (info.sample_rate == 0 || info.sample_rate > 96000)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "rate"));
    if (info.uncompressed_size == 0)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "size"));
    return {};
}

static Result<void> parse_aud(AudReaderImpl& impl,
                               std::span<const uint8_t> data) {
    if (data.size() < 12)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "AUD"));

    const uint8_t* p = data.data();
    impl.info.sample_rate = read_u16(p);
    impl.info.uncompressed_size = read_u32(p + 2);
    impl.info.compressed_size = read_u32(p + 6);
    impl.info.channels = (p[10] & 0x01) ? 2 : 1;
    impl.info.bits = (p[10] & 0x02) ? 16 : 8;

    auto codec = decode_codec(p[11]);
    if (!codec) return std::unexpected(codec.error());
    impl.info.codec = *codec;

    auto v = validate_aud_header(impl.info);
    if (!v) return v;

    impl.info.file_size = static_cast<uint32_t>(data.size());
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
