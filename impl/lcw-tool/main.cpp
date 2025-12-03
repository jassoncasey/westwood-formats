#include <westwood/lcw.h>
#include <westwood/io.h>
#include <westwood/cli.h>

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const char* VERSION = "0.1.0";

static void print_usage(std::ostream& out = std::cout) {
    out << "Usage: lcw-tool <command> [options]\n"
              << "\n"
              << "Commands:\n"
              << "    decompress    Decompress LCW/Format80 data\n"
              << "    format40      Apply Format40/XOR delta to buffer\n"
              << "    test          Run built-in test vectors\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -V, --version   Show version\n"
              << "    -v, --verbose   Verbose output\n"
              << "    -q, --quiet     Suppress non-essential output\n"
              << "    -o, --output    Output file path (default: stdout)\n"
              << "    -s, --size      Expected output size (required)\n"
              << "    -r, --relative  Use relative addressing mode\n"
              << "    --hex           Input is hex string instead of file\n";
}

// Parse hex string to bytes
static std::vector<uint8_t> parse_hex(const std::string& hex) {
    std::vector<uint8_t> result;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 >= hex.length()) break;
        std::string byte_str = hex.substr(i, 2);
        auto val = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        result.push_back(val);
    }
    return result;
}

// Format bytes as hex string
static std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream ss;
    for (uint8_t b : data) {
        ss << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<int>(b);
    }
    return ss.str();
}

static int cmd_decompress(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path = "-";
    std::string hex_input;
    size_t output_size = SIZE_MAX;  // Sentinel: SIZE_MAX means not specified
    bool use_relative = false;
    bool hex_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: lcw-tool decompress "
                      << "[-s SIZE] [-r] [--hex] <input> [-o output]\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "lcw-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "-s") == 0 || std::strcmp(arg, "--size") == 0) {
            if (i + 1 < argc) {
                output_size = std::stoul(argv[++i]);
            } else {
                std::cerr << "lcw-tool: error: -s requires an argument\n";
                return 1;
            }
            continue;
        }
        bool is_rel = std::strcmp(arg, "-r") == 0;
        is_rel = is_rel || std::strcmp(arg, "--relative") == 0;
        if (is_rel) {
            use_relative = true;
            continue;
        }
        if (std::strcmp(arg, "--hex") == 0) {
            hex_mode = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "lcw-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (input_path.empty()) {
            input_path = arg;
        }
    }

    if (input_path.empty()) {
        std::cerr << "lcw-tool: error: missing input argument\n";
        return 1;
    }

    if (output_size == SIZE_MAX) {
        std::cerr << "lcw-tool: error: -s/--size is required\n";
        return 1;
    }

    // Load input data
    std::vector<uint8_t> input_data;
    if (hex_mode) {
        input_data = parse_hex(input_path);
    } else if (input_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "lcw-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        input_data = std::move(*data);
    } else {
        auto data = wwd::load_file(input_path);
        if (!data) {
            std::cerr << "lcw-tool: error: " << data.error().message() << "\n";
            return 2;
        }
        input_data = std::move(*data);
    }

    // Decompress
    auto result = wwd::lcw_decompress(
        std::span(input_data),
        output_size,
        use_relative);

    if (!result) {
        std::cerr << "lcw-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    const auto& output = *result;

    // Write output
    if (output_path == "-") {
        // Output as hex to stdout
        std::cout << to_hex(output) << "\n";
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "lcw-tool: error: cannot open: "
                      << output_path << "\n";
            return 3;
        }
        const char* ptr = reinterpret_cast<const char*>(output.data());
        out.write(ptr, output.size());
    }

    return 0;
}

static int cmd_format40(int argc, char* argv[]) {
    std::string input_path;
    std::string buffer_path;
    std::string output_path = "-";
    std::string hex_input;
    std::string hex_buffer;
    bool hex_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: lcw-tool format40 "
                      << "[--hex] <delta> <buffer> [-o output]\n";
            std::cerr << "  With --hex: lcw-tool format40 "
                      << "--hex <delta_hex> <buffer_hex>\n";
            return 0;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "lcw-tool: error: -o requires an argument\n";
                return 1;
            }
            continue;
        }
        if (std::strcmp(arg, "--hex") == 0) {
            hex_mode = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "lcw-tool: error: unknown option: " << arg << "\n";
            return 1;
        }
        if (input_path.empty()) {
            input_path = arg;
        } else if (buffer_path.empty()) {
            buffer_path = arg;
        }
    }

    if (input_path.empty() || buffer_path.empty()) {
        std::cerr << "lcw-tool: error: missing delta and buffer arguments\n";
        return 1;
    }

    // Load input data
    std::vector<uint8_t> delta_data;
    std::vector<uint8_t> buffer_data;

    if (hex_mode) {
        delta_data = parse_hex(input_path);
        buffer_data = parse_hex(buffer_path);
    } else {
        auto delta = wwd::load_file(input_path);
        if (!delta) {
            std::cerr << "lcw-tool: error: " << delta.error().message() << "\n";
            return 2;
        }
        delta_data = std::move(*delta);

        auto buffer = wwd::load_file(buffer_path);
        if (!buffer) {
            std::cerr << "lcw-tool: error: "
                      << buffer.error().message() << "\n";
            return 2;
        }
        buffer_data = std::move(*buffer);
    }

    // Apply format40
    auto result = wwd::format40_decompress(
        std::span(delta_data),
        std::span(buffer_data));

    if (!result) {
        std::cerr << "lcw-tool: error: " << result.error().message() << "\n";
        return 2;
    }

    // Write output
    if (output_path == "-") {
        // Output as hex to stdout
        std::cout << to_hex(buffer_data) << "\n";
    } else {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "lcw-tool: error: cannot open: "
                      << output_path << "\n";
            return 3;
        }
        const char* ptr = reinterpret_cast<const char*>(buffer_data.data());
        out.write(ptr, buffer_data.size());
    }

    return 0;
}

// Built-in test vectors
struct TestVector {
    const char* name;
    const char* input_hex;
    size_t output_size;
    const char* expected_hex;
    bool relative;
};

static const TestVector lcw_tests[] = {
    // End marker only
    {
        "empty (end marker)",
        "80",  // 0x80 = end marker
        0,
        "",
        false
    },
    // Literal copy: 0x83 = copy 3 bytes
    {
        "literal 3 bytes",
        "83414243" "80",  // copy 3 bytes: ABC, then end
        3,
        "414243",
        false
    },
    // Short relative copy: 0x00-0x7F
    // Format: 0cccpppp pppppppp
    // First write some data, then copy from it
    {
        "literal then short copy",
        "83414243"  // literal: ABC
        "0003"      // short copy: count=3, offset=3 (copies ABC again)
        "80",       // end
        6,
        "414243414243",
        false
    },
    // Fill operation: 0xFE count_lo count_hi value
    {
        "fill 5 bytes with 0x55",
        "fe0500" "55"  // fill 5 bytes with 0x55
        "80",          // end
        5,
        "5555555555",
        false
    },
    // Combined: literal + fill
    {
        "literal then fill",
        "82" "4142"    // literal 2 bytes: AB
        "fe0300" "43"  // fill 3 bytes with C
        "80",          // end
        5,
        "4142434343",
        false
    },
};

static const TestVector format40_tests[] = {
    // Empty delta (no changes)
    {
        "empty delta",
        "800000",  // END marker (0x80 followed by 0x0000)
        4,
        "41424344",  // Original: ABCD, unchanged
        false
    },
    // SHORTDUMP: XOR next N bytes
    {
        "xor 2 bytes",
        "02" "0102"  // XOR 2 bytes with 01, 02
        "800000",    // END
        4,
        "40404344",  // ABCD XOR 01,02,00,00 = @@ CD
        false
    },
    // SHORTRUN: XOR fill (0x00 count value)
    {
        "xor fill",
        "00" "03" "FF"  // XOR fill 3 bytes with 0xFF
        "800000",       // END
        4,
        "bebdbc44",     // ABCD XOR FF,FF,FF,00 = ¾½¼D
        false
    },
    // SHORTSKIP: skip bytes (0x81-0xFF)
    {
        "skip 2 then xor",
        "82"         // skip 2 bytes
        "01" "01"    // XOR 1 byte with 01
        "800000",    // END
        4,
        "41424244",  // ABCD with C XOR 01 = ABBD
        false
    },
};

static int cmd_test(int argc, char* argv[], bool verbose) {
    (void)argc;
    (void)argv;

    int passed = 0;
    int failed = 0;

    if (verbose) {
        std::cerr << "LCW decompression tests:\n";
    }
    for (const auto& test : lcw_tests) {
        auto input = parse_hex(test.input_hex);
        auto expected = parse_hex(test.expected_hex);

        auto result = wwd::lcw_decompress(
            std::span(input),
            test.output_size,
            test.relative);

        bool ok = false;
        if (result) {
            ok = (*result == expected);
        }

        if (ok) {
            if (verbose) {
                std::cerr << "  PASS: " << test.name << "\n";
            }
            passed++;
        } else {
            std::cerr << "  FAIL: " << test.name << "\n";
            if (result) {
                std::cerr << "    expected: " << test.expected_hex << "\n";
                std::cerr << "    got:      " << to_hex(*result) << "\n";
            } else {
                std::cerr << "    error: " << result.error().message() << "\n";
            }
            failed++;
        }
    }

    if (verbose) {
        std::cerr << "\nFormat40 XOR delta tests:\n";
    }
    for (const auto& test : format40_tests) {
        auto delta = parse_hex(test.input_hex);
        auto buffer = parse_hex("41424344");  // ABCD
        auto expected = parse_hex(test.expected_hex);

        auto result = wwd::format40_decompress(
            std::span(delta),
            std::span(buffer));

        bool ok = false;
        if (result) {
            ok = (buffer == expected);
        }

        if (ok) {
            if (verbose) {
                std::cerr << "  PASS: " << test.name << "\n";
            }
            passed++;
        } else {
            std::cerr << "  FAIL: " << test.name << "\n";
            if (result) {
                std::cerr << "    expected: " << test.expected_hex << "\n";
                std::cerr << "    got:      " << to_hex(buffer) << "\n";
            } else {
                std::cerr << "    error: " << result.error().message() << "\n";
            }
            failed++;
        }
    }

    if (verbose || failed > 0) {
        std::cerr << "\nTotal: " << passed << " passed, "
                  << failed << " failed\n";
    }

    return failed > 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 1;
    }

    if (wwd::check_help_version(argc, argv, "lcw-tool", VERSION, print_usage)) {
        return 0;
    }

    auto flags = wwd::scan_output_flags(argc, argv);
    const char* cmd = argv[1];

    if (std::strcmp(cmd, "decompress") == 0) {
        return cmd_decompress(argc - 1, argv + 1);
    }
    if (std::strcmp(cmd, "format40") == 0) {
        return cmd_format40(argc - 1, argv + 1);
    }
    if (std::strcmp(cmd, "test") == 0) {
        return cmd_test(argc - 1, argv + 1, flags.verbose);
    }

    std::cerr << "lcw-tool: error: unknown command '" << cmd << "'\n";
    print_usage(std::cerr);
    return 1;
}
