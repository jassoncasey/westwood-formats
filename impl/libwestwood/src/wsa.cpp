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

// Format40 decompression (XOR delta)
// Used by WSA to encode frame differences
static Result<void> format40_decompress(std::span<const uint8_t> src,
                                         std::vector<uint8_t>& dst) {
    size_t src_pos = 0;
    size_t dst_pos = 0;
    size_t dst_size = dst.size();

    while (src_pos < src.size() && dst_pos < dst_size) {
        uint8_t cmd = src[src_pos++];

        if (cmd == 0) {
            // Two-byte command
            if (src_pos >= src.size()) break;
            uint8_t count = src[src_pos++];

            if (count == 0) {
                // End of frame
                break;
            } else if (count & 0x80) {
                // Skip (count & 0x7F) bytes
                dst_pos += (count & 0x7F);
            } else {
                // Copy next count bytes, XOR with dest
                for (uint8_t i = 0; i < count && src_pos < src.size() && dst_pos < dst_size; ++i) {
                    dst[dst_pos++] ^= src[src_pos++];
                }
            }
        } else if (cmd & 0x80) {
            // XOR next (cmd & 0x7F) bytes with same value
            if (src_pos >= src.size()) break;
            uint8_t value = src[src_pos++];
            uint8_t count = cmd & 0x7F;
            for (uint8_t i = 0; i < count && dst_pos < dst_size; ++i) {
                dst[dst_pos++] ^= value;
            }
        } else {
            // Skip cmd bytes
            dst_pos += cmd;
        }
    }

    return {};
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

    // First decompress with LCW
    auto lcw_result = lcw_decompress(frame_data, info.delta_size);
    if (!lcw_result) {
        return std::unexpected(lcw_result.error());
    }

    // Then apply Format40 XOR delta
    auto fmt40_result = format40_decompress(
        std::span(*lcw_result), delta_buffer);
    if (!fmt40_result) {
        return std::unexpected(fmt40_result.error());
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
