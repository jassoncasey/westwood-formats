#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wwd {

enum class MixGame {
    Unknown,
    TiberianDawn,
    RedAlert,
    TiberianSun,
    RedAlert2,
    YurisRevenge,
    Renegade,
    Generals,
    ZeroHour
};

enum class MixFormat {
    Unknown,
    TD,      // Tiberian Dawn: simple header
    RA,      // Red Alert: encrypted
    TS,      // Tiberian Sun: unencrypted, aligned
    RG,      // Renegade: separate index/tailer
    BIG      // Generals: big-endian counts
};

struct MixEntry {
    uint32_t    hash;
    uint32_t    offset;
    uint32_t    size;
    std::string name;  // resolved name (empty if unknown)
};

struct MixInfo {
    MixFormat format;
    MixGame   game;
    bool      encrypted;
    bool      has_checksum;
    uint32_t  file_count;
    uint64_t  file_size;
};

class MixReader {
public:
    static Result<std::unique_ptr<MixReader>> open(const std::string& path);
    static Result<std::unique_ptr<MixReader>> open(
        std::span<const uint8_t> data);

    ~MixReader();

    const MixInfo& info() const;
    const std::vector<MixEntry>& entries() const;

    const MixEntry* find(uint32_t hash) const;
    const MixEntry* find(std::string_view name) const;

    Result<std::vector<uint8_t>> read(const MixEntry& entry) const;

    void resolve_names(const std::vector<std::string>& names);

private:
    MixReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Hash functions
WWD_API uint32_t mix_hash_td(std::string_view filename);
WWD_API uint32_t mix_hash_ts(std::string_view filename);
WWD_API uint32_t mix_hash(MixGame game, std::string_view filename);

// Name helpers
WWD_API const char* mix_format_name(MixFormat format);
WWD_API const char* mix_game_name(MixGame game);

// Game detection
WWD_API MixGame mix_detect_game(MixFormat format,
                                 const std::vector<MixEntry>& entries);

} // namespace wwd
