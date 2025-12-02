#include <westwood/tmp.h>
#include <westwood/io.h>

namespace wwd {

struct TmpReaderImpl {
    TmpInfo info{};
    std::vector<TmpTileInfo> tiles;
    std::vector<uint8_t> data;  // Full file data for decoding
};

struct TmpReader::Impl : TmpReaderImpl {};

TmpReader::TmpReader() : impl_(std::make_unique<Impl>()) {}
TmpReader::~TmpReader() = default;

const TmpInfo& TmpReader::info() const { return impl_->info; }
const std::vector<TmpTileInfo>& TmpReader::tiles() const {
    return impl_->tiles;
}

static TmpFormat detect_format(std::span<const uint8_t> data) {
    // RA TMP: offset 20 = 0x00000000, offset 26 = 0x2C73
    // TD TMP: offset 16 = 0x00000000, offset 20 = 0x0D1AFFFF

    if (data.size() >= 28) {
        uint32_t val20 = read_u32(data.data() + 20);
        uint16_t val26 = read_u16(data.data() + 26);

        if (val20 == 0 && val26 == 0x2C73) {
            return TmpFormat::RA;
        }
    }

    if (data.size() >= 24) {
        uint32_t val16 = read_u32(data.data() + 16);
        uint32_t val20 = read_u32(data.data() + 20);

        if (val16 == 0 && val20 == 0x0D1AFFFF) {
            return TmpFormat::TD;
        }
    }

    // Default to RA format
    return TmpFormat::RA;
}

static Result<void> parse_tmp(TmpReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // TD/RA TMP header (40 bytes):
    // Offset  Size  Description
    // 0       2     Tile width (24)
    // 2       2     Tile height (24)
    // 4       4     Tile count
    // 8       4     Unknown1
    // 12      4     Tile size (576 = 24*24)
    // 16      4     Unknown2 (0 for TD)
    // 20      4     Magic (0x0D1AFFFF for TD, 0 for RA)
    // 24      4     Unknown3 (contains 0x2C73 at bytes 26-27 for RA)
    // 28      4     Index start offset
    // 32      4     Index end offset
    // 36      4     Image data start offset

    if (data.size() < 40) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TMP file too small"));
    }

    impl.info.format = detect_format(data);

    const uint8_t* p = data.data();

    impl.info.tile_width = read_u16(p);
    impl.info.tile_height = read_u16(p + 2);
    impl.info.tile_count = read_u32(p + 4);
    impl.info.index_start = read_u32(p + 28);
    impl.info.index_end = read_u32(p + 32);
    impl.info.image_start = read_u32(p + 36);
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Validate
    if (impl.info.tile_width == 0 || impl.info.tile_height == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TMP invalid tile size"));
    }

    if (impl.info.tile_count == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TMP has no tiles"));
    }

    // Read tile index
    // Index contains offsets for each tile (0 = invalid/empty tile)
    size_t index_size = impl.info.index_end - impl.info.index_start;

    if (impl.info.index_start + index_size > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "TMP index truncated"));
    }

    uint32_t tile_size = impl.info.tile_width * impl.info.tile_height;
    impl.tiles.reserve(impl.info.tile_count);

    const uint8_t* index = p + impl.info.index_start;
    uint16_t empty_count = 0;
    for (uint32_t i = 0; i < impl.info.tile_count; ++i) {
        uint32_t offset = read_u32(index + i * 4);

        TmpTileInfo tile{};
        tile.offset = offset;
        tile.size = tile_size;
        tile.valid = (offset != 0);

        if (!tile.valid) {
            empty_count++;
        }

        impl.tiles.push_back(tile);
    }

    impl.info.empty_count = empty_count;

    return {};
}

Result<std::unique_ptr<TmpReader>> TmpReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<TmpReader>(new TmpReader());
    reader->impl_->data = std::move(*data);

    auto result = parse_tmp(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

Result<std::unique_ptr<TmpReader>> TmpReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<TmpReader>(new TmpReader());
    reader->impl_->data.assign(data.begin(), data.end());

    auto result = parse_tmp(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

std::vector<uint8_t> TmpReader::decode_tile(size_t tile_index) const {
    if (tile_index >= impl_->tiles.size()) {
        return {};
    }

    const auto& tile = impl_->tiles[tile_index];
    if (!tile.valid) {
        return {};
    }

    // Check bounds
    if (tile.offset + tile.size > impl_->data.size()) {
        return {};
    }

    // Return the raw tile data (palette indices)
    return std::vector<uint8_t>(
        impl_->data.begin() + tile.offset,
        impl_->data.begin() + tile.offset + tile.size);
}

std::vector<std::vector<uint8_t>> TmpReader::decode_all_tiles() const {
    std::vector<std::vector<uint8_t>> result;
    result.reserve(impl_->tiles.size());

    for (size_t i = 0; i < impl_->tiles.size(); ++i) {
        result.push_back(decode_tile(i));
    }

    return result;
}

uint32_t TmpReader::valid_tile_count() const {
    return impl_->info.tile_count - impl_->info.empty_count;
}

} // namespace wwd
