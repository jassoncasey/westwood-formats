/*
 * libmix C API
 *
 * Pure C interface for FFI compatibility with C, Rust, Python, etc.
 * All functions use C linkage and only C-compatible types.
 *
 * Memory ownership rules:
 * - Functions returning mix_reader_t* transfer ownership to caller
 * - Caller must call mix_reader_free() to release
 * - Strings returned by mix_entry_name() are owned by the reader
 * - Data returned by mix_reader_read() must be freed with mix_free()
 *
 * Thread safety:
 * - A single mix_reader_t is NOT thread-safe
 * - Different readers can be used from different threads
 */

#ifndef MIX_C_H
#define MIX_C_H

#include <mix/export.h>
#include <stddef.h>
#include <stdint.h>

MIX_EXTERN_C_BEGIN

/* Opaque handle types */
typedef struct mix_reader mix_reader_t;

/* Error codes */
typedef enum mix_error {
    MIX_OK = 0,
    MIX_ERROR_FILE_NOT_FOUND,
    MIX_ERROR_READ_ERROR,
    MIX_ERROR_INVALID_FORMAT,
    MIX_ERROR_UNSUPPORTED_FORMAT,
    MIX_ERROR_CORRUPT_HEADER,
    MIX_ERROR_CORRUPT_INDEX,
    MIX_ERROR_DECRYPTION_FAILED,
    MIX_ERROR_INVALID_KEY,
    MIX_ERROR_INVALID_ARGUMENT,
    MIX_ERROR_OUT_OF_MEMORY
} mix_error_t;

/* Format types */
typedef enum mix_format {
    MIX_FORMAT_UNKNOWN = 0,
    MIX_FORMAT_TD,      /* Tiberian Dawn */
    MIX_FORMAT_RA,      /* Red Alert (encrypted) */
    MIX_FORMAT_TS,      /* Tiberian Sun */
    MIX_FORMAT_RG,      /* Renegade */
    MIX_FORMAT_BIG      /* Generals */
} mix_format_t;

/* Game types */
typedef enum mix_game {
    MIX_GAME_UNKNOWN = 0,
    MIX_GAME_TIBERIAN_DAWN,
    MIX_GAME_RED_ALERT,
    MIX_GAME_TIBERIAN_SUN,
    MIX_GAME_RED_ALERT_2,
    MIX_GAME_YURIS_REVENGE,
    MIX_GAME_RENEGADE,
    MIX_GAME_GENERALS,
    MIX_GAME_ZERO_HOUR
} mix_game_t;

/* Archive information (read-only, valid while reader is open) */
typedef struct mix_info {
    mix_format_t format;
    mix_game_t   game;
    int          encrypted;     /* 0 or 1 */
    int          has_checksum;  /* 0 or 1 */
    uint32_t     file_count;
    uint64_t     file_size;
} mix_info_t;

/* Entry information (read-only, valid while reader is open) */
typedef struct mix_entry {
    uint32_t    hash;
    uint32_t    offset;
    uint32_t    size;
    const char* name;  /* NULL if unknown, owned by reader */
} mix_entry_t;

/* --------------------------------------------------------------------
 * Reader lifecycle
 * -------------------------------------------------------------------- */

/**
 * Open a MIX file from path.
 *
 * @param path      Path to MIX file (UTF-8 encoded)
 * @param out       Receives pointer to reader on success
 * @return          MIX_OK on success, error code on failure
 */
MIX_API mix_error_t mix_reader_open(const char* path, mix_reader_t** out);

/**
 * Open a MIX file from memory buffer.
 *
 * @param data      Pointer to MIX file data
 * @param size      Size of data in bytes
 * @param out       Receives pointer to reader on success
 * @return          MIX_OK on success, error code on failure
 *
 * Note: The data is copied internally; caller may free after this call.
 */
MIX_API mix_error_t mix_reader_open_memory(const uint8_t* data, size_t size,
                                           mix_reader_t** out);

/**
 * Close a reader and free all resources.
 *
 * @param reader    Reader to close (may be NULL)
 */
MIX_API void mix_reader_free(mix_reader_t* reader);

/* --------------------------------------------------------------------
 * Archive information
 * -------------------------------------------------------------------- */

/**
 * Get archive metadata.
 *
 * @param reader    Open reader
 * @param info      Receives archive info
 * @return          MIX_OK on success
 */
MIX_API mix_error_t mix_reader_info(const mix_reader_t* reader, mix_info_t* info);

/**
 * Get number of entries in archive.
 *
 * @param reader    Open reader
 * @return          Number of entries
 */
MIX_API uint32_t mix_reader_count(const mix_reader_t* reader);

/* --------------------------------------------------------------------
 * Entry access
 * -------------------------------------------------------------------- */

/**
 * Get entry by index.
 *
 * @param reader    Open reader
 * @param index     Entry index (0 to count-1)
 * @param entry     Receives entry info
 * @return          MIX_OK on success, MIX_ERROR_INVALID_ARGUMENT if out of range
 */
MIX_API mix_error_t mix_reader_entry(const mix_reader_t* reader, uint32_t index,
                                     mix_entry_t* entry);

/**
 * Find entry by hash.
 *
 * @param reader    Open reader
 * @param hash      Hash to find
 * @param entry     Receives entry info if found
 * @return          MIX_OK if found, MIX_ERROR_INVALID_ARGUMENT if not found
 */
MIX_API mix_error_t mix_reader_find_hash(const mix_reader_t* reader, uint32_t hash,
                                         mix_entry_t* entry);

/**
 * Find entry by name (requires names to be resolved first).
 *
 * @param reader    Open reader
 * @param name      Filename to find
 * @param entry     Receives entry info if found
 * @return          MIX_OK if found, MIX_ERROR_INVALID_ARGUMENT if not found
 */
MIX_API mix_error_t mix_reader_find_name(const mix_reader_t* reader, const char* name,
                                         mix_entry_t* entry);

/* --------------------------------------------------------------------
 * File data access
 * -------------------------------------------------------------------- */

/**
 * Read entry data.
 *
 * @param reader    Open reader
 * @param entry     Entry to read (from mix_reader_entry or mix_reader_find_*)
 * @param data      Receives pointer to data (caller must free with mix_free)
 * @param size      Receives size of data
 * @return          MIX_OK on success
 */
MIX_API mix_error_t mix_reader_read(const mix_reader_t* reader,
                                    const mix_entry_t* entry,
                                    uint8_t** data, size_t* size);

/**
 * Free data allocated by mix_reader_read.
 *
 * @param data      Data pointer to free (may be NULL)
 */
MIX_API void mix_free(void* data);

/* --------------------------------------------------------------------
 * Filename resolution
 * -------------------------------------------------------------------- */

/**
 * Resolve filenames from an array of strings.
 *
 * @param reader    Open reader
 * @param names     Array of filename strings
 * @param count     Number of names in array
 */
MIX_API void mix_reader_resolve_names(mix_reader_t* reader,
                                      const char* const* names, size_t count);

/* --------------------------------------------------------------------
 * Hash computation utilities
 * -------------------------------------------------------------------- */

/**
 * Compute filename hash for TD/RA games (rotate-add algorithm).
 *
 * @param filename  Filename to hash
 * @return          32-bit hash value
 */
MIX_API uint32_t mix_hash_td(const char* filename);

/**
 * Compute filename hash for TS/RA2 games (CRC32 algorithm).
 *
 * @param filename  Filename to hash
 * @return          32-bit hash value
 */
MIX_API uint32_t mix_hash_ts(const char* filename);

/**
 * Compute filename hash using appropriate algorithm for game type.
 *
 * @param game      Game type
 * @param filename  Filename to hash
 * @return          32-bit hash value
 */
MIX_API uint32_t mix_hash(mix_game_t game, const char* filename);

/* --------------------------------------------------------------------
 * Error handling
 * -------------------------------------------------------------------- */

/**
 * Get human-readable error message.
 *
 * @param error     Error code
 * @return          Static string describing error (never NULL)
 */
MIX_API const char* mix_error_string(mix_error_t error);

/* --------------------------------------------------------------------
 * Version information
 * -------------------------------------------------------------------- */

/**
 * Get library version string.
 *
 * @return          Version string (e.g., "0.1.0")
 */
MIX_API const char* mix_version(void);

/**
 * Get library version components.
 *
 * @param major     Receives major version (may be NULL)
 * @param minor     Receives minor version (may be NULL)
 * @param patch     Receives patch version (may be NULL)
 */
MIX_API void mix_version_components(int* major, int* minor, int* patch);

MIX_EXTERN_C_END

#endif /* MIX_C_H */
