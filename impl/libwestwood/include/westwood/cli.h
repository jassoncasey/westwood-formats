#pragma once

#include <westwood/export.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace wwd {

// Exit codes for CLI tools (consistent across all tools)
enum class ExitCode : int {
    Success = 0,
    InvalidArgs = 1,
    FormatError = 2,
    IoError = 3
};

// Simple argument parser for CLI tools
class WWD_API ArgParser {
public:
    ArgParser(int argc, char* argv[])
        : argc_(argc), argv_(argv), pos_(1) {}

    // Check if more arguments remain
    bool has_next() const { return pos_ < argc_; }

    // Get current position
    int pos() const { return pos_; }

    // Peek at current argument without consuming
    const char* peek() const {
        return pos_ < argc_ ? argv_[pos_] : nullptr;
    }

    // Consume and return next argument
    const char* next() {
        return pos_ < argc_ ? argv_[pos_++] : nullptr;
    }

    // Skip n arguments
    void skip(int n = 1) { pos_ += n; }

    // Check if current arg matches short or long form
    bool match(const char* short_form, const char* long_form) const {
        if (pos_ >= argc_) return false;
        const char* arg = argv_[pos_];
        return (std::strcmp(arg, short_form) == 0 ||
                std::strcmp(arg, long_form) == 0);
    }

    // Check if current arg matches single form
    bool match(const char* form) const {
        if (pos_ >= argc_) return false;
        return std::strcmp(argv_[pos_], form) == 0;
    }

    // Consume if matches, return true if matched
    bool consume(const char* short_form, const char* long_form) {
        if (match(short_form, long_form)) {
            pos_++;
            return true;
        }
        return false;
    }

    // Consume if matches single form
    bool consume(const char* form) {
        if (match(form)) {
            pos_++;
            return true;
        }
        return false;
    }

    // Check if current arg is an option (starts with -)
    bool is_option() const {
        if (pos_ >= argc_) return false;
        return argv_[pos_][0] == '-' && argv_[pos_][1] != '\0';
    }

    // Get option argument (for -o <arg> style options)
    // Returns nullptr if no argument available
    const char* get_option_arg(const char* tool_name, const char* opt_name) {
        if (pos_ >= argc_) {
            std::cerr << tool_name << ": error: "
                      << opt_name << " requires an argument\n";
            return nullptr;
        }
        return argv_[pos_++];
    }

    // Parse remaining args as positional files
    std::vector<std::string> collect_files() {
        std::vector<std::string> files;
        while (pos_ < argc_) {
            files.push_back(argv_[pos_++]);
        }
        return files;
    }

    // Report unknown option error
    void report_unknown(const char* tool_name) const {
        if (pos_ < argc_) {
            std::cerr << tool_name << ": error: unknown option: "
                      << argv_[pos_] << "\n";
        }
    }

private:
    int argc_;
    char** argv_;
    int pos_;
};

// Print version in standard format
inline void print_version(const char* tool_name, const char* version) {
    std::cout << tool_name << " " << version << "\n";
}

// Check for help/version flags at start of args
// Returns true if handled (and caller should exit with 0)
inline bool check_help_version(
    int argc, char* argv[],
    const char* tool_name,
    const char* version,
    void (*print_usage)(std::ostream&))
{
    if (argc < 2) return false;

    const char* arg = argv[1];
    if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
        print_usage(std::cout);
        return true;
    }
    if (std::strcmp(arg, "-V") == 0 || std::strcmp(arg, "--version") == 0) {
        print_version(tool_name, version);
        return true;
    }
    return false;
}

// Output level flags
// Quiet: suppress non-essential output (only errors to stderr)
// Verbose: show progress/debug info to stderr
// Default: normal output (results to stdout, errors to stderr)
struct OutputFlags {
    bool verbose = false;
    bool quiet = false;
};

// Scan arguments for -v/--verbose and -q/--quiet flags
// Does not consume the flags, just detects them
inline OutputFlags scan_output_flags(int argc, char* argv[]) {
    OutputFlags flags;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-v") == 0 ||
            std::strcmp(arg, "--verbose") == 0) {
            flags.verbose = true;
        }
        if (std::strcmp(arg, "-q") == 0 ||
            std::strcmp(arg, "--quiet") == 0) {
            flags.quiet = true;
        }
    }
    return flags;
}

} // namespace wwd
