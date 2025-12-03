#include <westwood/png.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace wwd {

// CRC32 table for PNG (polynomial 0xedb88320)
static uint32_t crc_table[256];
static bool crc_initialized = false;

static void init_crc_table() {
    if (crc_initialized) return;
    for (int n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_initialized = true;
}

uint32_t crc32_png(const uint8_t* data, size_t len) {
    init_crc_table();
    uint32_t c = 0xffffffffL;
    for (size_t i = 0; i < len; i++) {
        c = crc_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffL;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// Write big-endian uint32
static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

// Write PNG chunk: length, type, data, CRC
static void write_chunk(
    std::ostream& out,
    const char* type,
    const uint8_t* data,
    size_t len)
{
    // Length (big-endian)
    uint8_t len_be[4];
    write_be32(len_be, static_cast<uint32_t>(len));
    out.write(reinterpret_cast<const char*>(len_be), 4);

    // Type
    out.write(type, 4);

    // Data
    if (len > 0) {
        out.write(reinterpret_cast<const char*>(data), len);
    }

    // CRC over type + data
    std::vector<uint8_t> crc_data(4 + len);
    std::memcpy(crc_data.data(), type, 4);
    if (len > 0) {
        std::memcpy(crc_data.data() + 4, data, len);
    }
    uint32_t crc = crc32_png(crc_data.data(), crc_data.size());
    uint8_t crc_be[4];
    write_be32(crc_be, crc);
    out.write(reinterpret_cast<const char*>(crc_be), 4);
}

// Compress data using zlib store mode (no actual compression)
static std::vector<uint8_t> zlib_store(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.reserve(raw.size() + 64);

    // Zlib header (store mode)
    out.push_back(0x78);
    out.push_back(0x01);

    // Split into deflate uncompressed blocks (max 65535 bytes each)
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t block_size = std::min(size_t(65535), raw.size() - pos);
        bool is_final = (pos + block_size >= raw.size());

        // Block header: BFINAL (1 bit) + BTYPE=00 (2 bits)
        out.push_back(is_final ? 0x01 : 0x00);

        // LEN and NLEN (little-endian)
        uint16_t len = static_cast<uint16_t>(block_size);
        uint16_t nlen = ~len;
        out.push_back(len & 0xFF);
        out.push_back((len >> 8) & 0xFF);
        out.push_back(nlen & 0xFF);
        out.push_back((nlen >> 8) & 0xFF);

        // Block data
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + block_size);
        pos += block_size;
    }

    // Adler-32 checksum (big-endian)
    uint32_t checksum = adler32(raw.data(), raw.size());
    out.push_back((checksum >> 24) & 0xFF);
    out.push_back((checksum >> 16) & 0xFF);
    out.push_back((checksum >> 8) & 0xFF);
    out.push_back(checksum & 0xFF);

    return out;
}

// Core PNG writing logic
static bool write_png_impl(
    std::ostream& out,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    PngColorType color_type,
    uint8_t bytes_per_pixel)
{
    init_crc_table();

    // PNG signature
    static const uint8_t png_sig[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    out.write(reinterpret_cast<const char*>(png_sig), 8);

    // IHDR chunk
    uint8_t ihdr[13];
    write_be32(ihdr, width);
    write_be32(ihdr + 4, height);
    ihdr[8] = 8;  // Bit depth
    ihdr[9] = static_cast<uint8_t>(color_type);
    ihdr[10] = 0; // Compression
    ihdr[11] = 0; // Filter
    ihdr[12] = 0; // Interlace
    write_chunk(out, "IHDR", ihdr, 13);

    // Build raw image data with filter bytes
    std::vector<uint8_t> raw;
    raw.reserve(height * (1 + width * bytes_per_pixel));

    for (uint32_t y = 0; y < height; y++) {
        raw.push_back(0);  // Filter: None
        size_t row_start = y * width * bytes_per_pixel;
        for (uint32_t x = 0; x < width * bytes_per_pixel; x++) {
            raw.push_back(pixels[row_start + x]);
        }
    }

    // Compress and write IDAT
    auto compressed = zlib_store(raw);
    write_chunk(out, "IDAT", compressed.data(), compressed.size());

    // IEND
    write_chunk(out, "IEND", nullptr, 0);

    return out.good();
}

bool write_png_rgba(
    std::ostream& out,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height)
{
    return write_png_impl(out, rgba, width, height, PngColorType::Rgba, 4);
}

bool write_png_rgba(
    const std::string& path,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    return write_png_rgba(f, rgba, width, height);
}

bool write_png_ga(
    std::ostream& out,
    const uint8_t* ga,
    uint32_t width,
    uint32_t height)
{
    return write_png_impl(
        out, ga, width, height, PngColorType::GrayscaleAlpha, 2);
}

bool write_png_ga(
    const std::string& path,
    const uint8_t* ga,
    uint32_t width,
    uint32_t height)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    return write_png_ga(f, ga, width, height);
}

} // namespace wwd
