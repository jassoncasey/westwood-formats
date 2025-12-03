#include <westwood/aud.h>
#include <westwood/io.h>

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: aud-tool <command> [options] <file>\n"
        << "\n"
        << "Commands:\n"
        << "    info        Show audio information\n"
        << "    export      Export to WAV format\n"
        << "\n"
        << "Options:\n"
        << "    -h, --help      Show help message\n"
        << "    -V, --version   Show version\n"
        << "    -v, --verbose   Verbose output\n"
        << "    -q, --quiet     Suppress non-essential output\n"
        << "    -o, --output    Output file path\n"
        << "    -f, --force     Overwrite existing files\n"
        << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "aud-tool " << VERSION << "\n";
}

static const char* codec_name(wwd::AudCodec codec) {
    switch (codec) {
        case wwd::AudCodec::WestwoodADPCM: return "Westwood ADPCM";
        case wwd::AudCodec::IMAADPCM:      return "IMA ADPCM";
        default:                            return "Unknown";
    }
}

static const char* codec_name_json(wwd::AudCodec codec) {
    switch (codec) {
        case wwd::AudCodec::WestwoodADPCM: return "westwood_adpcm";
        case wwd::AudCodec::IMAADPCM:      return "ima_adpcm";
        default:                            return "unknown";
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
    bool json_output = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: aud-tool info [--json] [-v] <file.aud>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "aud-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "aud-tool: error: missing file argument\n";
        return 1;
    }

    // Open from file or stdin
    wwd::Result<std::unique_ptr<wwd::AudReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "aud-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        result = wwd::AudReader::open(std::span(*data));
    } else {
        result = wwd::AudReader::open(file_path);
    }
    if (!result) {
        std::cerr << "aud-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood AUD\",\n";
        const char* cname = codec_name_json(info.codec);
        std::cout << "  \"codec\": \"" << cname << "\",\n";
        std::cout << "  \"sample_rate\": " << info.sample_rate << ",\n";
        int ch = static_cast<int>(info.channels);
        std::cout << "  \"channels\": " << ch << ",\n";
        std::cout << "  \"bits\": " << static_cast<int>(info.bits) << ",\n";
        std::cout << "  \"samples\": " << reader.sample_count() << ",\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  \"duration\": " << reader.duration() << ",\n";
        std::cout << "  \"compressed_size\": " << info.compressed_size << ",\n";
        std::cout << "  \"uncompressed_size\": " << info.uncompressed_size
                  << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:       Westwood AUD\n";
        std::cout << "Codec:        " << codec_name(info.codec);
        if (info.codec == wwd::AudCodec::WestwoodADPCM) {
            std::cout << " (0x01)";
        } else if (info.codec == wwd::AudCodec::IMAADPCM) {
            std::cout << " (0x63)";
        }
        std::cout << "\n";
        std::cout << "Sample rate:  " << info.sample_rate << " Hz\n";
        const char* ch_str = (info.channels == 1 ? "mono" : "stereo");
        std::cout << "Channels:     " << ch_str << "\n";
        std::cout << "Output bits:  16-bit signed\n";
        std::cout << "Samples:      " << format_size(reader.sample_count())
                  << "\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Duration:     " << reader.duration() << " seconds\n";
        std::cout << "Compressed:   " << format_size(info.compressed_size)
                  << " bytes\n";
        std::cout << "Uncompressed: " << format_size(info.uncompressed_size)
                  << " bytes\n";
        if (info.compressed_size > 0) {
            float ratio = static_cast<float>(info.uncompressed_size) /
                         static_cast<float>(info.compressed_size);
            std::cout << "Ratio:        " << std::setprecision(1)
                      << ratio << ":1\n";
        }
        if (verbose) {
            std::cout << "\nDetailed info:\n";
            std::cout << "  Header size:    12 bytes\n";
            std::cout << "  File size:      " << format_size(info.file_size)
                      << " bytes\n";
            if (file_path != "-") {
                std::cout << "  File:           " << file_path << "\n";
            }
        }
    }

    return 0;
}

// Write WAV file
static bool write_wav(const std::string& path,
                      const std::vector<int16_t>& samples,
                      uint32_t sample_rate,
                      uint8_t channels) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
    uint32_t file_size = 36 + data_size;
    uint16_t block_align = channels * 2;
    uint32_t byte_rate = sample_rate * block_align;

    // RIFF header
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&file_size), 4);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    uint16_t audio_format = 1;  // PCM
    out.write(reinterpret_cast<const char*>(&audio_format), 2);
    uint16_t num_channels = channels;
    out.write(reinterpret_cast<const char*>(&num_channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    uint16_t bits_per_sample = 16;
    out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    // data chunk
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return out.good();
}

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    bool force = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: aud-tool export <file.aud> "
                      << "[-o output.wav] [-f]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "aud-tool: error: -o requires an argument\n";
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
            std::cerr << "aud-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "aud-tool: error: missing file argument\n";
        return 1;
    }

    bool from_stdin = (file_path == "-");

    // Default output path
    if (output_path.empty()) {
        if (from_stdin) {
            output_path = "-";  // Default to stdout when reading from stdin
        } else {
            fs::path p(file_path);
            output_path = p.stem().string() + ".wav";
        }
    }

    // Check if output exists
    if (output_path != "-" && fs::exists(output_path) && !force) {
        std::cerr << "aud-tool: error: output file exists: " << output_path
                  << " (use --force to overwrite)\n";
        return 1;
    }

    // Open AUD file from file or stdin
    std::vector<uint8_t> stdin_data;  // Keep data alive for span
    wwd::Result<std::unique_ptr<wwd::AudReader>> result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "aud-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::AudReader::open(std::span(stdin_data));
    } else {
        result = wwd::AudReader::open(file_path);
    }
    if (!result) {
        std::cerr << "aud-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (verbose) {
        std::cerr << "Decoding " << file_path << "...\n";
        std::cerr << "  Codec: " << codec_name(info.codec) << "\n";
        std::cerr << "  Sample rate: " << info.sample_rate << " Hz\n";
        const char* ch_name = (info.channels == 1 ? "mono" : "stereo");
        std::cerr << "  Channels: " << ch_name << "\n";
    }

    // Decode audio
    auto decode_result = reader.decode();
    if (!decode_result) {
        std::cerr << "aud-tool: error: "
                  << decode_result.error().message() << "\n";
        return 2;
    }

    const auto& samples = *decode_result;

    if (verbose) {
        std::cerr << "  Decoded " << samples.size() << " samples\n";
    }

    // Write WAV
    if (output_path == "-") {
        // Write to stdout
        uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
        uint32_t file_size = 36 + data_size;
        uint16_t block_align = info.channels * 2;
        uint32_t byte_rate = info.sample_rate * block_align;

        std::cout.write("RIFF", 4);
        std::cout.write(reinterpret_cast<const char*>(&file_size), 4);
        std::cout.write("WAVE", 4);
        std::cout.write("fmt ", 4);
        uint32_t fmt_size = 16;
        std::cout.write(reinterpret_cast<const char*>(&fmt_size), 4);
        uint16_t audio_format = 1;
        std::cout.write(reinterpret_cast<const char*>(&audio_format), 2);
        uint16_t num_channels = info.channels;
        std::cout.write(reinterpret_cast<const char*>(&num_channels), 2);
        std::cout.write(reinterpret_cast<const char*>(&info.sample_rate), 4);
        std::cout.write(reinterpret_cast<const char*>(&byte_rate), 4);
        std::cout.write(reinterpret_cast<const char*>(&block_align), 2);
        uint16_t bits_per_sample = 16;
        std::cout.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
        std::cout.write("data", 4);
        std::cout.write(reinterpret_cast<const char*>(&data_size), 4);
        const char* ptr = reinterpret_cast<const char*>(samples.data());
        std::cout.write(ptr, data_size);
    } else {
        if (!write_wav(output_path, samples, info.sample_rate, info.channels)) {
            std::cerr << "aud-tool: error: failed to write: "
                      << output_path << "\n";
            return 3;  // I/O error
        }

        if (verbose) {
            std::cerr << "Wrote " << output_path << "\n";
        }
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 1;
    }

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0) {
        print_usage(std::cout);
        return 0;
    }
    if (std::strcmp(cmd, "-V") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    if (std::strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1);
    }
    if (std::strcmp(cmd, "export") == 0) {
        return cmd_export(argc - 1, argv + 1);
    }

    std::cerr << "aud-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
