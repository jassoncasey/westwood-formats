#include <westwood/mix.h>
#include <westwood/io.h>
#include <westwood/blowfish.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace wwd {

// Constants
constexpr uint32_t FLAG_CHECKSUM  = 0x00010000;
constexpr uint32_t FLAG_ENCRYPTED = 0x00020000;
constexpr uint32_t MIX_RG_MAGIC   = 0x3158494D;  // "MIX1"
constexpr uint32_t BIG_MAGIC      = 0x46474942;  // "BIGF"
constexpr uint32_t BIG4_MAGIC     = 0x34474942;  // "BIG4"
constexpr uint32_t TS_MARKER_ID   = 0x763C81DD;
constexpr uint32_t MAX_FILE_COUNT = 4095;
constexpr size_t   INDEX_ENTRY_SIZE = 12;

struct MixReaderImpl {
    MixInfo info{};
    std::vector<MixEntry> entries;
    std::unordered_map<uint32_t, size_t> hash_to_index;
    std::unordered_map<std::string, size_t> name_to_index;
    std::vector<uint8_t> data;
    uint32_t body_offset{0};
};

struct MixReader::Impl : MixReaderImpl {};

MixReader::MixReader() : impl_(std::make_unique<Impl>()) {}
MixReader::~MixReader() = default;

const MixInfo& MixReader::info() const { return impl_->info; }
const std::vector<MixEntry>& MixReader::entries() const {
    return impl_->entries;
}

const MixEntry* MixReader::find(uint32_t hash) const {
    auto it = impl_->hash_to_index.find(hash);
    return (it != impl_->hash_to_index.end())
        ? &impl_->entries[it->second] : nullptr;
}

const MixEntry* MixReader::find(std::string_view name) const {
    auto it = impl_->name_to_index.find(std::string(name));
    return (it != impl_->name_to_index.end())
        ? &impl_->entries[it->second] : nullptr;
}

void MixReader::resolve_names(const std::vector<std::string>& names) {
    MixGame game = impl_->info.game;
    for (const auto& name : names) {
        uint32_t hash = mix_hash(game, name);
        auto it = impl_->hash_to_index.find(hash);
        if (it == impl_->hash_to_index.end()) continue;

        MixEntry& entry = impl_->entries[it->second];
        if (entry.name.empty()) {
            entry.name = name;
            impl_->name_to_index[name] = it->second;
        }
    }
}

Result<std::vector<uint8_t>> MixReader::read(const MixEntry& entry) const {
    size_t end = entry.offset + entry.size;
    if (end > impl_->data.size()) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Entry beyond EOF"));
    }

    std::vector<uint8_t> result(entry.size);
    std::memcpy(result.data(), impl_->data.data() + entry.offset, entry.size);
    return result;
}

// Hash implementations
//
// TD/RA hash: Rotate-add algorithm processing 4 characters at a time
// From xcc.md section 8.1:
//   - Uppercase and normalize slashes
//   - Process 4 chars at a time, building a 32-bit word
//   - rotate_left(hash, 1) + word
//
uint32_t mix_hash_td(std::string_view filename) {
    // Preprocess: uppercase and normalize slashes
    std::string name;
    name.reserve(filename.size());
    for (char c : filename) {
        if (c >= 'a' && c <= 'z') c -= 0x20;
        if (c == '/') c = '\\';
        name.push_back(c);
    }

    uint32_t id = 0;
    size_t i = 0;
    size_t len = name.size();

    while (i < len) {
        uint32_t a = 0;
        for (int j = 0; j < 4; ++j) {
            a >>= 8;
            if (i < len) {
                a |= static_cast<uint32_t>(static_cast<uint8_t>(name[i])) << 24;
                ++i;
            }
        }
        // rotate_left(id, 1) + a
        id = ((id << 1) | (id >> 31)) + a;
    }

    return id;
}

uint32_t mix_hash_ts(std::string_view filename) {
    // CRC32 variant used by TS/RA2
    static const uint32_t crc_table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
        0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
        0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
        0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
        0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
        0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
        0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
        0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
        0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
        0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
        0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
        0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
        0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
        0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
        0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
        0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
        0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
        0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
        0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
        0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
        0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
        0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };

    uint32_t crc = 0;
    size_t len = filename.length();

    for (size_t i = 0; i < len; ++i) {
        uint8_t c = static_cast<uint8_t>(filename[i]);
        if (c >= 'A' && c <= 'Z') c += 0x20;  // lowercase for TS
        crc = crc_table[(crc ^ c) & 0xFF] ^ (crc >> 8);
    }

    return crc;
}

uint32_t mix_hash(MixGame game, std::string_view filename) {
    switch (game) {
        case MixGame::TiberianDawn:
        case MixGame::RedAlert:
            return mix_hash_td(filename);
        case MixGame::TiberianSun:
        case MixGame::RedAlert2:
        case MixGame::YurisRevenge:
            return mix_hash_ts(filename);
        default:
            return mix_hash_td(filename);
    }
}

const char* mix_format_name(MixFormat format) {
    switch (format) {
        case MixFormat::TD:  return "TD";
        case MixFormat::RA:  return "RA";
        case MixFormat::TS:  return "TS";
        case MixFormat::RG:  return "Renegade";
        case MixFormat::BIG: return "BIG";
        default:             return "Unknown";
    }
}

const char* mix_game_name(MixGame game) {
    switch (game) {
        case MixGame::TiberianDawn:  return "Tiberian Dawn";
        case MixGame::RedAlert:      return "Red Alert";
        case MixGame::TiberianSun:   return "Tiberian Sun";
        case MixGame::RedAlert2:     return "Red Alert 2";
        case MixGame::YurisRevenge:  return "Yuri's Revenge";
        case MixGame::Renegade:      return "Renegade";
        case MixGame::Generals:      return "Generals";
        case MixGame::ZeroHour:      return "Zero Hour";
        default:                     return "Unknown";
    }
}

MixGame mix_detect_game(MixFormat format, const std::vector<MixEntry>& entries) {
    // Check for TS marker
    for (const auto& e : entries) {
        if (e.hash == TS_MARKER_ID) {
            return MixGame::TiberianSun;
        }
    }

    switch (format) {
        case MixFormat::TD:  return MixGame::TiberianDawn;
        case MixFormat::RA:  return MixGame::RedAlert;
        case MixFormat::TS:  return MixGame::TiberianSun;
        case MixFormat::RG:  return MixGame::Renegade;
        case MixFormat::BIG: return MixGame::Generals;
        default:             return MixGame::Unknown;
    }
}

// Index parsing
static void parse_index(MixReaderImpl& impl,
                        const uint8_t* ptr,
                        uint16_t count,
                        uint32_t base_offset) {
    impl.entries.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        MixEntry e;
        e.hash   = read_u32(ptr);
        e.offset = read_u32(ptr + 4) + base_offset;
        e.size   = read_u32(ptr + 8);
        impl.hash_to_index[e.hash] = impl.entries.size();
        impl.entries.push_back(std::move(e));
        ptr += INDEX_ENTRY_SIZE;
    }
}

// TD format
static Result<void> parse_td(MixReaderImpl& impl,
                              const uint8_t* data,
                              size_t size) {
    if (size < 6) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "TD header too small"));
    }

    uint16_t count = read_u16(data);
    if (count > MAX_FILE_COUNT) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "File count too large"));
    }

    size_t hdr_size = 6 + size_t(count) * INDEX_ENTRY_SIZE;
    if (size < hdr_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "Index truncated"));
    }

    impl.info.format       = MixFormat::TD;
    impl.info.encrypted    = false;
    impl.info.has_checksum = false;
    impl.info.file_count   = count;
    impl.info.file_size    = size;
    impl.body_offset       = static_cast<uint32_t>(hdr_size);

    parse_index(impl, data + 6, count, impl.body_offset);
    impl.info.game = mix_detect_game(MixFormat::TD, impl.entries);

    return {};
}

// RA format - encrypted
static Result<void> parse_ra_encrypted(MixReaderImpl& impl,
                                        const uint8_t* data,
                                        size_t size) {
    // Layout per xcc.md:
    // Offset 0:  4 bytes  - flags
    // Offset 4:  80 bytes - key_source (RSA-encrypted Blowfish key)
    // Offset 84: 8 bytes  - encrypted header block
    // Offset 92: P bytes  - encrypted index (P = ((c_files*12)+5) & ~7)
    // Body follows, NOT encrypted

    constexpr size_t KEY_SOURCE_OFFSET = 4;
    constexpr size_t ENCRYPTED_HDR_OFFSET = 84;
    constexpr size_t ENCRYPTED_IDX_OFFSET = 92;

    if (size < ENCRYPTED_IDX_OFFSET) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader,
                       "Encrypted RA: file too small for header"));
    }

    // Derive Blowfish key from RSA-encrypted key source
    std::span<const uint8_t, 80> key_source(data + KEY_SOURCE_OFFSET, 80);
    auto key_result = derive_blowfish_key(key_source);
    if (!key_result) {
        return std::unexpected(key_result.error());
    }

    Blowfish bf(*key_result);

    // Decrypt the 8-byte header block
    uint8_t dec_header[8];
    std::memcpy(dec_header, data + ENCRYPTED_HDR_OFFSET, 8);
    bf.decrypt_block(dec_header);

    // Extract c_files and body_size from decrypted header
    uint16_t count = read_u16(dec_header);
    // body_size at dec_header+2 is available for validation if needed
    (void)read_u32(dec_header + 2);

    if (count == 0 || count > MAX_FILE_COUNT) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader,
                       "Encrypted RA: invalid file count"));
    }

    // Calculate encrypted index size: P = ((c_files * 12) + 5) & ~7
    size_t raw_index_size = size_t(count) * INDEX_ENTRY_SIZE;
    size_t P = (raw_index_size + 5) & ~size_t(7);

    if (size < ENCRYPTED_IDX_OFFSET + P) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex,
                       "Encrypted RA: truncated encrypted index"));
    }

    // Decrypt the index block
    std::vector<uint8_t> dec_index(P);
    std::memcpy(dec_index.data(), data + ENCRYPTED_IDX_OFFSET, P);
    bf.decrypt(dec_index);

    // Reconstruct full index:
    // - First 2 bytes come from dec_header[6..7]
    // - Remaining (c_files*12 - 2) bytes from dec_index
    std::vector<uint8_t> full_index(raw_index_size);
    full_index[0] = dec_header[6];
    full_index[1] = dec_header[7];
    if (raw_index_size > 2) {
        std::memcpy(full_index.data() + 2, dec_index.data(), raw_index_size - 2);
    }

    // Body offset = 92 + P
    uint32_t body_offset = static_cast<uint32_t>(ENCRYPTED_IDX_OFFSET + P);

    impl.info.file_count = count;
    impl.body_offset = body_offset;

    parse_index(impl, full_index.data(), count, body_offset);
    impl.info.game = mix_detect_game(MixFormat::RA, impl.entries);

    return {};
}

// RA format - unencrypted
static Result<void> parse_ra_unencrypted(MixReaderImpl& impl,
                                          const uint8_t* data,
                                          size_t size) {
    if (size < 10) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "RA header too small"));
    }

    uint16_t count = read_u16(data + 4);
    if (count > MAX_FILE_COUNT) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "File count too large"));
    }

    size_t hdr_size = 10 + size_t(count) * INDEX_ENTRY_SIZE;
    if (size < hdr_size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "Index truncated"));
    }

    impl.info.file_count = count;
    impl.body_offset = static_cast<uint32_t>(hdr_size);

    parse_index(impl, data + 10, count, impl.body_offset);
    impl.info.game = mix_detect_game(MixFormat::RA, impl.entries);

    return {};
}

// RA format
static Result<void> parse_ra(MixReaderImpl& impl,
                              const uint8_t* data,
                              size_t size,
                              uint32_t flags) {
    impl.info.format       = MixFormat::RA;
    impl.info.encrypted    = (flags & FLAG_ENCRYPTED) != 0;
    impl.info.has_checksum = (flags & FLAG_CHECKSUM) != 0;
    impl.info.file_size    = size;

    if (impl.info.encrypted) {
        return parse_ra_encrypted(impl, data, size);
    }

    return parse_ra_unencrypted(impl, data, size);
}

// Renegade format (MIX1)
// Header (12 bytes):
//   0-3:   "MIX1" signature
//   4-7:   header_offset (index section offset)
//   8-11:  names_offset (filename table offset)
// At header_offset:
//   file_count (4 bytes)
//   file_count entries of 12 bytes each: CRC32, offset (from data start), size
// At names_offset:
//   series of: 1-byte length + filename bytes
// Data section starts at offset 12 (after header)
static Result<void> parse_rg(MixReaderImpl& impl,
                              const uint8_t* data,
                              size_t size) {
    constexpr size_t HEADER_SIZE = 12;
    constexpr size_t DATA_START = 12;

    if (size < HEADER_SIZE) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "RG header too small"));
    }

    // Read header
    uint32_t header_offset = read_u32(data + 4);
    uint32_t names_offset = read_u32(data + 8);

    impl.info.format = MixFormat::RG;
    impl.info.game = MixGame::Renegade;
    impl.info.encrypted = false;
    impl.info.has_checksum = false;
    impl.info.file_size = size;
    impl.body_offset = DATA_START;

    // Read file count from header_offset
    if (header_offset + 4 > size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "RG index offset beyond file"));
    }

    uint32_t file_count = read_u32(data + header_offset);

    if (file_count > MAX_FILE_COUNT) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "RG file count too large"));
    }

    impl.info.file_count = static_cast<uint16_t>(file_count);

    // Read index entries (after file count)
    size_t index_start = header_offset + 4;
    size_t index_size = file_count * INDEX_ENTRY_SIZE;

    if (index_start + index_size > size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "RG index truncated"));
    }

    // Parse entries - offsets are relative to data section start (offset 12)
    impl.entries.reserve(file_count);
    const uint8_t* ptr = data + index_start;

    for (uint32_t i = 0; i < file_count; ++i) {
        MixEntry e;
        e.hash = read_u32(ptr);         // CRC32
        e.offset = read_u32(ptr + 4) + DATA_START;  // Convert to absolute offset
        e.size = read_u32(ptr + 8);
        impl.hash_to_index[e.hash] = impl.entries.size();
        impl.entries.push_back(std::move(e));
        ptr += INDEX_ENTRY_SIZE;
    }

    // Read filename table if present
    if (names_offset > 0 && names_offset < size) {
        const uint8_t* name_ptr = data + names_offset;
        const uint8_t* name_end = data + size;

        for (size_t i = 0; i < impl.entries.size() && name_ptr < name_end; ++i) {
            uint8_t name_len = *name_ptr++;
            if (name_ptr + name_len > name_end) break;

            std::string filename(reinterpret_cast<const char*>(name_ptr), name_len);
            name_ptr += name_len;

            // Match filename to entry by CRC
            uint32_t crc = mix_hash_ts(filename);  // RG uses CRC32 like TS
            auto it = impl.hash_to_index.find(crc);
            if (it != impl.hash_to_index.end()) {
                impl.entries[it->second].name = filename;
                impl.name_to_index[filename] = it->second;
            }
        }
    }

    return {};
}

// BIG format (Generals/Zero Hour)
// Header: magic(4) + archive_size(4) + entry_count(4,BE) + index_size(4,BE)
// Entry: offset(4,BE) + size(4,BE) + filename(null-terminated)
static Result<void> parse_big(MixReaderImpl& impl,
                               const uint8_t* data,
                               size_t size) {
    if (size < 16) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "BIG header too small"));
    }

    uint32_t magic = read_u32(data);
    impl.info.format = MixFormat::BIG;
    impl.info.game = (magic == BIG4_MAGIC) ? MixGame::ZeroHour : MixGame::Generals;
    impl.info.encrypted = false;
    impl.info.has_checksum = false;
    impl.info.file_size = size;

    // Archive size is LE, but entry count and index size are BE (mixed endianness)
    uint32_t archive_size = read_u32(data + 4);
    uint32_t entry_count = read_u32be(data + 8);
    uint32_t index_size = read_u32be(data + 12);

    (void)archive_size;  // Not used for parsing

    impl.info.file_count = static_cast<uint16_t>(std::min(entry_count, uint32_t(65535)));

    // Parse entries starting at offset 16
    // Each entry: offset(4,BE) + size(4,BE) + filename(null-terminated)
    const uint8_t* ptr = data + 16;
    const uint8_t* end = data + 16 + index_size;

    if (end > data + size) {
        return std::unexpected(
            make_error(ErrorCode::CorruptIndex, "BIG index beyond file"));
    }

    impl.entries.reserve(entry_count);

    for (uint32_t i = 0; i < entry_count; ++i) {
        if (ptr + 8 > end) {
            return std::unexpected(
                make_error(ErrorCode::CorruptIndex, "BIG entry truncated"));
        }

        MixEntry e;
        e.offset = read_u32be(ptr);
        e.size = read_u32be(ptr + 4);
        ptr += 8;

        // Read null-terminated filename
        const uint8_t* name_start = ptr;
        while (ptr < end && *ptr != '\0') {
            ++ptr;
        }
        if (ptr >= end) {
            return std::unexpected(
                make_error(ErrorCode::CorruptIndex, "BIG filename unterminated"));
        }

        e.name.assign(reinterpret_cast<const char*>(name_start), ptr - name_start);
        ++ptr;  // Skip null terminator

        // Generate hash from filename for lookup compatibility
        e.hash = mix_hash_ts(e.name);

        impl.hash_to_index[e.hash] = impl.entries.size();
        impl.name_to_index[e.name] = impl.entries.size();
        impl.entries.push_back(std::move(e));
    }

    impl.body_offset = 0;  // Offsets are absolute in BIG format

    return {};
}

// Format detection
static Result<void> parse(MixReaderImpl& impl,
                           const uint8_t* data,
                           size_t size) {
    if (size < 6) {
        return std::unexpected(
            make_error(ErrorCode::InvalidFormat, "File too small"));
    }

    uint32_t magic = read_u32(data);

    if (magic == MIX_RG_MAGIC) {
        return parse_rg(impl, data, size);
    }
    if (magic == BIG_MAGIC || magic == BIG4_MAGIC) {
        return parse_big(impl, data, size);
    }

    if (read_u16(data) == 0) {
        uint32_t flags = read_u32(data);
        if ((flags & ~(FLAG_CHECKSUM | FLAG_ENCRYPTED)) == 0) {
            return parse_ra(impl, data, size, flags);
        }
    }

    return parse_td(impl, data, size);
}

Result<std::unique_ptr<MixReader>> MixReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<MixReader>(new MixReader());
    reader->impl_->data = std::move(*data);

    auto result = parse(*reader->impl_,
                        reader->impl_->data.data(),
                        reader->impl_->data.size());
    if (!result) return std::unexpected(result.error());

    return reader;
}

Result<std::unique_ptr<MixReader>> MixReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<MixReader>(new MixReader());
    reader->impl_->data.assign(data.begin(), data.end());

    auto result = parse(*reader->impl_,
                        reader->impl_->data.data(),
                        reader->impl_->data.size());
    if (!result) return std::unexpected(result.error());

    return reader;
}

} // namespace wwd
