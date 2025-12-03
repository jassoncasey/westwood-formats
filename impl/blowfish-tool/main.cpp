#include <westwood/blowfish.h>
#include <westwood/io.h>

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static void print_usage() {
    std::cerr << "Usage: blowfish-tool <command> [options]\n"
              << "\n"
              << "Commands:\n"
              << "    decrypt <input> <output>   Decrypt file with Westwood MIX key\n"
              << "    encrypt <input> <output>   Encrypt file with Westwood MIX key\n"
              << "    info <file.mix>            Show encryption info for MIX file\n"
              << "    derive <keyfile>           Derive Blowfish key from 80-byte key source\n"
              << "\n"
              << "Options:\n"
              << "    -h, --help     Show help message\n"
              << "    -V, --version  Show version\n"
              << "    -v, --verbose  Verbose output\n"
              << "\n"
              << "The decrypt/encrypt commands use the Westwood public key to derive\n"
              << "the Blowfish key from the first 80 bytes of input.\n";
}

static void print_version() {
    std::cout << "blowfish-tool 0.1.0\n";
}

static std::string format_hex(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0 && i % 16 == 0) ss << "\n";
        else if (i > 0) ss << " ";
        ss << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<int>(data[i]);
    }
    return ss.str();
}

static int cmd_info(const std::string& path) {
    auto data = wwd::load_file(path);
    if (!data) {
        std::cerr << "blowfish-tool: " << data.error().message() << "\n";
        return 1;
    }

    if (data->size() < 84) {
        std::cerr << "blowfish-tool: file too small for MIX format\n";
        return 1;
    }

    uint32_t flags = wwd::read_u32(data->data());
    bool encrypted = (flags & 0x00020000) != 0;
    bool checksum = (flags & 0x00010000) != 0;

    std::cout << "File:      " << path << "\n";
    std::cout << "Size:      " << data->size() << " bytes\n";
    std::cout << "Flags:     0x" << std::hex << std::setfill('0')
              << std::setw(8) << flags << std::dec << "\n";
    std::cout << "Encrypted: " << (encrypted ? "yes" : "no") << "\n";
    std::cout << "Checksum:  " << (checksum ? "yes" : "no") << "\n";

    if (encrypted) {
        std::cout << "\nKey source (80 bytes at offset 4):\n";
        std::cout << format_hex(data->data() + 4, 80) << "\n";

        // Try to derive the key
        std::array<uint8_t, 80> key_source;
        std::memcpy(key_source.data(), data->data() + 4, 80);

        auto key_result = wwd::derive_blowfish_key(
            std::span<const uint8_t, 80>(key_source));
        if (key_result) {
            std::cout << "\nDerived Blowfish key (56 bytes):\n";
            std::cout << format_hex(key_result->data(), 56) << "\n";
        } else {
            std::cout << "\nFailed to derive key: "
                      << key_result.error().message() << "\n";
        }
    }

    return 0;
}

static int cmd_derive(const std::string& path) {
    auto data = wwd::load_file(path);
    if (!data) {
        std::cerr << "blowfish-tool: " << data.error().message() << "\n";
        return 1;
    }

    // Determine offset: MIX files have 4-byte flags, then 80-byte key_source
    // For raw key_source files, read directly from offset 0
    size_t offset = 0;
    size_t min_size = 80;

    // Check if this looks like an encrypted MIX file
    // RA MIX format: first 2 bytes = 0x0000, flags at offset 0
    if (data->size() >= 84) {
        uint16_t first_word = (*data)[0] | (uint16_t((*data)[1]) << 8);
        uint32_t flags = (*data)[0] | (uint32_t((*data)[1]) << 8) |
                         (uint32_t((*data)[2]) << 16) | (uint32_t((*data)[3]) << 24);
        constexpr uint32_t FLAG_ENCRYPTED = 0x00020000;
        if (first_word == 0 && (flags & FLAG_ENCRYPTED)) {
            offset = 4;
            min_size = 84;
            std::cout << "Detected encrypted MIX file, reading key_source from offset 4\n\n";
        }
    }

    if (data->size() < min_size) {
        std::cerr << "blowfish-tool: file too small for key_source\n";
        return 1;
    }

    std::array<uint8_t, 80> key_source;
    std::memcpy(key_source.data(), data->data() + offset, 80);

    auto key_result = wwd::derive_blowfish_key(
        std::span<const uint8_t, 80>(key_source));
    if (!key_result) {
        std::cerr << "blowfish-tool: " << key_result.error().message() << "\n";
        return 1;
    }

    std::cout << "Key source:\n" << format_hex(key_source.data(), 80) << "\n\n";
    std::cout << "Blowfish key:\n" << format_hex(key_result->data(), 56) << "\n";

    return 0;
}

static int cmd_decrypt(const std::string& input_path,
                       const std::string& output_path) {
    auto data = wwd::load_file(input_path);
    if (!data) {
        std::cerr << "blowfish-tool: " << data.error().message() << "\n";
        return 1;
    }

    if (data->size() < 84) {
        std::cerr << "blowfish-tool: file too small (need header + key source)\n";
        return 1;
    }

    // Check if encrypted
    uint32_t flags = wwd::read_u32(data->data());
    if (!(flags & 0x00020000)) {
        std::cerr << "blowfish-tool: file is not encrypted\n";
        return 1;
    }

    // Derive Blowfish key from key source at offset 4
    std::array<uint8_t, 80> key_source;
    std::memcpy(key_source.data(), data->data() + 4, 80);

    auto key_result = wwd::derive_blowfish_key(
        std::span<const uint8_t, 80>(key_source));
    if (!key_result) {
        std::cerr << "blowfish-tool: failed to derive key: "
                  << key_result.error().message() << "\n";
        return 1;
    }

    wwd::Blowfish bf{std::span<const uint8_t, 56>(*key_result)};

    // Decrypt first 8-byte block at offset 84 to get the header
    if (data->size() < 92) {
        std::cerr << "blowfish-tool: file too small for encrypted header\n";
        return 1;
    }
    bf.decrypt_block(data->data() + 84);

    // Read file count from decrypted header (bytes 0-1 of decrypted data)
    uint16_t c_files = wwd::read_u16(data->data() + 84);

    // Sanity check
    if (c_files > 10000) {
        std::cerr << "blowfish-tool: invalid file count: " << c_files << "\n";
        return 1;
    }

    // Calculate total encrypted size:
    // Header (6 bytes) + index (c_files * 12 bytes), rounded up to 8-byte blocks
    size_t header_and_index_size = 6 + (size_t(c_files) * 12);
    size_t total_encrypted_size = (header_and_index_size + 7) & ~size_t(7);

    // We already decrypted first 8 bytes, decrypt remaining blocks
    size_t remaining_encrypted = total_encrypted_size - 8;

    if (data->size() < 84 + total_encrypted_size) {
        std::cerr << "blowfish-tool: file too small for index\n";
        return 1;
    }

    // Decrypt remaining blocks starting at offset 92
    if (remaining_encrypted > 0) {
        bf.decrypt(std::span<uint8_t>(data->data() + 92, remaining_encrypted));
    }

    // Now the decrypted data at offset 84 contains:
    // [0-1] file_count (2 bytes)
    // [2-5] body_size (4 bytes)
    // [6...] index entries (c_files * 12 bytes)
    // Index entries start at byte 6, not byte 8!

    // Calculate body offset: flags(4) + key(80) + total_encrypted
    size_t enc_body_offset = 84 + total_encrypted_size;

    // Write non-encrypted RA MIX format
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "blowfish-tool: cannot open output file: "
                  << output_path << "\n";
        return 1;
    }

    // Write flags (remove encrypted bit, keep checksum if present)
    uint32_t new_flags = flags & ~uint32_t(0x00020000);
    uint8_t flags_buf[4];
    flags_buf[0] = new_flags & 0xFF;
    flags_buf[1] = (new_flags >> 8) & 0xFF;
    flags_buf[2] = (new_flags >> 16) & 0xFF;
    flags_buf[3] = (new_flags >> 24) & 0xFF;
    out.write(reinterpret_cast<const char*>(flags_buf), 4);

    // Write file_count (2 bytes) and body_size (4 bytes) - from offset 84
    out.write(reinterpret_cast<const char*>(data->data() + 84), 6);

    // Write index entries (c_files * 12 bytes) - from offset 84 + 6 = 90
    size_t raw_index_size = size_t(c_files) * 12;
    out.write(reinterpret_cast<const char*>(data->data() + 90), raw_index_size);

    // Write body data (not encrypted)
    if (enc_body_offset < data->size()) {
        out.write(reinterpret_cast<const char*>(data->data() + enc_body_offset),
                  data->size() - enc_body_offset);
    }

    if (!out) {
        std::cerr << "blowfish-tool: write error\n";
        return 1;
    }

    std::cout << "Decrypted " << c_files << " file entries\n";
    std::cout << "Output: " << output_path << "\n";

    return 0;
}

static int cmd_encrypt(const std::string& input_path,
                       const std::string& output_path) {
    auto data = wwd::load_file(input_path);
    if (!data) {
        std::cerr << "blowfish-tool: " << data.error().message() << "\n";
        return 1;
    }

    if (data->size() < 84) {
        std::cerr << "blowfish-tool: file too small\n";
        return 1;
    }

    // Check that it's marked as encrypted (we need the key source)
    uint32_t flags = wwd::read_u32(data->data());
    if (!(flags & 0x00020000)) {
        std::cerr << "blowfish-tool: file is not marked as encrypted\n";
        return 1;
    }

    // Derive Blowfish key
    std::array<uint8_t, 80> key_source;
    std::memcpy(key_source.data(), data->data() + 4, 80);

    auto key_result = wwd::derive_blowfish_key(
        std::span<const uint8_t, 80>(key_source));
    if (!key_result) {
        std::cerr << "blowfish-tool: failed to derive key\n";
        return 1;
    }

    wwd::Blowfish bf{std::span<const uint8_t, 56>(*key_result)};

    // Read file count from header (should already be decrypted)
    uint16_t c_files = wwd::read_u16(data->data() + 84);

    // Calculate total encrypted size:
    // Header (6 bytes) + index (c_files * 12 bytes), rounded up to 8-byte blocks
    size_t header_and_index_size = 6 + (size_t(c_files) * 12);
    size_t total_encrypted_size = (header_and_index_size + 7) & ~size_t(7);

    if (data->size() < 84 + total_encrypted_size) {
        std::cerr << "blowfish-tool: file too small for index\n";
        return 1;
    }

    // Encrypt remaining blocks after first 8 bytes
    size_t remaining = total_encrypted_size - 8;
    if (remaining > 0) {
        bf.encrypt(std::span<uint8_t>(data->data() + 92, remaining));
    }

    // Encrypt header block (first 8 bytes at offset 84)
    bf.encrypt_block(data->data() + 84);

    // Write output
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "blowfish-tool: cannot open output file\n";
        return 1;
    }

    out.write(reinterpret_cast<const char*>(data->data()), data->size());

    std::cout << "Encrypted " << c_files << " file entries\n";
    std::cout << "Output: " << output_path << "\n";

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(cmd, "-V") == 0 || std::strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    // Check for verbose flag
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            break;
        }
    }
    (void)verbose;  // Used for consistent interface, not yet implemented

    if (std::strcmp(cmd, "info") == 0) {
        if (argc < 3) {
            std::cerr << "Usage: blowfish-tool info <file.mix>\n";
            return 2;
        }
        return cmd_info(argv[2]);
    }

    if (std::strcmp(cmd, "derive") == 0) {
        if (argc < 3) {
            std::cerr << "Usage: blowfish-tool derive <keyfile>\n";
            return 2;
        }
        return cmd_derive(argv[2]);
    }

    if (std::strcmp(cmd, "decrypt") == 0) {
        if (argc < 4) {
            std::cerr << "Usage: blowfish-tool decrypt <input> <output>\n";
            return 2;
        }
        return cmd_decrypt(argv[2], argv[3]);
    }

    if (std::strcmp(cmd, "encrypt") == 0) {
        if (argc < 4) {
            std::cerr << "Usage: blowfish-tool encrypt <input> <output>\n";
            return 2;
        }
        return cmd_encrypt(argv[2], argv[3]);
    }

    std::cerr << "blowfish-tool: unknown command '" << cmd << "'\n";
    print_usage();
    return 2;
}
