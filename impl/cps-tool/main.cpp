#include <westwood/cps.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: cps-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show CPS file information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "cps-tool 0.1.0\n";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: cps-tool info <file.cps>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "cps-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        } else {
            std::cerr << "cps-tool: unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (file_path.empty()) {
        std::cerr << "cps-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::CpsReader::open(file_path);
    if (!result) {
        std::cerr << "cps-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    std::cout << "Format:       CPS (Compressed Picture)\n";
    std::cout << "Dimensions:   " << info.width << "x" << info.height << "\n";
    std::cout << "Compression:  ";
    switch (info.compression) {
        case 0:  std::cout << "None (raw)\n"; break;
        case 4:  std::cout << "LCW (Format80)\n"; break;
        default: std::cout << "Unknown (" << info.compression << ")\n"; break;
    }
    std::cout << "Uncomp size:  " << info.uncomp_size << " bytes\n";
    std::cout << "File size:    " << (info.file_size + 2) << " bytes\n";
    std::cout << "Palette:      " << (info.has_palette ? "Embedded (768 bytes)" : "None") << "\n";

    if (info.has_palette) {
        const auto* pal = reader.palette();
        if (pal) {
            std::cout << "\n";
            std::cout << "First 16 palette colors (R,G,B):\n";
            for (int row = 0; row < 2; ++row) {
                std::cout << "  ";
                for (int col = 0; col < 8; ++col) {
                    int i = row * 8 + col;
                    const auto& c = (*pal)[i];
                    std::cout << std::setw(2) << i << ":("
                              << std::setw(2) << int(c.r) << ","
                              << std::setw(2) << int(c.g) << ","
                              << std::setw(2) << int(c.b) << ") ";
                }
                std::cout << "\n";
            }
        }
    }

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

    std::cerr << "cps-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
