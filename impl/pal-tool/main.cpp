#include <westwood/pal.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: pal-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show palette information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "pal-tool 0.1.0\n";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: pal-tool info <file.pal>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "pal-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        } else {
            std::cerr << "pal-tool: unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (file_path.empty()) {
        std::cerr << "pal-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::PalReader::open(file_path);
    if (!result) {
        std::cerr << "pal-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& colors = reader.colors();

    std::cout << "Entries:    " << info.entries << "\n";
    std::cout << "Bit depth:  " << int(info.bit_depth) << "-bit (0-"
              << ((info.bit_depth == 6) ? "63" : "255") << ")\n";
    std::cout << "File size:  " << info.file_size << " bytes\n";
    std::cout << "\n";

    // Show first 16 colors as sample
    std::cout << "First 16 colors (R,G,B):\n";
    for (int row = 0; row < 2; ++row) {
        std::cout << "  ";
        for (int col = 0; col < 8; ++col) {
            int i = row * 8 + col;
            const auto& c = colors[i];
            std::cout << std::setw(2) << i << ":("
                      << std::setw(2) << int(c.r) << ","
                      << std::setw(2) << int(c.g) << ","
                      << std::setw(2) << int(c.b) << ") ";
        }
        std::cout << "\n";
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

    std::cerr << "pal-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
