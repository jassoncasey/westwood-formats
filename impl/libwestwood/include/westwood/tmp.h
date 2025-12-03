#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wwd {

enum class TmpFormat {
    TD,   // Tiberian Dawn - orthographic 24x24
    RA,   // Red Alert - orthographic 24x24
    TS,   // Tiberian Sun - isometric diamond 48x24
    RA2   // Red Alert 2 - isometric diamond 60x30
};

struct TmpTileInfo {
    uint32_t offset;
    uint32_t size;
    bool     valid;  // some tiles may be empty/invalid

    // TS/RA2 specific fields
    int32_t  x_offset;     // Cell X position in tile grid
    int32_t  y_offset;     // Cell Y position in tile grid
    int32_t  extra_x;      // Extra image X offset
    int32_t  extra_y;      // Extra image Y offset
    uint32_t extra_width;  // Extra image width (0 if none)
    uint32_t extra_height; // Extra image height (0 if none)
    uint32_t extra_offset; // Offset to extra image data
    uint32_t z_offset;     // Offset to Z-data (depth)
    uint8_t  height;       // Terrain elevation
    uint8_t  land_type;    // Terrain type (road, water, etc)
    uint8_t  slope_type;   // Slope direction
    bool     has_extra;    // Has perpendicular extra image
    bool     has_z_data;   // Has depth/Z data
    bool     has_damaged;  // Has damaged variant
};

struct TmpInfo {
    TmpFormat format;
    uint16_t  tile_width;      // 24 (TD/RA), 48 (TS), 60 (RA2)
    uint16_t  tile_height;     // 24 (TD/RA), 24 (TS), 30 (RA2)
    uint16_t  tile_count;
    uint16_t  empty_count;     // count of invalid/empty tiles
    uint32_t  index_start;
    uint32_t  index_end;
    uint32_t  image_start;
    uint32_t  file_size;

    // TS/RA2 specific
    uint32_t  template_width;  // Width in cells
    uint32_t  template_height; // Height in cells
};

class TmpReader {
public:
    static Result<std::unique_ptr<TmpReader>> open(const std::string& path);
    static Result<std::unique_ptr<TmpReader>> open(
        std::span<const uint8_t> data);

    ~TmpReader();

    const TmpInfo& info() const;
    const std::vector<TmpTileInfo>& tiles() const;

    // Decode a single tile to palette indices
    // For TD/RA: returns rectangular pixel data (width * height)
    // For TS/RA2: returns diamond-shaped pixel data (see tile_width/height)
    // Returns empty vector for invalid tiles
    std::vector<uint8_t> decode_tile(size_t tile_index) const;

    // Decode extra image for TS/RA2 tiles (perpendicular overlay)
    // Returns empty vector if tile has no extra image or for TD/RA format
    std::vector<uint8_t> decode_extra(size_t tile_index) const;

    // Decode Z-data (depth buffer) for TS/RA2 tiles
    // Returns empty vector if tile has no Z-data or for TD/RA format
    std::vector<uint8_t> decode_z_data(size_t tile_index) const;

    // Decode all tiles (valid tiles only - empty tiles are skipped)
    std::vector<std::vector<uint8_t>> decode_all_tiles() const;

    // Count of valid (non-empty) tiles
    uint32_t valid_tile_count() const;

    // Check if this is an isometric format (TS/RA2)
    bool is_isometric() const;

private:
    TmpReader();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wwd
