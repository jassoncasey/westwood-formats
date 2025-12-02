#include <westwood/aud.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: aud-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show audio information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "aud-tool 0.1.0\n";
}

static const char* codec_name(wwd::AudCodec codec) {
    switch (codec) {
        case wwd::AudCodec::WestwoodADPCM: return "Westwood ADPCM";
        case wwd::AudCodec::IMAADPCM:      return "IMA ADPCM";
        default:                            return "Unknown";
    }
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
            std::cerr << "Usage: aud-tool info <file.aud>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "aud-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "aud-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::AudReader::open(file_path);
    if (!result) {
        std::cerr << "aud-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    std::cout << "Sample rate:  " << info.sample_rate << " Hz\n";
    std::cout << "Channels:     " << (info.channels == 1 ? "mono" : "stereo")
              << "\n";
    std::cout << "Bits:         " << int(info.bits) << "\n";
    std::cout << "Compression:  " << codec_name(info.codec) << "\n";
    std::cout << "Samples:      " << format_size(reader.sample_count()) << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Duration:     " << reader.duration() << "s\n";
    std::cout << "Data size:    " << format_size(info.compressed_size)
              << " bytes\n";

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

    std::cerr << "aud-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
