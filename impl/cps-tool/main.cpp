#include <westwood/cps.h>
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
    out << "Usage: cps-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show CPS file information\n"
              << "    export      Export to PNG format\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    -p, --palette   External PAL file (if no embedded palette)\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "cps-tool " << VERSION << "\n";
}

static const char* compression_name(uint16_t comp) {
    switch (comp) {
        case 0:  return "none";
        case 1:  return "LZW-12";
        case 2:  return "LZW-14";
        case 3:  return "RLE";
        case 4:  return "LCW";
        default: return "unknown";
    }
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: cps-tool info [--json] <file.cps>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "cps-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "cps-tool: error: missing file argument\n";
        return 1;
    }

    auto result = wwd::CpsReader::open(file_path);
    if (!result) {
        std::cerr << "cps-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood CPS\",\n";
        std::cout << "  \"width\": " << info.width << ",\n";
        std::cout << "  \"height\": " << info.height << ",\n";
        std::cout << "  \"compression\": \"" << compression_name(info.compression) << "\",\n";
        std::cout << "  \"has_palette\": " << (info.has_palette ? "true" : "false") << ",\n";
        std::cout << "  \"compressed_size\": " << info.compressed_size << ",\n";
        std::cout << "  \"uncompressed_size\": " << info.uncomp_size << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:              Westwood CPS\n";
        std::cout << "Dimensions:          " << info.width << "x" << info.height << "\n";
        std::cout << "Compression:         " << compression_name(info.compression) << "\n";
        std::cout << "Has embedded palette: " << (info.has_palette ? "yes" : "no") << "\n";
        std::cout << "Compressed size:     " << info.compressed_size << " bytes\n";
        std::cout << "Uncompressed size:   " << info.uncomp_size << " bytes\n";
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

static bool write_png_rgba(std::ostream& out,
                           const uint8_t* rgba,
                           uint32_t width, uint32_t height) {
    init_crc_table();

    static const uint8_t png_sig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    out.write(reinterpret_cast<const char*>(png_sig), 8);

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

    // IHDR
    uint8_t ihdr[13] = {
        uint8_t(width >> 24), uint8_t(width >> 16),
        uint8_t(width >> 8), uint8_t(width),
        uint8_t(height >> 24), uint8_t(height >> 16),
        uint8_t(height >> 8), uint8_t(height),
        8,   // Bit depth
        6,   // Color type: RGBA
        0,   // Compression
        0,   // Filter
        0    // Interlace
    };
    write_chunk("IHDR", ihdr, 13);

    // Generate image data with filter bytes
    std::vector<uint8_t> raw_data;
    raw_data.reserve(height * (1 + width * 4));

    for (uint32_t y = 0; y < height; y++) {
        raw_data.push_back(0);  // Filter: None
        for (uint32_t x = 0; x < width; x++) {
            size_t idx = (y * width + x) * 4;
            raw_data.push_back(rgba[idx]);
            raw_data.push_back(rgba[idx + 1]);
            raw_data.push_back(rgba[idx + 2]);
            raw_data.push_back(rgba[idx + 3]);
        }
    }

    // Compress with zlib (store mode)
    std::vector<uint8_t> compressed;
    compressed.push_back(0x78);
    compressed.push_back(0x01);

    size_t pos = 0;
    while (pos < raw_data.size()) {
        size_t block_size = std::min(size_t(65535), raw_data.size() - pos);
        bool is_final = (pos + block_size >= raw_data.size());

        compressed.push_back(is_final ? 0x01 : 0x00);
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

    uint32_t adler = adler32(raw_data.data(), raw_data.size());
    compressed.push_back((adler >> 24) & 0xFF);
    compressed.push_back((adler >> 16) & 0xFF);
    compressed.push_back((adler >> 8) & 0xFF);
    compressed.push_back(adler & 0xFF);

    write_chunk("IDAT", compressed.data(), compressed.size());
    write_chunk("IEND", nullptr, 0);

    return out.good();
}

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    std::string palette_path;
    bool force = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: cps-tool export <file.cps> [-p palette] [-o output.png]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "cps-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--palette") == 0) {
            if (i + 1 < argc) {
                palette_path = argv[++i];
            } else {
                std::cerr << "cps-tool: error: -p requires an argument\n";
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
            std::cerr << "cps-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "cps-tool: error: missing file argument\n";
        return 1;
    }

    // Open CPS file
    auto cps_result = wwd::CpsReader::open(file_path);
    if (!cps_result) {
        std::cerr << "cps-tool: error: " << cps_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *cps_result.value();
    const auto& info = reader.info();
    const auto& pixels = reader.pixels();

    // Get palette
    const std::array<wwd::Color, 256>* palette = nullptr;
    std::unique_ptr<wwd::PalReader> ext_pal_reader;
    std::array<wwd::Color, 256> ext_palette;

    if (!palette_path.empty()) {
        // Use external palette (overrides embedded)
        auto pal_result = wwd::PalReader::open(palette_path);
        if (!pal_result) {
            std::cerr << "cps-tool: error: " << pal_result.error().message() << "\n";
            return 2;
        }
        ext_pal_reader = std::move(*pal_result);
        // Copy palette data
        for (int i = 0; i < 256; ++i) {
            ext_palette[i] = ext_pal_reader->color_8bit(i);
        }
        palette = &ext_palette;
    } else if (info.has_palette) {
        palette = reader.palette();
    }

    if (!palette) {
        std::cerr << "cps-tool: error: no palette available (use -p <file.pal>)\n";
        return 1;
    }

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + ".png";
    }

    // Check if output exists
    if (output_path != "-" && fs::exists(output_path) && !force) {
        std::cerr << "cps-tool: error: output file exists: " << output_path
                  << " (use --force to overwrite)\n";
        return 1;
    }

    if (verbose) {
        std::cerr << "Converting " << file_path << " to " << output_path << "\n";
        std::cerr << "  Dimensions: " << info.width << "x" << info.height << "\n";
    }

    // Convert to RGBA
    std::vector<uint8_t> rgba(info.width * info.height * 4);

    for (size_t i = 0; i < pixels.size(); ++i) {
        uint8_t pal_idx = pixels[i];
        const auto& color = (*palette)[pal_idx];
        rgba[i * 4] = color.r;
        rgba[i * 4 + 1] = color.g;
        rgba[i * 4 + 2] = color.b;
        rgba[i * 4 + 3] = (pal_idx == 0) ? 0 : 255;  // Index 0 = transparent
    }

    if (output_path == "-") {
        // Write to stdout
        std::ios_base::sync_with_stdio(false);
        if (!write_png_rgba(std::cout, rgba.data(), info.width, info.height)) {
            std::cerr << "cps-tool: error: failed to write to stdout\n";
            return 1;
        }
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "cps-tool: error: cannot open: " << output_path << "\n";
            return 1;
        }
        if (!write_png_rgba(out, rgba.data(), info.width, info.height)) {
            std::cerr << "cps-tool: error: failed to write: " << output_path << "\n";
            return 1;
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

    std::cerr << "cps-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
