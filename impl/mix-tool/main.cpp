#include <westwood/mix.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

static void print_usage() {
    std::cerr << "Usage: mix-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show archive information\n"
              << "    list        List contents of archive\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "mix-tool 0.1.0\n";
}

static std::string format_size(uint64_t size) {
    std::string result = std::to_string(size);
    int pos = static_cast<int>(result.length()) - 3;
    while (pos > 0) {
        result.insert(pos, ",");
        pos -= 3;
    }
    return result;
}

static std::string format_hash(uint32_t hash) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << hash;
    return ss.str();
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool info <file.mix>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "mix-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "mix-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::MixReader::open(file_path);
    if (!result) {
        std::cerr << "mix-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    std::cout << "Format:      " << wwd::mix_format_name(info.format) << "\n";
    std::cout << "Game:        " << wwd::mix_game_name(info.game) << "\n";
    std::cout << "Encrypted:   " << (info.encrypted ? "yes" : "no") << "\n";
    std::cout << "Checksum:    " << (info.has_checksum ? "yes" : "no") << "\n";
    std::cout << "Files:       " << info.file_count << "\n";
    std::cout << "Size:        " << format_size(info.file_size) << " bytes\n";

    return 0;
}

static int cmd_list(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool list <file.mix>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "mix-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "mix-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::MixReader::open(file_path);
    if (!result) {
        std::cerr << "mix-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& entries = reader.entries();

    // Sort by offset
    std::vector<const wwd::MixEntry*> sorted;
    sorted.reserve(entries.size());
    for (const auto& e : entries) {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const wwd::MixEntry* a, const wwd::MixEntry* b) {
                  return a->offset < b->offset;
              });

    // Header
    std::cout << std::left << std::setw(14) << "Hash"
              << std::right << std::setw(12) << "Offset"
              << std::setw(12) << "Size" << "\n";
    std::cout << std::string(38, '-') << "\n";

    for (const auto* e : sorted) {
        std::cout << std::left << std::setw(14) << format_hash(e->hash)
                  << std::right << std::setw(12) << e->offset
                  << std::setw(12) << e->size << "\n";
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
    if (std::strcmp(cmd, "list") == 0) {
        return cmd_list(argc - 1, argv + 1);
    }

    std::cerr << "mix-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
