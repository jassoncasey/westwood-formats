#pragma once

#include <cstddef>
#include <cstdint>

namespace mix {

// Constants from format spec
constexpr uint32_t FLAG_CHECKSUM  = 0x00010000;
constexpr uint32_t FLAG_ENCRYPTED = 0x00020000;

constexpr size_t MIX_KEY_SIZE        = 56;   // Blowfish key
constexpr size_t MIX_KEY_SOURCE_SIZE = 80;   // RSA-encrypted key
constexpr size_t MIX_CHECKSUM_SIZE   = 20;   // SHA-1

constexpr uint32_t MIX_RG_MAGIC = 0x3158494D;  // "MIX1"
constexpr uint32_t BIG_MAGIC    = 0x46474942;  // "BIGF"
constexpr uint32_t BIG4_MAGIC   = 0x34474942;  // "BIG4"

constexpr uint32_t TS_MARKER_ID = 0x763C81DD;  // Indicates TS game

constexpr uint32_t MAX_FILE_COUNT = 4095;

// Index entry size in bytes
constexpr size_t INDEX_ENTRY_SIZE = 12;  // hash(4) + offset(4) + size(4)

} // namespace mix
