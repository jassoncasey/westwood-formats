#include <westwood/fnt.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: fnt-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show font information\n"
              << "    export      Export to PNG atlas + JSON metrics\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -o, --output    PNG atlas path (default: input name + .png)\n"
              << "    -m, --metrics   JSON metrics path (default: input name + .json)\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "fnt-tool " << VERSION << "\n";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: fnt-tool info [--json] <file.fnt>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "fnt-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "fnt-tool: error: missing file argument\n";
        return 1;
    }

    auto result = wwd::FntReader::open(file_path);
    if (!result) {
        std::cerr << "fnt-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood FNT v3\",\n";
        std::cout << "  \"glyphs\": " << info.glyph_count << ",\n";
        std::cout << "  \"first_char\": " << static_cast<int>(info.first_char) << ",\n";
        std::cout << "  \"last_char\": " << static_cast<int>(info.last_char) << ",\n";
        std::cout << "  \"max_width\": " << static_cast<int>(info.max_width) << ",\n";
        std::cout << "  \"max_height\": " << static_cast<int>(info.height) << ",\n";
        std::cout << "  \"bit_depth\": 4\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:          Westwood FNT v3\n";
        std::cout << "Glyphs:          " << info.glyph_count << "\n";
        std::cout << "Character range: " << static_cast<int>(info.first_char)
                  << "-" << static_cast<int>(info.last_char) << "\n";
        std::cout << "Max dimensions:  " << static_cast<int>(info.max_width)
                  << "x" << static_cast<int>(info.height) << "\n";
        std::cout << "Bit depth:       4-bit (16 colors)\n";
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

static bool write_png_rgba(const std::string& path,
                           const uint8_t* rgba,
                           uint32_t width, uint32_t height) {
    init_crc_table();

    static const uint8_t png_sig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

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

// Escape a string for JSON
static std::string json_escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    std::string metrics_path;
    bool force = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: fnt-tool export <file.fnt> [-o output.png] [-m metrics.json]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "fnt-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-m") == 0 || std::strcmp(arg, "--metrics") == 0) {
            if (i + 1 < argc) {
                metrics_path = argv[++i];
            } else {
                std::cerr << "fnt-tool: error: -m requires an argument\n";
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
            std::cerr << "fnt-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "fnt-tool: error: missing file argument\n";
        return 1;
    }

    // Open FNT file
    auto fnt_result = wwd::FntReader::open(file_path);
    if (!fnt_result) {
        std::cerr << "fnt-tool: error: " << fnt_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *fnt_result.value();
    const auto& info = reader.info();
    const auto& glyphs = reader.glyphs();

    // Default output paths
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + ".png";
    }
    if (metrics_path.empty()) {
        fs::path p(file_path);
        metrics_path = p.stem().string() + ".json";
    }

    // Check if outputs exist
    if (fs::exists(output_path) && !force) {
        std::cerr << "fnt-tool: error: output file exists: " << output_path
                  << " (use --force to overwrite)\n";
        return 1;
    }
    if (fs::exists(metrics_path) && !force) {
        std::cerr << "fnt-tool: error: output file exists: " << metrics_path
                  << " (use --force to overwrite)\n";
        return 1;
    }

    // Calculate atlas dimensions
    // Pack glyphs row by row, trying to minimize wasted space
    uint32_t total_width = 0;
    for (const auto& g : glyphs) {
        total_width += g.width + 1;  // 1px padding between glyphs
    }

    // Aim for roughly square atlas, with power-of-2 width
    uint32_t atlas_width = 128;
    while (atlas_width < total_width / 4 && atlas_width < 2048) {
        atlas_width *= 2;
    }

    // Pack glyphs and calculate height
    struct PackedGlyph {
        uint32_t x, y;
        uint8_t width, height;
        uint8_t y_offset;
        size_t glyph_index;
    };
    std::vector<PackedGlyph> packed;
    packed.reserve(glyphs.size());

    uint32_t cur_x = 0;
    uint32_t cur_y = 0;
    uint32_t row_height = info.height;
    uint32_t atlas_height = row_height;

    for (size_t i = 0; i < glyphs.size(); ++i) {
        const auto& g = glyphs[i];
        if (g.width == 0) {
            // Still track empty glyphs (like space)
            packed.push_back({cur_x, cur_y, g.width, g.height, g.y_offset, i});
            continue;
        }

        // Check if glyph fits in current row
        if (cur_x + g.width > atlas_width) {
            // Move to next row
            cur_x = 0;
            cur_y += row_height + 1;  // 1px padding between rows
            atlas_height = cur_y + row_height;
        }

        packed.push_back({cur_x, cur_y, g.width, g.height, g.y_offset, i});
        cur_x += g.width + 1;
    }

    // Round atlas height up
    uint32_t final_height = 1;
    while (final_height < atlas_height) {
        final_height *= 2;
    }

    if (verbose) {
        std::cerr << "Exporting " << file_path << "\n";
        std::cerr << "  Glyphs: " << info.glyph_count << "\n";
        std::cerr << "  Atlas: " << atlas_width << "x" << final_height << "\n";
        std::cerr << "  PNG: " << output_path << "\n";
        std::cerr << "  JSON: " << metrics_path << "\n";
    }

    // Create RGBA atlas (transparent background, white glyphs)
    std::vector<uint8_t> rgba(atlas_width * final_height * 4, 0);

    // Render glyphs
    for (const auto& pg : packed) {
        if (pg.width == 0) continue;

        auto glyph_data = reader.decode_glyph(pg.glyph_index);
        if (glyph_data.empty()) continue;

        for (uint32_t gy = 0; gy < pg.height; ++gy) {
            for (uint32_t gx = 0; gx < pg.width; ++gx) {
                size_t src_idx = gy * pg.width + gx;
                uint8_t value = glyph_data[src_idx];

                // Convert 4-bit value (0-15) to intensity (0-255)
                // intensity = value * 17 (0->0, 15->255)
                uint8_t intensity = value * 17;

                size_t dst_x = pg.x + gx;
                size_t dst_y = pg.y + gy;
                size_t dst_idx = (dst_y * atlas_width + dst_x) * 4;

                // White glyph with intensity as alpha
                rgba[dst_idx] = 255;      // R
                rgba[dst_idx + 1] = 255;  // G
                rgba[dst_idx + 2] = 255;  // B
                rgba[dst_idx + 3] = intensity;  // A
            }
        }
    }

    // Write PNG
    if (!write_png_rgba(output_path, rgba.data(), atlas_width, final_height)) {
        std::cerr << "fnt-tool: error: failed to write: " << output_path << "\n";
        return 1;
    }

    // Write JSON metrics
    std::ofstream json_out(metrics_path);
    if (!json_out) {
        std::cerr << "fnt-tool: error: failed to write: " << metrics_path << "\n";
        return 1;
    }

    json_out << "{\n";
    json_out << "  \"source\": \"" << json_escape(reader.source_filename()) << "\",\n";
    json_out << "  \"atlasWidth\": " << atlas_width << ",\n";
    json_out << "  \"atlasHeight\": " << final_height << ",\n";
    json_out << "  \"maxHeight\": " << static_cast<int>(info.height) << ",\n";
    json_out << "  \"maxWidth\": " << static_cast<int>(info.max_width) << ",\n";
    json_out << "  \"glyphs\": {\n";

    bool first = true;
    for (const auto& pg : packed) {
        // Character index is glyph_index (0-based, representing ASCII chars starting from first_char)
        int char_code = info.first_char + static_cast<int>(pg.glyph_index);

        if (!first) json_out << ",\n";
        first = false;

        json_out << "    \"" << char_code << "\": { "
                 << "\"x\": " << pg.x << ", "
                 << "\"y\": " << pg.y << ", "
                 << "\"width\": " << static_cast<int>(pg.width) << ", "
                 << "\"height\": " << static_cast<int>(pg.height) << ", "
                 << "\"yOffset\": " << static_cast<int>(pg.y_offset) << " }";
    }

    json_out << "\n  }\n";
    json_out << "}\n";

    if (!json_out.good()) {
        std::cerr << "fnt-tool: error: failed to write: " << metrics_path << "\n";
        return 1;
    }

    if (verbose) {
        std::cerr << "Wrote " << output_path << " and " << metrics_path << "\n";
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

    std::cerr << "fnt-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
