#include <westwood/tmp.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: tmp-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show tileset information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "tmp-tool 0.1.0\n";
}

static const char* format_name(wwd::TmpFormat format) {
    switch (format) {
        case wwd::TmpFormat::TD: return "TD";
        case wwd::TmpFormat::RA: return "RA";
        default:                  return "Unknown";
    }
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: tmp-tool info <file.tmp>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "tmp-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "tmp-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::TmpReader::open(file_path);
    if (!result) {
        std::cerr << "tmp-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& tiles = reader.tiles();

    std::cout << "Format:      " << format_name(info.format) << " (orthographic)\n";
    std::cout << "Tile size:   " << info.tile_width << "x"
              << info.tile_height << "\n";
    std::cout << "Tiles:       " << info.tile_count << "\n";

    // Count valid tiles
    size_t valid = 0;
    for (const auto& t : tiles) {
        if (t.valid) ++valid;
    }
    std::cout << "Valid:       " << valid << "\n";

    std::cout << "Index:       0x" << std::hex << info.index_start
              << " - 0x" << info.index_end << std::dec << "\n";
    std::cout << "Image data:  0x" << std::hex << info.image_start
              << std::dec << "\n";

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(cmd, "-v") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    if (std::strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1);
    }

    std::cerr << "tmp-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
