#include <westwood/error.h>

namespace wwd {

Error::Error(ErrorCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

Error make_error(ErrorCode code, std::string_view message) {
    return Error(code, std::string(message));
}

const char* error_code_name(ErrorCode code) {
    switch (code) {
        case ErrorCode::None:              return "None";
        case ErrorCode::FileNotFound:      return "FileNotFound";
        case ErrorCode::ReadError:         return "ReadError";
        case ErrorCode::UnexpectedEof:     return "UnexpectedEof";
        case ErrorCode::InvalidFormat:     return "InvalidFormat";
        case ErrorCode::UnsupportedFormat: return "UnsupportedFormat";
        case ErrorCode::CorruptHeader:     return "CorruptHeader";
        case ErrorCode::CorruptIndex:      return "CorruptIndex";
        case ErrorCode::CorruptData:       return "CorruptData";
        case ErrorCode::DecryptionFailed:  return "DecryptionFailed";
        case ErrorCode::InvalidKey:        return "InvalidKey";
        case ErrorCode::DecompressError:   return "DecompressError";
        case ErrorCode::OutputOverflow:    return "OutputOverflow";
    }
    return "Unknown";
}

} // namespace wwd
