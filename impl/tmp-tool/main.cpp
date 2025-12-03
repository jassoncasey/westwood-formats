#include <westwood/tmp.h>
#include <westwood/pal.h>
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
              << "    -q, --quiet     Suppress non-essential output\n"
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
        case wwd::TmpFormat::TD:  return "TD TMP (orthographic)";
        case wwd::TmpFormat::RA:  return "RA TMP (orthographic)";
        case wwd::TmpFormat::TS:  return "TS TMP (isometric)";
        case wwd::TmpFormat::RA2: return "RA2 TMP (isometric)";
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
        if (arg[0] == '-' && arg[1] != '\0') {
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

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::TmpReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "tmp-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::TmpReader::open(std::span(stdin_data));
    } else {
        result = wwd::TmpReader::open(file_path);
    }
    if (!result) {
        std::cerr << "tmp-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    bool is_iso = reader.is_isometric();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"" << format_name(info.format) << "\",\n";
        const char* iso_str = (is_iso ? "true" : "false");
        std::cout << "  \"isometric\": " << iso_str << ",\n";
        std::cout << "  \"tiles\": " << info.tile_count << ",\n";
        std::cout << "  \"empty_tiles\": " << info.empty_count << ",\n";
        std::cout << "  \"tile_width\": " << info.tile_width << ",\n";
        std::cout << "  \"tile_height\": " << info.tile_height << ",\n";
        if (is_iso) {
            std::cout << "  \"template_width\": " << info.template_width
                      << ",\n";
            std::cout << "  \"template_height\": " << info.template_height
                      << ",\n";
        }
        std::cout << "  \"index_offset\": " << info.index_start << ",\n";
        std::cout << "  \"image_offset\": " << info.image_start << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:             " << format_name(info.format) << "\n";
        std::cout << "Tiles:              " << info.tile_count << " total ("
                  << info.empty_count << " empty)\n";
        std::cout << "Tile dimensions:    " << info.tile_width << "x"
                  << info.tile_height;
        if (is_iso) {
            std::cout << " (diamond shape)";
        }
        std::cout << "\n";
        if (is_iso) {
            std::cout << "Template size:      " << info.template_width << "x"
                      << info.template_height << " cells\n";

            // Show tile details for isometric tiles
            const auto& tiles = reader.tiles();
            int extra_count = 0, z_data_count = 0;
            for (const auto& tile : tiles) {
                if (tile.valid) {
                    if (tile.has_extra) extra_count++;
                    if (tile.has_z_data) z_data_count++;
                }
            }
            std::cout << "Extra images:       " << extra_count << " tiles\n";
            std::cout << "Z-data (depth):     " << z_data_count << " tiles\n";
        }
        std::cout << "Image data offset:  0x" << std::hex << info.image_start
                  << std::dec << "\n";
        std::cout << "Index table offset: 0x" << std::hex << info.index_start
                  << std::dec << "\n";
    }

    return 0;
}

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    std::string palette_path;
    bool force = false;
    bool verbose = false;
    bool frames_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: tmp-tool export <file.tmp> -p <pal> "
                      << "[-o output.png]\n"
                      << "       tmp-tool export <file.tmp> -p <pal> "
                      << "--frames [-o output_prefix]\n";
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
        if (std::strcmp(arg, "--frames") == 0) {
            frames_mode = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
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

    bool from_stdin = (file_path == "-");

    // Open TMP file from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::TmpReader>> tmp_result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "tmp-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        tmp_result = wwd::TmpReader::open(std::span(stdin_data));
    } else {
        tmp_result = wwd::TmpReader::open(file_path);
    }
    if (!tmp_result) {
        std::cerr << "tmp-tool: error: "
                  << tmp_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *tmp_result.value();
    const auto& info = reader.info();
    const auto& tiles = reader.tiles();

    // Open palette
    auto pal_result = wwd::PalReader::open(palette_path);
    if (!pal_result) {
        std::cerr << "tmp-tool: error: "
                  << pal_result.error().message() << "\n";
        return 2;
    }

    const auto& palette = *pal_result.value();

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string() + (frames_mode ? "" : ".png");
    }

    bool is_iso = reader.is_isometric();

    // Frames mode - export individual tiles
    if (frames_mode) {
        double log_val = std::log10(tiles.size() + 1);
        int digits = std::max(3, static_cast<int>(std::ceil(log_val)));
        size_t exported = 0;

        for (size_t i = 0; i < tiles.size(); ++i) {
            auto tile_data = reader.decode_tile(i);
            if (tile_data.empty()) continue;

            std::ostringstream fname;
            fname << output_path << "_" << std::setfill('0')
                  << std::setw(digits) << i << ".png";
            std::string final_path = fname.str();

            if (fs::exists(final_path) && !force) {
                std::cerr << "tmp-tool: error: output file exists: "
                          << final_path << " (use --force to overwrite)\n";
                return 1;
            }

            // Create RGBA image for this tile
            size_t img_sz = info.tile_width * info.tile_height * 4;
            std::vector<uint8_t> rgba(img_sz, 0);

            if (is_iso) {
                // Isometric rendering for individual tile
                uint32_t half_height = info.tile_height / 2;
                uint32_t half_width = info.tile_width / 2;
                size_t src_idx = 0;

                for (uint32_t ty = 0; ty < info.tile_height; ++ty) {
                    uint32_t row_pixels;
                    if (ty < half_height) {
                        row_pixels = 4 + ty * 4;
                    } else {
                        row_pixels = 4 + (info.tile_height - 1 - ty) * 4;
                    }
                    uint32_t x_start = half_width - row_pixels / 2;

                    bool in_bounds = src_idx < tile_data.size();
                    for (uint32_t px = 0; px < row_pixels && in_bounds; ++px) {
                        uint32_t tx = x_start + px;
                        size_t dst_idx = (ty * info.tile_width + tx) * 4;

                        uint8_t pal_idx = tile_data[src_idx++];
                        auto color = palette.color_8bit(pal_idx);

                        rgba[dst_idx] = color.r;
                        rgba[dst_idx + 1] = color.g;
                        rgba[dst_idx + 2] = color.b;
                        rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                    }
                }
            } else {
                // Rectangular tile
                for (uint32_t ty = 0; ty < info.tile_height; ++ty) {
                    for (uint32_t tx = 0; tx < info.tile_width; ++tx) {
                        size_t src_idx = ty * info.tile_width + tx;
                        if (src_idx >= tile_data.size()) break;

                        size_t dst_idx = src_idx * 4;
                        uint8_t pal_idx = tile_data[src_idx];
                        auto color = palette.color_8bit(pal_idx);

                        rgba[dst_idx] = color.r;
                        rgba[dst_idx + 1] = color.g;
                        rgba[dst_idx + 2] = color.b;
                        rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                    }
                }
            }

            std::ofstream out(final_path, std::ios::binary);
            if (!out) {
                std::cerr << "tmp-tool: error: cannot open: "
                          << final_path << "\n";
                return 3;
            }
            bool ok = wwd::write_png_rgba(out, rgba.data(),
                                          info.tile_width, info.tile_height);
            if (!ok) {
                std::cerr << "tmp-tool: error: failed to write: "
                          << final_path << "\n";
                return 3;
            }

            if (verbose) {
                std::cerr << "Wrote " << final_path << "\n";
            }
            exported++;
        }

        std::cout << "Exported " << exported << " tiles\n";
        return 0;
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
    double sq = std::sqrt(static_cast<double>(info.tile_count));
    uint32_t grid_cols = static_cast<uint32_t>(std::ceil(sq));
    uint32_t grid_rows = (info.tile_count + grid_cols - 1) / grid_cols;

    uint32_t img_width = grid_cols * info.tile_width;
    uint32_t img_height = grid_rows * info.tile_height;

    if (verbose) {
        std::cerr << "Exporting " << file_path << " to " << output_path << "\n";
        std::cerr << "  Format: " << format_name(info.format) << "\n";
        std::cerr << "  Tiles: " << info.tile_count << " ("
                  << valid_tiles << " valid)\n";
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

        uint32_t idx = static_cast<uint32_t>(i);
        uint32_t tile_x = (idx % grid_cols) * info.tile_width;
        uint32_t tile_y = (idx / grid_cols) * info.tile_height;

        if (is_iso) {
            // Isometric diamond shape: data is stored row by row
            // Row 0: center 4px, expanding out then contracting
            // The pixel count per row follows: 4, 8, 12, ..., 48, ..., 12, 8, 4
            // Pixel indices increase by 4 each row until middle, then decrease
            uint32_t half_height = info.tile_height / 2;
            uint32_t half_width = info.tile_width / 2;
            size_t src_idx = 0;

            for (uint32_t ty = 0; ty < info.tile_height; ++ty) {
                // Row width: +4 until middle, then decreases
                uint32_t row_pixels;
                if (ty < half_height) {
                    row_pixels = 4 + ty * 4;  // 4, 8, 12, ..., up to tile_width
                } else {
                    row_pixels = 4 + (info.tile_height - 1 - ty) * 4;
                }

                // Calculate starting x offset for this row (centered)
                uint32_t x_start = half_width - row_pixels / 2;

                bool in_bounds = src_idx < tile_data.size();
                for (uint32_t px = 0; px < row_pixels && in_bounds; ++px) {
                    uint32_t tx = x_start + px;
                    size_t dy = tile_y + ty;
                    size_t dx = tile_x + tx;
                    size_t dst_idx = (dy * img_width + dx) * 4;

                    uint8_t pal_idx = tile_data[src_idx++];
                    auto color = palette.color_8bit(pal_idx);

                    rgba[dst_idx] = color.r;
                    rgba[dst_idx + 1] = color.g;
                    rgba[dst_idx + 2] = color.b;
                    rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                }
            }
        } else {
            // Rectangular tiles (TD/RA)
            for (uint32_t ty = 0; ty < info.tile_height; ++ty) {
                for (uint32_t tx = 0; tx < info.tile_width; ++tx) {
                    size_t src_idx = ty * info.tile_width + tx;
                    size_t dy = tile_y + ty;
                    size_t dx = tile_x + tx;
                    size_t dst_idx = (dy * img_width + dx) * 4;

                    uint8_t pal_idx = tile_data[src_idx];
                    auto color = palette.color_8bit(pal_idx);

                    rgba[dst_idx] = color.r;
                    rgba[dst_idx + 1] = color.g;
                    rgba[dst_idx + 2] = color.b;
                    // Index 0 = transparent
                    rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                }
            }
        }
    }

    if (output_path == "-") {
        std::ios_base::sync_with_stdio(false);
        if (!wwd::write_png_rgba(std::cout, rgba.data(), img_width, img_height)) {
            std::cerr << "tmp-tool: error: failed to write to stdout\n";
            return 1;
        }
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "tmp-tool: error: cannot open: "
                      << output_path << "\n";
            return 1;
        }
        if (!wwd::write_png_rgba(out, rgba.data(), img_width, img_height)) {
            std::cerr << "tmp-tool: error: failed to write: "
                      << output_path << "\n";
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
