#include <westwood/shp.h>
#include <westwood/io.h>

#include <algorithm>

namespace wwd {

struct ShpReaderImpl {
    ShpInfo info{};
    std::vector<ShpFrameInfo> frames;
};

struct ShpReader::Impl : ShpReaderImpl {};

ShpReader::ShpReader() : impl_(std::make_unique<Impl>()) {}
ShpReader::~ShpReader() = default;

const ShpInfo& ShpReader::info() const { return impl_->info; }
const std::vector<ShpFrameInfo>& ShpReader::frames() const {
    return impl_->frames;
}

static Result<void> parse_shp_td(ShpReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    // TD/RA SHP header:
    // Offset  Size  Description
    // 0       2     Frame count
    // 2       2     X offset (not used, usually 0)
    // 4       2     Y offset (not used, usually 0)
    // 6       2     Width
    // 8       2     Height
    // 10      2     Delta size (largest frame)
    // Then frame offset table: (frame_count + 2) * 8 bytes

    if (data.size() < 14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP TD header too small"));
    }

    const uint8_t* p = data.data();

    uint16_t frame_count = read_u16(p);
    if (frame_count == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP has no frames"));
    }

    impl.info.format = ShpFormat::TD;
    impl.info.frame_count = frame_count;
    impl.info.max_width = read_u16(p + 6);
    impl.info.max_height = read_u16(p + 8);
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Offset table starts at byte 14
    // Each entry: 3 bytes offset, 1 byte format, 3 bytes ref_offset, 1 byte ref_format
    // Total entries: frame_count + 2 (includes end markers)
    size_t table_size = (frame_count + 2) * 8;
    if (data.size() < 14 + table_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "SHP offset table truncated"));
    }

    impl.frames.reserve(frame_count);
    const uint8_t* table = p + 14;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * 8;

        ShpFrameInfo frame{};
        frame.width = impl.info.max_width;
        frame.height = impl.info.max_height;
        frame.offset_x = 0;
        frame.offset_y = 0;

        // 24-bit offset + 8-bit format
        frame.data_offset = entry[0] | (entry[1] << 8) | (entry[2] << 16);
        frame.format = entry[3];

        // 24-bit ref offset + 8-bit ref format (for XORLCW)
        frame.ref_offset = entry[4] | (entry[5] << 8) | (entry[6] << 16);

        // Calculate size from next entry offset
        const uint8_t* next_entry = table + (i + 1) * 8;
        uint32_t next_offset = next_entry[0] | (next_entry[1] << 8) |
                               (next_entry[2] << 16);
        frame.data_size = next_offset - frame.data_offset;

        impl.frames.push_back(frame);
    }

    return {};
}

static Result<void> parse_shp_ts([[maybe_unused]] ShpReaderImpl& impl,
                                  [[maybe_unused]] std::span<const uint8_t> data) {
    // TS/RA2 SHP has different header structure
    // For now, just detect and report unsupported
    return std::unexpected(
        make_error(ErrorCode::UnsupportedFormat, "TS/RA2 SHP not yet supported"));
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

} // namespace wwd
