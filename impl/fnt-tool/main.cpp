#include <westwood/fnt.h>
#include <westwood/io.h>
#include <westwood/png.h>

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
              << "    -q, --quiet     Suppress non-essential output\n"
              << "    -o, --output    PNG atlas path (default: name.png)\n"
              << "    -m, --metrics   JSON metrics path (default: name.json)\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "fnt-tool " << VERSION << "\n";
}

static const char* format_name(wwd::FntFormat format) {
    switch (format) {
        case wwd::FntFormat::V2:            return "Westwood FNT v2 (1-bit)";
        case wwd::FntFormat::V3:            return "Westwood FNT v3 (4-bit)";
        case wwd::FntFormat::V4:            return "Westwood FNT v4 (8-bit)";
        case wwd::FntFormat::BitFont:       return "Westwood BitFont (1-bit)";
        case wwd::FntFormat::UnicodeBitFont:
            return "Westwood Unicode BitFont (1-bit)";
        default:                            return "Unknown";
    }
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
        if (arg[0] == '-' && arg[1] != '\0') {
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

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::FntReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "fnt-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::FntReader::open(std::span(stdin_data));
    } else {
        result = wwd::FntReader::open(file_path);
    }
    if (!result) {
        std::cerr << "fnt-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"" << format_name(info.format) << "\",\n";
        std::cout << "  \"glyphs\": " << info.glyph_count << ",\n";
        int fc = static_cast<int>(info.first_char);
        std::cout << "  \"first_char\": " << fc << ",\n";
        int lc = static_cast<int>(info.last_char);
        std::cout << "  \"last_char\": " << lc << ",\n";
        int mw = static_cast<int>(info.max_width);
        std::cout << "  \"max_width\": " << mw << ",\n";
        int mh = static_cast<int>(info.height);
        std::cout << "  \"max_height\": " << mh << ",\n";
        int bpp = static_cast<int>(info.bits_per_pixel);
        std::cout << "  \"bits_per_pixel\": " << bpp << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:          " << format_name(info.format) << "\n";
        std::cout << "Glyphs:          " << info.glyph_count << "\n";
        std::cout << "Character range: " << static_cast<int>(info.first_char)
                  << "-" << static_cast<int>(info.last_char) << "\n";
        std::cout << "Max dimensions:  " << static_cast<int>(info.max_width)
                  << "x" << static_cast<int>(info.height) << "\n";
        int bpp = static_cast<int>(info.bits_per_pixel);
        std::cout << "Bits per pixel:  " << bpp << "\n";
    }

    return 0;
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
    bool frames_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: fnt-tool export <file.fnt> "
                      << "[-o out.png] [-m metrics.json]\n"
                      << "       fnt-tool export <file.fnt> --frames "
                      << "[-o output_prefix]\n";
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
        if (std::strcmp(arg, "--frames") == 0) {
            frames_mode = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
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

    // Open FNT file from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::FntReader>> fnt_result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "fnt-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        fnt_result = wwd::FntReader::open(std::span(stdin_data));
    } else {
        fnt_result = wwd::FntReader::open(file_path);
    }
    if (!fnt_result) {
        std::cerr << "fnt-tool: error: "
                  << fnt_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *fnt_result.value();
    const auto& info = reader.info();
    const auto& glyphs = reader.glyphs();

    // Default output paths
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + (frames_mode ? "" : ".png");
    }

    // Frames mode - export individual glyphs
    if (frames_mode) {
        double log_val = std::log10(glyphs.size() + 1);
        int digits = std::max(3, static_cast<int>(std::ceil(log_val)));
        size_t exported = 0;

        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& g = glyphs[i];
            if (g.width == 0 || g.height == 0) continue;

            auto glyph_data = reader.decode_glyph(i);
            if (glyph_data.empty()) continue;

            std::ostringstream fname;
            fname << output_path << "_" << std::setfill('0')
                  << std::setw(digits) << i << ".png";
            std::string final_path = fname.str();

            if (fs::exists(final_path) && !force) {
                std::cerr << "fnt-tool: error: output file exists: "
                          << final_path << " (use --force to overwrite)\n";
                return 1;
            }

            // Create grayscale+alpha image
            std::vector<uint8_t> ga(g.width * g.height * 2);
            for (size_t j = 0; j < glyph_data.size(); ++j) {
                ga[j * 2] = 255;            // White
                ga[j * 2 + 1] = glyph_data[j];  // Alpha = intensity
            }

            if (!wwd::write_png_ga(final_path, ga.data(), g.width, g.height)) {
                std::cerr << "fnt-tool: error: failed to write: "
                          << final_path << "\n";
                return 3;
            }

            if (verbose) {
                std::cerr << "Wrote " << final_path << " (" << g.width << "x"
                          << g.height << ")\n";
            }
            exported++;
        }

        std::cout << "Exported " << exported << " glyphs\n";
        return 0;
    }

    if (metrics_path.empty()) {
        // Derive JSON path from output path (same directory and stem)
        fs::path p(output_path);
        metrics_path = (p.parent_path() / p.stem()).string() + ".json";
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

    // Create grayscale+alpha atlas (transparent background, white glyphs)
    std::vector<uint8_t> ga(atlas_width * final_height * 2, 0);

    // Render glyphs
    for (const auto& pg : packed) {
        if (pg.width == 0) continue;

        auto glyph_data = reader.decode_glyph(pg.glyph_index);
        if (glyph_data.empty()) continue;

        for (uint32_t gy = 0; gy < pg.height; ++gy) {
            for (uint32_t gx = 0; gx < pg.width; ++gx) {
                size_t src_idx = gy * pg.width + gx;
                uint8_t intensity = glyph_data[src_idx];

                size_t dst_x = pg.x + gx;
                size_t dst_y = pg.y + gy;
                size_t dst_idx = (dst_y * atlas_width + dst_x) * 2;

                // White glyph with intensity as alpha
                ga[dst_idx] = 255;          // Gray (white)
                ga[dst_idx + 1] = intensity;  // Alpha
            }
        }
    }

    // Write PNG
    if (!wwd::write_png_ga(output_path, ga.data(), atlas_width, final_height)) {
        std::cerr << "fnt-tool: error: failed to write: "
                  << output_path << "\n";
        return 1;
    }

    // Write JSON metrics
    std::ofstream json_out(metrics_path);
    if (!json_out) {
        std::cerr << "fnt-tool: error: failed to write: "
                  << metrics_path << "\n";
        return 1;
    }

    json_out << "{\n";
    std::string src_esc = json_escape(reader.source_filename());
    json_out << "  \"source\": \"" << src_esc << "\",\n";
    json_out << "  \"atlasWidth\": " << atlas_width << ",\n";
    json_out << "  \"atlasHeight\": " << final_height << ",\n";
    json_out << "  \"maxHeight\": " << static_cast<int>(info.height) << ",\n";
    json_out << "  \"maxWidth\": " << static_cast<int>(info.max_width) << ",\n";
    json_out << "  \"glyphs\": {\n";

    bool first = true;
    for (const auto& pg : packed) {
        // Glyph index is 0-based, representing ASCII chars from first_char
        int gidx = static_cast<int>(pg.glyph_index);
        int char_code = info.first_char + gidx;

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
        std::cerr << "fnt-tool: error: failed to write: "
                  << metrics_path << "\n";
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
