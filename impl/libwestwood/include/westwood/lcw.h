#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <span>
#include <vector>

namespace wwd {

// LCW / Format80 decompression
//
// Two modes:
// - Absolute (default): long copy offsets from output start
// - Relative: long copy offsets backwards from current position
//
// Returns number of bytes written to output.

WWD_API Result<size_t> lcw_decompress(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    bool relative = false);

// Convenience: decompress to new vector (size must be known)
WWD_API Result<std::vector<uint8_t>> lcw_decompress(
    std::span<const uint8_t> input,
    size_t output_size,
    bool relative = false);

} // namespace wwd
