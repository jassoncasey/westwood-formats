#include <westwood/wsa.h>
#include <westwood/io.h>

namespace wwd {

struct WsaReaderImpl {
    WsaInfo info{};
    std::vector<WsaFrameInfo> frames;
};

struct WsaReader::Impl : WsaReaderImpl {};

WsaReader::WsaReader() : impl_(std::make_unique<Impl>()) {}
WsaReader::~WsaReader() = default;

const WsaInfo& WsaReader::info() const { return impl_->info; }
const std::vector<WsaFrameInfo>& WsaReader::frames() const {
    return impl_->frames;
}

static Result<void> parse_wsa(WsaReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // WSA header:
    // Offset  Size  Description
    // 0       2     Frame count
    // 2       2     X (unused)
    // 4       2     Y (unused)
    // 6       2     Width
    // 8       2     Height
    // 10      4     Delta buffer size (largest frame uncompressed)
    // 14      4*N   Frame offset table (N = frame_count + 2)

    if (data.size() < 14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "WSA file too small"));
    }

    const uint8_t* p = data.data();

    uint16_t frame_count = read_u16(p);
    if (frame_count == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "WSA has no frames"));
    }

    impl.info.frame_count = frame_count;
    impl.info.width = read_u16(p + 6);
    impl.info.height = read_u16(p + 8);
    impl.info.delta_size = read_u32(p + 10);
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Offset table: frame_count + 2 entries (includes end marker and palette)
    size_t table_entries = frame_count + 2;
    size_t table_size = table_entries * 4;
    if (data.size() < 14 + table_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "WSA offset table truncated"));
    }

    const uint8_t* table = p + 14;

    // Check for looping animation (frame 0 offset != 0)
    uint32_t first_offset = read_u32(table);
    impl.info.has_loop = (first_offset != 0);

    // Read frame offsets
    impl.frames.reserve(frame_count);
    for (uint16_t i = 0; i < frame_count; ++i) {
        uint32_t offset = read_u32(table + i * 4);
        uint32_t next_offset = read_u32(table + (i + 1) * 4);

        WsaFrameInfo frame{};
        frame.offset = offset;
        frame.size = (next_offset > offset) ? (next_offset - offset) : 0;
        impl.frames.push_back(frame);
    }

    // Check for embedded palette
    // Palette offset is stored after the last frame offset
    uint32_t palette_offset = read_u32(table + (frame_count + 1) * 4);
    impl.info.has_palette = (palette_offset != 0);
    impl.info.palette_offset = palette_offset;

    return {};
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());
    return open(std::span(*data));
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<WsaReader>(new WsaReader());
    auto result = parse_wsa(*reader->impl_, data);
    if (!result) return std::unexpected(result.error());
    return reader;
}

} // namespace wwd
