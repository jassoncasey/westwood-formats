#pragma once

#include <westwood/export.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace wwd {

enum class ErrorCode : uint32_t {
    None = 0,

    // I/O errors
    FileNotFound,
    ReadError,
    UnexpectedEof,

    // Format errors
    InvalidFormat,
    UnsupportedFormat,
    CorruptHeader,
    CorruptIndex,
    CorruptData,

    // Crypto errors
    DecryptionFailed,
    InvalidKey,

    // Decompression errors
    DecompressError,
    OutputOverflow,
};

class WWD_API Error {
public:
    Error() : code_(ErrorCode::None) {}
    Error(ErrorCode code, std::string message = {});

    ErrorCode code() const { return code_; }
    std::string_view message() const { return message_; }

    explicit operator bool() const { return code_ != ErrorCode::None; }

private:
    ErrorCode code_;
    std::string message_;
};

WWD_API Error make_error(ErrorCode code, std::string_view message);
WWD_API const char* error_code_name(ErrorCode code);

template<typename T>
using Result = std::expected<T, Error>;

} // namespace wwd
