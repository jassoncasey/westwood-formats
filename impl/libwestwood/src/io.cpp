#include <westwood/io.h>

#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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

Result<std::vector<uint8_t>> load_stdin() {
#ifdef _WIN32
    // Set stdin to binary mode on Windows
    _setmode(_fileno(stdin), _O_BINARY);
#endif

    std::vector<uint8_t> data;
    constexpr size_t chunk_size = 65536;

    // Read in chunks until EOF
    while (true) {
        size_t old_size = data.size();
        data.resize(old_size + chunk_size);

        std::cin.read(reinterpret_cast<char*>(data.data() + old_size), chunk_size);
        size_t bytes_read = static_cast<size_t>(std::cin.gcount());

        data.resize(old_size + bytes_read);

        if (bytes_read < chunk_size) {
            break;  // EOF or error
        }
    }

    if (std::cin.bad()) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Failed to read from stdin"));
    }

    if (data.empty()) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "No data received from stdin"));
    }

    return data;
}

} // namespace wwd
