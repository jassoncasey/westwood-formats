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

static Result<void> parse_wsa(WsaReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // WSA header (C&C/RA version):
    // Offset  Size  Type        Description
    // 0       2     uint16      Frame count
    // 2       2     uint16      X position (display offset)
    // 4       2     uint16      Y position (display offset)
    // 6       2     uint16      Width
    // 8       2     uint16      Height
    // 10      2     uint16      Delta buffer size
    // 12      2     uint16      Flags (bit 1 = has palette)
    // 14      8*N   entries     Frame offset table (N = frame_count + 2)
    //
    // Each table entry is 8 bytes:
    //   4 bytes: data offset (24-bit) + flags (8-bit high byte)
    //   4 bytes: reference offset (24-bit) + flags (8-bit high byte)
    //
    // Flags in high byte:
    //   0x80 = LCW compressed base frame
    //   0x40 = XOR delta against reference
    //   0x20 = XOR delta against previous frame

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
    impl.info.delta_size = read_u16(p + 10);  // uint16, not uint32
    uint16_t flags = read_u16(p + 12);
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Offset table: (frame_count + 2) entries, 8 bytes each
    size_t table_entries = frame_count + 2;
    size_t table_size = table_entries * 8;
    if (data.size() < 14 + table_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "WSA offset table truncated"));
    }

    const uint8_t* table = p + 14;

    // Check for looping animation (last entry non-zero means loop frame exists)
    uint32_t last_entry = read_u32(table + (frame_count + 1) * 8);
    impl.info.has_loop = (last_entry != 0);

    // Read frame offsets (8 bytes per entry)
    impl.frames.reserve(frame_count);
    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * 8;
        uint32_t data_raw = read_u32(entry);
        uint32_t ref_raw = read_u32(entry + 4);

        WsaFrameInfo frame{};
        frame.offset = data_raw & 0x00FFFFFF;  // Lower 24 bits = offset
        frame.format = (data_raw >> 24) & 0xFF;  // High byte = flags
        frame.ref_offset = ref_raw & 0x00FFFFFF;
        frame.ref_format = (ref_raw >> 24) & 0xFF;

        // Calculate size from next entry
        uint32_t next_raw = read_u32(table + (i + 1) * 8);
        uint32_t next_offset = next_raw & 0x00FFFFFF;
        frame.size = (next_offset > frame.offset) ? (next_offset - frame.offset) : 0;

        impl.frames.push_back(frame);
    }

    // Check for embedded palette (flag bit 1)
    impl.info.has_palette = (flags & 0x02) != 0;
    if (impl.info.has_palette) {
        // Palette follows immediately after offset table
        impl.info.palette_offset = static_cast<uint32_t>(14 + table_size);
    } else {
        impl.info.palette_offset = 0;
    }

    return {};
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<WsaReader>(new WsaReader());
    reader->impl_->data = std::move(*data);

    auto result = parse_wsa(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    // Load embedded palette if present
    if (reader->impl_->info.has_palette) {
        uint32_t pal_offset = reader->impl_->info.palette_offset;
        if (pal_offset + 768 <= reader->impl_->data.size()) {
            const uint8_t* pal_data = reader->impl_->data.data() + pal_offset;
            for (int i = 0; i < 256; ++i) {
                // 6-bit to 8-bit conversion: (val << 2) | (val >> 4)
                uint8_t r = pal_data[i * 3];
                uint8_t g = pal_data[i * 3 + 1];
                uint8_t b = pal_data[i * 3 + 2];
                reader->impl_->palette[i] = {
                    static_cast<uint8_t>((r << 2) | (r >> 4)),
                    static_cast<uint8_t>((g << 2) | (g >> 4)),
                    static_cast<uint8_t>((b << 2) | (b >> 4))
                };
            }
            reader->impl_->palette_loaded = true;
        }
    }

    return reader;
}

Result<std::unique_ptr<WsaReader>> WsaReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<WsaReader>(new WsaReader());
    reader->impl_->data.assign(data.begin(), data.end());

    auto result = parse_wsa(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    // Load embedded palette if present
    if (reader->impl_->info.has_palette) {
        uint32_t pal_offset = reader->impl_->info.palette_offset;
        if (pal_offset + 768 <= reader->impl_->data.size()) {
            const uint8_t* pal_data = reader->impl_->data.data() + pal_offset;
            for (int i = 0; i < 256; ++i) {
                uint8_t r = pal_data[i * 3];
                uint8_t g = pal_data[i * 3 + 1];
                uint8_t b = pal_data[i * 3 + 2];
                reader->impl_->palette[i] = {
                    static_cast<uint8_t>((r << 2) | (r >> 4)),
                    static_cast<uint8_t>((g << 2) | (g >> 4)),
                    static_cast<uint8_t>((b << 2) | (b >> 4))
                };
            }
            reader->impl_->palette_loaded = true;
        }
    }

    return reader;
}


Result<std::vector<uint8_t>> WsaReader::decode_frame(
    size_t frame_index,
    std::vector<uint8_t>& delta_buffer) const
{
    if (frame_index >= impl_->frames.size()) {
        return std::unexpected(
            make_error(ErrorCode::InvalidKey, "Frame index out of range"));
    }

    const auto& info = impl_->info;
    size_t frame_size = static_cast<size_t>(info.width) * info.height;

    // Ensure delta buffer is properly sized
    if (delta_buffer.size() != frame_size) {
        delta_buffer.resize(frame_size, 0);
    }

    const auto& frame = impl_->frames[frame_index];

    // Check for empty frame
    if (frame.size == 0 || frame.offset == 0) {
        // Return current delta buffer as-is
        return delta_buffer;
    }

    // Get frame data
    if (frame.offset + frame.size > impl_->data.size()) {
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "Frame data out of bounds"));
    }

    std::span<const uint8_t> frame_data(
        impl_->data.data() + frame.offset,
        frame.size);

    // Decode based on frame format flags:
    // 0x80 = LCW-compressed base frame (LCW -> Format40 -> buffer)
    // 0x40 = XOR delta against reference (direct Format40 -> buffer)
    // 0x20 = XOR delta against previous (direct Format40 -> buffer)

    if (frame.format & 0x80) {
        // Base frame: LCW decompress first, then Format40
        // LCW output is Format40 XOR delta commands - allocate enough space
        // The delta_size field appears to be unrelated to LCW output size
        size_t lcw_buffer_size = frame_size * 4;  // Conservative estimate
        auto lcw_result = lcw_decompress(frame_data, lcw_buffer_size);
        if (!lcw_result) {
            return std::unexpected(lcw_result.error());
        }

        auto fmt40_result = format40_decompress(
            std::span(*lcw_result), std::span(delta_buffer));
        if (!fmt40_result) {
            return std::unexpected(fmt40_result.error());
        }
    } else {
        // Delta frame (0x40 or 0x20): Direct Format40, no LCW
        auto fmt40_result = format40_decompress(
            frame_data, std::span(delta_buffer));
        if (!fmt40_result) {
            return std::unexpected(fmt40_result.error());
        }
    }

    // Return copy of delta buffer
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
