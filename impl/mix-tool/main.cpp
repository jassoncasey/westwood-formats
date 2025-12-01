#include <mix/mix.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Forward declarations
int cmd_list(int argc, char* argv[]);

static void print_usage() {
    std::cerr << "Usage: mix-tool <command> [options] <file>\n"
              << "\n"
              << "Commands:\n"
              << "    list        List contents of a MIX archive\n"
              << "\n"
              << "Global Options:\n"
              << "    -h, --help      Show help message\n"
              << "    -v, --version   Show version information\n";
}

static void print_version() {
    std::cout << "mix-tool 0.1.0\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const char* cmd = argv[1];

    // Global options
    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(cmd, "-v") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    // Commands
    if (std::strcmp(cmd, "list") == 0) {
        return cmd_list(argc - 1, argv + 1);
    }

    std::cerr << "mix-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
