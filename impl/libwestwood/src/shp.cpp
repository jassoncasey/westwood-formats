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
    // 12      2     Flags (unused)
    // Then frame offset table: 8 bytes per frame
    //   Entry format: 3-byte offset + 1-byte format + 3-byte ref + 1-byte ref_format
    //   Offsets are ABSOLUTE file positions (not relative)

    if (data.size() < 14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP TD header too small"));
    }

    const uint8_t* p = data.data();
    uint32_t file_size = static_cast<uint32_t>(data.size());

    // Read header fields
    impl.info.format = ShpFormat::TD;
    uint16_t frame_count = read_u16(p + 0);
    impl.info.max_width = read_u16(p + 6);
    impl.info.max_height = read_u16(p + 8);
    impl.info.delta_buffer_size = read_u16(p + 10);
    impl.info.file_size = file_size;

    if (frame_count == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP has no frames"));
    }

    // Check we have enough data for the frame table
    size_t table_size = static_cast<size_t>(frame_count) * 8;
    if (data.size() < 14 + table_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "SHP frame table truncated"));
    }

    impl.info.frame_count = frame_count;
    impl.frames.reserve(frame_count);
    const uint8_t* table = p + 14;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * 8;

        ShpFrameInfo frame{};
        frame.width = impl.info.max_width;
        frame.height = impl.info.max_height;
        frame.offset_x = 0;
        frame.offset_y = 0;

        // 24-bit ABSOLUTE offset + 8-bit format
        uint32_t abs_offset = entry[0] | (entry[1] << 8) | (entry[2] << 16);
        frame.data_offset = abs_offset;
        frame.format = entry[3];

        // 24-bit ref offset + 8-bit ref format (for XOR frames)
        uint32_t ref_offset = entry[4] | (entry[5] << 8) | (entry[6] << 16);
        frame.ref_offset = ref_offset;

        // Calculate size: use next entry's offset or end of file
        if (i + 1 < frame_count) {
            const uint8_t* next_entry = table + (i + 1) * 8;
            uint32_t next_abs_offset = next_entry[0] | (next_entry[1] << 8) |
                                       (next_entry[2] << 16);
            if (next_abs_offset > abs_offset) {
                frame.data_size = next_abs_offset - abs_offset;
            } else {
                // Next frame might reference same or earlier offset (XOR chains)
                // Just set a reasonable max size
                frame.data_size = file_size - abs_offset;
            }
        } else {
            // Last frame: size to end of file
            frame.data_size = file_size - abs_offset;
        }

        impl.frames.push_back(frame);

        // Count frame types
        if (frame.format & 0x80) {
            impl.info.lcw_frames++;
        } else if (frame.format & 0x40 || frame.format & 0x20) {
            impl.info.xor_frames++;
        } else {
            impl.info.lcw_frames++;  // Raw frames count as base frames
        }
    }

    // Store data for later decoding
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
            for (uint8_t i = 0; i < count && output.size() < expected_size; ++i) {
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

static Result<void> parse_shp_ts(ShpReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    // TS/RA2 SHP header (8 bytes):
    // Offset  Size  Description
    // 0       2     Zero (0x0000) - format identifier
    // 2       2     Width
    // 4       2     Height
    // 6       2     Frame count
    //
    // Followed by frame entries (24 bytes each):
    // Offset  Size  Description
    // 0       4     File offset to frame data
    // 4       4     Unknown (often 0)
    // 8       4     Unknown
    // 12      4     Unknown
    // 16      2     X offset
    // 18      2     Y offset
    // 20      2     Frame width
    // 22      2     Frame height

    if (data.size() < 8) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP TS header too small"));
    }

    const uint8_t* p = data.data();

    // First 2 bytes should be 0x0000 (already checked in parse_shp)
    uint16_t width = read_u16(p + 2);
    uint16_t height = read_u16(p + 4);
    uint16_t frame_count = read_u16(p + 6);

    if (frame_count == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "SHP TS has no frames"));
    }

    impl.info.format = ShpFormat::TS;
    impl.info.frame_count = frame_count;
    impl.info.max_width = width;
    impl.info.max_height = height;
    impl.info.delta_buffer_size = width * height;  // Each frame is independent
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Frame entries start at offset 8
    size_t entry_size = 24;
    size_t table_offset = 8;
    size_t table_size = frame_count * entry_size;

    if (data.size() < table_offset + table_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "SHP TS frame table truncated"));
    }

    impl.frames.reserve(frame_count);
    const uint8_t* table = p + table_offset;

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* entry = table + i * entry_size;

        ShpFrameInfo frame{};
        frame.data_offset = read_u32(entry + 0);
        // entry[4-15] are unknown/reserved
        frame.offset_x = static_cast<int16_t>(read_u16(entry + 16));
        frame.offset_y = static_cast<int16_t>(read_u16(entry + 18));
        frame.width = read_u16(entry + 20);
        frame.height = read_u16(entry + 22);

        // TS frames use RLE-Zero compression, flag it
        frame.format = 0x01;  // Use 0x01 to indicate TS RLE-Zero format
        frame.ref_offset = 0;

        // Calculate size from next frame offset or end of file
        if (i + 1 < frame_count) {
            const uint8_t* next_entry = table + (i + 1) * entry_size;
            uint32_t next_offset = read_u32(next_entry);
            // Handle case where next offset is 0 (empty frame)
            if (next_offset > frame.data_offset) {
                frame.data_size = next_offset - frame.data_offset;
            } else {
                frame.data_size = 0;
            }
        } else {
            // Last frame - size to end of file
            if (frame.data_offset < data.size()) {
                frame.data_size = static_cast<uint32_t>(data.size()) - frame.data_offset;
            } else {
                frame.data_size = 0;
            }
        }

        impl.frames.push_back(frame);
        impl.info.lcw_frames++;  // Count as base frames (no XOR delta in TS)
    }

    // Store data for later decoding
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

Result<std::vector<uint8_t>> ShpReader::decode_frame(
    size_t frame_index,
    std::vector<uint8_t>& delta_buffer) const
{
    if (frame_index >= impl_->frames.size()) {
        return std::unexpected(
            make_error(ErrorCode::InvalidKey, "Frame index out of range"));
    }

    const auto& frame = impl_->frames[frame_index];
    const auto& info = impl_->info;

    // For TS format, use per-frame dimensions; for TD, use max dimensions
    size_t frame_width = (info.format == ShpFormat::TS) ? frame.width : info.max_width;
    size_t frame_height = (info.format == ShpFormat::TS) ? frame.height : info.max_height;
    size_t frame_size = frame_width * frame_height;

    // Handle empty frames (TS can have frames with 0 dimensions)
    if (frame_size == 0 || frame.data_size == 0) {
        return std::vector<uint8_t>();
    }

    // For TD format, ensure delta buffer is properly sized
    if (info.format == ShpFormat::TD) {
        size_t td_frame_size = static_cast<size_t>(info.max_width) * info.max_height;
        if (delta_buffer.size() != td_frame_size) {
            delta_buffer.resize(td_frame_size, 0);
        }
    }

    std::vector<uint8_t> output(frame_size, 0);

    // Get frame data
    if (frame.data_offset + frame.data_size > impl_->data.size()) {
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "Frame data out of bounds"));
    }

    std::span<const uint8_t> frame_data(
        impl_->data.data() + frame.data_offset,
        frame.data_size);

    if (info.format == ShpFormat::TS) {
        // TS/RA2 format: RLE-Zero compression
        auto decomp = rle_zero_decompress(frame_data, frame_size);
        if (!decomp) {
            return std::unexpected(decomp.error());
        }
        output = std::move(*decomp);
    }
    else {
        // TD/RA format: LCW or Format40
        uint8_t format = frame.format;

        // Format flags:
        // 0x80 = LCW compressed base frame
        // 0x40 = XOR delta against reference frame (Format40)
        // 0x20 = XOR chain from previous frame (Format40)
        // 0x00 = Depends on context:
        //   - First frame or if delta_buffer is empty: LCW compressed
        //   - Otherwise: XOR delta against previous frame (Format40)

        bool is_first_frame = delta_buffer.empty();

        if (format & 0x80) {
            // LCW compressed base frame
            auto decomp = lcw_decompress(frame_data, frame_size);
            if (!decomp) {
                return std::unexpected(decomp.error());
            }
            output = std::move(*decomp);
            if (output.size() < frame_size) {
                output.resize(frame_size, 0);
            }
        }
        else if ((format & 0x40) || (format == 0x00 && !is_first_frame)) {
            // XOR delta against reference frame (Format40 encoded)
            // delta_buffer should already contain the reference frame
            output = delta_buffer;
            auto fmt40_result = format40_decompress(frame_data, std::span(output));
            if (!fmt40_result) {
                return std::unexpected(fmt40_result.error());
            }
        }
        else if (format & 0x20) {
            // XOR delta against previous frame (Format40 encoded)
            // delta_buffer contains previous frame
            output = delta_buffer;
            auto fmt40_result = format40_decompress(frame_data, std::span(output));
            if (!fmt40_result) {
                return std::unexpected(fmt40_result.error());
            }
        }
        else if (format == 0x00 && is_first_frame) {
            // First frame with format 0x00 = LCW compressed base frame
            auto decomp = lcw_decompress(frame_data, frame_size);
            if (!decomp) {
                return std::unexpected(decomp.error());
            }
            output = std::move(*decomp);
            if (output.size() < frame_size) {
                output.resize(frame_size, 0);
            }
        }
        else {
            // Unknown format - treat as raw
            size_t copy_len = std::min(frame_data.size(), frame_size);
            std::memcpy(output.data(), frame_data.data(), copy_len);
        }

        // Update delta buffer for next frame (TD format only)
        delta_buffer = output;
    }

    return output;
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
