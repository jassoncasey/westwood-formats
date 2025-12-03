#include <westwood/shp.h>
#include <westwood/lcw.h>
#include <westwood/io.h>

#include <algorithm>
#include <cstring>

namespace wwd {

struct ShpReaderImpl {
    ShpInfo info{};
    std::vector<ShpFrameInfo> frames;
    std::vector<uint8_t> data;  // Full file data for decoding
};

struct ShpReader::Impl : ShpReaderImpl {};

ShpReader::ShpReader() : impl_(std::make_unique<Impl>()) {}
ShpReader::~ShpReader() = default;

const ShpInfo& ShpReader::info() const { return impl_->info; }
const std::vector<ShpFrameInfo>& ShpReader::frames() const {
    return impl_->frames;
}

// Read 24-bit little-endian value
static uint32_t read_u24(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

// Parse a single TD frame entry (8 bytes)
static ShpFrameInfo parse_td_entry(
    const uint8_t* entry, uint16_t w, uint16_t h)
{
    ShpFrameInfo frame{};
    frame.width = w;
    frame.height = h;
    frame.offset_x = 0;
    frame.offset_y = 0;
    frame.data_offset = read_u24(entry);
    frame.format = entry[3];
    frame.ref_offset = read_u24(entry + 4);
    return frame;
}

// Calculate frame data size from next offset or file end
static uint32_t calc_frame_size(
    uint32_t offset, uint32_t next_off, uint32_t end)
{
    if (next_off > offset) return next_off - offset;
    return end - offset;
}

// Count frame types for TD format
static void count_td_frame_type(ShpInfo& info, uint8_t format) {
    if (format & 0x80)
        info.lcw_frames++;
    else if (format & 0x40 || format & 0x20)
        info.xor_frames++;
    else
        info.lcw_frames++;
}

static Result<void> parse_shp_td(ShpReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 14)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "SHP TD"));

    const uint8_t* p = data.data();
    uint32_t file_size = static_cast<uint32_t>(data.size());

    impl.info.format = ShpFormat::TD;
    uint16_t frame_count = read_u16(p + 0);
    impl.info.max_width = read_u16(p + 6);
    impl.info.max_height = read_u16(p + 8);
    impl.info.delta_buffer_size = read_u16(p + 10);
    impl.info.file_size = file_size;

    if (frame_count == 0)
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "no frames"));

    size_t table_size = static_cast<size_t>(frame_count) * 8;
    if (data.size() < 14 + table_size)
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "SHP table"));

    impl.info.frame_count = frame_count;
    impl.frames.reserve(frame_count);
    const uint8_t* table = p + 14;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * 8;
        auto frame = parse_td_entry(
            entry, impl.info.max_width, impl.info.max_height);

        uint32_t next_off = (i + 1 < frame_count)
            ? read_u24(table + (i + 1) * 8) : file_size;
        frame.data_size = calc_frame_size(
            frame.data_offset, next_off, file_size);

        impl.frames.push_back(frame);
        count_td_frame_type(impl.info, frame.format);
    }

    impl.data.assign(data.begin(), data.end());
    return {};
}

// RLE-Zero decompression for TS/RA2 SHP
// Format: 0x00 + count = run of zeros, other bytes = literal
static Result<std::vector<uint8_t>> rle_zero_decompress(
    std::span<const uint8_t> src, size_t expected_size)
{
    std::vector<uint8_t> output;
    output.reserve(expected_size);

    size_t pos = 0;
    while (pos < src.size() && output.size() < expected_size) {
        uint8_t byte = src[pos++];
        if (byte == 0x00) {
            // Run of zeros
            if (pos >= src.size()) break;
            uint8_t count = src[pos++];
            size_t expected = expected_size;
            for (uint8_t i = 0; i < count && output.size() < expected; ++i) {
                output.push_back(0);
            }
        } else {
            // Literal byte
            output.push_back(byte);
        }
    }

    // Pad to expected size if needed
    while (output.size() < expected_size) {
        output.push_back(0);
    }

    return output;
}

// Parse a single TS frame entry (24 bytes)
static ShpFrameInfo parse_ts_entry(const uint8_t* entry) {
    ShpFrameInfo frame{};
    frame.data_offset = read_u32(entry + 0);
    frame.offset_x = static_cast<int16_t>(read_u16(entry + 16));
    frame.offset_y = static_cast<int16_t>(read_u16(entry + 18));
    frame.width = read_u16(entry + 20);
    frame.height = read_u16(entry + 22);
    frame.format = 0x01;  // TS RLE-Zero format
    frame.ref_offset = 0;
    return frame;
}

// Calculate TS frame size from next offset or file end
static uint32_t calc_ts_frame_size(
    uint32_t offset, uint32_t next_off, size_t end)
{
    if (next_off > offset) return next_off - offset;
    if (offset < end) return static_cast<uint32_t>(end) - offset;
    return 0;
}

static Result<void> parse_shp_ts(ShpReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 8)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "SHP TS"));

    const uint8_t* p = data.data();
    uint16_t width = read_u16(p + 2);
    uint16_t height = read_u16(p + 4);
    uint16_t frame_count = read_u16(p + 6);

    if (frame_count == 0)
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "no frames"));

    impl.info.format = ShpFormat::TS;
    impl.info.frame_count = frame_count;
    impl.info.max_width = width;
    impl.info.max_height = height;
    impl.info.delta_buffer_size = width * height;
    impl.info.file_size = static_cast<uint32_t>(data.size());

    constexpr size_t entry_size = 24;
    if (data.size() < 8 + frame_count * entry_size)
        return std::unexpected(make_error(ErrorCode::CorruptIndex, "TS table"));

    impl.frames.reserve(frame_count);
    const uint8_t* table = p + 8;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * entry_size;
        auto frame = parse_ts_entry(entry);

        uint32_t next_off = (i + 1 < frame_count)
            ? read_u32(table + (i + 1) * entry_size) : 0;
        frame.data_size = calc_ts_frame_size(
            frame.data_offset, next_off, data.size());

        impl.frames.push_back(frame);
        impl.info.lcw_frames++;
    }

    impl.data.assign(data.begin(), data.end());
    return {};
}

static Result<void> parse_shp(ShpReaderImpl& impl,
                               std::span<const uint8_t> data) {
    if (data.size() < 14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP file too small"));
    }

    // Detect format by checking if it's a TS SHP
    // TS SHP starts with 0x00 0x00 at offset 0
    // TD SHP has frame count at offset 0 (non-zero)
    uint16_t first_word = read_u16(data.data());

    if (first_word == 0) {
        return parse_shp_ts(impl, data);
    }

    return parse_shp_td(impl, data);
}

Result<std::unique_ptr<ShpReader>> ShpReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());
    return open(std::span(*data));
}

Result<std::unique_ptr<ShpReader>> ShpReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<ShpReader>(new ShpReader());
    auto result = parse_shp(*reader->impl_, data);
    if (!result) return std::unexpected(result.error());
    return reader;
}

// Decode TS frame (RLE-Zero)
static Result<std::vector<uint8_t>> decode_ts_frame(
    std::span<const uint8_t> data, size_t frame_size)
{
    return rle_zero_decompress(data, frame_size);
}

// Decode LCW base frame
static Result<std::vector<uint8_t>> decode_lcw_frame(
    std::span<const uint8_t> data, size_t frame_size)
{
    auto decomp = lcw_decompress(data, frame_size);
    if (!decomp) return std::unexpected(decomp.error());
    if (decomp->size() < frame_size) decomp->resize(frame_size, 0);
    return decomp;
}

// Decode Format40 XOR delta frame
static Result<std::vector<uint8_t>> decode_xor_frame(
    std::span<const uint8_t> data, const std::vector<uint8_t>& ref)
{
    auto output = ref;
    auto r = format40_decompress(data, std::span(output));
    if (!r) return std::unexpected(r.error());
    return output;
}

// Decode raw frame (copy)
static std::vector<uint8_t> decode_raw_frame(
    std::span<const uint8_t> data, size_t frame_size)
{
    std::vector<uint8_t> output(frame_size, 0);
    std::memcpy(output.data(), data.data(), std::min(data.size(), frame_size));
    return output;
}

// Decode TD/RA frame based on format flags
static Result<std::vector<uint8_t>> decode_td_frame(
    std::span<const uint8_t> data, size_t frame_size,
    uint8_t format, std::vector<uint8_t>& delta)
{
    bool is_first = delta.empty();

    if (format & 0x80 || (format == 0x00 && is_first))
        return decode_lcw_frame(data, frame_size);

    if ((format & 0x40) || (format & 0x20) || (format == 0x00 && !is_first))
        return decode_xor_frame(data, delta);

    return decode_raw_frame(data, frame_size);
}

Result<std::vector<uint8_t>> ShpReader::decode_frame(
    size_t frame_index,
    std::vector<uint8_t>& delta_buffer) const
{
    if (frame_index >= impl_->frames.size())
        return std::unexpected(make_error(ErrorCode::InvalidKey, "frame idx"));

    const auto& frame = impl_->frames[frame_index];
    const auto& info = impl_->info;

    size_t w = (info.format == ShpFormat::TS) ? frame.width : info.max_width;
    size_t h = (info.format == ShpFormat::TS) ? frame.height : info.max_height;
    size_t frame_size = w * h;

    if (frame_size == 0 || frame.data_size == 0)
        return std::vector<uint8_t>();

    if (info.format == ShpFormat::TD) {
        size_t td_size = static_cast<size_t>(info.max_width) * info.max_height;
        if (delta_buffer.size() != td_size) delta_buffer.resize(td_size, 0);
    }

    if (frame.data_offset + frame.data_size > impl_->data.size())
        return std::unexpected(make_error(ErrorCode::UnexpectedEof, "frame"));

    std::span<const uint8_t> data(impl_->data.data() + frame.data_offset,
                                   frame.data_size);

    Result<std::vector<uint8_t>> result;
    if (info.format == ShpFormat::TS)
        result = decode_ts_frame(data, frame_size);
    else
        result = decode_td_frame(data, frame_size, frame.format, delta_buffer);

    if (!result) return result;

    if (info.format == ShpFormat::TD)
        delta_buffer = *result;

    return result;
}

Result<std::vector<std::vector<uint8_t>>> ShpReader::decode_all_frames() const
{
    std::vector<std::vector<uint8_t>> result;
    result.reserve(impl_->frames.size());

    std::vector<uint8_t> delta_buffer;

    for (size_t i = 0; i < impl_->frames.size(); ++i) {
        auto frame = decode_frame(i, delta_buffer);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        result.push_back(std::move(*frame));
    }

    return result;
}

} // namespace wwd
