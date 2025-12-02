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

static Result<void> parse_fnt(FntReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // FNT header:
    // Offset  Size  Description
    // 0       2     Data offset (start of glyph bitmap data)
    // 2       1     Palette size (usually 0)
    // 3       1     Font height
    // 4       1     Max width
    // 5       1     Unknown (usually 0)
    // Then width table and offset table

    if (data.size() < 6) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT file too small"));
    }

    const uint8_t* p = data.data();

    uint16_t data_offset = read_u16(p);
    impl.info.height = p[3];
    impl.info.max_width = p[4];
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Detect glyph count from data offset
    // Header is 6 bytes, then width table, then offset table
    // data_offset = 6 + glyph_count + glyph_count * 2 = 6 + 3*glyph_count
    if (data_offset <= 6) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT invalid data offset"));
    }

    uint16_t glyph_count = (data_offset - 6) / 3;
    if (glyph_count == 0 || glyph_count > 256) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT invalid glyph count"));
    }

    impl.info.glyph_count = glyph_count;
    impl.info.first_char = 0;
    impl.info.last_char = static_cast<uint8_t>(glyph_count - 1);
    impl.info.data_size = static_cast<uint32_t>(data.size() - data_offset);

    // Validate tables fit
    size_t width_table_offset = 6;
    size_t offset_table_offset = 6 + glyph_count;
    size_t expected_size = offset_table_offset + glyph_count * 2;

    if (data.size() < expected_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "FNT tables truncated"));
    }

    // Read glyph info
    impl.glyphs.reserve(glyph_count);
    const uint8_t* widths = p + width_table_offset;
    const uint8_t* offsets = p + offset_table_offset;

    for (uint16_t i = 0; i < glyph_count; ++i) {
        FntGlyphInfo glyph{};
        glyph.width = widths[i];
        glyph.height = impl.info.height;
        glyph.offset = read_u16(offsets + i * 2);
        impl.glyphs.push_back(glyph);
    }

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
