#include "format.h"

#include <mix/mix.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void print_list_usage() {
    std::cerr << "Usage: mix-tool list [options] <file>\n"
              << "\n"
              << "Options:\n"
              << "    -t, --tree              Display as tree view (default: table)\n"
              << "    -j, --json              Output as JSON\n"
              << "    -r, --recursive         Recurse into nested MIX files\n"
              << "    -F, --names-file=FILE   Load filename mappings from FILE\n"
              << "                            Can be specified multiple times\n"
              << "    -n, --name=NAME         Add a known filename mapping\n"
              << "                            Can be specified multiple times\n"
              << "    -h, --help              Show this help message\n";
}

// Load filenames from a file (one per line, # for comments)
static std::vector<std::string> load_names_file(const std::string& path) {
    std::vector<std::string> names;
    std::ifstream file(path);
    if (!file) {
        std::cerr << "mix-tool: warning: cannot open names file: " << path << "\n";
        return names;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        names.push_back(line);
    }

    return names;
}

int cmd_list(int argc, char* argv[]) {
    OutputFormat format = OutputFormat::Table;
    bool recursive = false;
    std::vector<std::string> names_files;
    std::vector<std::string> names;
    std::string file_path;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            print_list_usage();
            return 0;
        }
        if (std::strcmp(arg, "-t") == 0 || std::strcmp(arg, "--tree") == 0) {
            format = OutputFormat::Tree;
            continue;
        }
        if (std::strcmp(arg, "-j") == 0 || std::strcmp(arg, "--json") == 0) {
            format = OutputFormat::Json;
            continue;
        }
        if (std::strcmp(arg, "-r") == 0 || std::strcmp(arg, "--recursive") == 0) {
            recursive = true;
            continue;
        }

        // -F FILE or --names-file=FILE
        if (std::strcmp(arg, "-F") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "mix-tool: -F requires an argument\n";
                return 2;
            }
            names_files.push_back(argv[++i]);
            continue;
        }
        if (std::strncmp(arg, "--names-file=", 13) == 0) {
            names_files.push_back(arg + 13);
            continue;
        }

        // -n NAME or --name=NAME
        if (std::strcmp(arg, "-n") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "mix-tool: -n requires an argument\n";
                return 2;
            }
            names.push_back(argv[++i]);
            continue;
        }
        if (std::strncmp(arg, "--name=", 7) == 0) {
            names.push_back(arg + 7);
            continue;
        }

        // Unknown option
        if (arg[0] == '-') {
            std::cerr << "mix-tool: unknown option: " << arg << "\n";
            print_list_usage();
            return 2;
        }

        // File path (must be last positional argument)
        if (file_path.empty()) {
            file_path = arg;
        } else {
            std::cerr << "mix-tool: unexpected argument: " << arg << "\n";
            print_list_usage();
            return 2;
        }
    }

    if (file_path.empty()) {
        std::cerr << "mix-tool: missing file argument\n";
        print_list_usage();
        return 2;
    }

    // Load names from files
    std::vector<std::string> all_names = names;
    for (const auto& nf : names_files) {
        auto loaded = load_names_file(nf);
        all_names.insert(all_names.end(), loaded.begin(), loaded.end());
    }

    // Open the MIX file
    auto result = mix::MixReader::open(file_path);
    if (!result) {
        std::cerr << "mix-tool: " << result.error().message() << "\n";
        return 1;
    }

    auto& reader = *result.value();

    // Resolve names
    if (!all_names.empty()) {
        reader.resolve_names(all_names);
    }

    // Extract filename from path
    std::string filename = file_path;
    size_t sep = filename.find_last_of("/\\");
    if (sep != std::string::npos) {
        filename = filename.substr(sep + 1);
    }

    // Output
    switch (format) {
        case OutputFormat::Table:
            format_table(filename, reader);
            break;
        case OutputFormat::Tree:
            format_tree(filename, reader);
            break;
        case OutputFormat::Json:
            format_json(filename, reader);
            break;
    }

    // TODO: recursive mode for nested MIX files
    if (recursive) {
        // Not yet implemented - would need to detect .MIX entries and recurse
    }

    return 0;
}
