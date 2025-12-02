#include <westwood/io.h>

#include <fstream>

namespace wwd {

Result<std::vector<uint8_t>> load_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(
            make_error(ErrorCode::FileNotFound, "Cannot open: " + path));
    }

    auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Read failed: " + path));
    }

    return data;
}

} // namespace wwd
