#include <westwood/vqa.h>
#include <westwood/io.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: vqa-tool <command> [options] <file>\n"
        << "\n"
        << "Commands:\n"
        << "    info        Show video information\n"
        << "    export      Export to MP4 or PNG sequence + WAV\n"
        << "\n"
        << "Options:\n"
        << "    -h, --help      Show help message\n"
        << "    -V, --version   Show version\n"
        << "    -v, --verbose   Verbose output\n";
}

static void print_version() {
    std::cout << "vqa-tool 0.1.0\n";
}

static const char* codec_name(uint8_t codec_id) {
    switch (codec_id) {
        case 0:  return "PCM (SND0)";
        case 1:  return "Westwood ADPCM (SND1)";
        case 2:  return "IMA ADPCM (SND2)";
        default: return "Unknown";
    }
}

// Minimal PNG writer for RGB images
static bool write_png_rgb(const std::string& path,
                          const uint8_t* rgb_data,
                          uint32_t width, uint32_t height) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // PNG signature
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    f.write(reinterpret_cast<const char*>(sig), 8);

    auto write_u32be = [&](uint32_t v) {
        uint8_t b[4] = {
            static_cast<uint8_t>(v >> 24),
            static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v)
        };
        f.write(reinterpret_cast<const char*>(b), 4);
    };

    // CRC32 calculation
    auto crc32 = [](const uint8_t* data, size_t len) -> uint32_t {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
            }
        }
        return ~crc;
    };

    // IHDR chunk
    {
        uint8_t ihdr[13] = {
            static_cast<uint8_t>(width >> 24), static_cast<uint8_t>(width >> 16),
            static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width),
            static_cast<uint8_t>(height >> 24), static_cast<uint8_t>(height >> 16),
            static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height),
            8,  // bit depth
            2,  // color type: RGB
            0,  // compression
            0,  // filter
            0   // interlace
        };
        uint8_t chunk[17];
        std::memcpy(chunk, "IHDR", 4);
        std::memcpy(chunk + 4, ihdr, 13);
        write_u32be(13);
        f.write(reinterpret_cast<const char*>(chunk), 17);
        write_u32be(crc32(chunk, 17));
    }

    // IDAT chunk - uncompressed zlib
    {
        size_t row_size = width * 3 + 1;  // +1 for filter byte
        size_t raw_size = row_size * height;

        // Build raw data with filter bytes
        std::vector<uint8_t> raw(raw_size);
        for (uint32_t y = 0; y < height; ++y) {
            raw[y * row_size] = 0;  // filter: none
            std::memcpy(&raw[y * row_size + 1],
                       rgb_data + y * width * 3,
                       width * 3);
        }

        // Zlib store blocks (uncompressed)
        std::vector<uint8_t> zlib;
        zlib.push_back(0x78);  // CMF
        zlib.push_back(0x01);  // FLG

        size_t pos = 0;
        while (pos < raw_size) {
            size_t block_size = std::min(raw_size - pos, size_t(65535));
            bool last = (pos + block_size >= raw_size);
            zlib.push_back(last ? 0x01 : 0x00);
            zlib.push_back(block_size & 0xFF);
            zlib.push_back((block_size >> 8) & 0xFF);
            zlib.push_back(~block_size & 0xFF);
            zlib.push_back((~block_size >> 8) & 0xFF);
            zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + block_size);
            pos += block_size;
        }

        // Adler32
        uint32_t s1 = 1, s2 = 0;
        for (size_t i = 0; i < raw_size; ++i) {
            s1 = (s1 + raw[i]) % 65521;
            s2 = (s2 + s1) % 65521;
        }
        uint32_t adler = (s2 << 16) | s1;
        zlib.push_back((adler >> 24) & 0xFF);
        zlib.push_back((adler >> 16) & 0xFF);
        zlib.push_back((adler >> 8) & 0xFF);
        zlib.push_back(adler & 0xFF);

        // Write IDAT
        std::vector<uint8_t> idat_chunk(4 + zlib.size());
        std::memcpy(idat_chunk.data(), "IDAT", 4);
        std::memcpy(idat_chunk.data() + 4, zlib.data(), zlib.size());

        write_u32be(static_cast<uint32_t>(zlib.size()));
        f.write(reinterpret_cast<const char*>(idat_chunk.data()), idat_chunk.size());
        write_u32be(crc32(idat_chunk.data(), idat_chunk.size()));
    }

    // IEND chunk
    {
        uint8_t iend[4] = {'I', 'E', 'N', 'D'};
        write_u32be(0);
        f.write(reinterpret_cast<const char*>(iend), 4);
        write_u32be(crc32(iend, 4));
    }

    return f.good();
}

// WAV file writer
static bool write_wav(const std::string& path,
                      const std::vector<int16_t>& samples,
                      uint32_t sample_rate,
                      uint8_t channels) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
    uint32_t file_size = 36 + data_size;
    uint16_t block_align = channels * 2;
    uint32_t byte_rate = sample_rate * block_align;

    // RIFF header
    f.write("RIFF", 4);
    uint32_t riff_size = file_size;
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    uint32_t fmt_size = 16;
    f.write(reinterpret_cast<const char*>(&fmt_size), 4);
    uint16_t audio_format = 1;  // PCM
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    uint16_t num_channels = channels;
    f.write(reinterpret_cast<const char*>(&num_channels), 2);
    f.write(reinterpret_cast<const char*>(&sample_rate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    uint16_t bits_per_sample = 16;
    f.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    // data chunk
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_size), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return f.good();
}

static bool write_wav_stream(std::ostream& out,
                             const std::vector<int16_t>& samples,
                             uint32_t sample_rate,
                             uint8_t channels) {
    uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
    uint32_t file_size = 36 + data_size;
    uint16_t block_align = channels * 2;
    uint32_t byte_rate = sample_rate * block_align;

    // RIFF header
    out.write("RIFF", 4);
    uint32_t riff_size = file_size;
    out.write(reinterpret_cast<const char*>(&riff_size), 4);
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

// Check if ffmpeg is available
static bool ffmpeg_available() {
    int result = std::system("ffmpeg -version > /dev/null 2>&1");
    return result == 0;
}

static int cmd_info(int argc, char* argv[], bool verbose) {
    std::string file_path;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cout << "Usage: vqa-tool info [--json] <file.vqa>\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            continue;  // Already handled in main
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "vqa-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "vqa-tool: error: missing file argument\n";
        return 1;
    }

    if (verbose) {
        std::cerr << "Opening: " << file_path << "\n";
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::VqaReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "vqa-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::VqaReader::open(std::span(stdin_data));
    } else {
        result = wwd::VqaReader::open(file_path);
    }
    if (!result) {
        std::cerr << "vqa-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& hdr = info.header;

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood VQA\",\n";
        std::cout << "  \"version\": " << hdr.version << ",\n";
        std::cout << "  \"video\": {\n";
        std::cout << "    \"width\": " << hdr.width << ",\n";
        std::cout << "    \"height\": " << hdr.height << ",\n";
        std::cout << "    \"blockWidth\": " << int(hdr.block_w) << ",\n";
        std::cout << "    \"blockHeight\": " << int(hdr.block_h) << ",\n";
        std::cout << "    \"frameRate\": " << int(hdr.frame_rate) << ",\n";
        std::cout << "    \"frames\": " << hdr.frame_count << ",\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "    \"duration\": " << reader.duration() << ",\n";
        std::cout << "    \"codebookParts\": " << int(hdr.cb_parts) << ",\n";
        std::cout << "    \"maxBlocks\": " << hdr.max_blocks << ",\n";
        std::cout << "    \"colors\": " << (reader.is_hicolor() ? 32768 : 256) << ",\n";
        std::cout << "    \"hicolor\": " << (reader.is_hicolor() ? "true" : "false") << "\n";
        std::cout << "  },\n";
        std::cout << "  \"audio\": {\n";
        std::cout << "    \"present\": " << (info.audio.has_audio ? "true" : "false");
        if (info.audio.has_audio) {
            std::cout << ",\n";
            std::cout << "    \"codec\": \"" << codec_name(info.audio.codec_id) << "\",\n";
            std::cout << "    \"sampleRate\": " << info.audio.sample_rate << ",\n";
            std::cout << "    \"channels\": " << int(info.audio.channels) << ",\n";
            std::cout << "    \"bits\": " << int(info.audio.bits);
        }
        std::cout << "\n  },\n";
        std::cout << "  \"flags\": " << hdr.flags << ",\n";
        std::cout << "  \"fileSize\": " << info.file_size << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format: Westwood VQA v" << hdr.version << "\n";
        std::cout << "Video:\n";
        std::cout << "  Dimensions: " << hdr.width << "x" << hdr.height << "\n";
        std::cout << "  Block size: " << int(hdr.block_w) << "x" << int(hdr.block_h) << "\n";
        std::cout << "  Frame rate: " << int(hdr.frame_rate) << " fps\n";
        std::cout << "  Frames: " << hdr.frame_count << "\n";
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  Duration: " << reader.duration() << " seconds\n";
        std::cout << "  Codebook: " << int(hdr.cb_parts) << " parts, max "
                  << hdr.max_blocks << " blocks\n";
        std::cout << "  Colors: " << (reader.is_hicolor() ? "32768 (hicolor)" : "256 (indexed)") << "\n";
        std::cout << "Audio:\n";
        if (info.audio.has_audio) {
            std::cout << "  Present: yes\n";
            std::cout << "  Codec: " << codec_name(info.audio.codec_id) << "\n";
            std::cout << "  Sample rate: " << info.audio.sample_rate << " Hz\n";
            std::cout << "  Channels: " << int(info.audio.channels)
                      << " (" << (info.audio.channels == 1 ? "mono" : "stereo") << ")\n";
            std::cout << "  Bit depth: " << int(info.audio.bits) << "-bit\n";
        } else {
            std::cout << "  Present: no\n";
        }
    }

    return 0;
}

static int cmd_export(int argc, char* argv[], bool verbose) {
    std::string file_path;
    std::string output_path;
    bool frames_mode = false;
    bool wav_only = false;
    bool mp4_mode = false;
    bool force = false;
    int quality = 18;  // CRF default

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cout << "Usage: vqa-tool export <file.vqa> [--quality N] [-o output.mp4]\n"
                      << "       vqa-tool export <file.vqa> --frames [-o output_prefix]\n"
                      << "       vqa-tool export <file.vqa> --wav [-o output.wav]\n"
                      << "\n"
                      << "Options:\n"
                      << "    --mp4           Export as MP4 (default, requires ffmpeg)\n"
                      << "    --quality <N>   Quality: high/medium/low or CRF 0-51 (default: 18)\n"
                      << "    --frames        Export as PNG sequence + WAV\n"
                      << "    --wav           Export audio only as WAV\n"
                      << "    -o, --output    Output path (default: input name + .mp4)\n"
                      << "    -f, --force     Overwrite existing files\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            continue;  // Already handled in main
        }
        if (std::strcmp(arg, "--frames") == 0) {
            frames_mode = true;
            continue;
        }
        if (std::strcmp(arg, "--wav") == 0) {
            wav_only = true;
            continue;
        }
        if (std::strcmp(arg, "--mp4") == 0) {
            mp4_mode = true;  // Explicit flag, but MP4 is default anyway
            continue;
        }
        if (std::strcmp(arg, "-f") == 0 || std::strcmp(arg, "--force") == 0) {
            force = true;
            continue;
        }
        if (std::strcmp(arg, "--quality") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "vqa-tool: error: --quality requires a value\n";
                return 1;
            }
            const char* qval = argv[++i];
            // Accept named presets or numeric CRF
            if (std::strcmp(qval, "high") == 0) {
                quality = 15;  // Higher quality, larger file
            } else if (std::strcmp(qval, "medium") == 0) {
                quality = 23;  // Balanced
            } else if (std::strcmp(qval, "low") == 0) {
                quality = 28;  // Lower quality, smaller file
            } else {
                quality = std::atoi(qval);
                if (quality < 0 || quality > 51) {
                    std::cerr << "vqa-tool: error: quality must be high/medium/low or 0-51\n";
                    return 1;
                }
            }
            continue;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "vqa-tool: error: -o requires a path\n";
                return 1;
            }
            output_path = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--palette") == 0) {
            // VQA files have embedded palettes, ignore external palette arg
            if (i + 1 >= argc) {
                std::cerr << "vqa-tool: error: -p requires a path\n";
                return 1;
            }
            ++i;  // Skip the palette path
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "vqa-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "vqa-tool: error: missing file argument\n";
        return 1;
    }

    (void)mp4_mode;  // Silence unused warning - MP4 is default mode

    if (verbose) {
        std::cerr << "Opening: " << file_path << "\n";
    }

    bool from_stdin = (file_path == "-");

    // Determine output path
    std::filesystem::path input_p(file_path);
    std::string basename = from_stdin ? "output" : input_p.stem().string();

    if (output_path.empty()) {
        if (frames_mode) {
            output_path = basename;
        } else if (wav_only) {
            output_path = from_stdin ? "-" : (basename + ".wav");
        } else {
            output_path = from_stdin ? "-" : (basename + ".mp4");
        }
    }

    // For MP4 mode (default when not frames or wav), check ffmpeg availability
    bool need_ffmpeg = !frames_mode && !wav_only;
    if (need_ffmpeg && !ffmpeg_available()) {
        std::cerr << "vqa-tool: error: ffmpeg not found in PATH; use --frames for PNG+WAV output\n";
        return 1;
    }

    // Open and decode VQA from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::VqaReader>> result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "vqa-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::VqaReader::open(std::span(stdin_data));
    } else {
        result = wwd::VqaReader::open(file_path);
    }
    if (!result) {
        std::cerr << "vqa-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    // Decode video
    auto video_result = reader.decode_video();
    if (!video_result) {
        std::cerr << "vqa-tool: error: failed to decode video: "
                  << video_result.error().message() << "\n";
        return 2;
    }

    auto& frames = *video_result;

    // Decode audio
    std::vector<int16_t> audio_samples;
    if (info.audio.has_audio) {
        auto audio_result = reader.decode_audio();
        if (!audio_result) {
            std::cerr << "vqa-tool: error: failed to decode audio: "
                      << audio_result.error().message() << "\n";
            return 2;
        }
        audio_samples = std::move(*audio_result);
    }

    if (frames_mode) {
        // PNG sequence + WAV mode
        int digits = std::max(3, static_cast<int>(std::ceil(std::log10(frames.size() + 1))));

        for (size_t i = 0; i < frames.size(); ++i) {
            std::ostringstream oss;
            oss << output_path << "_" << std::setfill('0') << std::setw(digits) << i << ".png";
            std::string frame_path = oss.str();

            if (!force && std::filesystem::exists(frame_path)) {
                std::cerr << "vqa-tool: error: output file exists: " << frame_path
                          << " (use --force to overwrite)\n";
                return 1;
            }

            if (!write_png_rgb(frame_path, frames[i].rgb.data(),
                              frames[i].width, frames[i].height)) {
                std::cerr << "vqa-tool: error: failed to write " << frame_path << "\n";
                return 1;
            }
        }

        // Write audio if present
        if (!audio_samples.empty()) {
            std::string wav_path = output_path + ".wav";
            if (!force && std::filesystem::exists(wav_path)) {
                std::cerr << "vqa-tool: error: output file exists: " << wav_path
                          << " (use --force to overwrite)\n";
                return 1;
            }

            if (!write_wav(wav_path, audio_samples,
                          info.audio.sample_rate, info.audio.channels)) {
                std::cerr << "vqa-tool: error: failed to write " << wav_path << "\n";
                return 1;
            }
        }

        std::cout << "Exported " << frames.size() << " frames to " << output_path << "_*.png";
        if (!audio_samples.empty()) {
            std::cout << " and " << output_path << ".wav";
        }
        std::cout << "\n";
    } else if (wav_only) {
        // WAV-only mode
        if (audio_samples.empty()) {
            std::cerr << "vqa-tool: error: no audio in VQA file\n";
            return 2;
        }

        bool to_stdout = (output_path == "-");
        if (!to_stdout && !force && std::filesystem::exists(output_path)) {
            std::cerr << "vqa-tool: error: output file exists: " << output_path
                      << " (use --force to overwrite)\n";
            return 1;
        }

        if (to_stdout) {
            // Write WAV to stdout
            std::ios_base::sync_with_stdio(false);
            if (!write_wav_stream(std::cout, audio_samples,
                                  info.audio.sample_rate, info.audio.channels)) {
                std::cerr << "vqa-tool: error: failed to write to stdout\n";
                return 3;
            }
        } else {
            if (!write_wav(output_path, audio_samples,
                          info.audio.sample_rate, info.audio.channels)) {
                std::cerr << "vqa-tool: error: failed to write " << output_path << "\n";
                return 3;
            }
            std::cout << "Exported audio to " << output_path << "\n";
        }
    } else {
        // MP4 mode via ffmpeg
        bool to_stdout = (output_path == "-");

        if (!to_stdout && !force && std::filesystem::exists(output_path)) {
            std::cerr << "vqa-tool: error: output file exists: " << output_path
                      << " (use --force to overwrite)\n";
            return 1;
        }

        // Create temp directory for intermediate files
        std::string temp_dir = "/tmp/vqa_export_" + std::to_string(getpid());
        std::filesystem::create_directories(temp_dir);

        // Write frames as PNG
        int digits = std::max(3, static_cast<int>(std::ceil(std::log10(frames.size() + 1))));
        for (size_t i = 0; i < frames.size(); ++i) {
            std::ostringstream oss;
            oss << temp_dir << "/frame_" << std::setfill('0') << std::setw(digits) << i << ".png";
            if (!write_png_rgb(oss.str(), frames[i].rgb.data(),
                              frames[i].width, frames[i].height)) {
                std::cerr << "vqa-tool: error: failed to write temp frame\n";
                std::filesystem::remove_all(temp_dir);
                return 1;
            }
        }

        // Write audio if present
        std::string temp_wav;
        if (!audio_samples.empty()) {
            temp_wav = temp_dir + "/audio.wav";
            if (!write_wav(temp_wav, audio_samples,
                          info.audio.sample_rate, info.audio.channels)) {
                std::cerr << "vqa-tool: error: failed to write temp audio\n";
                std::filesystem::remove_all(temp_dir);
                return 1;
            }
        }

        // Build ffmpeg command
        std::ostringstream cmd;
        cmd << "ffmpeg -y -framerate " << int(info.header.frame_rate)
            << " -i \"" << temp_dir << "/frame_%0" << digits << "d.png\"";

        if (!temp_wav.empty()) {
            cmd << " -i \"" << temp_wav << "\"";
        }

        cmd << " -c:v libx264 -crf " << quality
            << " -pix_fmt yuv420p";

        if (!temp_wav.empty()) {
            cmd << " -c:a aac -b:a 192k";
        }

        if (to_stdout) {
            // Output to stdout with explicit format
            cmd << " -f mp4 -movflags frag_keyframe+empty_moov pipe:1 2>/dev/null";
        } else {
            cmd << " \"" << output_path << "\" 2>/dev/null";
        }

        int ffmpeg_result = std::system(cmd.str().c_str());

        // Cleanup temp files
        std::filesystem::remove_all(temp_dir);

        if (ffmpeg_result != 0) {
            std::cerr << "vqa-tool: error: ffmpeg encoding failed\n";
            return 1;
        }

        if (!to_stdout) {
            std::cout << "Exported to " << output_path << "\n";
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
        print_usage();
        return 0;
    }
    if (std::strcmp(cmd, "-V") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    // Check for verbose flag
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            break;
        }
    }

    if (std::strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1, verbose);
    }
    if (std::strcmp(cmd, "export") == 0) {
        return cmd_export(argc - 1, argv + 1, verbose);
    }

    std::cerr << "vqa-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
