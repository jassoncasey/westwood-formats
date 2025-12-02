#include <westwood/cps.h>
#include <westwood/io.h>
#include <westwood/lcw.h>

namespace wwd {

struct CpsReaderImpl {
    CpsInfo info{};
    std::vector<uint8_t> pixels;
    std::array<Color, 256> palette{};
    bool has_palette = false;
};

struct CpsReader::Impl : CpsReaderImpl {};

CpsReader::CpsReader() : impl_(std::make_unique<Impl>()) {}
CpsReader::~CpsReader() = default;

const CpsInfo& CpsReader::info() const { return impl_->info; }
const std::vector<uint8_t>& CpsReader::pixels() const { return impl_->pixels; }

const std::array<Color, 256>* CpsReader::palette() const {
    return impl_->has_palette ? &impl_->palette : nullptr;
}

static Result<void> parse_cps(CpsReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // Minimum header size: 10 bytes
    if (data.size() < 10) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "CPS file too small"));
    }

    SpanReader r(data);

    // Read header
    auto file_size = r.read_u16();
    if (!file_size) return std::unexpected(file_size.error());
    impl.info.file_size = *file_size;

    auto compression = r.read_u16();
    if (!compression) return std::unexpected(compression.error());
    impl.info.compression = *compression;

    auto uncomp_size = r.read_u32();
    if (!uncomp_size) return std::unexpected(uncomp_size.error());
    impl.info.uncomp_size = *uncomp_size;

    auto pal_size = r.read_u16();
    if (!pal_size) return std::unexpected(pal_size.error());
    impl.info.palette_size = *pal_size;

    // Validate header
    // file_size + 2 should equal actual file size
    if (impl.info.file_size + 2 != data.size()) {
        // Some files may have padding; allow if close
        if (impl.info.file_size + 2 > data.size()) {
            return std::unexpected(
                make_error(ErrorCode::CorruptHeader, "CPS file size mismatch"));
        }
    }

    // Validate compression
    if (impl.info.compression != 0 && impl.info.compression != 4) {
        return std::unexpected(
            make_error(ErrorCode::UnsupportedFormat,
                       "Unknown CPS compression method"));
    }

    // Standard CPS is 320x200
    impl.info.width = 320;
    impl.info.height = 200;
    impl.info.has_palette = (impl.info.palette_size == 768);

    // Read embedded palette if present
    if (impl.info.has_palette) {
        auto pal_data = r.read_bytes(768);
        if (!pal_data) return std::unexpected(pal_data.error());

        const uint8_t* p = pal_data->data();
        for (int i = 0; i < 256; ++i) {
            impl.palette[i] = Color{p[0], p[1], p[2]};
            p += 3;
        }
        impl.has_palette = true;
    }

    // Read image data
    size_t image_offset = r.pos();
    size_t image_size = data.size() - image_offset;
    auto image_data = data.subspan(image_offset, image_size);

    if (impl.info.compression == 4) {
        // LCW compressed
        auto result = lcw_decompress(image_data, impl.info.uncomp_size);
        if (!result) return std::unexpected(result.error());
        impl.pixels = std::move(*result);
    } else {
        // Uncompressed
        impl.pixels.assign(image_data.begin(), image_data.end());
    }

    // Validate pixel count
    if (impl.pixels.size() != 64000) {
        return std::unexpected(
            make_error(ErrorCode::CorruptData,
                       "CPS pixel data size mismatch"));
    }

    return {};
}

Result<std::unique_ptr<CpsReader>> CpsReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) {
        return std::unexpected(data.error());
    }
    return open(std::span(*data));
}

Result<std::unique_ptr<CpsReader>> CpsReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<CpsReader>(new CpsReader());
    auto result = parse_cps(*reader->impl_, data);
    if (!result) {
        return std::unexpected(result.error());
    }
    return reader;
}

} // namespace wwd
