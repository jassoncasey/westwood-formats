#pragma once

#include <mix/types.h>
#include <mix/error.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mix {

// Forward declarations
class MixReader;

// Game type enumeration
enum class GameType {
    Unknown,
    TiberianDawn,   // TD format
    RedAlert,       // RA format (encrypted)
    TiberianSun,    // TS format
    RedAlert2,      // TS format
    YurisRevenge,   // TS format
    Renegade,       // MIX-RG format
    Generals,       // BIG format
    ZeroHour        // BIG format
};

// Format type (structural, independent of game)
enum class FormatType {
    Unknown,
    TD,         // Tiberian Dawn: simple header
    RA,         // Red Alert: encrypted
    TS,         // Tiberian Sun: unencrypted, aligned
    MixRG,      // Renegade: separate index/tailer
    BIG         // Generals: big-endian counts
};

// Single index entry
struct Entry {
    uint32_t    hash;       // Filename hash (ID)
    uint32_t    offset;     // Absolute byte offset in file
    uint32_t    size;       // Size in bytes
    std::string name;       // Resolved name (empty if unknown)
};

// Archive metadata
struct ArchiveInfo {
    FormatType  format;
    GameType    game;
    bool        encrypted;
    bool        has_checksum;
    uint32_t    file_count;
    uint64_t    file_size;
};

// Result type for operations that can fail
template<typename T>
using Result = std::expected<T, Error>;

// Main reader class
class MixReader {
public:
    // Open a MIX file from path
    static Result<std::unique_ptr<MixReader>> open(const std::string& path);

    // Open from memory buffer
    static Result<std::unique_ptr<MixReader>> open(std::span<const uint8_t> data);

    ~MixReader();

    // Archive information
    const ArchiveInfo& info() const;

    // Get all entries
    const std::vector<Entry>& entries() const;

    // Get entry by hash
    const Entry* find(uint32_t hash) const;

    // Get entry by name (requires name to be resolved)
    const Entry* find(std::string_view name) const;

    // Read file data by entry
    Result<std::vector<uint8_t>> read(const Entry& entry) const;

    // Resolve filenames from a list
    // Computes hash for each name and matches against index
    void resolve_names(const std::vector<std::string>& names);

private:
    MixReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Utility: compute filename hash for a given game type
uint32_t compute_hash(GameType game, std::string_view filename);

// Utility: compute hash using TD/RA algorithm (rotate-add)
uint32_t compute_hash_td(std::string_view filename);

// Utility: compute hash using TS algorithm (CRC32)
uint32_t compute_hash_ts(std::string_view filename);

// Utility: detect game type from format and heuristics
GameType detect_game(FormatType format, const std::vector<Entry>& entries);

// Utility: format name for display
const char* format_name(FormatType format);
const char* game_name(GameType game);

} // namespace mix
