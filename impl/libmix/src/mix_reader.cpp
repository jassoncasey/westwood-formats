#include <mix/mix.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace mix {
namespace detail {

struct MixReaderImpl {
    ArchiveInfo info{};
    std::vector<Entry> entries;
    std::unordered_map<uint32_t, size_t> hash_to_index;
    std::unordered_map<std::string, size_t> name_to_index;
    std::string file_path;
    std::vector<uint8_t> memory_data;
    bool using_memory{false};
    uint32_t body_offset{0};
};

} // namespace detail

struct MixReader::Impl : detail::MixReaderImpl {};

MixReader::MixReader() : impl_(std::make_unique<Impl>()) {}
MixReader::~MixReader() = default;

const ArchiveInfo& MixReader::info() const { return impl_->info; }
const std::vector<Entry>& MixReader::entries() const { return impl_->entries; }

const Entry* MixReader::find(uint32_t hash) const {
    auto it = impl_->hash_to_index.find(hash);
    return (it != impl_->hash_to_index.end())
        ? &impl_->entries[it->second] : nullptr;
}

const Entry* MixReader::find(std::string_view name) const {
    auto it = impl_->name_to_index.find(std::string(name));
    return (it != impl_->name_to_index.end())
        ? &impl_->entries[it->second] : nullptr;
}

void MixReader::resolve_names(const std::vector<std::string>& names) {
    GameType game = impl_->info.game;
    for (const auto& name : names) {
        uint32_t hash = compute_hash(game, name);
        auto it = impl_->hash_to_index.find(hash);
        if (it == impl_->hash_to_index.end()) continue;
        Entry& entry = impl_->entries[it->second];
        if (entry.name.empty()) {
            entry.name = name;
            impl_->name_to_index[name] = it->second;
        }
    }
}

Result<std::vector<uint8_t>> MixReader::read(const Entry& entry) const {
    std::vector<uint8_t> data(entry.size);
    if (impl_->using_memory) {
        size_t end = entry.offset + entry.size;
        if (end > impl_->memory_data.size()) {
            return std::unexpected(
                make_error(ErrorCode::ReadError, "Entry beyond EOF"));
        }
        auto* src = impl_->memory_data.data() + entry.offset;
        std::memcpy(data.data(), src, entry.size);
        return data;
    }
    std::ifstream file(impl_->file_path, std::ios::binary);
    if (!file) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Cannot open file"));
    }
    file.seekg(entry.offset);
    if (!file.read(reinterpret_cast<char*>(data.data()), entry.size)) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Read failed"));
    }
    return data;
}

// ---------------------------------------------------------------------------
// Binary readers
// ---------------------------------------------------------------------------

static uint16_t read_u16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// Index parsing
// ---------------------------------------------------------------------------

static void parse_index(detail::MixReaderImpl& impl,
                        const uint8_t* ptr,
                        uint16_t count,
                        uint32_t base_offset) {
    impl.entries.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        Entry e;
        e.hash   = read_u32(ptr);
        e.offset = read_u32(ptr + 4) + base_offset;
        e.size   = read_u32(ptr + 8);
        impl.hash_to_index[e.hash] = impl.entries.size();
        impl.entries.push_back(std::move(e));
        ptr += INDEX_ENTRY_SIZE;
    }
}

// ---------------------------------------------------------------------------
// TD format
// ---------------------------------------------------------------------------

static Result<void> parse_td(detail::MixReaderImpl& impl,
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
    impl.info.format       = FormatType::TD;
    impl.info.encrypted    = false;
    impl.info.has_checksum = false;
    impl.info.file_count   = count;
    impl.info.file_size    = size;
    impl.body_offset       = uint32_t(hdr_size);

    parse_index(impl, data + 6, count, impl.body_offset);
    impl.info.game = detect_game(FormatType::TD, impl.entries);
    return {};
}

// ---------------------------------------------------------------------------
// RA format
// ---------------------------------------------------------------------------

static Result<void> parse_ra(detail::MixReaderImpl& impl,
                             const uint8_t* data,
                             size_t size,
                             uint32_t flags) {
    impl.info.format       = FormatType::RA;
    impl.info.encrypted    = (flags & FLAG_ENCRYPTED) != 0;
    impl.info.has_checksum = (flags & FLAG_CHECKSUM) != 0;
    impl.info.file_size    = size;

    if (impl.info.encrypted) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat,
                       "Encrypted RA format not yet supported"));
    }
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
    impl.body_offset     = uint32_t(hdr_size);

    parse_index(impl, data + 10, count, impl.body_offset);
    impl.info.game = detect_game(FormatType::RA, impl.entries);
    return {};
}

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------

static Result<void> parse(detail::MixReaderImpl& impl,
                          const uint8_t* data,
                          size_t size) {
    if (size < 6) {
        return std::unexpected(
            make_error(ErrorCode::InvalidFormat, "File too small"));
    }
    uint32_t magic = read_u32(data);
    if (magic == MIX_RG_MAGIC) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "MIX-RG not supported"));
    }
    if (magic == BIG_MAGIC || magic == BIG4_MAGIC) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat, "BIG not supported"));
    }
    if (read_u16(data) == 0) {
        uint32_t flags = read_u32(data);
        if ((flags & ~(FLAG_CHECKSUM | FLAG_ENCRYPTED)) == 0) {
            return parse_ra(impl, data, size, flags);
        }
    }
    return parse_td(impl, data, size);
}

// ---------------------------------------------------------------------------
// Public open functions
// ---------------------------------------------------------------------------

Result<std::unique_ptr<MixReader>> MixReader::open(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(
            make_error(ErrorCode::FileNotFound, "Cannot open: " + path));
    }
    auto size = size_t(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return std::unexpected(
            make_error(ErrorCode::ReadError, "Read failed"));
    }
    auto reader = std::unique_ptr<MixReader>(new MixReader());
    reader->impl_->file_path = path;
    reader->impl_->memory_data = std::move(data);
    reader->impl_->using_memory = true;

    auto result = parse(*reader->impl_,
                        reader->impl_->memory_data.data(),
                        reader->impl_->memory_data.size());
    if (!result) return std::unexpected(result.error());
    return reader;
}

Result<std::unique_ptr<MixReader>> MixReader::open(
        std::span<const uint8_t> data) {
    auto reader = std::unique_ptr<MixReader>(new MixReader());
    reader->impl_->memory_data.assign(data.begin(), data.end());
    reader->impl_->using_memory = true;

    auto result = parse(*reader->impl_,
                        reader->impl_->memory_data.data(),
                        reader->impl_->memory_data.size());
    if (!result) return std::unexpected(result.error());
    return reader;
}

} // namespace mix
