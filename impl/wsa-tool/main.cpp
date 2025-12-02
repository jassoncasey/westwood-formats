#include <westwood/wsa.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

static void print_usage() {
    std::cerr << "Usage: wsa-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show animation information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "wsa-tool 0.1.0\n";
}

static std::string format_size(uint32_t size) {
    std::string result = std::to_string(size);
    int pos = static_cast<int>(result.length()) - 3;
    while (pos > 0) {
        result.insert(pos, ",");
        pos -= 3;
    }
    return result;
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: wsa-tool info <file.wsa>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "wsa-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "wsa-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::WsaReader::open(file_path);
    if (!result) {
        std::cerr << "wsa-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& frames = reader.frames();

    std::cout << "Frames:      " << info.frame_count << "\n";
    std::cout << "Dimensions:  " << info.width << "x" << info.height << "\n";
    std::cout << "Delta size:  " << format_size(info.delta_size) << " bytes\n";
    std::cout << "Palette:     " << (info.has_palette ? "embedded" : "none")
              << "\n";
    std::cout << "Loop:        " << (info.has_loop ? "yes" : "no") << "\n";
    std::cout << "\n";

    // Frame table
    std::cout << std::left << std::setw(8) << "Frame"
              << std::right << std::setw(12) << "Offset"
              << std::setw(12) << "Size" << "\n";
    std::cout << std::string(32, '-') << "\n";

    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        std::cout << std::left << std::setw(8) << i
                  << std::right << std::setw(12) << f.offset
                  << std::setw(12) << f.size << "\n";
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

    std::cerr << "wsa-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
