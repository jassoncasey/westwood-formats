#include <westwood/shp.h>
#include <westwood/lcw.h>
#include <westwood/io.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace wwd {

// D2: detect offset size (2 or 4 bytes) from header
static int d2_detect_offset_size(const uint8_t* p) {
    uint32_t test = read_u32(p + 2);
    return (test & 0xFF0000) ? 2 : 4;
}

// D2: read offset at position (2 or 4 byte)
static uint32_t d2_read_offset(const uint8_t* p, int size) {
    return (size == 2) ? read_u16(p) : read_u32(p);
}

// D2 format detection (from OpenRA ShpD2Loader.cs)
static bool is_valid_d2_shp(std::span<const uint8_t> data) {
    if (data.size() < 4) return false;

    uint16_t frame_count = read_u16(data.data());
    if (frame_count == 0) return false;

    int off_size = d2_detect_offset_size(data.data());
    size_t eof_pos = 2 + static_cast<size_t>(off_size) * frame_count;
    if (eof_pos + off_size > data.size()) return false;

    uint32_t eof = d2_read_offset(data.data() + eof_pos, off_size);
    if (eof + 2 != data.size()) return false;

    uint32_t first_off = d2_read_offset(data.data() + 2, off_size);
    if (first_off + 4 > data.size()) return false;

    uint16_t flags = read_u16(data.data() + first_off + 2);
    return flags <= 3 || flags == 5;
}

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

// D2: read offset table into vector
static std::vector<uint32_t> d2_read_offsets(
    const uint8_t* p, uint16_t count, int off_size)
{
    std::vector<uint32_t> offsets;
    offsets.reserve(count + 1);
    size_t pos = 2;
    for (uint16_t i = 0; i <= count; ++i) {
        offsets.push_back(d2_read_offset(p + pos, off_size));
        pos += off_size;
    }
    return offsets;
}

// D2: parse single frame header (10 bytes at offset+2)
static ShpFrameInfo d2_parse_frame(const uint8_t* fp, size_t frame_pos) {
    ShpFrameInfo f{};
    f.format = read_u16(fp);
    f.width = read_u16(fp + 3);
    f.height = fp[5];
    f.offset_x = 0;
    f.offset_y = 0;
    f.data_offset = static_cast<uint32_t>(frame_pos);
    f.data_size = read_u16(fp + 6) + 10;
    f.ref_offset = 0;
    return f;
}

// Parse D2 SHP format (Dune 2, some Red Alert assets like MOUSE.SHP)
static Result<void> parse_shp_d2(ShpReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 4)
        return std::unexpected(make_error(ErrorCode::CorruptHeader, "SHP D2"));

    const uint8_t* p = data.data();
    uint16_t count = read_u16(p);
    int off_size = d2_detect_offset_size(p);

    impl.info.format = ShpFormat::D2;
    impl.info.frame_count = count;
    impl.info.file_size = static_cast<uint32_t>(data.size());
    impl.info.offset_size = static_cast<uint8_t>(off_size);
    impl.info.lcw_frames = count;
    impl.info.xor_frames = 0;

    auto offsets = d2_read_offsets(p, count, off_size);
    uint16_t max_w = 0, max_h = 0;
    impl.frames.reserve(count);

    for (uint16_t i = 0; i < count; ++i) {
        size_t pos = offsets[i] + 2;
        if (pos + 10 > data.size())
            return std::unexpected(
                make_error(ErrorCode::CorruptIndex, "D2 frame"));
        auto f = d2_parse_frame(p + pos, pos);
        if (f.width > max_w) max_w = f.width;
        if (f.height > max_h) max_h = f.height;
        impl.frames.push_back(f);
    }

    impl.info.max_width = max_w;
    impl.info.max_height = max_h;
    impl.info.delta_buffer_size = max_w * max_h;
    impl.data.assign(data.begin(), data.end());
    return {};
}

// RLE-Zero decompression for TS/RA2 SHP and D2
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
    if (data.size() < 4) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP file too small"));
    }

    // Detection order: TS, D2, TD (from most specific to fallback)
    // TS SHP starts with 0x00 0x00 at offset 0
    uint16_t first_word = read_u16(data.data());

    if (first_word == 0) {
        return parse_shp_ts(impl, data);
    }

    // D2 format: EOF + 2 == file_size, format flag 0-3 or 5
    if (is_valid_d2_shp(data)) {
        return parse_shp_d2(impl, data);
    }

    // TD format: fallback (requires at least 14 bytes)
    if (data.size() < 14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP file too small"));
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

// D2: init identity palette with default shadow remapping
static std::array<uint8_t, 256> d2_init_palette() {
    std::array<uint8_t, 256> t;
    for (int i = 0; i < 256; ++i) t[i] = static_cast<uint8_t>(i);
    t[1] = 0x7F; t[2] = 0x7E; t[3] = 0x7D; t[4] = 0x7C;
    return t;
}

// D2: read palette table from data, return bytes consumed
static size_t d2_read_palette(
    std::span<const uint8_t> data, uint16_t flags, std::array<uint8_t,256>& t)
{
    constexpr uint16_t kHas = 1, kVar = 4;
    if (!(flags & kHas) || data.empty()) return 0;

    if (flags & kVar) {
        size_t n = data[0], c = 1;
        for (size_t i = 0; i < n && c < data.size(); ++i) t[i] = data[c++];
        return c;
    }
    size_t n = std::min<size_t>(16, data.size());
    for (size_t i = 0; i < n; ++i) t[i] = data[i];
    return n;
}

// D2: decompress frame data (LCW optional, then RLE-zero)
static Result<std::vector<uint8_t>> d2_decompress(
    std::span<const uint8_t> src, size_t frame_size, bool use_lcw)
{
    std::vector<uint8_t> tmp;
    if (use_lcw) {
        auto r = lcw_decompress(src, frame_size * 2);
        if (!r) return std::unexpected(r.error());
        tmp = std::move(*r);
    } else {
        tmp.assign(src.begin(), src.end());
    }
    return rle_zero_decompress(std::span(tmp), frame_size);
}

// Decode D2 frame: LCW + RLE-zero + palette lookup
static Result<std::vector<uint8_t>> decode_d2_frame(
    std::span<const uint8_t> data, uint16_t w, uint8_t h, uint16_t flags)
{
    if (data.size() < 10)
        return std::unexpected(make_error(ErrorCode::CorruptData, "D2"));

    auto pal = d2_init_palette();
    auto post = data.subspan(10);
    size_t consumed = d2_read_palette(post, flags, pal);
    auto compressed = post.subspan(std::min(consumed, post.size()));

    bool use_lcw = !(flags & 2);
    auto pixels = d2_decompress(compressed, size_t(w) * h, use_lcw);
    if (!pixels) return pixels;

    for (auto& p : *pixels) p = pal[p];
    return pixels;
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

    // D2 and TS have per-frame dimensions, TD uses global dimensions
    size_t w, h;
    if (info.format == ShpFormat::TS || info.format == ShpFormat::D2) {
        w = frame.width;
        h = frame.height;
    } else {
        w = info.max_width;
        h = info.max_height;
    }
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
    if (info.format == ShpFormat::TS) {
        result = decode_ts_frame(data, frame_size);
    } else if (info.format == ShpFormat::D2) {
        result = decode_d2_frame(data, frame.width,
                                  static_cast<uint8_t>(frame.height),
                                  frame.format);
    } else {
        result = decode_td_frame(data, frame_size, frame.format, delta_buffer);
    }

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
