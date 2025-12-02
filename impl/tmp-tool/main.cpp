#include <westwood/tmp.h>
#include <westwood/pal.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: tmp-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show tileset information\n"
              << "    export      Export to PNG format\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    -p, --palette   PAL file for color lookup\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "tmp-tool " << VERSION << "\n";
}

static const char* format_name(wwd::TmpFormat format) {
    switch (format) {
        case wwd::TmpFormat::TD: return "TD/RA TMP";
        case wwd::TmpFormat::RA: return "TD/RA TMP";
        default:                  return "Unknown";
    }
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: tmp-tool info [--json] <file.tmp>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "tmp-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "tmp-tool: error: missing file argument\n";
        return 1;
    }

    auto result = wwd::TmpReader::open(file_path);
    if (!result) {
        std::cerr << "tmp-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"" << format_name(info.format) << "\",\n";
        std::cout << "  \"tiles\": " << info.tile_count << ",\n";
        std::cout << "  \"empty_tiles\": " << info.empty_count << ",\n";
        std::cout << "  \"tile_width\": " << info.tile_width << ",\n";
        std::cout << "  \"tile_height\": " << info.tile_height << ",\n";
        std::cout << "  \"index_offset\": " << info.index_start << ",\n";
        std::cout << "  \"image_offset\": " << info.image_start << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:           " << format_name(info.format) << "\n";
        std::cout << "Tiles:            " << info.tile_count << " total ("
                  << info.empty_count << " empty)\n";
        std::cout << "Tile dimensions:  " << info.tile_width << "x"
                  << info.tile_height << "\n";
        std::cout << "Image data offset: 0x" << std::hex << info.image_start
                  << std::dec << "\n";
        std::cout << "Index table offset: 0x" << std::hex << info.index_start
                  << std::dec << "\n";
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
            std::cerr << "Usage: tmp-tool export <file.tmp> -p <palette> [-o output.png]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "tmp-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--palette") == 0) {
            if (i + 1 < argc) {
                palette_path = argv[++i];
            } else {
                std::cerr << "tmp-tool: error: -p requires an argument\n";
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
            std::cerr << "tmp-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "tmp-tool: error: missing file argument\n";
        return 1;
    }

    if (palette_path.empty()) {
        std::cerr << "tmp-tool: error: palette required (use -p <file.pal>)\n";
        return 1;
    }

    // Open TMP file
    auto tmp_result = wwd::TmpReader::open(file_path);
    if (!tmp_result) {
        std::cerr << "tmp-tool: error: " << tmp_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *tmp_result.value();
    const auto& info = reader.info();
    const auto& tiles = reader.tiles();

    // Open palette
    auto pal_result = wwd::PalReader::open(palette_path);
    if (!pal_result) {
        std::cerr << "tmp-tool: error: " << pal_result.error().message() << "\n";
        return 2;
    }

    const auto& palette = *pal_result.value();

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + ".png";
    }

    // Check if output exists
    if (output_path != "-" && fs::exists(output_path) && !force) {
        std::cerr << "tmp-tool: error: output file exists: " << output_path
                  << " (use --force to overwrite)\n";
        return 1;
    }

    // Calculate grid dimensions based on tile count
    // Aim for roughly square grid
    uint32_t valid_tiles = reader.valid_tile_count();
    uint32_t grid_cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(info.tile_count))));
    uint32_t grid_rows = (info.tile_count + grid_cols - 1) / grid_cols;

    uint32_t img_width = grid_cols * info.tile_width;
    uint32_t img_height = grid_rows * info.tile_height;

    if (verbose) {
        std::cerr << "Exporting " << file_path << " to " << output_path << "\n";
        std::cerr << "  Tiles: " << info.tile_count << " (" << valid_tiles << " valid)\n";
        std::cerr << "  Grid: " << grid_cols << "x" << grid_rows << "\n";
        std::cerr << "  Output: " << img_width << "x" << img_height << "\n";
    }

    // Create RGBA image (transparent by default)
    std::vector<uint8_t> rgba(img_width * img_height * 4, 0);

    // Decode and place each tile
    for (size_t i = 0; i < tiles.size(); ++i) {
        auto tile_data = reader.decode_tile(i);
        if (tile_data.empty()) {
            // Empty/invalid tile - leave as transparent
            continue;
        }

        uint32_t tile_x = (static_cast<uint32_t>(i) % grid_cols) * info.tile_width;
        uint32_t tile_y = (static_cast<uint32_t>(i) / grid_cols) * info.tile_height;

        for (uint32_t ty = 0; ty < info.tile_height; ++ty) {
            for (uint32_t tx = 0; tx < info.tile_width; ++tx) {
                size_t src_idx = ty * info.tile_width + tx;
                size_t dst_idx = ((tile_y + ty) * img_width + (tile_x + tx)) * 4;

                uint8_t pal_idx = tile_data[src_idx];
                auto color = palette.color_8bit(pal_idx);

                rgba[dst_idx] = color.r;
                rgba[dst_idx + 1] = color.g;
                rgba[dst_idx + 2] = color.b;
                rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;  // Index 0 = transparent
            }
        }
    }

    if (output_path == "-") {
        std::ios_base::sync_with_stdio(false);
        if (!write_png_rgba(std::cout, rgba.data(), img_width, img_height)) {
            std::cerr << "tmp-tool: error: failed to write to stdout\n";
            return 1;
        }
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "tmp-tool: error: cannot open: " << output_path << "\n";
            return 1;
        }
        if (!write_png_rgba(out, rgba.data(), img_width, img_height)) {
            std::cerr << "tmp-tool: error: failed to write: " << output_path << "\n";
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

    std::cerr << "tmp-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
