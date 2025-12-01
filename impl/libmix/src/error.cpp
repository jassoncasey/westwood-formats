#include <mix/error.h>

namespace mix {

Error::Error(ErrorCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

Error make_error(ErrorCode code, std::string_view message) {
    return Error(code, std::string(message));
}

} // namespace mix
