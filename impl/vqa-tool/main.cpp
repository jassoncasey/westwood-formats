#include <westwood/vqa.h>

#include <cstring>
#include <iomanip>
#include <iostream>

static void print_usage() {
    std::cerr << "Usage: vqa-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show video information\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version\n";
}

static void print_version() {
    std::cout << "vqa-tool 0.1.0\n";
}

static const char* codec_name(bool compressed) {
    return compressed ? "IMA ADPCM" : "PCM";
}

static int cmd_info(int argc, char* argv[]) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: vqa-tool info <file.vqa>\n";
            return 0;
        }
        if (arg[0] == '-') {
            std::cerr << "vqa-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "vqa-tool: missing file argument\n";
        return 2;
    }

    auto result = wwd::VqaReader::open(file_path);
    if (!result) {
        std::cerr << "vqa-tool: " << result.error().message() << "\n";
        return 1;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& hdr = info.header;

    std::cout << "Version:     " << hdr.version << "\n";
    std::cout << "Dimensions:  " << hdr.width << "x" << hdr.height << "\n";
    std::cout << "Frames:      " << hdr.frame_count << "\n";
    std::cout << "Framerate:   " << int(hdr.frame_rate) << " fps\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Duration:    " << reader.duration() << "s\n";
    std::cout << "Block size:  " << int(hdr.block_w) << "x"
              << int(hdr.block_h) << "\n";
    std::cout << "Codebook:    " << hdr.max_blocks << " entries, "
              << int(hdr.cb_parts) << " parts\n";

    if (info.audio.has_audio) {
        std::cout << "Audio:       " << info.audio.sample_rate << " Hz, "
                  << (info.audio.channels == 1 ? "mono" : "stereo") << ", "
                  << codec_name(info.audio.compressed) << "\n";
    } else {
        std::cout << "Audio:       none\n";
    }

    std::cout << "Flags:       0x" << std::hex << std::setfill('0')
              << std::setw(4) << hdr.flags << std::dec << "\n";

    if (reader.is_hicolor()) {
        std::cout << "HiColor:     yes (16-bit RGB555)\n";
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

    std::cerr << "vqa-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
