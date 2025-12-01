#include "format.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

// Format a number with thousand separators
static std::string format_size(uint64_t size) {
    std::string result = std::to_string(size);
    int pos = static_cast<int>(result.length()) - 3;
    while (pos > 0) {
        result.insert(pos, ",");
        pos -= 3;
    }
    return result;
}

// Format a hash as hex
static std::string format_hash(uint32_t hash) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << hash;
    return ss.str();
}

// Format an offset as hex
static std::string format_offset(uint32_t offset) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << offset;
    return ss.str();
}

// Get display name for entry
static std::string display_name(const mix::Entry& entry) {
    return entry.name.empty() ? "<unknown>" : entry.name;
}

void format_table(const std::string& filename, const mix::MixReader& reader) {
    const auto& info = reader.info();
    const auto& entries = reader.entries();

    // Header line
    std::cout << filename << " ("
              << mix::game_name(info.game);
    if (info.encrypted) {
        std::cout << ", encrypted";
    }
    std::cout << ", " << info.file_count << " files, "
              << format_size(info.file_size) << " bytes)\n\n";

    // Column headers
    std::cout << std::left << std::setw(20) << "Name"
              << std::setw(14) << "Hash"
              << std::setw(14) << "Offset"
              << std::right << std::setw(12) << "Size" << "\n";

    // Separator line
    std::cout << std::string(60, '-') << "\n";

    // Sort entries by offset for display
    std::vector<const mix::Entry*> sorted;
    sorted.reserve(entries.size());
    for (const auto& e : entries) {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const mix::Entry* a, const mix::Entry* b) {
                  return a->offset < b->offset;
              });

    // Entry rows
    for (const auto* entry : sorted) {
        std::cout << std::left << std::setw(20) << display_name(*entry)
                  << std::setw(14) << format_hash(entry->hash)
                  << std::setw(14) << format_offset(entry->offset)
                  << std::right << std::setw(12) << format_size(entry->size) << "\n";
    }
}

void format_tree(const std::string& filename, const mix::MixReader& reader) {
    const auto& info = reader.info();
    const auto& entries = reader.entries();

    // Header line
    std::cout << filename << " ("
              << mix::game_name(info.game);
    if (info.encrypted) {
        std::cout << ", encrypted";
    }
    std::cout << ", " << info.file_count << " files, "
              << format_size(info.file_size) << " bytes)\n";

    // Sort entries by offset
    std::vector<const mix::Entry*> sorted;
    sorted.reserve(entries.size());
    for (const auto& e : entries) {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const mix::Entry* a, const mix::Entry* b) {
                  return a->offset < b->offset;
              });

    // Tree entries
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto* entry = sorted[i];
        bool is_last = (i == sorted.size() - 1);

        std::cout << (is_last ? "\u2514\u2500\u2500 " : "\u251c\u2500\u2500 ")
                  << display_name(*entry)
                  << " (" << format_hash(entry->hash)
                  << ", offset=" << format_offset(entry->offset)
                  << ", " << format_size(entry->size) << " bytes)\n";
    }
}

void format_json(const std::string& filename, const mix::MixReader& reader) {
    const auto& info = reader.info();
    const auto& entries = reader.entries();

    std::cout << "{\n";
    std::cout << "  \"file\": \"" << filename << "\",\n";
    std::cout << "  \"format\": \"" << mix::format_name(info.format) << "\",\n";
    std::cout << "  \"encrypted\": " << (info.encrypted ? "true" : "false") << ",\n";
    std::cout << "  \"checksum\": " << (info.has_checksum ? "true" : "false") << ",\n";
    std::cout << "  \"file_count\": " << info.file_count << ",\n";
    std::cout << "  \"file_size\": " << info.file_size << ",\n";
    std::cout << "  \"entries\": [\n";

    // Sort entries by offset
    std::vector<const mix::Entry*> sorted;
    sorted.reserve(entries.size());
    for (const auto& e : entries) {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const mix::Entry* a, const mix::Entry* b) {
                  return a->offset < b->offset;
              });

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto* entry = sorted[i];
        std::cout << "    {\n";
        if (entry->name.empty()) {
            std::cout << "      \"name\": null,\n";
        } else {
            std::cout << "      \"name\": \"" << entry->name << "\",\n";
        }
        std::cout << "      \"hash\": \"" << format_hash(entry->hash) << "\",\n";
        std::cout << "      \"offset\": " << entry->offset << ",\n";
        std::cout << "      \"size\": " << entry->size << "\n";
        std::cout << "    }" << (i < sorted.size() - 1 ? "," : "") << "\n";
    }

    std::cout << "  ]\n";
    std::cout << "}\n";
}
