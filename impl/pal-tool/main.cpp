#include <westwood/pal.h>

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: pal-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show palette information\n"
              << "    export      Export palette as swatch PNG (512x512)\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "pal-tool " << VERSION << "\n";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: pal-tool info [--json] <file.pal>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "pal-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "pal-tool: error: missing file argument\n";
        return 1;
    }

    auto result = wwd::PalReader::open(file_path);
    if (!result) {
        std::cerr << "pal-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood PAL\",\n";
        std::cout << "  \"colors\": " << info.entries << ",\n";
        std::cout << "  \"bit_depth\": " << static_cast<int>(info.bit_depth) << ",\n";
        std::cout << "  \"file_size\": " << info.file_size << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:    Westwood PAL\n";
        std::cout << "Colors:    " << info.entries << "\n";
        std::cout << "Bit depth: " << static_cast<int>(info.bit_depth) << "-bit per channel";
        if (info.bit_depth == 6) {
            std::cout << " (18-bit color)";
        } else {
            std::cout << " (24-bit color)";
        }
        std::cout << "\n";
        std::cout << "File size: " << info.file_size << " bytes\n";
    }

    return 0;
}

// CRC32 table for PNG
static uint32_t crc_table[256];
static bool crc_init = false;

static void init_crc_table() {
    if (crc_init) return;
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
    crc_init = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xffffffffL;
    for (size_t i = 0; i < len; i++) {
        c = crc_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffL;
}

static uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// Simple PNG writer for 512x512 RGB image
static bool write_png_swatch(std::ostream& out,
                              const wwd::PalReader& pal) {
    init_crc_table();

    // PNG header
    static const uint8_t png_sig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    // Write PNG signature
    out.write(reinterpret_cast<const char*>(png_sig), 8);

    // Helper to write chunk
    auto write_chunk = [&](const char* type, const uint8_t* data, size_t len) {
        uint32_t length = static_cast<uint32_t>(len);
        uint8_t len_be[4] = {
            uint8_t(length >> 24), uint8_t(length >> 16),
            uint8_t(length >> 8), uint8_t(length)
        };
        out.write(reinterpret_cast<const char*>(len_be), 4);
        out.write(type, 4);
        if (len > 0) {
            out.write(reinterpret_cast<const char*>(data), len);
        }
        // CRC includes type and data
        std::vector<uint8_t> crc_data(4 + len);
        std::memcpy(crc_data.data(), type, 4);
        if (len > 0) {
            std::memcpy(crc_data.data() + 4, data, len);
        }
        uint32_t crc = crc32(crc_data.data(), crc_data.size());
        uint8_t crc_be[4] = {
            uint8_t(crc >> 24), uint8_t(crc >> 16),
            uint8_t(crc >> 8), uint8_t(crc)
        };
        out.write(reinterpret_cast<const char*>(crc_be), 4);
    };

    // IHDR: 512x512, 8-bit RGB
    uint8_t ihdr[13] = {
        0, 0, 2, 0,     // Width: 512 (big-endian)
        0, 0, 2, 0,     // Height: 512
        8,              // Bit depth
        2,              // Color type: RGB
        0,              // Compression
        0,              // Filter
        0               // Interlace
    };
    write_chunk("IHDR", ihdr, 13);

    // Generate image data: 512x512 RGB with 16x16 grid of 32x32 swatches
    std::vector<uint8_t> raw_data;
    raw_data.reserve(512 * (1 + 512 * 3));  // filter byte + row

    for (int y = 0; y < 512; y++) {
        raw_data.push_back(0);  // Filter: None
        int swatch_row = y / 32;
        for (int x = 0; x < 512; x++) {
            int swatch_col = x / 32;
            int color_index = swatch_row * 16 + swatch_col;
            auto c = pal.color_8bit(static_cast<uint8_t>(color_index));
            raw_data.push_back(c.r);
            raw_data.push_back(c.g);
            raw_data.push_back(c.b);
        }
    }

    // Compress with zlib (store mode - no compression for simplicity)
    std::vector<uint8_t> compressed;

    // zlib header (no compression)
    compressed.push_back(0x78);  // CMF
    compressed.push_back(0x01);  // FLG (no dict, fastest)

    // Split into 65535-byte blocks
    size_t pos = 0;
    while (pos < raw_data.size()) {
        size_t block_size = std::min(size_t(65535), raw_data.size() - pos);
        bool is_final = (pos + block_size >= raw_data.size());

        compressed.push_back(is_final ? 0x01 : 0x00);  // BFINAL + BTYPE=0 (stored)
        uint16_t len = static_cast<uint16_t>(block_size);
        uint16_t nlen = ~len;
        compressed.push_back(len & 0xFF);
        compressed.push_back((len >> 8) & 0xFF);
        compressed.push_back(nlen & 0xFF);
        compressed.push_back((nlen >> 8) & 0xFF);
        compressed.insert(compressed.end(),
                         raw_data.begin() + pos,
                         raw_data.begin() + pos + block_size);
        pos += block_size;
    }

    // Adler32 checksum
    uint32_t adler = adler32(raw_data.data(), raw_data.size());
    compressed.push_back((adler >> 24) & 0xFF);
    compressed.push_back((adler >> 16) & 0xFF);
    compressed.push_back((adler >> 8) & 0xFF);
    compressed.push_back(adler & 0xFF);

    // IDAT chunk
    write_chunk("IDAT", compressed.data(), compressed.size());

    // IEND chunk
    write_chunk("IEND", nullptr, 0);

    return out.good();
}

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    bool force = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: pal-tool export <file.pal> [-o output.png] [-f]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "pal-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-f") == 0 || std::strcmp(arg, "--force") == 0) {
            force = true;
            continue;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "pal-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "pal-tool: error: missing file argument\n";
        return 1;
    }

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + ".png";
    }

    // Check if output exists
    if (output_path != "-" && fs::exists(output_path) && !force) {
        std::cerr << "pal-tool: error: output file exists: " << output_path
                  << " (use --force to overwrite)\n";
        return 1;
    }

    // Open PAL file
    auto result = wwd::PalReader::open(file_path);
    if (!result) {
        std::cerr << "pal-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();

    if (verbose) {
        std::cerr << "Exporting " << file_path << " to " << output_path << "\n";
        std::cerr << "  Swatch size: 512x512 (16x16 grid, 32px per color)\n";
    }

    if (output_path == "-") {
        std::ios_base::sync_with_stdio(false);
        if (!write_png_swatch(std::cout, reader)) {
            std::cerr << "pal-tool: error: failed to write to stdout\n";
            return 3;
        }
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "pal-tool: error: cannot open: " << output_path << "\n";
            return 3;
        }
        if (!write_png_swatch(out, reader)) {
            std::cerr << "pal-tool: error: failed to write: " << output_path << "\n";
            return 3;
        }
    }

    if (verbose) {
        std::cerr << "Wrote " << output_path << "\n";
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 1;
    }

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0) {
        print_usage(std::cout);
        return 0;
    }
    if (std::strcmp(cmd, "-V") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    if (std::strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1);
    }
    if (std::strcmp(cmd, "export") == 0) {
        return cmd_export(argc - 1, argv + 1);
    }

    std::cerr << "pal-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
