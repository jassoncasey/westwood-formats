#include <westwood/tmp.h>
#include <westwood/io.h>

#include <cstring>

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

// TS/RA2 isometric tile cell header (52 bytes)
// Offset  Size  Field
// 0       4     X position (cell offset)
// 4       4     Y position (cell offset)
// 8       4     Extra data offset (0 if none)
// 12      4     Z-data offset (0 if none)
// 16      4     Extra Z-data offset (0 if none)
// 20      4     Extra X offset
// 24      4     Extra Y offset
// 28      4     Extra width
// 32      4     Extra height
// 36      1     Flags (bit 0=hasExtra, bit 1=hasZData, bit 2=hasDamaged)
// 37      3     Padding
// 40      1     Height (terrain elevation)
// 41      1     LandType
// 42      1     SlopeType
// 43      3     TopLeft radar color (RGB)
// 46      3     BottomRight radar color (RGB)
// 49      3     Padding
static constexpr size_t TS_TILE_HEADER_SIZE = 52;

static TmpFormat detect_format(std::span<const uint8_t> data) {
    // TS/RA2 TMP detection:
    // - First 16 bytes are the file header
    // - template_width (4), template_height (4), tile_width (4), tile_height (4)
    // - TS uses 48x24 tiles, RA2 uses 60x30 tiles
    // - After header comes the index (offsets to each tile)

    if (data.size() >= 16) {
        uint32_t template_width = read_u32(data.data());
        uint32_t template_height = read_u32(data.data() + 4);
        uint32_t tile_width = read_u32(data.data() + 8);
        uint32_t tile_height = read_u32(data.data() + 12);

        // TS: 48x24 tiles, RA2: 60x30 tiles
        if (tile_width == 48 && tile_height == 24 &&
            template_width > 0 && template_width <= 10 &&
            template_height > 0 && template_height <= 10) {
            return TmpFormat::TS;
        }
        if (tile_width == 60 && tile_height == 30 &&
            template_width > 0 && template_width <= 10 &&
            template_height > 0 && template_height <= 10) {
            return TmpFormat::RA2;
        }
    }

    // TD/RA detection (orthographic 24x24)
    // TD TMP: ID1=0xFFFF + ID2=0x0D1A at offset 0x14 (reads as 0x0D1AFFFF LE)
    // RA TMP: Zero2=0 at offset 0x10, and 0x2C73 at offset 0x1A
    // The 0x2C73 value appears within the IndexImagesInfo offset field in RA files

    if (data.size() >= 24) {
        uint32_t id_field = read_u32(data.data() + 0x14);  // ID1 + ID2

        if (id_field == 0x0D1AFFFF) {
            return TmpFormat::TD;
        }
    }

    if (data.size() >= 28) {
        uint32_t zero2 = read_u32(data.data() + 0x10);
        uint16_t marker = read_u16(data.data() + 0x1A);  // Within IndexImagesInfo

        if (zero2 == 0 && marker == 0x2C73) {
            return TmpFormat::RA;
        }
    }

    // Default to RA format
    return TmpFormat::RA;
}

// Parse TS/RA2 isometric TMP format
static Result<void> parse_tmp_ts(TmpReaderImpl& impl,
                                  std::span<const uint8_t> data,
                                  TmpFormat format) {
    // TS/RA2 TMP header (16 bytes):
    // Offset  Size  Description
    // 0       4     Template width in cells
    // 4       4     Template height in cells
    // 8       4     Tile width (48 for TS, 60 for RA2)
    // 12      4     Tile height (24 for TS, 30 for RA2)
    // Then: index of offsets (4 bytes each, template_width * template_height entries)
    // Then: tile data with 52-byte headers followed by pixel data

    if (data.size() < 16) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TS TMP header too small"));
    }

    const uint8_t* p = data.data();

    impl.info.format = format;
    impl.info.template_width = read_u32(p);
    impl.info.template_height = read_u32(p + 4);
    impl.info.tile_width = static_cast<uint16_t>(read_u32(p + 8));
    impl.info.tile_height = static_cast<uint16_t>(read_u32(p + 12));
    impl.info.file_size = static_cast<uint32_t>(data.size());

    uint32_t tile_count = impl.info.template_width * impl.info.template_height;
    impl.info.tile_count = static_cast<uint16_t>(tile_count);

    // Validate
    if (impl.info.template_width == 0 || impl.info.template_height == 0) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TS TMP invalid template size"));
    }

    if (impl.info.template_width > 10 || impl.info.template_height > 10) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TS TMP template too large"));
    }

    // Index starts immediately after header
    impl.info.index_start = 16;
    impl.info.index_end = 16 + tile_count * 4;

    if (impl.info.index_end > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "TS TMP index truncated"));
    }

    // Calculate isometric diamond pixel count:
    // Diamond shape is tile_width x tile_height but only the diamond area has pixels
    // For a 48x24 tile, the diamond area is: sum of row widths
    // Row widths: 4, 8, 12, ..., 48, ..., 12, 8, 4 (for 24 rows)
    // This equals tile_width * tile_height / 2
    uint32_t diamond_size = (impl.info.tile_width * impl.info.tile_height) / 2;

    impl.tiles.reserve(tile_count);
    const uint8_t* index = p + impl.info.index_start;
    uint16_t empty_count = 0;

    for (uint32_t i = 0; i < tile_count; ++i) {
        uint32_t offset = read_u32(index + i * 4);

        TmpTileInfo tile{};
        tile.offset = offset;
        tile.valid = (offset != 0);

        if (!tile.valid) {
            empty_count++;
            impl.tiles.push_back(tile);
            continue;
        }

        // Validate tile offset
        if (offset + TS_TILE_HEADER_SIZE > data.size()) {
            return std::unexpected(
                make_error(ErrorCode::CorruptIndex, "TS TMP tile header out of bounds"));
        }

        // Parse 52-byte tile cell header
        const uint8_t* th = p + offset;
        tile.x_offset = static_cast<int32_t>(read_u32(th + 0));
        tile.y_offset = static_cast<int32_t>(read_u32(th + 4));
        tile.extra_offset = read_u32(th + 8);   // Relative to tile start
        tile.z_offset = read_u32(th + 12);      // Relative to tile start
        // th + 16 = extra z-data offset (unused)
        tile.extra_x = static_cast<int32_t>(read_u32(th + 20));
        tile.extra_y = static_cast<int32_t>(read_u32(th + 24));
        tile.extra_width = read_u32(th + 28);
        tile.extra_height = read_u32(th + 32);

        uint8_t flags = th[36];
        tile.has_extra = (flags & 0x01) != 0;
        tile.has_z_data = (flags & 0x02) != 0;
        tile.has_damaged = (flags & 0x04) != 0;

        tile.height = th[40];
        tile.land_type = th[41];
        tile.slope_type = th[42];

        // Tile image data starts after the 52-byte header
        tile.size = diamond_size;

        impl.tiles.push_back(tile);
    }

    impl.info.empty_count = empty_count;
    impl.info.image_start = impl.info.index_end;

    return {};
}

// Parse TD/RA orthographic TMP format
static Result<void> parse_tmp_tdra(TmpReaderImpl& impl,
                                    std::span<const uint8_t> data) {
    // TD/RA TMP header (40 bytes):
    // Based on ModdingWiki "Command & Conquer Tileset Format"
    // Offset  Size  Description
    // 0x00    2     TileWidth - always 24
    // 0x02    2     TileHeight - always 24
    // 0x04    2     NrOfTiles - tile count (max 255 usable in maps)
    // 0x06    2     Zero1 - always 0
    // 0x08    4     Size - total file size
    // 0x0C    4     ImgStart - offset of first image data
    // 0x10    4     Zero2 - always 0
    // 0x14    2     ID1 - always 0xFFFF
    // 0x16    2     ID2 - always 0x0D1A (used for TD detection)
    // 0x18    4     IndexImagesInfo - offset of images info array (byte per image)
    // 0x1C    4     IndexTilesetImages - offset of tile-to-image index
    //
    // Detection: TD has 0x0D1AFFFF at offset 0x14, RA has 0 at offset 0x14
    // and 0x2C73 at offset 0x1A (within IndexImagesInfo field)

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
    impl.info.template_width = 1;
    impl.info.template_height = impl.info.tile_count;

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

static Result<void> parse_tmp(TmpReaderImpl& impl,
                               std::span<const uint8_t> data) {
    TmpFormat format = detect_format(data);

    if (format == TmpFormat::TS || format == TmpFormat::RA2) {
        return parse_tmp_ts(impl, data, format);
    }

    return parse_tmp_tdra(impl, data);
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

    const auto& info = impl_->info;

    if (info.format == TmpFormat::TS || info.format == TmpFormat::RA2) {
        // TS/RA2: tile data starts after 52-byte header
        size_t data_offset = tile.offset + TS_TILE_HEADER_SIZE;
        if (data_offset + tile.size > impl_->data.size()) {
            return {};
        }

        return std::vector<uint8_t>(
            impl_->data.begin() + data_offset,
            impl_->data.begin() + data_offset + tile.size);
    }

    // TD/RA: tile data starts at offset directly
    if (tile.offset + tile.size > impl_->data.size()) {
        return {};
    }

    return std::vector<uint8_t>(
        impl_->data.begin() + tile.offset,
        impl_->data.begin() + tile.offset + tile.size);
}

std::vector<uint8_t> TmpReader::decode_extra(size_t tile_index) const {
    if (tile_index >= impl_->tiles.size()) {
        return {};
    }

    const auto& info = impl_->info;

    // Extra images only exist in TS/RA2 format
    if (info.format != TmpFormat::TS && info.format != TmpFormat::RA2) {
        return {};
    }

    const auto& tile = impl_->tiles[tile_index];
    if (!tile.valid || !tile.has_extra) {
        return {};
    }

    if (tile.extra_width == 0 || tile.extra_height == 0) {
        return {};
    }

    // Extra data offset is relative to tile start
    size_t extra_data_offset = tile.offset + tile.extra_offset;
    size_t extra_size = tile.extra_width * tile.extra_height;

    if (extra_data_offset + extra_size > impl_->data.size()) {
        return {};
    }

    return std::vector<uint8_t>(
        impl_->data.begin() + extra_data_offset,
        impl_->data.begin() + extra_data_offset + extra_size);
}

std::vector<uint8_t> TmpReader::decode_z_data(size_t tile_index) const {
    if (tile_index >= impl_->tiles.size()) {
        return {};
    }

    const auto& info = impl_->info;

    // Z-data only exists in TS/RA2 format
    if (info.format != TmpFormat::TS && info.format != TmpFormat::RA2) {
        return {};
    }

    const auto& tile = impl_->tiles[tile_index];
    if (!tile.valid || !tile.has_z_data) {
        return {};
    }

    // Z-data offset is relative to tile start
    // Z-data is one byte per pixel in the diamond
    size_t z_data_offset = tile.offset + tile.z_offset;

    if (z_data_offset + tile.size > impl_->data.size()) {
        return {};
    }

    return std::vector<uint8_t>(
        impl_->data.begin() + z_data_offset,
        impl_->data.begin() + z_data_offset + tile.size);
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

bool TmpReader::is_isometric() const {
    return impl_->info.format == TmpFormat::TS ||
           impl_->info.format == TmpFormat::RA2;
}

} // namespace wwd
