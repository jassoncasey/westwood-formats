#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

enum class TmpFormat { TD, RA };  // Both are orthographic 24x24

struct TmpTileInfo {
    uint32_t offset;
    uint32_t size;
    bool     valid;  // some tiles may be empty/invalid
};

struct TmpInfo {
    TmpFormat format;
    uint16_t  tile_width;   // 24 for TD/RA
    uint16_t  tile_height;  // 24 for TD/RA
    uint16_t  tile_count;
    uint16_t  empty_count;  // count of invalid/empty tiles
    uint32_t  index_start;
    uint32_t  index_end;
    uint32_t  image_start;
    uint32_t  file_size;
};

class TmpReader {
public:
    static Result<std::unique_ptr<TmpReader>> open(const std::string& path);
    static Result<std::unique_ptr<TmpReader>> open(std::span<const uint8_t> data);

    ~TmpReader();

    const TmpInfo& info() const;
    const std::vector<TmpTileInfo>& tiles() const;

    // Decode a single tile to palette indices
    // Returns empty vector for invalid tiles
    std::vector<uint8_t> decode_tile(size_t tile_index) const;

    // Decode all tiles (valid tiles only - empty tiles are skipped)
    std::vector<std::vector<uint8_t>> decode_all_tiles() const;

    // Count of valid (non-empty) tiles
    uint32_t valid_tile_count() const;

private:
    TmpReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
