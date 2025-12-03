#include <westwood/fnt.h>
#include <westwood/io.h>

#include <algorithm>
#include <cstring>
#include <optional>

namespace wwd {

struct FntReaderImpl {
    FntInfo info{};
    std::vector<FntGlyphInfo> glyphs;
    std::vector<uint8_t> data;  // Full file data for decoding
    std::string source_filename;
    uint32_t data_blk_offset = 0;  // Offset to glyph data
    // Unicode -> glyph index (for UnicodeBitFont)
    std::vector<uint16_t> unicode_table;
};

struct FntReader::Impl : FntReaderImpl {};

FntReader::FntReader() : impl_(std::make_unique<Impl>()) {}
FntReader::~FntReader() = default;

const FntInfo& FntReader::info() const { return impl_->info; }
const std::vector<FntGlyphInfo>& FntReader::glyphs() const {
    return impl_->glyphs;
}
const std::string& FntReader::source_filename() const {
    return impl_->source_filename;
}

// Detect FNT format from header
// Returns nullopt if format cannot be detected
static std::optional<FntFormat> detect_fnt_format(
    std::span<const uint8_t> data)
{
    if (data.size() < 4) {
        return std::nullopt;  // Too small to detect
    }

    // Unicode BitFont: starts with "fonT"
    if (std::memcmp(data.data(), "fonT", 4) == 0) {
        return FntFormat::UnicodeBitFont;
    }

    // BitFont: starts with "FoNt" (RA2) or "tNoF" (Nox, reversed)
    if (std::memcmp(data.data(), "FoNt", 4) == 0 ||
        std::memcmp(data.data(), "tNoF", 4) == 0) {
        return FntFormat::BitFont;
    }

    // V2/V3/V4 detection via CompMethod/NumBlks at offset 2-3
    uint8_t comp_method = data[2];
    uint8_t num_blks = data[3];

    // V2: CompMethod=0, NumBlks=4 (uses 4 data blocks)
    if (comp_method == 0x00 && num_blks == 0x04) {
        return FntFormat::V2;
    }
    // V3: CompMethod=0, NumBlks=5 (4-bit grayscale)
    if (comp_method == 0x00 && num_blks == 0x05) {
        return FntFormat::V3;
    }
    // V4: CompMethod=2, NumBlks=0 (8-bit grayscale)
    if (comp_method == 0x02 && num_blks == 0x00) {
        return FntFormat::V4;
    }

    return std::nullopt;  // Unknown format
}

// FNT Version 2 format (1-bit monochrome, 128 chars)
// Used in: BattleTech, Eye of the Beholder 1/2
//
// Header (260 bytes = 0x104):
//   0-1:     uint16  Size          File size - 2
//   2-257:   uint16[128] Offsets   Offset for each of 128 glyphs
//   258:     uint8   Height        Global glyph height
//   259:     uint8   Width         Global glyph width (max 8)
static Result<void> parse_fnt_v2(FntReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 0x104) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT v2 header too small"));
    }

    impl.info.format = FntFormat::V2;
    impl.info.file_size = static_cast<uint32_t>(data.size());
    impl.info.bits_per_pixel = 1;

    // Read header
    uint16_t size = read_u16(data.data());
    uint8_t height = data[0x102];
    uint8_t width = data[0x103];

    impl.info.height = height;
    impl.info.max_width = width;
    impl.info.glyph_count = 128;
    impl.info.first_char = 0;
    impl.info.last_char = 127;
    impl.info.data_size = size;

    // Read glyph offsets
    impl.glyphs.reserve(128);
    const uint8_t* offsets = data.data() + 2;

    for (int i = 0; i < 128; ++i) {
        FntGlyphInfo glyph{};
        glyph.offset = read_u16(offsets + i * 2);
        glyph.width = width;   // Global width
        glyph.height = height; // Global height
        glyph.y_offset = 0;
        impl.glyphs.push_back(glyph);
    }

    impl.data_blk_offset = 0x104;  // Data starts after header

    return {};
}

// FNT Version 3 (TD/RA) format (4-bit grayscale)
//
// Header (14 bytes):
//   0-1:   uint16  FileSize      Total file size
//   2:     uint8   CompMethod    Always 0x00
//   3:     uint8   NumBlks       Always 0x05
//   4-5:   uint16  InfoBlk       FontInfo offset (typically 0x0E)
//   6-7:   uint16  OffsetBlk     Glyph offset array location
//   8-9:   uint16  WidthBlk      Width array location
//   10-11: uint16  DataBlk       Start of font data
//   12-13: uint16  HeightBlk     Height/Y-offset array location
//
// FontInfo block (6 bytes at InfoBlk):
//   0-2:   uint8[3]  Reserved    Fixed: 0x12, 0x10, 0x00
//   3:     uint8     NrOfChars   Last character index
//   4:     uint8     MaxHeight   Maximum glyph height
//   5:     uint8     MaxWidth    Maximum glyph width
static Result<void> parse_fnt_v3(FntReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 20) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT v3 file too small"));
    }

    impl.info.format = FntFormat::V3;
    impl.info.bits_per_pixel = 4;

    SpanReader r(data);

    auto file_size = r.read_u16();
    if (!file_size) return std::unexpected(file_size.error());
    impl.info.file_size = *file_size;

    r.skip(2);  // Skip CompMethod (0x00) and NumBlks (0x05)

    auto info_blk = r.read_u16();
    if (!info_blk) return std::unexpected(info_blk.error());

    auto offset_blk = r.read_u16();
    if (!offset_blk) return std::unexpected(offset_blk.error());

    auto width_blk = r.read_u16();
    if (!width_blk) return std::unexpected(width_blk.error());

    auto data_blk = r.read_u16();
    if (!data_blk) return std::unexpected(data_blk.error());
    impl.data_blk_offset = *data_blk;

    auto height_blk = r.read_u16();
    if (!height_blk) return std::unexpected(height_blk.error());

    if (*info_blk + 6 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT FontInfo out of bounds"));
    }

    // Read FontInfo block
    const uint8_t* info_p = data.data() + *info_blk;
    uint8_t nr_of_chars = info_p[3];
    impl.info.height = info_p[4];
    impl.info.max_width = info_p[5];

    uint16_t glyph_count = static_cast<uint16_t>(nr_of_chars) + 1;
    impl.info.glyph_count = glyph_count;
    impl.info.first_char = 0;
    impl.info.last_char = nr_of_chars;

    if (*offset_blk + glyph_count * 2 > data.size() ||
        *width_blk + glyph_count > data.size() ||
        *height_blk + glyph_count * 2 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "FNT arrays out of bounds"));
    }

    impl.glyphs.reserve(glyph_count);
    const uint8_t* offsets = data.data() + *offset_blk;
    const uint8_t* widths = data.data() + *width_blk;
    const uint8_t* heights = data.data() + *height_blk;

    for (uint16_t i = 0; i < glyph_count; ++i) {
        FntGlyphInfo glyph{};
        glyph.offset = read_u16(offsets + i * 2);
        glyph.width = widths[i];
        glyph.y_offset = heights[i * 2];
        glyph.height = heights[i * 2 + 1];
        impl.glyphs.push_back(glyph);
    }

    impl.info.data_size = static_cast<uint32_t>(data.size() - *info_blk);

    return {};
}

// FNT Version 4 format (8-bit grayscale)
// Used in: Tiberian Sun, RA2, Lands of Lore 3
//
// Header (16 bytes = 0x10):
//   0-1:   uint16  FontLength      Total file size
//   2:     uint8   FontCompress    0x02 for v4
//   3:     uint8   FontDataBlocks  0x00 for v4
//   4-5:   uint16  InfoBlockOffset Always 0x0E (right after header)
//   6-7:   uint16  OffsetBlockOffset
//   8-9:   uint16  WidthBlockOffset
//   10-11: uint16  DataBlockOffset Start of glyph data
//   12-13: uint16  HeightOffset
//   14-15: uint16  (padding/reserved)
//
// InfoBlock (6 bytes at offset 0x0E):
//   0-3:   uint32  unused (0)
//   4:     uint8   MaxHeight
//   5:     uint8   MaxWidth
static Result<void> parse_fnt_v4(FntReaderImpl& impl,
                                  std::span<const uint8_t> data) {
    if (data.size() < 0x14) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "FNT v4 file too small"));
    }

    impl.info.format = FntFormat::V4;
    impl.info.bits_per_pixel = 8;

    const uint8_t* p = data.data();

    impl.info.file_size = read_u16(p);
    // Skip FontCompress (0x02), FontDataBlocks (0x00)
    uint16_t info_blk = read_u16(p + 4);
    uint16_t offset_blk = read_u16(p + 6);
    uint16_t width_blk = read_u16(p + 8);
    uint16_t data_blk = read_u16(p + 10);
    uint16_t height_blk = read_u16(p + 12);

    impl.data_blk_offset = data_blk;

    if (info_blk + 6 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader,
                       "FNT v4 InfoBlock out of bounds"));
    }

    // Read InfoBlock
    const uint8_t* info_p = data.data() + info_blk;
    impl.info.height = info_p[4];
    impl.info.max_width = info_p[5];

    // Calculate glyph count from offset array size
    // The offset array runs from offset_blk to width_blk
    uint16_t glyph_count = (width_blk - offset_blk) / 2;
    impl.info.glyph_count = glyph_count;
    impl.info.first_char = 0;
    impl.info.last_char = static_cast<uint8_t>(
        glyph_count > 0 ? glyph_count - 1 : 0);

    if (offset_blk + glyph_count * 2 > data.size() ||
        width_blk + glyph_count > data.size() ||
        height_blk + glyph_count * 2 > data.size()) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "FNT v4 arrays out of bounds"));
    }

    impl.glyphs.reserve(glyph_count);
    const uint8_t* offsets = data.data() + offset_blk;
    const uint8_t* widths = data.data() + width_blk;
    const uint8_t* heights = data.data() + height_blk;

    for (uint16_t i = 0; i < glyph_count; ++i) {
        FntGlyphInfo glyph{};
        glyph.offset = read_u16(offsets + i * 2);
        glyph.width = widths[i];
        glyph.y_offset = heights[i * 2];
        glyph.height = heights[i * 2 + 1];
        impl.glyphs.push_back(glyph);
    }

    impl.info.data_size = static_cast<uint32_t>(data.size() - data_blk);

    return {};
}

// BitFont format (1-bit, RA2/Nox)
//
// RA2 Header (0x30 bytes):
//   0-3:   char[4]   "FoNt"
//   4-7:   uint32    FontWidth? (default space width)
//   8-11:  uint32    Stride (bytes per scanline)
//   12-15: uint32    Lines (pixel lines per symbol)
//   16-19: uint32    FontHeight (total height with padding)
//   20-23: uint32    0x01
//   24-27: uint32    SymbolDataSize (1 + Stride*Lines)
//   28-31: uint32    0x24
//   32-35: uint32    0x30 (header size)
//   36-39: uint32    0x00
//   40-43: uint32    StartSymbol
//   44-47: uint32    EndSymbol
//
// Nox Header (0x24 bytes):
//   0-3:   char[4]   "tNoF" (reversed)
//   4-7:   uint32    0x01
//   8-11:  uint32    FontWidth?
//   12-15: uint32    Stride
//   16-19: uint32    Lines (also FontHeight)
//   20-23: uint32    0x01
//   24-27: uint32    SymbolDataSize
//   28-31: uint32    0x00
//   32-33: uint16    StartSymbol
//   34-35: uint16    EndSymbol
static Result<void> parse_fnt_bitfont(FntReaderImpl& impl,
                                       std::span<const uint8_t> data) {
    if (data.size() < 0x24) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "BitFont header too small"));
    }

    impl.info.format = FntFormat::BitFont;
    impl.info.bits_per_pixel = 1;
    impl.info.file_size = static_cast<uint32_t>(data.size());

    const uint8_t* p = data.data();
    bool is_ra2 = (std::memcmp(p, "FoNt", 4) == 0);
    bool is_nox = (std::memcmp(p, "tNoF", 4) == 0);

    if (!is_ra2 && !is_nox) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "Unknown BitFont magic"));
    }

    uint32_t stride, lines, font_height, symbol_data_size;
    uint32_t start_symbol, end_symbol;
    uint32_t header_size;

    if (is_ra2) {
        if (data.size() < 0x30) {
            return std::unexpected(
                make_error(ErrorCode::CorruptHeader,
                           "RA2 BitFont header too small"));
        }
        stride = read_u32(p + 8);
        lines = read_u32(p + 12);
        font_height = read_u32(p + 16);
        symbol_data_size = read_u32(p + 24);
        start_symbol = read_u32(p + 40);
        end_symbol = read_u32(p + 44);
        header_size = 0x30;
    } else {
        // Nox format
        stride = read_u32(p + 12);
        lines = read_u32(p + 16);
        font_height = lines;  // Same as lines in Nox
        symbol_data_size = read_u32(p + 24);
        start_symbol = read_u16(p + 32);
        end_symbol = read_u16(p + 34);
        header_size = 0x24;
    }

    impl.info.stride = stride;
    impl.info.height = static_cast<uint8_t>(font_height);
    // Max possible width
    impl.info.max_width = static_cast<uint8_t>(stride * 8);
    impl.info.first_char = static_cast<uint8_t>(start_symbol);
    impl.info.last_char = static_cast<uint8_t>(end_symbol);

    uint32_t glyph_count = end_symbol - start_symbol + 1;
    impl.info.glyph_count = static_cast<uint16_t>(glyph_count);

    // Read glyph data
    // Each glyph: 1 byte width + (stride * lines) bytes bitmap
    impl.glyphs.reserve(glyph_count);
    impl.data_blk_offset = static_cast<uint16_t>(header_size);

    for (uint32_t i = 0; i < glyph_count; ++i) {
        size_t glyph_offset = header_size + i * symbol_data_size;
        if (glyph_offset >= data.size()) {
            break;
        }

        FntGlyphInfo glyph{};
        glyph.width = data[glyph_offset];
        glyph.height = static_cast<uint8_t>(lines);
        glyph.y_offset = 0;
        glyph.offset = static_cast<uint16_t>(glyph_offset);
        impl.glyphs.push_back(glyph);
    }

    impl.info.data_size = static_cast<uint32_t>(data.size() - header_size);

    return {};
}

// Unicode BitFont format (1-bit, up to 65K glyphs)
//
// Header (0x2001C bytes):
//   0-3:       char[4]     "fonT"
//   4-7:       uint32      IdeographWidth (width for 0x3000)
//   8-11:      uint32      Stride (bytes per scanline)
//   12-15:     uint32      Lines (pixel lines per symbol)
//   16-19:     uint32      FontHeight (total height)
//   20-23:     uint32      Count (number of stored glyphs)
//   24-27:     uint32      SymbolDataSize (1 + Stride*Lines)
//   28-0x2001B: uint16[0x10000] UnicodeTable (maps Unicode to glyph index)
//
// Glyph data starts at 0x2001C:
//   For each glyph:
//     0:        uint8       SymbolWidth
//     1+:       byte[Stride*Lines] 1-bit bitmap
static Result<void> parse_fnt_unicode_bitfont(FntReaderImpl& impl,
                                               std::span<const uint8_t> data) {
    // Header is 0x1C bytes + 0x20000 bytes for Unicode table = 0x2001C
    constexpr size_t UNICODE_TABLE_OFFSET = 0x1C;
    // 65536 entries * 2 bytes
    constexpr size_t UNICODE_TABLE_SIZE = 0x10000 * 2;
    // 0x2001C
    constexpr size_t HEADER_SIZE = UNICODE_TABLE_OFFSET + UNICODE_TABLE_SIZE;

    if (data.size() < HEADER_SIZE) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader,
                       "Unicode BitFont header too small"));
    }

    impl.info.format = FntFormat::UnicodeBitFont;
    impl.info.bits_per_pixel = 1;
    impl.info.file_size = static_cast<uint32_t>(data.size());

    const uint8_t* p = data.data();

    // Verify magic
    if (std::memcmp(p, "fonT", 4) != 0) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat,
                       "Invalid Unicode BitFont magic"));
    }

    uint32_t ideograph_width = read_u32(p + 4);
    uint32_t stride = read_u32(p + 8);
    uint32_t lines = read_u32(p + 12);
    uint32_t font_height = read_u32(p + 16);
    uint32_t glyph_count = read_u32(p + 20);
    uint32_t symbol_data_size = read_u32(p + 24);

    impl.info.stride = stride;
    impl.info.height = static_cast<uint8_t>(font_height);
    impl.info.max_width = static_cast<uint8_t>(stride * 8);
    impl.info.glyph_count = static_cast<uint16_t>(glyph_count);
    impl.info.first_char = 0;
    impl.info.last_char = 0xFFFF;  // Full Unicode BMP range
    impl.data_blk_offset = HEADER_SIZE;

    // Read Unicode table (65536 entries of uint16)
    impl.unicode_table.resize(0x10000);
    const uint8_t* table = p + UNICODE_TABLE_OFFSET;
    for (size_t i = 0; i < 0x10000; ++i) {
        impl.unicode_table[i] = read_u16(table + i * 2);
    }

    // Pre-allocate glyphs array
    impl.glyphs.resize(glyph_count);

    // Read glyph data
    for (uint32_t i = 0; i < glyph_count; ++i) {
        size_t glyph_offset = HEADER_SIZE + i * symbol_data_size;
        if (glyph_offset >= data.size()) {
            break;
        }

        impl.glyphs[i].width = data[glyph_offset];
        impl.glyphs[i].height = static_cast<uint8_t>(lines);
        impl.glyphs[i].y_offset = 0;
        impl.glyphs[i].offset = static_cast<uint32_t>(glyph_offset);
    }

    impl.info.data_size = static_cast<uint32_t>(data.size() - HEADER_SIZE);

    // Store ideograph width for special handling (unused for now but parsed)
    (void)ideograph_width;

    return {};
}

static Result<void> parse_fnt(FntReaderImpl& impl,
                               std::span<const uint8_t> data) {
    auto format = detect_fnt_format(data);

    if (!format) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "Unknown FNT format"));
    }

    switch (*format) {
        case FntFormat::V2:
            return parse_fnt_v2(impl, data);
        case FntFormat::V3:
            return parse_fnt_v3(impl, data);
        case FntFormat::V4:
            return parse_fnt_v4(impl, data);
        case FntFormat::BitFont:
            return parse_fnt_bitfont(impl, data);
        case FntFormat::UnicodeBitFont:
            return parse_fnt_unicode_bitfont(impl, data);
    }

    // Should not reach here, but satisfy compiler
    return std::unexpected(
        make_error(ErrorCode::UnsupportedFormat, "Unknown FNT format"));
}

Result<std::unique_ptr<FntReader>> FntReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<FntReader>(new FntReader());
    reader->impl_->data = std::move(*data);

    // Extract filename from path
    size_t pos = path.find_last_of("/\\");
    reader->impl_->source_filename = (pos != std::string::npos)
        ? path.substr(pos + 1) : path;

    auto result = parse_fnt(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

Result<std::unique_ptr<FntReader>> FntReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<FntReader>(new FntReader());
    reader->impl_->data.assign(data.begin(), data.end());
    reader->impl_->source_filename = "unknown.fnt";

    auto result = parse_fnt(*reader->impl_, std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

// Decode V2: 1-bit monochrome, 1 byte per row
static bool decode_glyph_v2(
    const uint8_t* src,
    uint8_t w, uint8_t h,
    std::vector<uint8_t>& out)
{
    for (uint8_t y = 0; y < h; ++y) {
        uint8_t row = src[y];
        for (uint8_t x = 0; x < w && x < 8; ++x) {
            out[y * w + x] = ((row >> (7 - x)) & 1) ? 255 : 0;
        }
    }
    return true;
}

// Decode V3: 4-bit grayscale, 2 pixels per byte
static bool decode_glyph_v3(
    const uint8_t* src,
    uint8_t w, uint8_t h,
    std::vector<uint8_t>& out)
{
    size_t row_bytes = (w + 1) / 2;
    for (uint8_t y = 0; y < h; ++y) {
        for (uint8_t x = 0; x < w; ++x) {
            uint8_t packed = src[y * row_bytes + x / 2];
            uint8_t val = (x & 1) ? (packed & 0x0F) : ((packed >> 4) & 0x0F);
            out[y * w + x] = val * 17;
        }
    }
    return true;
}

// Decode BitFont: 1-bit monochrome with stride
static bool decode_glyph_bitfont(
    const uint8_t* src,
    uint8_t w, uint8_t h,
    uint32_t stride,
    std::vector<uint8_t>& out)
{
    for (uint8_t y = 0; y < h; ++y) {
        for (uint8_t x = 0; x < w; ++x) {
            size_t byte_idx = y * stride + x / 8;
            uint8_t bit_pos = 7 - (x % 8);
            out[y * w + x] = ((src[byte_idx] >> bit_pos) & 1) ? 255 : 0;
        }
    }
    return true;
}

std::vector<uint8_t> FntReader::decode_glyph(size_t glyph_index) const {
    if (glyph_index >= impl_->glyphs.size()) return {};

    const auto& g = impl_->glyphs[glyph_index];
    const auto& info = impl_->info;
    if (g.width == 0 || g.height == 0) return {};

    std::vector<uint8_t> result(g.width * g.height);
    const auto& data = impl_->data;

    switch (info.format) {
        case FntFormat::V2:
            if (g.offset + g.height > data.size()) return {};
            decode_glyph_v2(data.data() + g.offset, g.width, g.height, result);
            break;

        case FntFormat::V3: {
            size_t sz = ((g.width + 1) / 2) * g.height;
            if (g.offset + sz > data.size()) return {};
            decode_glyph_v3(data.data() + g.offset, g.width, g.height, result);
            break;
        }

        case FntFormat::V4: {
            size_t sz = g.width * g.height;
            size_t off = impl_->data_blk_offset + g.offset;
            if (off + sz > data.size()) return {};
            std::memcpy(result.data(), data.data() + off, sz);
            break;
        }

        case FntFormat::BitFont:
        case FntFormat::UnicodeBitFont: {
            size_t off = g.offset + 1;
            if (off + info.stride * g.height > data.size()) return {};
            decode_glyph_bitfont(
                data.data() + off, g.width, g.height, info.stride, result);
            break;
        }

        default:
            return {};
    }

    return result;
}

int32_t FntReader::glyph_index_for_char(uint16_t code_point) const {
    if (impl_->info.format == FntFormat::UnicodeBitFont) {
        // Use Unicode table lookup
        if (code_point < impl_->unicode_table.size()) {
            uint16_t index = impl_->unicode_table[code_point];
            // Index 0 means empty/not present, actual indices are 1-based
            if (index == 0) {
                return -1;
            }
            return static_cast<int32_t>(index - 1);
        }
        return -1;
    }

    // For other formats, code_point is the glyph index if in range
    if (code_point >= impl_->info.first_char &&
        code_point <= impl_->info.last_char) {
        return static_cast<int32_t>(code_point - impl_->info.first_char);
    }
    return -1;
}

std::vector<uint8_t> FntReader::decode_char(uint16_t code_point) const {
    int32_t index = glyph_index_for_char(code_point);
    if (index < 0) {
        return {};
    }
    return decode_glyph(static_cast<size_t>(index));
}

} // namespace wwd
