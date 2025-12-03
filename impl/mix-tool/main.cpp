#include <westwood/mix.h>
#include <westwood/io.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static void print_usage() {
    std::cerr << "Usage: mix-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    info        Show archive information\n"
              << "    list        List contents of archive\n"
              << "    extract     Extract files from archive\n"
              << "    hash        Compute hash for filename\n"
              << "\n"
              << "Options:\n"
              << "    -n, --names <file>  Load filename database\n"
              << "    -o, --output <dir>  Output directory (extract command)\n"
              << "    -h, --help          Show help message\n"
              << "    -V, --version       Show version\n"
              << "    -v, --verbose       Verbose output\n"
              << "    -q, --quiet         Suppress non-essential output\n"
              << "\n"
              << "Names file format:\n"
              << "    One filename per line. Comments start with #.\n"
              << "    Can also use OpenRA's global mix database.dat format.\n";
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

// Check if data looks like a MIX file
static bool is_mix_data(const std::vector<uint8_t>& data) {
    if (data.size() < 6) return false;

    // Check for RA/TS format: first 2 bytes are 0x0000
    uint16_t first_word = data[0] | (uint16_t(data[1]) << 8);
    if (first_word == 0) {
        // RA/TS format - check flags
        if (data.size() < 4) return false;
        uint16_t flags = data[2] | (uint16_t(data[3]) << 8);
        // Valid flags: 0, 1 (checksum), 2 (encrypted), 3 (both)
        return (flags & ~0x0003) == 0;
    }

    // TD format: first 2 bytes are file count (1-4095)
    if (first_word >= 1 && first_word <= 4095) {
        if (data.size() < 6) return false;
        uint32_t body_size = data[2] | (uint32_t(data[3]) << 8) |
                         (uint32_t(data[4]) << 16) |
                         (uint32_t(data[5]) << 24);
        size_t header_size = 6 + (size_t(first_word) * 12);

        // Strict check: total must match data size closely
        size_t expected_size = header_size + body_size;
        if (expected_size > data.size() || expected_size < data.size() - 20) {
            return false;  // Size mismatch
        }

        // Additional sanity: check that index entries have valid offsets
        if (data.size() >= header_size && first_word > 0) {
            // Check first entry's offset - should be < body_size
            size_t entry_offset = 6;  // First entry at offset 6
            uint32_t e_offset = data[entry_offset + 4] |
                               (uint32_t(data[entry_offset + 5]) << 8) |
                               (uint32_t(data[entry_offset + 6]) << 16) |
                               (uint32_t(data[entry_offset + 7]) << 24);
            if (e_offset >= body_size && body_size > 0) {
                return false;  // Invalid entry offset
            }
        }

        return true;
    }

    return false;
}

// Check if entry might be a MIX file (by name or data)
static bool might_be_mix(const wwd::MixEntry& entry,
                         const std::vector<uint8_t>* data = nullptr) {
    // Check by name extension
    if (!entry.name.empty()) {
        size_t len = entry.name.size();
        if (len >= 4) {
            std::string ext = entry.name.substr(len - 4);
            // Convert to uppercase for comparison
            for (char& c : ext) c = std::toupper(static_cast<unsigned char>(c));
            if (ext == ".MIX") return true;
        }
    }

    // Check by data if provided
    if (data && !data->empty()) {
        return is_mix_data(*data);
    }

    return false;
}

// Load filename database (one name per line, supports comments with #)
// Also supports OpenRA format: "filename.ext	description"
static std::vector<std::string> load_names(const std::string& path) {
    std::vector<std::string> names;
    std::ifstream file(path);
    if (!file) {
        std::cerr << "mix-tool: warning: cannot open names file: "
                  << path << "\n";
        return names;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Handle OpenRA format: "filename.ext\tdescription"
        // Also handle simple format: filename.ext
        std::string name;

        if (line[0] == '"') {
            // Quoted format - find closing quote
            size_t end = line.find('"', 1);
            if (end != std::string::npos) {
                name = line.substr(1, end - 1);
            }
        } else {
            // Unquoted - take until whitespace or end
            size_t end = line.find_first_of(" \t");
            if (end == std::string::npos) {
                name = line;
            } else {
                name = line.substr(0, end);
            }
        }

        // Trim trailing whitespace
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t' ||
                                  name.back() == '\r' || name.back() == '\n')) {
            name.pop_back();
        }

        if (!name.empty()) {
            names.push_back(name);
        }
    }

    return names;
}

static int cmd_info(int argc, char* argv[], bool verbose) {
    std::string file_path;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool info <file.mix>\n"
                      << "\n"
                      << "Use '-' to read from stdin.\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            continue;  // Already handled in main
        }
        if (arg[0] == '-' && arg[1] != '\0') {
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

    if (verbose) {
        std::cerr << "Opening: " << file_path << "\n";
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::MixReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "mix-tool: error: " << data.error().message() << "\n";
            return 1;
        }
        stdin_data = std::move(*data);
        result = wwd::MixReader::open(std::span(stdin_data));
    } else {
        result = wwd::MixReader::open(file_path);
    }

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

    (void)verbose;  // Verbose info already shown during open

    return 0;
}

static constexpr int MAX_RECURSION_DEPTH = 10;

// Forward declaration
static void list_recursive(wwd::MixReader& reader,
                           const std::vector<std::string>& names,
                           int depth,
                           const std::string& prefix,
                           bool is_last);

static void print_entry_tree(const wwd::MixEntry& entry,
                             wwd::MixReader& reader,
                             const std::vector<std::string>& names,
                             int depth,
                             const std::string& prefix,
                             bool is_last,
                             bool recursive) {
    // Print tree connector
    std::cout << prefix;
    std::cout << (is_last ? "└── " : "├── ");

    // Print entry info
    std::string display_name = entry.name.empty()
        ? format_hash(entry.hash)
        : entry.name;
    std::cout << display_name << " (" << format_size(entry.size) << " bytes)";

    // Check if this might be a nested MIX and we should recurse
    bool is_nested_mix = false;
    std::vector<uint8_t> entry_data;

    if (recursive && depth < MAX_RECURSION_DEPTH) {
        // First check by name
        if (might_be_mix(entry, nullptr)) {
            is_nested_mix = true;
        } else if (entry.size >= 6 && entry.size < 100 * 1024 * 1024) {
            // For unnamed files, read a small amount to check magic
            // Only for reasonable sizes (< 100MB)
            auto data_result = reader.read(entry);
            if (data_result) {
                entry_data = std::move(*data_result);
                is_nested_mix = is_mix_data(entry_data);
            }
        }
    }

    if (is_nested_mix) {
        // Try to open as nested MIX
        if (entry_data.empty()) {
            auto data_result = reader.read(entry);
            if (data_result) {
                entry_data = std::move(*data_result);
            }
        }

        if (!entry_data.empty()) {
            auto nested = wwd::MixReader::open(
                std::span<const uint8_t>(entry_data));
            if (nested) {
                const auto& info = nested.value()->info();
                std::cout << " [" << wwd::mix_format_name(info.format)
                          << ", " << info.file_count << " files";
                if (info.encrypted) std::cout << ", encrypted";
                std::cout << "]";
                std::cout << "\n";

                // Resolve names in nested reader
                if (!names.empty()) {
                    nested.value()->resolve_names(names);
                }

                // Build new prefix for children
                std::string suf = (is_last ? "    " : "│   ");
                std::string child_prefix = prefix + suf;
                list_recursive(*nested.value(), names, depth + 1,
                               child_prefix, false);
                return;
            }
        }
    }

    std::cout << "\n";
}

static void list_recursive(wwd::MixReader& reader,
                           const std::vector<std::string>& names,
                           int depth,
                           const std::string& prefix,
                           bool /* is_last */) {
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

    for (size_t i = 0; i < sorted.size(); ++i) {
        bool is_last_entry = (i == sorted.size() - 1);
        print_entry_tree(*sorted[i], reader, names, depth, prefix,
                         is_last_entry, true);
    }
}

static int cmd_list(int argc, char* argv[], bool verbose) {
    std::string file_path;
    std::string names_path;
    bool recursive = false;
    bool tree_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool list [options] <file.mix>\n"
                      << "\n"
                      << "Options:\n"
                      << "    -n, --names <file>  Load filename database\n"
                      << "    -r, --recursive     Recurse nested MIX files\n"
                      << "    -t, --tree          Tree view (implied by -r)\n"
                      << "\n"
                      << "Use '-' to read from stdin.\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            continue;  // Already handled in main
        }
        if (std::strcmp(arg, "-n") == 0 || std::strcmp(arg, "--names") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "mix-tool: --names requires an argument\n";
                return 2;
            }
            names_path = argv[++i];
            continue;
        }
        bool is_r = std::strcmp(arg, "-r") == 0;
        is_r = is_r || std::strcmp(arg, "--recursive") == 0;
        if (is_r) {
            recursive = true;
            tree_mode = true;  // Recursive implies tree mode
            continue;
        }
        if (std::strcmp(arg, "-t") == 0 || std::strcmp(arg, "--tree") == 0) {
            tree_mode = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
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

    if (verbose) {
        std::cerr << "Opening: " << file_path << "\n";
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::MixReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "mix-tool: error: " << data.error().message() << "\n";
            return 1;
        }
        stdin_data = std::move(*data);
        result = wwd::MixReader::open(std::span(stdin_data));
    } else {
        result = wwd::MixReader::open(file_path);
    }

    if (!result) {
        std::cerr << "mix-tool: " << result.error().message() << "\n";
        return 1;
    }

    auto& reader = *result.value();

    // Load names
    std::vector<std::string> names;
    if (!names_path.empty()) {
        names = load_names(names_path);
        if (verbose) {
            std::cerr << "Loaded " << names.size() << " names from "
                      << names_path << "\n";
        }
        if (!names.empty()) {
            reader.resolve_names(names);
        }
        if (verbose) {
            size_t resolved = 0;
            for (const auto& e : reader.entries()) {
                if (!e.name.empty()) ++resolved;
            }
            std::cerr << "Resolved " << resolved << " / "
                      << reader.entries().size() << " entries\n";
        }
    }

    const auto& info = reader.info();

    if (tree_mode || recursive) {
        // Tree view header
        std::cout << file_path << " ("
                  << wwd::mix_format_name(info.format) << ", "
                  << info.file_count << " files, "
                  << format_size(info.file_size) << " bytes";
        if (info.encrypted) std::cout << ", encrypted";
        std::cout << ")\n";

        // List entries as tree
        const auto& entries = reader.entries();
        std::vector<const wwd::MixEntry*> sorted;
        sorted.reserve(entries.size());
        for (const auto& e : entries) {
            sorted.push_back(&e);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const wwd::MixEntry* a, const wwd::MixEntry* b) {
                      return a->offset < b->offset;
                  });

        for (size_t i = 0; i < sorted.size(); ++i) {
            bool is_last = (i == sorted.size() - 1);
            print_entry_tree(*sorted[i], reader, names, 0, "",
                             is_last, recursive);
        }
    } else {
        // Table view
        const auto& entries = reader.entries();

        std::vector<const wwd::MixEntry*> sorted;
        sorted.reserve(entries.size());
        for (const auto& e : entries) {
            sorted.push_back(&e);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const wwd::MixEntry* a, const wwd::MixEntry* b) {
                      return a->offset < b->offset;
                  });

        bool has_names = false;
        for (const auto* e : sorted) {
            if (!e->name.empty()) {
                has_names = true;
                break;
            }
        }

        if (has_names) {
            std::cout << std::left << std::setw(14) << "Hash"
                      << std::right << std::setw(12) << "Offset"
                      << std::setw(12) << "Size"
                      << "  " << std::left << "Name" << "\n";
            std::cout << std::string(60, '-') << "\n";
        } else {
            std::cout << std::left << std::setw(14) << "Hash"
                      << std::right << std::setw(12) << "Offset"
                      << std::setw(12) << "Size" << "\n";
            std::cout << std::string(38, '-') << "\n";
        }

        for (const auto* e : sorted) {
            std::cout << std::left << std::setw(14) << format_hash(e->hash)
                      << std::right << std::setw(12) << e->offset
                      << std::setw(12) << e->size;
            if (has_names) {
                std::cout << "  " << std::left << e->name;
            }
            std::cout << "\n";
        }
    }

    return 0;
}

static int cmd_extract(int argc, char* argv[], bool verbose) {
    std::string file_path;
    std::string names_path;
    std::string output_dir = ".";
    std::vector<std::string> targets;  // files to extract (names or hashes)

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool extract [options] <file.mix>"
                      << " [files...]\n"
                      << "\n"
                      << "Extract files from a MIX archive.\n"
                      << "\n"
                      << "If no files are specified, all files are extracted.\n"
                      << "Files can be specified by name (if names loaded)"
                      << " or by hex hash (0x...).\n"
                      << "\n"
                      << "Use '-' to read from stdin.\n"
                      << "\n"
                      << "Note: Encrypted MIX files must be decrypted "
                      << "first with blowfish-tool.\n";
            return 0;
        }
        if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            continue;  // Already handled in main
        }
        if (std::strcmp(arg, "-n") == 0 || std::strcmp(arg, "--names") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "mix-tool: --names requires an argument\n";
                return 2;
            }
            names_path = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "mix-tool: --output requires an argument\n";
                return 2;
            }
            output_dir = argv[++i];
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            std::cerr << "mix-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (file_path.empty()) {
            file_path = arg;
        } else {
            targets.push_back(arg);
        }
    }

    if (file_path.empty()) {
        std::cerr << "mix-tool: missing file argument\n";
        return 2;
    }

    if (verbose) {
        std::cerr << "Opening: " << file_path << "\n";
        std::cerr << "Output dir: " << output_dir << "\n";
    }

    // Open from file or stdin
    std::vector<uint8_t> stdin_data;
    wwd::Result<std::unique_ptr<wwd::MixReader>> result;
    if (file_path == "-") {
        auto data = wwd::load_stdin();
        if (!data) {
            std::cerr << "mix-tool: error: " << data.error().message() << "\n";
            return 1;
        }
        stdin_data = std::move(*data);
        result = wwd::MixReader::open(std::span(stdin_data));
    } else {
        result = wwd::MixReader::open(file_path);
    }

    if (!result) {
        std::cerr << "mix-tool: " << result.error().message() << "\n";
        return 1;
    }

    auto& reader = *result.value();

    // Load and resolve names if provided
    if (!names_path.empty()) {
        auto names = load_names(names_path);
        if (verbose) {
            std::cerr << "Loaded " << names.size() << " names from "
                      << names_path << "\n";
        }
        if (!names.empty()) {
            reader.resolve_names(names);
        }
    }

    const auto& entries = reader.entries();

    // If no targets specified, extract all
    std::vector<const wwd::MixEntry*> to_extract;
    if (targets.empty()) {
        for (const auto& e : entries) {
            to_extract.push_back(&e);
        }
    } else {
        // Find each target
        for (const auto& target : targets) {
            const wwd::MixEntry* entry = nullptr;

            // Check if it's a hex hash
            if (target.size() > 2 && target[0] == '0' && target[1] == 'x') {
                uint32_t hash = std::stoul(target, nullptr, 16);
                entry = reader.find(hash);
            } else {
                // Try as filename
                entry = reader.find(target);
            }

            if (!entry) {
                std::cerr << "mix-tool: not found: " << target << "\n";
                continue;
            }
            to_extract.push_back(entry);
        }
    }

    if (to_extract.empty()) {
        std::cerr << "mix-tool: no files to extract\n";
        return 1;
    }

    // Extract each file
    int extracted = 0;
    for (const auto* entry : to_extract) {
        auto data = reader.read(*entry);
        if (!data) {
            std::cerr << "mix-tool: failed to read " << format_hash(entry->hash)
                      << ": " << data.error().message() << "\n";
            continue;
        }

        // Determine output filename
        std::string filename;
        if (!entry->name.empty()) {
            filename = entry->name;
        } else {
            filename = format_hash(entry->hash);
        }

        std::string out_path = output_dir + "/" + filename;

        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            std::cerr << "mix-tool: cannot create " << out_path << "\n";
            continue;
        }

        out.write(reinterpret_cast<const char*>(data->data()), data->size());
        if (!out) {
            std::cerr << "mix-tool: write error: " << out_path << "\n";
            continue;
        }

        std::cout << filename << " (" << data->size() << " bytes)\n";
        ++extracted;
    }

    std::cout << "Extracted " << extracted << " file(s)\n";
    return 0;
}

static int cmd_hash(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: mix-tool hash <filename> [--ts]\n";
        return 2;
    }

    bool use_ts = false;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            std::cerr << "Usage: mix-tool hash <filename> [--ts]\n"
                      << "\n"
                      << "Options:\n"
                      << "    --ts    Use Tiberian Sun hash (vs TD/RA)\n";
            return 0;
        }
        if (std::strcmp(arg, "--ts") == 0) {
            use_ts = true;
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "mix-tool: unknown option: " << arg << "\n";
            return 2;
        }
        if (filename.empty()) {
            filename = arg;
        }
    }

    if (filename.empty()) {
        std::cerr << "mix-tool: missing filename argument\n";
        return 2;
    }

    uint32_t hash;
    if (use_ts) {
        hash = wwd::mix_hash_ts(filename);
    } else {
        hash = wwd::mix_hash_td(filename);
    }

    std::cout << filename << " -> " << format_hash(hash) << "\n";

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
    if (std::strcmp(cmd, "-V") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    // Check for verbose flag
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        bool is_v = std::strcmp(argv[i], "-v") == 0;
        is_v = is_v || std::strcmp(argv[i], "--verbose") == 0;
        if (is_v) {
            verbose = true;
            break;
        }
    }

    if (std::strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1, verbose);
    }
    if (std::strcmp(cmd, "list") == 0) {
        return cmd_list(argc - 1, argv + 1, verbose);
    }
    if (std::strcmp(cmd, "extract") == 0) {
        return cmd_extract(argc - 1, argv + 1, verbose);
    }
    if (std::strcmp(cmd, "hash") == 0) {
        return cmd_hash(argc - 1, argv + 1);
    }

    std::cerr << "mix-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
