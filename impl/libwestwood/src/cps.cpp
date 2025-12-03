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
                // Extended count: next two bytes are 16-bit count (little-endian)
                if (pos + 2 > input.size()) break;
                count = input[pos] | (input[pos + 1] << 8);
                pos += 2;
            }
            if (pos >= input.size()) break;
            uint8_t value = input[pos++];
            for (uint16_t i = 0; i < count && output.size() < output_size; ++i) {
                output.push_back(value);
            }
        } else {
            // Literal: copy next (cmd) bytes
            uint8_t count = cmd;
            if (count == 0) continue;
            for (int i = 0; i < count && pos < input.size() && output.size() < output_size; ++i) {
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

// Westwood LZW decompression (CPS compression types 1 and 2)
// Standard LZW with configurable maximum code bits (12 or 14)
static Result<std::vector<uint8_t>> lzw_decompress(
    std::span<const uint8_t> input, size_t output_size, int max_bits)
{
    std::vector<uint8_t> output;
    output.reserve(output_size);

    // LZW dictionary: each entry is a string (stored as vector of bytes)
    const int clear_code = 256;
    const int end_code = 257;
    const int first_code = 258;
    const int max_code = (1 << max_bits) - 1;

    // Dictionary entries: index -> string
    std::vector<std::vector<uint8_t>> dict;

    auto reset_dict = [&]() {
        dict.clear();
        dict.resize(first_code);
        for (int i = 0; i < 256; ++i) {
            dict[i] = { static_cast<uint8_t>(i) };
        }
        // Reserve space for clear and end codes (empty entries)
        dict[clear_code] = {};
        dict[end_code] = {};
    };

    reset_dict();

    // Bit reader state
    size_t bit_pos = 0;
    int code_bits = 9;  // Start with 9-bit codes
    int next_code = first_code;

    auto read_code = [&]() -> int {
        if (bit_pos + code_bits > input.size() * 8) {
            return end_code;  // EOF
        }

        int code = 0;
        for (int i = 0; i < code_bits; ++i) {
            size_t byte_idx = (bit_pos + i) / 8;
            int bit_idx = (bit_pos + i) % 8;
            if (input[byte_idx] & (1 << bit_idx)) {
                code |= (1 << i);
            }
        }
        bit_pos += code_bits;
        return code;
    };

    // Read first code
    int prev_code = read_code();
    if (prev_code == end_code || prev_code == clear_code) {
        if (prev_code == clear_code) {
            prev_code = read_code();
        }
        if (prev_code == end_code) {
            return output;
        }
    }

    // Output first code
    if (prev_code < static_cast<int>(dict.size()) && !dict[prev_code].empty()) {
        for (uint8_t b : dict[prev_code]) {
            output.push_back(b);
        }
    } else if (prev_code < 256) {
        output.push_back(static_cast<uint8_t>(prev_code));
    }

    // Process remaining codes
    while (output.size() < output_size) {
        int code = read_code();

        if (code == end_code) {
            break;
        }

        if (code == clear_code) {
            reset_dict();
            code_bits = 9;
            next_code = first_code;
            prev_code = read_code();
            if (prev_code == end_code) break;
            if (prev_code < static_cast<int>(dict.size()) && !dict[prev_code].empty()) {
                for (uint8_t b : dict[prev_code]) {
                    output.push_back(b);
                }
            } else if (prev_code < 256) {
                output.push_back(static_cast<uint8_t>(prev_code));
            }
            continue;
        }

        std::vector<uint8_t> entry;
        if (code < static_cast<int>(dict.size()) && !dict[code].empty()) {
            entry = dict[code];
        } else if (code == next_code) {
            // Special case: code not yet in dictionary
            if (prev_code < static_cast<int>(dict.size()) && !dict[prev_code].empty()) {
                entry = dict[prev_code];
                entry.push_back(entry[0]);
            }
        } else {
            // Invalid code
            break;
        }

        // Output the entry
        for (uint8_t b : entry) {
            if (output.size() >= output_size) break;
            output.push_back(b);
        }

        // Add new dictionary entry: prev_code's string + first char of current entry
        if (next_code <= max_code && prev_code < static_cast<int>(dict.size())) {
            std::vector<uint8_t> new_entry;
            if (!dict[prev_code].empty()) {
                new_entry = dict[prev_code];
            } else if (prev_code < 256) {
                new_entry = { static_cast<uint8_t>(prev_code) };
            }
            if (!entry.empty()) {
                new_entry.push_back(entry[0]);
            }
            if (next_code < static_cast<int>(dict.size())) {
                dict[next_code] = std::move(new_entry);
            } else {
                dict.push_back(std::move(new_entry));
            }
            next_code++;

            // Increase code size if needed
            if (next_code > (1 << code_bits) && code_bits < max_bits) {
                code_bits++;
            }
        }

        prev_code = code;
    }

    // Pad with zeros if needed
    while (output.size() < output_size) {
        output.push_back(0);
    }

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

static Result<void> parse_cps(CpsReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // Minimum header size: 10 bytes
    if (data.size() < 10) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "CPS file too small"));
    }

    SpanReader r(data);

    // Read header
    auto file_size = r.read_u16();
    if (!file_size) return std::unexpected(file_size.error());
    impl.info.file_size = *file_size;

    auto compression = r.read_u16();
    if (!compression) return std::unexpected(compression.error());
    impl.info.compression = *compression;

    auto uncomp_size = r.read_u32();
    if (!uncomp_size) return std::unexpected(uncomp_size.error());
    impl.info.uncomp_size = *uncomp_size;

    auto pal_size = r.read_u16();
    if (!pal_size) return std::unexpected(pal_size.error());
    impl.info.palette_size = *pal_size;

    // Validate header
    // file_size + 2 should equal actual file size
    if (impl.info.file_size + 2 != data.size()) {
        // Some files may have padding; allow if close
        if (impl.info.file_size + 2 > data.size()) {
            return std::unexpected(
                make_error(ErrorCode::CorruptHeader, "CPS file size mismatch"));
        }
    }

    // Validate compression - we recognize all methods but may not support all
    if (impl.info.compression > 4) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat,
                       "Unknown CPS compression method"));
    }

    // Standard CPS is 320x200
    impl.info.width = 320;
    impl.info.height = 200;
    impl.info.has_palette = (impl.info.palette_size == 768);

    // Read embedded palette if present
    // CPS palettes are 6-bit (values 0-63), need to scale to 8-bit
    if (impl.info.has_palette) {
        auto pal_data = r.read_bytes(768);
        if (!pal_data) return std::unexpected(pal_data.error());

        const uint8_t* p = pal_data->data();
        for (int i = 0; i < 256; ++i) {
            uint8_t r6 = p[0];
            uint8_t g6 = p[1];
            uint8_t b6 = p[2];
            // 6-bit to 8-bit conversion: (val << 2) | (val >> 4)
            impl.palette[i] = Color{
                static_cast<uint8_t>((r6 << 2) | (r6 >> 4)),
                static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                static_cast<uint8_t>((b6 << 2) | (b6 >> 4))
            };
            p += 3;
        }
        impl.has_palette = true;
    }

    // Read image data
    size_t image_offset = r.pos();
    size_t image_size = data.size() - image_offset;
    impl.info.compressed_size = static_cast<uint32_t>(image_size);
    auto image_data = data.subspan(image_offset, image_size);

    switch (static_cast<CpsCompression>(impl.info.compression)) {
        case CpsCompression::None:
            // Uncompressed
            impl.pixels.assign(image_data.begin(), image_data.end());
            break;

        case CpsCompression::LCW:
            // LCW compressed (Format80)
            {
                auto result = lcw_decompress(image_data, impl.info.uncomp_size);
                if (!result) return std::unexpected(result.error());
                impl.pixels = std::move(*result);
            }
            break;

        case CpsCompression::LZW12:
            // LZW compressed with 12-bit codes
            {
                auto result = lzw_decompress(image_data, impl.info.uncomp_size, 12);
                if (!result) return std::unexpected(result.error());
                impl.pixels = std::move(*result);
            }
            break;

        case CpsCompression::LZW14:
            // LZW compressed with 14-bit codes
            {
                auto result = lzw_decompress(image_data, impl.info.uncomp_size, 14);
                if (!result) return std::unexpected(result.error());
                impl.pixels = std::move(*result);
            }
            break;

        case CpsCompression::RLE:
            // RLE compressed
            {
                auto result = rle_decompress(image_data, impl.info.uncomp_size);
                if (!result) return std::unexpected(result.error());
                impl.pixels = std::move(*result);
            }
            break;

        default:
            return std::unexpected(
                make_error(ErrorCode::UnsupportedFormat,
                           "Unknown CPS compression method"));
    }

    // Validate pixel count
    if (impl.pixels.size() != 64000) {
        return std::unexpected(
            make_error(ErrorCode::CorruptData,
                       "CPS pixel data size mismatch"));
    }

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
