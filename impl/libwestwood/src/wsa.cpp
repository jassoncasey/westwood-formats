#include <westwood/wsa.h>
#include <westwood/lcw.h>
#include <westwood/io.h>

#include <cstring>

namespace wwd {

struct WsaReaderImpl {
    WsaInfo info{};
    std::vector<WsaFrameInfo> frames;
    std::vector<uint8_t> data;  // Full file data for decoding
    std::array<Color, 256> palette;
    bool palette_loaded = false;
};

struct WsaReader::Impl : WsaReaderImpl {};

WsaReader::WsaReader() : impl_(std::make_unique<Impl>()) {}
WsaReader::~WsaReader() = default;

const WsaInfo& WsaReader::info() const { return impl_->info; }
const std::vector<WsaFrameInfo>& WsaReader::frames() const {
    return impl_->frames;
}

const std::array<Color, 256>* WsaReader::palette() const {
    if (!impl_->palette_loaded) return nullptr;
    return &impl_->palette;
}

// Parse a single WSA frame entry (8 bytes)
static WsaFrameInfo parse_wsa_entry(
    const uint8_t* entry, const uint8_t* table, uint16_t i)
{
    uint32_t data_raw = read_u32(entry);
    uint32_t ref_raw = read_u32(entry + 4);
    uint32_t next_raw = read_u32(table + (i + 1) * 8);

    WsaFrameInfo frame{};
    frame.offset = data_raw & 0x00FFFFFF;
    frame.format = (data_raw >> 24) & 0xFF;
    frame.ref_offset = ref_raw & 0x00FFFFFF;
    frame.ref_format = (ref_raw >> 24) & 0xFF;
    uint32_t next_offset = next_raw & 0x00FFFFFF;
    frame.size = (next_offset > frame.offset)
        ? (next_offset - frame.offset) : 0;
    return frame;
}

// Load 6-bit palette from WSA data
static void load_wsa_palette(WsaReaderImpl& impl, const uint8_t* pal_data) {
    for (int i = 0; i < 256; ++i) {
        uint8_t r = pal_data[i * 3];
        uint8_t g = pal_data[i * 3 + 1];
        uint8_t b = pal_data[i * 3 + 2];
        impl.palette[i] = {
            static_cast<uint8_t>((r << 2) | (r >> 4)),
            static_cast<uint8_t>((g << 2) | (g >> 4)),
            static_cast<uint8_t>((b << 2) | (b >> 4))
        };
    }
    impl.palette_loaded = true;
}

static Result<void> parse_wsa(
    WsaReaderImpl& impl, std::span<const uint8_t> data)
{
    if (data.size() < 14)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "WSA"));

    const uint8_t* p = data.data();
    uint16_t frame_count = read_u16(p);
    if (frame_count == 0)
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "no frames"));

    impl.info.frame_count = frame_count;
    impl.info.width = read_u16(p + 6);
    impl.info.height = read_u16(p + 8);
    impl.info.delta_size = read_u16(p + 10);
    uint16_t flags = read_u16(p + 12);
    impl.info.file_size = static_cast<uint32_t>(data.size());

    size_t table_size = (frame_count + 2) * 8;
    if (data.size() < 14 + table_size)
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "WSA table"));

    const uint8_t* table = p + 14;
    impl.info.has_loop = (read_u32(table + (frame_count + 1) * 8) != 0);

    impl.frames.reserve(frame_count);
    for (uint16_t i = 0; i < frame_count; ++i)
        impl.frames.push_back(parse_wsa_entry(table + i * 8, table, i));

    impl.info.has_palette = (flags & 0x02) != 0;
    impl.info.palette_offset = impl.info.has_palette
        ? static_cast<uint32_t>(14 + table_size) : 0;
    return {};
}

// Try loading embedded palette if present
static void try_load_palette(WsaReaderImpl& impl) {
    if (!impl.info.has_palette) return;
    uint32_t pal_off = impl.info.palette_offset;
    if (pal_off + 768 > impl.data.size()) return;
    load_wsa_palette(impl, impl.data.data() + pal_off);
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<WsaReader>(new WsaReader());
    reader->impl_->data = std::move(*data);

    auto r = parse_wsa(*reader->impl_, std::span(reader->impl_->data));
    if (!r) return std::unexpected(r.error());

    try_load_palette(*reader->impl_);
    return reader;
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<WsaReader>(new WsaReader());
    reader->impl_->data.assign(data.begin(), data.end());

    auto r = parse_wsa(*reader->impl_, std::span(reader->impl_->data));
    if (!r) return std::unexpected(r.error());

    try_load_palette(*reader->impl_);
    return reader;
}

// Decode LCW-compressed base frame (0x80): LCW -> Format40 -> buffer
static Result<void> decode_lcw_base_frame(
    std::span<const uint8_t> frame_data,
    std::span<uint8_t> delta_buffer)
{
    size_t lcw_buffer_size = delta_buffer.size() * 4;
    auto lcw_result = lcw_decompress(frame_data, lcw_buffer_size);
    if (!lcw_result) return std::unexpected(lcw_result.error());

    auto fmt40_result = format40_decompress(
        std::span(*lcw_result), delta_buffer);
    if (!fmt40_result) return std::unexpected(fmt40_result.error());
    return {};
}

// Decode delta frame (0x40/0x20): Direct Format40 -> buffer
static Result<void> decode_delta_frame(
    std::span<const uint8_t> frame_data,
    std::span<uint8_t> delta_buffer)
{
    auto result = format40_decompress(frame_data, delta_buffer);
    if (!result) return std::unexpected(result.error());
    return {};
}

// Validate frame data bounds
static bool validate_frame_bounds(
    const WsaReaderImpl& impl, const WsaFrameInfo& frame)
{
    return frame.offset + frame.size <= impl.data.size();
}

Result<std::vector<uint8_t>> WsaReader::decode_frame(
    size_t frame_index,
    std::vector<uint8_t>& delta_buffer) const
{
    if (frame_index >= impl_->frames.size())
        return std::unexpected(make_error(ErrorCode::InvalidKey, "frame idx"));

    size_t frame_size =
        static_cast<size_t>(impl_->info.width) * impl_->info.height;
    if (delta_buffer.size() != frame_size)
        delta_buffer.resize(frame_size, 0);

    const auto& frame = impl_->frames[frame_index];
    if (frame.size == 0 || frame.offset == 0)
        return delta_buffer;

    if (!validate_frame_bounds(*impl_, frame))
        return std::unexpected(make_error(ErrorCode::UnexpectedEof, "frame"));

    std::span<const uint8_t> frame_data(
        impl_->data.data() + frame.offset, frame.size);

    Result<void> r = (frame.format & 0x80)
        ? decode_lcw_base_frame(frame_data, std::span(delta_buffer))
        : decode_delta_frame(frame_data, std::span(delta_buffer));

    if (!r) return std::unexpected(r.error());
    return delta_buffer;
}

Result<std::vector<std::vector<uint8_t>>> WsaReader::decode_all_frames() const
{
    std::vector<std::vector<uint8_t>> result;
    result.reserve(impl_->frames.size());

    std::vector<uint8_t> delta_buffer;

    for (size_t i = 0; i < impl_->frames.size(); ++i) {
        auto frame = decode_frame(i, delta_buffer);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        result.push_back(*frame);
    }

    return result;
}

} // namespace wwd
