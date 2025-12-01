#pragma once

#include <mix/mix.h>
#include <string>

enum class OutputFormat {
    Table,
    Tree,
    Json
};

// Format archive listing as table
void format_table(const std::string& filename, const mix::MixReader& reader);

// Format archive listing as tree
void format_tree(const std::string& filename, const mix::MixReader& reader);

// Format archive listing as JSON
void format_json(const std::string& filename, const mix::MixReader& reader);
