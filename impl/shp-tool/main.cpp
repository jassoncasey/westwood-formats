#include <westwood/shp.h>
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
    out << "Usage: shp-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show sprite information\n"
              << "    export      Export to PNG or GIF format\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -q, --quiet     Suppress non-essential output\n"
              << "    -o, --output    Output file path\n"
              << "    -f, --force     Overwrite existing files\n"
              << "    -p, --palette   PAL file for color lookup\n"
              << "    --frames        Output one PNG per frame (default)\n"
              << "    --sheet         Output single sprite sheet PNG\n"
              << "    --gif           Output animated GIF\n"
              << "    --fps <N>       Frame rate for GIF (default: 15)\n"
              << "    --transparent   Treat index 0 as transparent in GIF\n"
              << "    --json          Output info in JSON format\n";
}

static void print_version() {
    std::cout << "shp-tool " << VERSION << "\n";
}

static const char* format_name(wwd::ShpFormat format) {
    switch (format) {
        case wwd::ShpFormat::TD: return "TD/RA SHP";
        case wwd::ShpFormat::TS: return "TS/RA2 SHP";
        default:                  return "Unknown";
    }
}

static std::string frame_format_str(uint8_t fmt, wwd::ShpFormat shp_fmt) {
    if (shp_fmt == wwd::ShpFormat::TS) {
        return "RLE-Zero";
    }
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
    bool json_output = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: shp-tool info [--json] <file.shp>\n";
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
            std::cerr << "shp-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "shp-tool: error: missing file argument\n";
        return 1;
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::ShpReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "shp-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        result = wwd::ShpReader::open(std::span(stdin_data));
    } else {
        result = wwd::ShpReader::open(file_path);
    }
    if (!result) {
        std::cerr << "shp-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *result.value();
    const auto& info = reader.info();
    const auto& frames = reader.frames();

    if (json_output) {
        std::cout << "{\n";
        std::cout << "  \"format\": \"" << format_name(info.format) << "\",\n";
        std::cout << "  \"frames\": " << info.frame_count << ",\n";
        std::cout << "  \"width\": " << info.max_width << ",\n";
        std::cout << "  \"height\": " << info.max_height << ",\n";
        std::cout << "  \"delta_buffer\": " << info.delta_buffer_size << ",\n";
        std::cout << "  \"file_size\": " << info.file_size << ",\n";
        std::cout << "  \"lcw_frames\": " << info.lcw_frames << ",\n";
        std::cout << "  \"xor_frames\": " << info.xor_frames << "\n";
        std::cout << "}\n";
    } else {
        std::cout << "Format:       " << format_name(info.format) << "\n";
        std::cout << "Frames:       " << info.frame_count << "\n";
        std::cout << "Dimensions:   " << info.max_width << "x"
                  << info.max_height << "\n";
        std::cout << "Delta buffer: " << info.delta_buffer_size << " bytes\n";
        std::cout << "Compression:  LCW + XOR delta\n";
        std::cout << "Frame breakdown:\n";
        std::cout << "  LCW base frames:  " << info.lcw_frames << "\n";
        std::cout << "  XOR delta frames: " << info.xor_frames << "\n";

        if (verbose) {
            std::cout << "\n";
            std::cout << std::left << std::setw(6) << "Frame"
                      << std::setw(10) << "Format"
                      << std::setw(10) << "Size"
                      << std::setw(12) << "Offset"
                      << "RefOffset\n";
            std::cout << std::string(48, '-') << "\n";

            for (size_t i = 0; i < frames.size(); ++i) {
                const auto& f = frames[i];
                std::string ffmt = frame_format_str(f.format, info.format);
                std::cout << std::left << std::setw(6) << i
                          << std::setw(10) << ffmt
                          << std::setw(10) << (std::to_string(f.width) + "x" +
                                               std::to_string(f.height))
                          << std::setw(12) << format_offset(f.data_offset);

                if (info.format == wwd::ShpFormat::TD && (f.format & 0x20)) {
                    std::cout << format_offset(f.ref_offset);
                } else {
                    std::cout << "-";
                }
                std::cout << "\n";
            }
        }
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
        const int min_code_size = 8;
        out_.put(min_code_size);

        const int clear_code = 256;
        const int eoi_code = 257;
        const int first_code = 258;
        const int max_code = 4095;

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

        std::vector<uint8_t> output;
        int bit_pos = 0;

        auto output_code = [&](int code) {
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

        output_code(clear_code);

        if (pixels.empty()) {
            output_code(eoi_code);
            flush_output();
            out_.put(0);
            return;
        }

        std::vector<uint8_t> current = { pixels[0] };

        for (size_t i = 1; i < pixels.size(); ++i) {
            std::vector<uint8_t> test = current;
            test.push_back(pixels[i]);

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
                for (size_t j = 0; j < table.size(); ++j) {
                    if (table[j] == current) {
                        output_code(static_cast<int>(j));
                        break;
                    }
                }

                if (next_code <= max_code) {
                    table.push_back(test);
                    next_code++;

                    if (next_code > (1 << code_size) && code_size < 12) {
                        code_size++;
                    }
                } else {
                    output_code(clear_code);
                    reset_table();
                    code_size = min_code_size + 1;
                    next_code = first_code;
                }

                current = { pixels[i] };
            }
        }

        for (size_t j = 0; j < table.size(); ++j) {
            if (table[j] == current) {
                output_code(static_cast<int>(j));
                break;
            }
        }

        output_code(eoi_code);
        flush_output();
        out_.put(0);
    }
};

static int cmd_export(int argc, char* argv[]) {
    std::string file_path;
    std::string output_path;
    std::string palette_path;
    bool force = false;
    bool verbose = false;
    bool as_sheet = false;
    bool as_gif = false;
    int fps = 15;
    bool transparent = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: shp-tool export <file.shp> -p <pal> "
                      << "[--frames|--sheet] [-o out]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "shp-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--palette") == 0) {
            if (i + 1 < argc) {
                palette_path = argv[++i];
            } else {
                std::cerr << "shp-tool: error: -p requires an argument\n";
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
        if (std::strcmp(arg, "--sheet") == 0) {
            as_sheet = true;
            continue;
        }
        if (std::strcmp(arg, "--frames") == 0) {
            // frames mode is default, nothing to do
            continue;
        }
        if (std::strcmp(arg, "--gif") == 0) {
            as_gif = true;
            continue;
        }
        if (std::strcmp(arg, "--fps") == 0) {
            if (i + 1 < argc) {
                fps = std::atoi(argv[++i]);
                if (fps <= 0) fps = 15;
            } else {
                std::cerr << "shp-tool: error: --fps requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "--transparent") == 0) {
            transparent = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "shp-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        }
    }

    if (file_path.empty()) {
        std::cerr << "shp-tool: error: missing file argument\n";
        return 1;
    }

    if (palette_path.empty()) {
        std::cerr << "shp-tool: error: palette required (use -p <file.pal>)\n";
        return 1;
    }

    bool from_stdin = (file_path == "-");

    // Default: frames mode (handled implicitly when !as_sheet)

    // Open SHP file from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::ShpReader>> shp_result;
    if (from_stdin) {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "shp-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        stdin_data = std::move(*data);
        shp_result = wwd::ShpReader::open(std::span(stdin_data));
    } else {
        shp_result = wwd::ShpReader::open(file_path);
    }
    if (!shp_result) {
        std::cerr << "shp-tool: error: "
                  << shp_result.error().message() << "\n";
        return 2;
    }

    const auto& reader = *shp_result.value();
    const auto& info = reader.info();

    // Open palette
    auto pal_result = wwd::PalReader::open(palette_path);
    if (!pal_result) {
        std::cerr << "shp-tool: error: "
                  << pal_result.error().message() << "\n";
        return 2;
    }

    const auto& palette = *pal_result.value();

    // Decode all frames
    auto frames_result = reader.decode_all_frames();
    if (!frames_result) {
        std::cerr << "shp-tool: error: "
                  << frames_result.error().message() << "\n";
        return 2;
    }

    const auto& frames = *frames_result;

    if (verbose) {
        std::cerr << "Decoded " << frames.size() << " frames from "
                  << file_path << "\n";
        std::cerr << "Frame size: " << info.max_width << "x"
                  << info.max_height << "\n";
    }

    // Default output path
    if (output_path.empty()) {
        fs::path p(file_path);
        output_path = p.stem().string();
    }

    if (as_gif) {
        // Animated GIF output
        std::string final_path = output_path;
        if (final_path.find('.') == std::string::npos) {
            final_path += ".gif";
        }

        if (final_path != "-" && fs::exists(final_path) && !force) {
            std::cerr << "shp-tool: error: output file exists: " << final_path
                      << " (use --force to overwrite)\n";
            return 1;
        }

        // Build palette array for GifWriter
        std::array<wwd::Color, 256> pal_colors;
        for (int i = 0; i < 256; ++i) {
            pal_colors[i] = palette.color_8bit(i);
        }

        // Calculate delay in centiseconds (GIF uses 1/100th second units)
        uint16_t delay_cs = static_cast<uint16_t>(100 / fps);
        if (delay_cs == 0) delay_cs = 1;

        if (final_path == "-") {
            std::ios_base::sync_with_stdio(false);
            uint16_t gw = static_cast<uint16_t>(info.max_width);
            uint16_t gh = static_cast<uint16_t>(info.max_height);
            GifWriter gif(std::cout, gw, gh, pal_colors, true);

            for (const auto& frame_data : frames) {
                gif.write_frame(frame_data, delay_cs, transparent, 0);
            }
            gif.finish();
        } else {
            std::ofstream out(final_path, std::ios::binary);
            if (!out) {
                std::cerr << "shp-tool: error: cannot open: "
                          << final_path << "\n";
                return 3;
            }

            uint16_t gw = static_cast<uint16_t>(info.max_width);
            uint16_t gh = static_cast<uint16_t>(info.max_height);
            GifWriter gif(out, gw, gh, pal_colors, true);

            for (const auto& frame_data : frames) {
                gif.write_frame(frame_data, delay_cs, transparent, 0);
            }
            gif.finish();

            if (!out.good()) {
                std::cerr << "shp-tool: error: failed to write: "
                          << final_path << "\n";
                return 3;
            }
        }

        if (verbose) {
            std::cerr << "Wrote " << final_path << " (" << frames.size()
                      << " frames, "
                      << fps << " fps)\n";
        }

        return 0;
    } else if (as_sheet) {
        // Single sprite sheet - horizontal layout
        uint32_t nf = static_cast<uint32_t>(frames.size());
        uint32_t sheet_width = info.max_width * nf;
        uint32_t sheet_height = info.max_height;

        std::vector<uint8_t> rgba(sheet_width * sheet_height * 4, 0);

        for (size_t f = 0; f < frames.size(); ++f) {
            const auto& frame_data = frames[f];
            uint32_t x_offset = static_cast<uint32_t>(f) * info.max_width;

            for (uint32_t y = 0; y < info.max_height; ++y) {
                for (uint32_t x = 0; x < info.max_width; ++x) {
                    size_t src_idx = y * info.max_width + x;
                    size_t dst_idx = (y * sheet_width + x_offset + x) * 4;

                    uint8_t pal_idx = frame_data[src_idx];
                    auto color = palette.color_8bit(pal_idx);

                    rgba[dst_idx] = color.r;
                    rgba[dst_idx + 1] = color.g;
                    rgba[dst_idx + 2] = color.b;
                    // Index 0 = transparent
                    rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                }
            }
        }

        std::string final_path = output_path;
        if (final_path.find('.') == std::string::npos) {
            final_path += ".png";
        }

        if (final_path != "-" && fs::exists(final_path) && !force) {
            std::cerr << "shp-tool: error: output file exists: " << final_path
                      << " (use --force to overwrite)\n";
            return 1;
        }

        if (final_path == "-") {
            std::ios_base::sync_with_stdio(false);
            bool ok = wwd::write_png_rgba(std::cout, rgba.data(),
                                          sheet_width, sheet_height);
            if (!ok) {
                std::cerr << "shp-tool: error: failed to write to stdout\n";
                return 3;
            }
        } else {
            std::ofstream out(final_path, std::ios::binary);
            if (!out) {
                std::cerr << "shp-tool: error: cannot open: "
                          << final_path << "\n";
                return 3;
            }
            bool ok = wwd::write_png_rgba(out, rgba.data(),
                                          sheet_width, sheet_height);
            if (!ok) {
                std::cerr << "shp-tool: error: failed to write: "
                          << final_path << "\n";
                return 3;
            }
        }

        if (verbose) {
            std::cerr << "Wrote " << final_path << " (" << sheet_width << "x"
                      << sheet_height << ")\n";
        }
    } else {
        // Individual frames
        double log_val = std::log10(frames.size() + 1);
        int digits = std::max(3, static_cast<int>(std::ceil(log_val)));

        for (size_t f = 0; f < frames.size(); ++f) {
            const auto& frame_data = frames[f];

            std::vector<uint8_t> rgba(info.max_width * info.max_height * 4, 0);

            for (uint32_t y = 0; y < info.max_height; ++y) {
                for (uint32_t x = 0; x < info.max_width; ++x) {
                    size_t src_idx = y * info.max_width + x;
                    size_t dst_idx = src_idx * 4;

                    uint8_t pal_idx = frame_data[src_idx];
                    auto color = palette.color_8bit(pal_idx);

                    rgba[dst_idx] = color.r;
                    rgba[dst_idx + 1] = color.g;
                    rgba[dst_idx + 2] = color.b;
                    rgba[dst_idx + 3] = (pal_idx == 0) ? 0 : 255;
                }
            }

            std::ostringstream fname;
            fname << output_path << "_" << std::setfill('0')
                  << std::setw(digits) << f << ".png";
            std::string final_path = fname.str();

            if (fs::exists(final_path) && !force) {
                std::cerr << "shp-tool: error: output file exists: "
                          << final_path << " (use --force)\n";
                return 1;
            }

            std::ofstream out(final_path, std::ios::binary);
            if (!out) {
                std::cerr << "shp-tool: error: cannot open: "
                          << final_path << "\n";
                return 3;
            }
            bool ok = wwd::write_png_rgba(out, rgba.data(),
                                          info.max_width, info.max_height);
            if (!ok) {
                std::cerr << "shp-tool: error: failed to write: "
                          << final_path << "\n";
                return 3;
            }

            if (verbose) {
                std::cerr << "Wrote " << final_path << "\n";
            }
        }

        if (!verbose) {
            std::cerr << "Exported " << frames.size() << " frames\n";
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

    std::cerr << "shp-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
