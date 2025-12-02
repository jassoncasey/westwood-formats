#include <westwood/fnt.h>
#include <westwood/io.h>

#include <algorithm>

namespace wwd {

struct FntReaderImpl {
    FntInfo info{};
    std::vector<FntGlyphInfo> glyphs;
};

struct FntReader::Impl : FntReaderImpl {};

FntReader::FntReader() : impl_(std::make_unique<Impl>()) {}
FntReader::~FntReader() = default;

const FntInfo& FntReader::info() const { return impl_->info; }
const std::vector<FntGlyphInfo>& FntReader::glyphs() const {
    return impl_->glyphs;
}

// FNT Version 3 (TD/RA) format:
//
// Header (14 bytes):
//   0-1:   uint16  FileSize      Total file size
//   2:     uint8   CompMethod    Always 0x00
//   3:     uint8   NumBlks       Always 0x05
//   4-5:   uint16  InfoBlk       FontInfo offset (typically 0x0E)
//   6-7:   uint16  OffsetBlk     Glyph offset array location
//   8-9:   uint16  WidthBlk      Width array location
//   10-11: uint16  DataBlk       (unused)
//   12-13: uint16  HeightBlk     Height/Y-offset array location
//
// FontInfo block (6 bytes at InfoBlk):
//   0-2:   uint8[3]  Reserved    Fixed: 0x12, 0x10, 0x00
//   3:     uint8     NrOfChars   Last character index
//   4:     uint8     MaxHeight   Maximum glyph height
//   5:     uint8     MaxWidth    Maximum glyph width

static Result<void> parse_fnt(FntReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // Minimum header: 14 bytes + 6 bytes FontInfo
    if (data.size() < 20) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT file too small"));
    }

    SpanReader r(data);

    // Read main header
    auto file_size = r.read_u16();
    if (!file_size) return std::unexpected(file_size.error());
    impl.info.file_size = *file_size;

    auto comp_method = r.read_u8();
    if (!comp_method) return std::unexpected(comp_method.error());
    if (*comp_method != 0x00) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "FNT compressed"));
    }

    auto num_blks = r.read_u8();
    if (!num_blks) return std::unexpected(num_blks.error());
    if (*num_blks != 0x05) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "FNT block count != 5"));
    }

    auto info_blk = r.read_u16();
    if (!info_blk) return std::unexpected(info_blk.error());

    auto offset_blk = r.read_u16();
    if (!offset_blk) return std::unexpected(offset_blk.error());

    auto width_blk = r.read_u16();
    if (!width_blk) return std::unexpected(width_blk.error());

    auto data_blk = r.read_u16();
    if (!data_blk) return std::unexpected(data_blk.error());

    auto height_blk = r.read_u16();
    if (!height_blk) return std::unexpected(height_blk.error());

    // Validate info block location
    if (*info_blk + 6 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT FontInfo out of bounds"));
    }

    // Read FontInfo block
    const uint8_t* info_p = data.data() + *info_blk;
    // Skip reserved bytes (0x12, 0x10, 0x00)
    uint8_t nr_of_chars = info_p[3];
    impl.info.height = info_p[4];
    impl.info.max_width = info_p[5];

    // Number of glyphs = nr_of_chars + 1 (0 to nr_of_chars inclusive)
    uint16_t glyph_count = static_cast<uint16_t>(nr_of_chars) + 1;
    impl.info.glyph_count = glyph_count;
    impl.info.first_char = 0;
    impl.info.last_char = nr_of_chars;

    // Validate arrays fit in file
    if (*offset_blk + glyph_count * 2 > data.size() ||
        *width_blk + glyph_count > data.size() ||
        *height_blk + glyph_count * 2 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "FNT arrays out of bounds"));
    }

    // Read glyph info
    impl.glyphs.reserve(glyph_count);
    const uint8_t* offsets = data.data() + *offset_blk;
    const uint8_t* widths = data.data() + *width_blk;
    const uint8_t* heights = data.data() + *height_blk;

    for (uint16_t i = 0; i < glyph_count; ++i) {
        FntGlyphInfo glyph{};
        glyph.offset = read_u16(offsets + i * 2);
        glyph.width = widths[i];
        // Height block has 2 bytes per glyph: y_offset, height
        // We just read the height for now
        glyph.height = heights[i * 2 + 1];
        impl.glyphs.push_back(glyph);
    }

    impl.info.data_size = static_cast<uint32_t>(data.size() - *info_blk);

    return {};
}

Result<std::unique_ptr<FntReader>> FntReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());
    return open(std::span(*data));
}

Result<std::unique_ptr<FntReader>> FntReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<FntReader>(new FntReader());
    auto result = parse_fnt(*reader->impl_, data);
    if (!result) return std::unexpected(result.error());
    return reader;
}

} // namespace wwd
