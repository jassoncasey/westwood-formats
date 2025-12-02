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
    impl.info.delta_buffer_size = read_u16(p + 10);
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
    size_t frame_size = static_cast<size_t>(info.max_width) * info.max_height;

    // Ensure delta buffer is properly sized
    if (delta_buffer.size() != frame_size) {
        delta_buffer.resize(frame_size, 0);
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

    uint8_t format = frame.format;

    if (format == 0x00) {
        // Raw data - just copy (rare but possible)
        if (frame_data.size() >= frame_size) {
            std::memcpy(output.data(), frame_data.data(), frame_size);
        } else {
            std::memcpy(output.data(), frame_data.data(), frame_data.size());
        }
    }
    else if (format & 0x80) {
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
    else if (format & 0x40) {
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
    else {
        // Unknown format - treat as raw
        size_t copy_len = std::min(frame_data.size(), frame_size);
        std::memcpy(output.data(), frame_data.data(), copy_len);
    }

    // Update delta buffer for next frame
    delta_buffer = output;

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
