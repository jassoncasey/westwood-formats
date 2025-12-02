#include <westwood/aud.h>
#include <westwood/io.h>

namespace wwd {

struct AudReaderImpl {
    AudInfo info{};
};

struct AudReader::Impl : AudReaderImpl {};

AudReader::AudReader() : impl_(std::make_unique<Impl>()) {}
AudReader::~AudReader() = default;

const AudInfo& AudReader::info() const { return impl_->info; }

float AudReader::duration() const {
    if (impl_->info.sample_rate == 0) return 0.0f;
    return float(sample_count()) / float(impl_->info.sample_rate);
}

uint32_t AudReader::sample_count() const {
    uint32_t bytes_per_sample = (impl_->info.bits / 8) * impl_->info.channels;
    if (bytes_per_sample == 0) return 0;
    return impl_->info.uncompressed_size / bytes_per_sample;
}

static Result<void> parse_aud(AudReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // AUD header: 12 bytes
    // Offset  Size  Description
    // 0       2     Sample rate
    // 2       4     Uncompressed size
    // 6       4     Compressed size (output size for old format)
    // 10      1     Flags (bit 0 = stereo, bit 1 = 16-bit)
    // 11      1     Compression type (1 = WW ADPCM, 99 = IMA ADPCM)

    if (data.size() < 12) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "AUD file too small"));
    }

    const uint8_t* p = data.data();

    impl.info.sample_rate = read_u16(p);
    impl.info.uncompressed_size = read_u32(p + 2);
    impl.info.compressed_size = read_u32(p + 6);

    uint8_t flags = p[10];
    impl.info.channels = (flags & 0x01) ? 2 : 1;
    impl.info.bits = (flags & 0x02) ? 16 : 8;

    uint8_t comp_type = p[11];
    switch (comp_type) {
        case 1:
            impl.info.codec = AudCodec::WestwoodADPCM;
            break;
        case 99:
            impl.info.codec = AudCodec::IMAADPCM;
            break;
        default:
            impl.info.codec = AudCodec::Unknown;
            break;
    }

    impl.info.file_size = static_cast<uint32_t>(data.size());

    return {};
}

Result<std::unique_ptr<AudReader>> AudReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());
    return open(std::span(*data));
}

Result<std::unique_ptr<AudReader>> AudReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<AudReader>(new AudReader());
    auto result = parse_aud(*reader->impl_, data);
    if (!result) return std::unexpected(result.error());
    return reader;
}

} // namespace wwd
