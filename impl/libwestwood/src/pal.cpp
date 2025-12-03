#include <westwood/pal.h>
#include <westwood/io.h>

namespace wwd {

struct PalReaderImpl {
    PalInfo info{};
    std::array<Color, 256> colors{};
};

struct PalReader::Impl : PalReaderImpl {};

PalReader::PalReader() : impl_(std::make_unique<Impl>()) {}
PalReader::~PalReader() = default;

const PalInfo& PalReader::info() const { return impl_->info; }
const std::array<Color, 256>& PalReader::colors() const {
    return impl_->colors;
}

Color PalReader::color_8bit(uint8_t index) const {
    const Color& c = impl_->colors[index];
    if (impl_->info.bit_depth == 6) {
        // Scale 6-bit (0-63) to 8-bit (0-255): v * 255 / 63 ~= v * 4 + v / 16
        return Color{
            static_cast<uint8_t>((c.r << 2) | (c.r >> 4)),
            static_cast<uint8_t>((c.g << 2) | (c.g >> 4)),
            static_cast<uint8_t>((c.b << 2) | (c.b >> 4))
        };
    }
    return c;
}

static Result<void> parse_pal(PalReaderImpl& impl,
                               std::span<const uint8_t> data) {
    // Standard palette is exactly 768 bytes (256 * 3)
    if (data.size() < 768) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "PAL file too small"));
    }
    if (data.size() > 768) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "PAL file too large"));
    }

    impl.info.entries = 256;
    impl.info.file_size = static_cast<uint32_t>(data.size());

    // Detect bit depth by scanning for values > 63
    bool is_8bit = false;
    for (size_t i = 0; i < 768 && !is_8bit; ++i) {
        if (data[i] > 63) {
            is_8bit = true;
        }
    }
    impl.info.bit_depth = is_8bit ? 8 : 6;

    // Read colors
    const uint8_t* p = data.data();
    for (int i = 0; i < 256; ++i) {
        impl.colors[i] = Color{p[0], p[1], p[2]};
        p += 3;
    }

    return {};
}

Result<std::unique_ptr<PalReader>> PalReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) {
        return std::unexpected(data.error());
    }
    return open(std::span(*data));
}

Result<std::unique_ptr<PalReader>> PalReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<PalReader>(new PalReader());
    auto result = parse_pal(*reader->impl_, data);
    if (!result) {
        return std::unexpected(result.error());
    }
    return reader;
}

} // namespace wwd
