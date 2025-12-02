#include <westwood/fnt.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: fnt-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show font information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "fnt-tool 0.1.0\n";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: fnt-tool info <file.fnt>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "fnt-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "fnt-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::FntReader::open(file_path);
    if (!result) {
        std::cerr << "fnt-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    std::cout << "Glyphs:      " << info.glyph_count << "\n";
    std::cout << "Height:      " << int(info.height) << " pixels\n";
    std::cout << "Max width:   " << int(info.max_width) << " pixels\n";
    std::cout << "Data size:   " << info.data_size << " bytes\n";

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

    std::cerr << "fnt-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
