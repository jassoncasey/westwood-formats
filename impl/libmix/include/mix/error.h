#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mix {

enum class ErrorCode : uint32_t {
    None = 0,

    // I/O errors
    FileNotFound,
    ReadError,

    // Format errors
    InvalidFormat,
    UnsupportedFormat,
    CorruptHeader,
    CorruptIndex,

    // Crypto errors (for future use)
    DecryptionFailed,
    InvalidKey,
};

class Error {
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

// Helper to create errors with formatted messages
Error make_error(ErrorCode code, std::string_view message);

} // namespace mix
