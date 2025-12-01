/*
 * libmix C API implementation
 */

#include <mix/mix_c.h>
#include <mix/mix.h>

#include <cstdlib>
#include <cstring>
#include <new>

// Version info
#define MIX_VERSION_MAJOR 0
#define MIX_VERSION_MINOR 1
#define MIX_VERSION_PATCH 0
#define MIX_VERSION_STRING "0.1.0"

static mix_error_t to_c_error(mix::ErrorCode code) {
    using E = mix::ErrorCode;
    switch (code) {
        case E::None:              return MIX_OK;
        case E::FileNotFound:      return MIX_ERROR_FILE_NOT_FOUND;
        case E::ReadError:         return MIX_ERROR_READ_ERROR;
        case E::InvalidFormat:     return MIX_ERROR_INVALID_FORMAT;
        case E::UnsupportedFormat: return MIX_ERROR_UNSUPPORTED_FORMAT;
        case E::CorruptHeader:     return MIX_ERROR_CORRUPT_HEADER;
        case E::CorruptIndex:      return MIX_ERROR_CORRUPT_INDEX;
        case E::DecryptionFailed:  return MIX_ERROR_DECRYPTION_FAILED;
        case E::InvalidKey:        return MIX_ERROR_INVALID_KEY;
        default:                   return MIX_ERROR_INVALID_FORMAT;
    }
}

// Convert C++ format to C format
static mix_format_t to_c_format(mix::FormatType format) {
    switch (format) {
        case mix::FormatType::TD:    return MIX_FORMAT_TD;
        case mix::FormatType::RA:    return MIX_FORMAT_RA;
        case mix::FormatType::TS:    return MIX_FORMAT_TS;
        case mix::FormatType::MixRG: return MIX_FORMAT_RG;
        case mix::FormatType::BIG:   return MIX_FORMAT_BIG;
        default:                     return MIX_FORMAT_UNKNOWN;
    }
}

// Convert C++ game to C game
static mix_game_t to_c_game(mix::GameType game) {
    switch (game) {
        case mix::GameType::TiberianDawn:  return MIX_GAME_TIBERIAN_DAWN;
        case mix::GameType::RedAlert:      return MIX_GAME_RED_ALERT;
        case mix::GameType::TiberianSun:   return MIX_GAME_TIBERIAN_SUN;
        case mix::GameType::RedAlert2:     return MIX_GAME_RED_ALERT_2;
        case mix::GameType::YurisRevenge:  return MIX_GAME_YURIS_REVENGE;
        case mix::GameType::Renegade:      return MIX_GAME_RENEGADE;
        case mix::GameType::Generals:      return MIX_GAME_GENERALS;
        case mix::GameType::ZeroHour:      return MIX_GAME_ZERO_HOUR;
        default:                           return MIX_GAME_UNKNOWN;
    }
}

// Convert C game to C++ game
static mix::GameType from_c_game(mix_game_t game) {
    switch (game) {
        case MIX_GAME_TIBERIAN_DAWN:  return mix::GameType::TiberianDawn;
        case MIX_GAME_RED_ALERT:      return mix::GameType::RedAlert;
        case MIX_GAME_TIBERIAN_SUN:   return mix::GameType::TiberianSun;
        case MIX_GAME_RED_ALERT_2:    return mix::GameType::RedAlert2;
        case MIX_GAME_YURIS_REVENGE:  return mix::GameType::YurisRevenge;
        case MIX_GAME_RENEGADE:       return mix::GameType::Renegade;
        case MIX_GAME_GENERALS:       return mix::GameType::Generals;
        case MIX_GAME_ZERO_HOUR:      return mix::GameType::ZeroHour;
        default:                      return mix::GameType::Unknown;
    }
}

// Wrapper struct holding the C++ reader
struct mix_reader {
    std::unique_ptr<mix::MixReader> cpp_reader;
};

// --------------------------------------------------------------------
// Reader lifecycle
// --------------------------------------------------------------------

MIX_API mix_error_t mix_reader_open(const char* path, mix_reader_t** out) {
    if (!path || !out) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    auto result = mix::MixReader::open(path);
    if (!result) {
        return to_c_error(result.error().code());
    }

    try {
        auto* reader = new mix_reader();
        reader->cpp_reader = std::move(result.value());
        *out = reader;
        return MIX_OK;
    } catch (const std::bad_alloc&) {
        return MIX_ERROR_OUT_OF_MEMORY;
    }
}

MIX_API mix_error_t mix_reader_open_memory(const uint8_t* data, size_t size,
                                           mix_reader_t** out) {
    if (!data || !out) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    auto result = mix::MixReader::open(std::span<const uint8_t>(data, size));
    if (!result) {
        return to_c_error(result.error().code());
    }

    try {
        auto* reader = new mix_reader();
        reader->cpp_reader = std::move(result.value());
        *out = reader;
        return MIX_OK;
    } catch (const std::bad_alloc&) {
        return MIX_ERROR_OUT_OF_MEMORY;
    }
}

MIX_API void mix_reader_free(mix_reader_t* reader) {
    delete reader;
}

// --------------------------------------------------------------------
// Archive information
// --------------------------------------------------------------------

MIX_API mix_error_t mix_reader_info(const mix_reader_t* reader,
                                    mix_info_t* info) {
    if (!reader || !info) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    const auto& cpp_info = reader->cpp_reader->info();
    info->format = to_c_format(cpp_info.format);
    info->game = to_c_game(cpp_info.game);
    info->encrypted = cpp_info.encrypted ? 1 : 0;
    info->has_checksum = cpp_info.has_checksum ? 1 : 0;
    info->file_count = cpp_info.file_count;
    info->file_size = cpp_info.file_size;

    return MIX_OK;
}

MIX_API uint32_t mix_reader_count(const mix_reader_t* reader) {
    if (!reader) {
        return 0;
    }
    return static_cast<uint32_t>(reader->cpp_reader->entries().size());
}

// --------------------------------------------------------------------
// Entry access
// --------------------------------------------------------------------

MIX_API mix_error_t mix_reader_entry(const mix_reader_t* reader, uint32_t index,
                                     mix_entry_t* entry) {
    if (!reader || !entry) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    const auto& entries = reader->cpp_reader->entries();
    if (index >= entries.size()) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    const auto& cpp_entry = entries[index];
    entry->hash = cpp_entry.hash;
    entry->offset = cpp_entry.offset;
    entry->size = cpp_entry.size;
    entry->name = cpp_entry.name.empty() ? nullptr : cpp_entry.name.c_str();

    return MIX_OK;
}

MIX_API mix_error_t mix_reader_find_hash(const mix_reader_t* reader,
                                         uint32_t hash,
                                         mix_entry_t* entry) {
    if (!reader || !entry) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    const auto* cpp_entry = reader->cpp_reader->find(hash);
    if (!cpp_entry) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    entry->hash = cpp_entry->hash;
    entry->offset = cpp_entry->offset;
    entry->size = cpp_entry->size;
    entry->name = cpp_entry->name.empty() ? nullptr : cpp_entry->name.c_str();

    return MIX_OK;
}

MIX_API mix_error_t mix_reader_find_name(const mix_reader_t* reader,
                                         const char* name,
                                         mix_entry_t* entry) {
    if (!reader || !name || !entry) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    const auto* cpp_entry = reader->cpp_reader->find(std::string_view(name));
    if (!cpp_entry) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    entry->hash = cpp_entry->hash;
    entry->offset = cpp_entry->offset;
    entry->size = cpp_entry->size;
    entry->name = cpp_entry->name.empty() ? nullptr : cpp_entry->name.c_str();

    return MIX_OK;
}

// --------------------------------------------------------------------
// File data access
// --------------------------------------------------------------------

MIX_API mix_error_t mix_reader_read(const mix_reader_t* reader,
                                    const mix_entry_t* entry,
                                    uint8_t** data, size_t* size) {
    if (!reader || !entry || !data || !size) {
        return MIX_ERROR_INVALID_ARGUMENT;
    }

    // Reconstruct C++ entry from C entry
    mix::Entry cpp_entry;
    cpp_entry.hash = entry->hash;
    cpp_entry.offset = entry->offset;
    cpp_entry.size = entry->size;

    auto result = reader->cpp_reader->read(cpp_entry);
    if (!result) {
        return to_c_error(result.error().code());
    }

    const auto& vec = result.value();
    *size = vec.size();

    // Allocate and copy data
    *data = static_cast<uint8_t*>(std::malloc(vec.size()));
    if (!*data) {
        return MIX_ERROR_OUT_OF_MEMORY;
    }

    std::memcpy(*data, vec.data(), vec.size());
    return MIX_OK;
}

MIX_API void mix_free(void* data) {
    std::free(data);
}

// --------------------------------------------------------------------
// Filename resolution
// --------------------------------------------------------------------

MIX_API void mix_reader_resolve_names(mix_reader_t* reader,
                                      const char* const* names, size_t count) {
    if (!reader || !names) {
        return;
    }

    std::vector<std::string> cpp_names;
    cpp_names.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (names[i]) {
            cpp_names.emplace_back(names[i]);
        }
    }

    reader->cpp_reader->resolve_names(cpp_names);
}

// --------------------------------------------------------------------
// Hash computation utilities
// --------------------------------------------------------------------

MIX_API uint32_t mix_hash_td(const char* filename) {
    if (!filename) {
        return 0;
    }
    return mix::compute_hash_td(filename);
}

MIX_API uint32_t mix_hash_ts(const char* filename) {
    if (!filename) {
        return 0;
    }
    return mix::compute_hash_ts(filename);
}

MIX_API uint32_t mix_hash(mix_game_t game, const char* filename) {
    if (!filename) {
        return 0;
    }
    return mix::compute_hash(from_c_game(game), filename);
}

// --------------------------------------------------------------------
// Error handling
// --------------------------------------------------------------------

MIX_API const char* mix_error_string(mix_error_t error) {
    switch (error) {
        case MIX_OK:                       return "Success";
        case MIX_ERROR_FILE_NOT_FOUND:     return "File not found";
        case MIX_ERROR_READ_ERROR:         return "Read error";
        case MIX_ERROR_INVALID_FORMAT:     return "Invalid format";
        case MIX_ERROR_UNSUPPORTED_FORMAT: return "Unsupported format";
        case MIX_ERROR_CORRUPT_HEADER:     return "Corrupt header";
        case MIX_ERROR_CORRUPT_INDEX:      return "Corrupt index";
        case MIX_ERROR_DECRYPTION_FAILED:  return "Decryption failed";
        case MIX_ERROR_INVALID_KEY:        return "Invalid key";
        case MIX_ERROR_INVALID_ARGUMENT:   return "Invalid argument";
        case MIX_ERROR_OUT_OF_MEMORY:      return "Out of memory";
        default:                           return "Unknown error";
    }
}

// --------------------------------------------------------------------
// Version information
// --------------------------------------------------------------------

MIX_API const char* mix_version(void) {
    return MIX_VERSION_STRING;
}

MIX_API void mix_version_components(int* major, int* minor, int* patch) {
    if (major) *major = MIX_VERSION_MAJOR;
    if (minor) *minor = MIX_VERSION_MINOR;
    if (patch) *patch = MIX_VERSION_PATCH;
}
