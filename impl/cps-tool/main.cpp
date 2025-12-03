#include <westwood/cps.h>
#include <westwood/pal.h>
#include <westwood/io.h>
#include <westwood/png.h>
#include <westwood/cli.h>

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
              << "    -q, --quiet     Suppress non-essential output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    -p, --palette   External PAL file (if no embed)\n"
              << "    --json          Output info in JSON format\n";
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
        if (arg[0] == '-' && arg[1] != '\0') {
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

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::CpsReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "cps-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::CpsReader::open(std::span(stdin_data));
    } else {
        result = wwd::CpsReader::open(file_path);
    }
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
        const char* comp = compression_name(info.compression);
        std::cout << "  \"compression\": \"" << comp << "\",\n";
        const char* pal_str = (info.has_palette ? "true" : "false");
        std::cout << "  \"has_palette\": " << pal_str << ",\n";
        std::cout << "  \"compressed_size\": " << info.compressed_size << ",\n";
        std::cout << "  \"uncompressed_size\": " << info.uncomp_size << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:              Westwood CPS\n";
        std::cout << "Dimensions:          " << info.width << "x"
                  << info.height << "\n";
        const char* comp = compression_name(info.compression);
        std::cout << "Compression:         " << comp << "\n";
        const char* pal_str = (info.has_palette ? "yes" : "no");
        std::cout << "Has embedded palette: " << pal_str << "\n";
        std::cout << "Compressed size:     " << info.compressed_size
                  << " bytes\n";
        std::cout << "Uncompressed size:   " << info.uncomp_size << " bytes\n";
    }

    return 0;
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
            std::cerr << "Usage: cps-tool export <file.cps> "
                      << "[-p pal] [-o out.png]\n";
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
        if (arg[0] == '-' && arg[1] != '\0') {
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

    bool from_stdin = (file_path == "-");

    // Open CPS file from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::CpsReader>> cps_result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "cps-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        cps_result = wwd::CpsReader::open(std::span(stdin_data));
    } else {
        cps_result = wwd::CpsReader::open(file_path);
    }
    if (!cps_result) {
        std::cerr << "cps-tool: error: "
                  << cps_result.error().message() << "\n";
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
            std::cerr << "cps-tool: error: "
                      << pal_result.error().message() << "\n";
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
        std::cerr << "cps-tool: error: no palette available "
                  << "(use -p <file.pal>)\n";
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
        std::cerr << "Converting " << file_path << " to "
                  << output_path << "\n";
        std::cerr << "  Dimensions: " << info.width << "x"
                  << info.height << "\n";
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
        bool ok = wwd::write_png_rgba(
            std::cout, rgba.data(), info.width, info.height);
        if (!ok) {
            std::cerr << "cps-tool: error: failed to write to stdout\n";
            return 1;
        }
    } else {
        bool ok = wwd::write_png_rgba(
            output_path, rgba.data(), info.width, info.height);
        if (!ok) {
            std::cerr << "cps-tool: error: failed to write: "
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

    if (wwd::check_help_version(argc, argv, "cps-tool", VERSION, print_usage)) {
        return 0;
    }

    const char* cmd = argv[1];

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
