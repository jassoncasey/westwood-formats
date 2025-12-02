#include <westwood/shp.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

static void print_usage() {
    std::cerr << "Usage: shp-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show sprite information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "shp-tool 0.1.0\n";
}

static const char* format_name(wwd::ShpFormat format) {
    switch (format) {
        case wwd::ShpFormat::TD: return "TD/RA";
        case wwd::ShpFormat::TS: return "TS/RA2";
        default:                  return "Unknown";
    }
}

static std::string frame_format_str(uint8_t fmt) {
    if (fmt & 0x80) return "LCW";
    if (fmt & 0x40) return "XORPrev";
    if (fmt & 0x20) return "XORLCW";
    return "Raw";
}

static std::string format_offset(uint32_t offset) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(6) << offset;
    return ss.str();
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: shp-tool info <file.shp>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "shp-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "shp-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::ShpReader::open(file_path);
    if (!result) {
        std::cerr << "shp-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& frames = reader.frames();

    std::cout << "Format:      " << format_name(info.format) << "\n";
    std::cout << "Frames:      " << info.frame_count << "\n";
    std::cout << "Max size:    " << info.max_width << "x"
              << info.max_height << "\n";
    std::cout << "\n";

    // Frame table
    std::cout << std::left << std::setw(6) << "Frame"
              << std::setw(10) << "Format"
              << std::setw(10) << "Size"
              << std::setw(12) << "Offset"
              << "RefOffset\n";
    std::cout << std::string(48, '-') << "\n";

    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        std::cout << std::left << std::setw(6) << i
                  << std::setw(10) << frame_format_str(f.format)
                  << std::setw(10) << (std::to_string(f.width) + "x" +
                                       std::to_string(f.height))
                  << std::setw(12) << format_offset(f.data_offset);

        if (f.format & 0x20) {  // XORLCW
            std::cout << format_offset(f.ref_offset);
        } else {
            std::cout << "-";
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

    std::cerr << "shp-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
