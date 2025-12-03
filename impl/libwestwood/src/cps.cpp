#include <westwood/cps.h>
#include <westwood/io.h>
#include <westwood/lcw.h>

#include <array>

namespace wwd {

// Westwood RLE decompression (CPS compression type 3)
// Format: count byte followed by value byte
// - If count has bit 7 set: repeat next byte (count & 0x7F) times
// - Otherwise: copy next (count) literal bytes
static Result<std::vector<uint8_t>> rle_decompress(
    std::span<const uint8_t> input, size_t output_size)
{
    std::vector<uint8_t> output;
    output.reserve(output_size);

    size_t pos = 0;
    while (pos < input.size() && output.size() < output_size) {
        uint8_t cmd = input[pos++];

        if (cmd & 0x80) {
            // Run: repeat next byte (cmd & 0x7F) times
            uint16_t count = cmd & 0x7F;
            if (count == 0) {
                // Extended count: 16-bit little-endian
                if (pos + 2 > input.size()) break;
                count = input[pos] | (input[pos + 1] << 8);
                pos += 2;
            }
            if (pos >= input.size()) break;
            uint8_t value = input[pos++];
            for (uint16_t i = 0; i < count; ++i) {
                if (output.size() >= output_size) break;
                output.push_back(value);
            }
        } else {
            // Literal: copy next (cmd) bytes
            uint8_t count = cmd;
            if (count == 0) continue;
            for (int i = 0; i < count; ++i) {
                if (pos >= input.size()) break;
                if (output.size() >= output_size) break;
                output.push_back(input[pos++]);
            }
        }
    }

    // Pad with zeros if needed
    while (output.size() < output_size) {
        output.push_back(0);
    }

    return output;
}

// LZW constants
static constexpr int LZW_CLEAR = 256;
static constexpr int LZW_END = 257;
static constexpr int LZW_FIRST = 258;

// LZW decoder state
struct LzwState {
    std::vector<std::vector<uint8_t>> dict;
    std::span<const uint8_t> input;
    size_t bit_pos = 0;
    int code_bits = 9;
    int next_code = LZW_FIRST;
    int max_bits;
    int max_code;

    LzwState(std::span<const uint8_t> in, int mb)
        : input(in), max_bits(mb), max_code((1 << mb) - 1) {}
};

// Initialize/reset LZW dictionary
static void lzw_reset_dict(LzwState& st) {
    st.dict.clear();
    st.dict.resize(LZW_FIRST);
    for (int i = 0; i < 256; ++i) {
        st.dict[i] = {static_cast<uint8_t>(i)};
    }
    st.dict[LZW_CLEAR] = {};
    st.dict[LZW_END] = {};
    st.code_bits = 9;
    st.next_code = LZW_FIRST;
}

// Read variable-length LZW code from bitstream
static int lzw_read_code(LzwState& st) {
    if (st.bit_pos + st.code_bits > st.input.size() * 8) return LZW_END;
    int code = 0;
    for (int i = 0; i < st.code_bits; ++i) {
        size_t byte_idx = (st.bit_pos + i) / 8;
        int bit_idx = (st.bit_pos + i) % 8;
        if (st.input[byte_idx] & (1 << bit_idx)) code |= (1 << i);
    }
    st.bit_pos += st.code_bits;
    return code;
}

// Output dictionary entry to output buffer
static void lzw_output_entry(
    int code,
    const LzwState& st,
    std::vector<uint8_t>& out,
    size_t limit)
{
    if (code < static_cast<int>(st.dict.size()) && !st.dict[code].empty()) {
        for (uint8_t b : st.dict[code]) {
            if (out.size() >= limit) break;
            out.push_back(b);
        }
    } else if (code < 256) {
        out.push_back(static_cast<uint8_t>(code));
    }
}

// Get entry for code (handling special case)
static std::vector<uint8_t> lzw_get_entry(
    int code, int prev, const LzwState& st)
{
    int dict_size = static_cast<int>(st.dict.size());
    if (code < dict_size && !st.dict[code].empty()) {
        return st.dict[code];
    }
    if (code == st.next_code && prev < dict_size) {
        if (!st.dict[prev].empty()) {
            auto e = st.dict[prev];
            e.push_back(e[0]);
            return e;
        }
    }
    return {};
}

// Add new dictionary entry
static void lzw_add_entry(
    int prev, const std::vector<uint8_t>& entry, LzwState& st)
{
    int dict_size = static_cast<int>(st.dict.size());
    if (st.next_code > st.max_code || prev >= dict_size) return;
    std::vector<uint8_t> ne;
    if (!st.dict[prev].empty()) {
        ne = st.dict[prev];
    } else if (prev < 256) {
        ne = {static_cast<uint8_t>(prev)};
    }
    if (!entry.empty()) ne.push_back(entry[0]);
    if (st.next_code < static_cast<int>(st.dict.size())) {
        st.dict[st.next_code] = std::move(ne);
    } else {
        st.dict.push_back(std::move(ne));
    }
    st.next_code++;
    if (st.next_code > (1 << st.code_bits) && st.code_bits < st.max_bits) {
        st.code_bits++;
    }
}

// Westwood LZW decompression (CPS compression types 1 and 2)
static Result<std::vector<uint8_t>> lzw_decompress(
    std::span<const uint8_t> input, size_t output_size, int max_bits)
{
    std::vector<uint8_t> output;
    output.reserve(output_size);
    LzwState st(input, max_bits);
    lzw_reset_dict(st);

    int prev = lzw_read_code(st);
    if (prev == LZW_CLEAR) prev = lzw_read_code(st);
    if (prev == LZW_END) return output;
    lzw_output_entry(prev, st, output, output_size);

    while (output.size() < output_size) {
        int code = lzw_read_code(st);
        if (code == LZW_END) break;
        if (code == LZW_CLEAR) {
            lzw_reset_dict(st);
            prev = lzw_read_code(st);
            if (prev == LZW_END) break;
            lzw_output_entry(prev, st, output, output_size);
            continue;
        }
        auto entry = lzw_get_entry(code, prev, st);
        if (entry.empty()) break;
        for (uint8_t b : entry) {
            if (output.size() >= output_size) break;
            output.push_back(b);
        }
        lzw_add_entry(prev, entry, st);
        prev = code;
    }

    while (output.size() < output_size) output.push_back(0);
    return output;
}

struct CpsReaderImpl {
    CpsInfo info{};
    std::vector<uint8_t> pixels;
    std::array<Color, 256> palette{};
    bool has_palette = false;
};

struct CpsReader::Impl : CpsReaderImpl {};

CpsReader::CpsReader() : impl_(std::make_unique<Impl>()) {}
CpsReader::~CpsReader() = default;

const CpsInfo& CpsReader::info() const { return impl_->info; }
const std::vector<uint8_t>& CpsReader::pixels() const { return impl_->pixels; }

const std::array<Color, 256>* CpsReader::palette() const {
    return impl_->has_palette ? &impl_->palette : nullptr;
}

// Read CPS header fields
static Result<void> read_cps_header(SpanReader& r, CpsInfo& info) {
    auto file_size = r.read_u16();
    if (!file_size) return std::unexpected(file_size.error());
    info.file_size = *file_size;

    auto compression = r.read_u16();
    if (!compression) return std::unexpected(compression.error());
    info.compression = *compression;

    auto uncomp_size = r.read_u32();
    if (!uncomp_size) return std::unexpected(uncomp_size.error());
    info.uncomp_size = *uncomp_size;

    auto pal_size = r.read_u16();
    if (!pal_size) return std::unexpected(pal_size.error());
    info.palette_size = *pal_size;

    return {};
}

// Validate CPS header
static Result<void> validate_cps_header(
    const CpsInfo& info, size_t data_size)
{
    if (info.file_size + 2 > data_size)
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "CPS size"));
    if (info.compression > 4)
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "CPS comp"));
    return {};
}

// Convert 6-bit palette to 8-bit
static void convert_6bit_palette(
    std::span<const uint8_t> src,
    std::array<Color, 256>& dst)
{
    const uint8_t* p = src.data();
    for (int i = 0; i < 256; ++i) {
        dst[i] = Color{
            static_cast<uint8_t>((p[0] << 2) | (p[0] >> 4)),
            static_cast<uint8_t>((p[1] << 2) | (p[1] >> 4)),
            static_cast<uint8_t>((p[2] << 2) | (p[2] >> 4))
        };
        p += 3;
    }
}

// Decompress CPS image data
static Result<std::vector<uint8_t>> decompress_cps_image(
    std::span<const uint8_t> data,
    uint16_t compression,
    uint32_t uncomp_size)
{
    switch (static_cast<CpsCompression>(compression)) {
        case CpsCompression::None:
            return std::vector<uint8_t>(data.begin(), data.end());
        case CpsCompression::LCW:
            return lcw_decompress(data, uncomp_size);
        case CpsCompression::LZW12:
            return lzw_decompress(data, uncomp_size, 12);
        case CpsCompression::LZW14:
            return lzw_decompress(data, uncomp_size, 14);
        case CpsCompression::RLE:
            return rle_decompress(data, uncomp_size);
        default:
            return std::unexpected(
                make_error(ErrorCode::UnsupportedFormat, "CPS"));
    }
}

static Result<void> parse_cps(
    CpsReaderImpl& impl, std::span<const uint8_t> data)
{
    if (data.size() < 10)
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "CPS small"));

    SpanReader r(data);
    auto hdr = read_cps_header(r, impl.info);
    if (!hdr) return hdr;

    auto val = validate_cps_header(impl.info, data.size());
    if (!val) return val;

    impl.info.width = 320;
    impl.info.height = 200;
    impl.info.has_palette = (impl.info.palette_size == 768);

    if (impl.info.has_palette) {
        auto pal_data = r.read_bytes(768);
        if (!pal_data) return std::unexpected(pal_data.error());
        convert_6bit_palette(*pal_data, impl.palette);
        impl.has_palette = true;
    }

    size_t img_off = r.pos();
    size_t comp_size = data.size() - img_off;
    impl.info.compressed_size = static_cast<uint32_t>(comp_size);
    auto img = data.subspan(img_off);

    auto pixels = decompress_cps_image(
        img, impl.info.compression, impl.info.uncomp_size);
    if (!pixels) return std::unexpected(pixels.error());
    impl.pixels = std::move(*pixels);

    if (impl.pixels.size() != 64000)
        return std::unexpected(
            make_error(ErrorCode::CorruptData, "CPS pixels"));

    return {};
}

Result<std::unique_ptr<CpsReader>> CpsReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) {
        return std::unexpected(data.error());
    }
    return open(std::span(*data));
}

Result<std::unique_ptr<CpsReader>> CpsReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<CpsReader>(new CpsReader());
    auto result = parse_cps(*reader->impl_, data);
    if (!result) {
        return std::unexpected(result.error());
    }
    return reader;
}

} // namespace wwd
