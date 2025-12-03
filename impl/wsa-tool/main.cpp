#include <westwood/wsa.h>
#include <westwood/pal.h>
#include <westwood/io.h>
#include <westwood/png.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: wsa-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show animation information\n"
              << "    export      Export to GIF or PNG sequence\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -q, --quiet     Suppress non-essential output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    -p, --palette   External PAL file (if no embedded)\n"
              << "    --frames        Export as PNG sequence instead of GIF\n"
              << "    --fps <N>       Frame rate for GIF (default: 15)\n"
              << "    --loop          Loop forever in GIF (default)\n"
              << "    --no-loop       Play once in GIF\n"
              << "    --transparent   Treat index 0 as transparent\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "wsa-tool " << VERSION << "\n";
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

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: wsa-tool info [--json] <file.wsa>\n";
            return 0;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "wsa-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "wsa-tool: error: missing file argument\n";
        return 1;
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::WsaReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "wsa-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::WsaReader::open(std::span(stdin_data));
    } else {
        result = wwd::WsaReader::open(file_path);
    }
    if (!result) {
        std::cerr << "wsa-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"Westwood WSA\",\n";
        std::cout << "  \"frames\": " << info.frame_count << ",\n";
        std::cout << "  \"width\": " << info.width << ",\n";
        std::cout << "  \"height\": " << info.height << ",\n";
        std::cout << "  \"delta_buffer\": " << info.delta_size << ",\n";
        const char* has_pal = info.has_palette ? "true" : "false";
        const char* has_lp = info.has_loop ? "true" : "false";
        std::cout << "  \"has_palette\": " << has_pal << ",\n";
        std::cout << "  \"has_loop\": " << has_lp << ",\n";
        std::cout << "  \"compression\": \"Format40 + LCW\"\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:              Westwood WSA\n";
        std::cout << "Frames:              " << info.frame_count << "\n";
        std::cout << "Dimensions:          " << info.width << "x"
                  << info.height << "\n";
        std::cout << "Delta buffer:        " << format_size(info.delta_size)
                  << " bytes\n";
        const char* pal_str = info.has_palette ? "yes" : "no";
        std::cout << "Has embedded palette: " << pal_str << "\n";
        const char* loop_str = info.has_loop ? "yes" : "no";
        std::cout << "Has loop frame:      " << loop_str << "\n";
        std::cout << "Compression:         Format40 + LCW\n";
    }

    return 0;
}

// Simple GIF writer with LZW compression
class GifWriter {
public:
    GifWriter(std::ostream& out, uint16_t width, uint16_t height,
              const std::array<wwd::Color, 256>& palette, bool loop)
        : out_(out), width_(width), height_(height) {

        // GIF header
        out_.write("GIF89a", 6);

        // Logical screen descriptor
        write_u16(width);
        write_u16(height);
        out_.put(0xF7);  // Global color table, 256 colors (8 bits)
        out_.put(0);     // Background color
        out_.put(0);     // Pixel aspect ratio

        // Global color table
        for (int i = 0; i < 256; ++i) {
            out_.put(palette[i].r);
            out_.put(palette[i].g);
            out_.put(palette[i].b);
        }

        // NETSCAPE extension for looping
        if (loop) {
            out_.put(0x21);  // Extension
            out_.put(0xFF);  // Application extension
            out_.put(11);    // Block size
            out_.write("NETSCAPE2.0", 11);
            out_.put(3);     // Sub-block size
            out_.put(1);     // Loop extension
            out_.put(0);     // Loop count (0 = infinite)
            out_.put(0);
            out_.put(0);     // Block terminator
        }
    }

    void write_frame(const std::vector<uint8_t>& pixels, uint16_t delay_cs,
                     bool transparent, uint8_t trans_idx = 0) {
        // Graphic control extension
        out_.put(0x21);  // Extension
        out_.put(0xF9);  // Graphic control
        out_.put(4);     // Block size

        uint8_t flags = 0x04;  // Disposal: do not dispose
        if (transparent) {
            flags |= 0x01;  // Transparent color flag
        }
        out_.put(flags);

        write_u16(delay_cs);
        out_.put(transparent ? trans_idx : 0);
        out_.put(0);  // Block terminator

        // Image descriptor
        out_.put(0x2C);
        write_u16(0);  // Left
        write_u16(0);  // Top
        write_u16(width_);
        write_u16(height_);
        out_.put(0);  // No local color table

        // LZW compressed data
        write_lzw(pixels);
    }

    void finish() {
        out_.put(0x3B);  // GIF trailer
    }

private:
    std::ostream& out_;
    uint16_t width_, height_;

    void write_u16(uint16_t v) {
        out_.put(v & 0xFF);
        out_.put((v >> 8) & 0xFF);
    }

    void write_lzw(const std::vector<uint8_t>& pixels) {
        // LZW compression for GIF
        // We use a simple but correct implementation

        const int min_code_size = 8;  // 256 colors
        out_.put(min_code_size);

        const int clear_code = 256;
        const int eoi_code = 257;
        const int first_code = 258;
        const int max_code = 4095;

        // Code table
        std::vector<std::vector<uint8_t>> table;
        auto reset_table = [&]() {
            table.clear();
            table.resize(first_code);
            for (int i = 0; i < 256; ++i) {
                table[i] = { static_cast<uint8_t>(i) };
            }
        };

        reset_table();
        int code_size = min_code_size + 1;
        int next_code = first_code;

        // Output buffer for sub-blocks
        std::vector<uint8_t> output;
        int bit_pos = 0;

        auto output_code = [&](int code) {
            // Pack code into output buffer
            for (int i = 0; i < code_size; ++i) {
                if ((bit_pos % 8) == 0) {
                    output.push_back(0);
                }
                if (code & (1 << i)) {
                    output.back() |= (1 << (bit_pos % 8));
                }
                bit_pos++;
            }
        };

        auto flush_output = [&]() {
            // Write sub-blocks (max 255 bytes each)
            size_t pos = 0;
            while (pos < output.size()) {
                size_t chunk = std::min(size_t(255), output.size() - pos);
                out_.put(static_cast<char>(chunk));
                const char* ptr = reinterpret_cast<const char*>(
                    output.data() + pos);
                out_.write(ptr, chunk);
                pos += chunk;
            }
            output.clear();
            bit_pos = 0;
        };

        // Start with clear code
        output_code(clear_code);

        if (pixels.empty()) {
            output_code(eoi_code);
            flush_output();
            out_.put(0);  // Block terminator
            return;
        }

        std::vector<uint8_t> current = { pixels[0] };

        for (size_t i = 1; i < pixels.size(); ++i) {
            std::vector<uint8_t> test = current;
            test.push_back(pixels[i]);

            // Search for test in table
            bool found = false;
            for (size_t j = 0; j < table.size(); ++j) {
                if (table[j] == test) {
                    found = true;
                    break;
                }
            }

            if (found) {
                current = test;
            } else {
                // Output code for current
                for (size_t j = 0; j < table.size(); ++j) {
                    if (table[j] == current) {
                        output_code(static_cast<int>(j));
                        break;
                    }
                }

                // Add test to table
                if (next_code <= max_code) {
                    table.push_back(test);
                    next_code++;

                    // Increase code size if needed
                    if (next_code > (1 << code_size) && code_size < 12) {
                        code_size++;
                    }
                } else {
                    // Table full, emit clear code and reset
                    output_code(clear_code);
                    reset_table();
                    code_size = min_code_size + 1;
                    next_code = first_code;
                }

                current = { pixels[i] };
            }
        }

        // Output final code
        for (size_t j = 0; j < table.size(); ++j) {
            if (table[j] == current) {
                output_code(static_cast<int>(j));
                break;
            }
        }

        output_code(eoi_code);
        flush_output();
        out_.put(0);  // Block terminator
    }
};

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    std::string palette_path;
    bool force = false;
    bool verbose = false;
    bool as_frames = false;
    int fps = 15;
    bool loop = true;
    bool transparent = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: wsa-tool export <file.wsa> [-p pal] "
                      << "[--frames|-o out.gif]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "wsa-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--palette") == 0) {
            if (i + 1 < argc) {
                palette_path = argv[++i];
            } else {
                std::cerr << "wsa-tool: error: -p requires an argument\n";
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
        if (std::strcmp(arg, "--frames") == 0) {
            as_frames = true;
            continue;
        }
        if (std::strcmp(arg, "--fps") == 0) {
            if (i + 1 < argc) {
                fps = std::atoi(argv[++i]);
                if (fps <= 0) fps = 15;
            } else {
                std::cerr << "wsa-tool: error: --fps requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "--loop") == 0) {
            loop = true;
            continue;
        }
        if (std::strcmp(arg, "--no-loop") == 0) {
            loop = false;
            continue;
        }
        if (std::strcmp(arg, "--transparent") == 0) {
            transparent = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "wsa-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "wsa-tool: error: missing file argument\n";
        return 1;
    }

    bool from_stdin = (file_path == "-");

    // Open WSA file from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::WsaReader>> wsa_result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "wsa-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        wsa_result = wwd::WsaReader::open(std::span(stdin_data));
    } else {
        wsa_result = wwd::WsaReader::open(file_path);
    }
    if (!wsa_result) {
        std::cerr << "wsa-tool: error: " << wsa_result.error().message()
                  << "\n";
        return 2;
    }

    const auto& reader = *wsa_result.value();
    const auto& info = reader.info();

    // Get palette
    const std::array<wwd::Color, 256>* palette = nullptr;
    std::unique_ptr<wwd::PalReader> ext_pal_reader;
    std::array<wwd::Color, 256> ext_palette;

    if (!palette_path.empty()) {
        // Use external palette (overrides embedded)
        auto pal_result = wwd::PalReader::open(palette_path);
        if (!pal_result) {
            std::cerr << "wsa-tool: error: "
                      << pal_result.error().message() << "\n";
            return 2;
        }
        ext_pal_reader = std::move(*pal_result);
        for (int i = 0; i < 256; ++i) {
            ext_palette[i] = ext_pal_reader->color_8bit(i);
        }
        palette = &ext_palette;
    } else if (info.has_palette) {
        palette = reader.palette();
    }

    if (!palette) {
        std::cerr << "wsa-tool: error: no palette (use -p <file.pal>)\n";
        return 1;
    }

    // Decode all frames
    auto frames_result = reader.decode_all_frames();
    if (!frames_result) {
        std::cerr << "wsa-tool: error: "
                  << frames_result.error().message() << "\n";
        return 2;
    }

    const auto& frames = *frames_result;

    if (verbose) {
        std::cerr << "Decoded " << frames.size() << " frames from "
                  << file_path << "\n";
        std::cerr << "Frame size: " << info.width << "x"
                  << info.height << "\n";
    }

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        std::string stem = p.stem().string();
        output_path = as_frames ? stem : (stem + ".gif");
    }

    if (as_frames) {
        // Export as PNG sequence
        double log_val = std::log10(frames.size() + 1);
        int digits = std::max(3, static_cast<int>(std::ceil(log_val)));

        for (size_t f = 0; f < frames.size(); ++f) {
            const auto& frame_data = frames[f];

            std::vector<uint8_t> rgba(info.width * info.height * 4, 0);

            for (uint32_t y = 0; y < info.height; ++y) {
                for (uint32_t x = 0; x < info.width; ++x) {
                    size_t src_idx = y * info.width + x;
                    size_t dst_idx = src_idx * 4;

                    uint8_t pal_idx = frame_data[src_idx];
                    const auto& color = (*palette)[pal_idx];

                    rgba[dst_idx] = color.r;
                    rgba[dst_idx + 1] = color.g;
                    rgba[dst_idx + 2] = color.b;
                    rgba[dst_idx + 3] = (transparent && pal_idx == 0) ? 0 : 255;
                }
            }

            std::ostringstream fname;
            fname << output_path << "_" << std::setfill('0')
                  << std::setw(digits) << f << ".png";
            std::string final_path = fname.str();

            if (fs::exists(final_path) && !force) {
                std::cerr << "wsa-tool: error: output exists: "
                          << final_path << " (use --force)\n";
                return 1;
            }

            const uint8_t* data = rgba.data();
            uint32_t w = info.width;
            uint32_t h = info.height;
            if (!wwd::write_png_rgba(final_path, data, w, h)) {
                std::cerr << "wsa-tool: error: failed to write: "
                          << final_path << "\n";
                return 1;
            }

            if (verbose) {
                std::cerr << "Wrote " << final_path << "\n";
            }
        }

        if (!verbose) {
            std::cerr << "Exported " << frames.size() << " frames\n";
        }
    } else {
        // Export as GIF
        if (output_path != "-" && fs::exists(output_path) && !force) {
            std::cerr << "wsa-tool: error: output file exists: " << output_path
                      << " (use --force to overwrite)\n";
            return 1;
        }

        // Frame delay in centiseconds (1/100 sec)
        uint16_t delay_cs = static_cast<uint16_t>(std::round(100.0 / fps));
        if (delay_cs < 1) delay_cs = 1;

        if (output_path == "-") {
            // Write to stdout
            std::ios_base::sync_with_stdio(false);
            GifWriter gif(std::cout, info.width, info.height, *palette, loop);
            for (size_t f = 0; f < frames.size(); ++f) {
                gif.write_frame(frames[f], delay_cs, transparent, 0);
            }
            gif.finish();
            if (!std::cout.good()) {
                std::cerr << "wsa-tool: error: failed to write to stdout\n";
                return 1;
            }
        } else {
            std::ofstream out(output_path, std::ios::binary);
            if (!out) {
                std::cerr << "wsa-tool: error: failed to open: "
                          << output_path << "\n";
                return 1;
            }

            GifWriter gif(out, info.width, info.height, *palette, loop);
            for (size_t f = 0; f < frames.size(); ++f) {
                gif.write_frame(frames[f], delay_cs, transparent, 0);
            }
            gif.finish();

            if (!out.good()) {
                std::cerr << "wsa-tool: error: failed to write: "
                          << output_path << "\n";
                return 1;
            }
        }

        if (verbose) {
            std::cerr << "Wrote " << output_path << " (" << frames.size()
                      << " frames, " << fps << " fps)\n";
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

    std::cerr << "wsa-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
