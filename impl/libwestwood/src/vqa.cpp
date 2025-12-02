#include <westwood/vqa.h>
#include <westwood/io.h>

namespace wwd {

struct VqaReaderImpl {
    VqaInfo info{};
    std::vector<uint8_t> data;
};

struct VqaReader::Impl : VqaReaderImpl {};

VqaReader::VqaReader() : impl_(std::make_unique<Impl>()) {}
VqaReader::~VqaReader() = default;

const VqaInfo& VqaReader::info() const { return impl_->info; }

float VqaReader::duration() const {
    if (impl_->info.header.frame_rate == 0) return 0.0f;
    return float(impl_->info.header.frame_count) /
           float(impl_->info.header.frame_rate);
}

bool VqaReader::is_hicolor() const {
    return (impl_->info.header.flags & 0x10) != 0;
}

uint32_t VqaReader::block_count() const {
    const auto& h = impl_->info.header;
    if (h.block_w == 0 || h.block_h == 0) return 0;
    return (h.width / h.block_w) * (h.height / h.block_h);
}

static Result<void> parse_vqhd(VqaHeader& hdr, SpanReader& r) {
    auto version = r.read_u16();
    if (!version) return std::unexpected(version.error());
    hdr.version = *version;

    auto flags = r.read_u16();
    if (!flags) return std::unexpected(flags.error());
    hdr.flags = *flags;

    auto frames = r.read_u16();
    if (!frames) return std::unexpected(frames.error());
    hdr.frame_count = *frames;

    auto width = r.read_u16();
    if (!width) return std::unexpected(width.error());
    hdr.width = *width;

    auto height = r.read_u16();
    if (!height) return std::unexpected(height.error());
    hdr.height = *height;

    auto block_w = r.read_u8();
    if (!block_w) return std::unexpected(block_w.error());
    hdr.block_w = *block_w;

    auto block_h = r.read_u8();
    if (!block_h) return std::unexpected(block_h.error());
    hdr.block_h = *block_h;

    auto fps = r.read_u8();
    if (!fps) return std::unexpected(fps.error());
    hdr.frame_rate = *fps;

    auto cb_parts = r.read_u8();
    if (!cb_parts) return std::unexpected(cb_parts.error());
    hdr.cb_parts = *cb_parts;

    auto colors = r.read_u16();
    if (!colors) return std::unexpected(colors.error());
    hdr.colors = *colors;

    auto max_blocks = r.read_u16();
    if (!max_blocks) return std::unexpected(max_blocks.error());
    hdr.max_blocks = *max_blocks;

    // Skip unknown1, unknown2
    r.skip(4);

    auto offset_x = r.read_u16();
    if (!offset_x) return std::unexpected(offset_x.error());
    hdr.offset_x = *offset_x;

    auto offset_y = r.read_u16();
    if (!offset_y) return std::unexpected(offset_y.error());
    hdr.offset_y = *offset_y;

    auto max_vpt = r.read_u16();
    if (!max_vpt) return std::unexpected(max_vpt.error());
    hdr.max_vpt_size = *max_vpt;

    auto sample_rate = r.read_u16();
    if (!sample_rate) return std::unexpected(sample_rate.error());
    hdr.sample_rate = *sample_rate;

    auto channels = r.read_u8();
    if (!channels) return std::unexpected(channels.error());
    hdr.channels = *channels;

    auto bits = r.read_u8();
    if (!bits) return std::unexpected(bits.error());
    hdr.bits = *bits;

    return {};
}

static Result<void> scan_audio_chunks(VqaAudioInfo& audio,
                                       std::span<const uint8_t> data) {
    SpanReader r(data);

    // Skip to after VQHD
    r.seek(12);  // FORM(4) + size(4) + WVQA(4) -> VQHD

    while (r.remaining() >= 8) {
        auto tag = r.read_u32();
        if (!tag) break;
        auto size = r.read_u32be();
        if (!size) break;

        uint32_t t = *tag;
        if (t == read_u32(reinterpret_cast<const uint8_t*>("SND0"))) {
            audio.has_audio = true;
            audio.compressed = false;
            break;
        }
        if (t == read_u32(reinterpret_cast<const uint8_t*>("SND2"))) {
            audio.has_audio = true;
            audio.compressed = true;
            break;
        }

        // Skip chunk + alignment
        size_t skip = *size + (*size & 1);
        if (!r.skip(skip)) break;
    }

    return {};
}

static Result<void> parse_vqa(VqaReaderImpl& impl,
                               std::span<const uint8_t> data) {
    if (data.size() < 20) {
        return std::unexpected(
            make_error(ErrorCode::CorruptHeader, "VQA file too small"));
    }

    SpanReader r(data);

    // FORM chunk
    auto form_tag = r.read_u32();
    if (!form_tag || *form_tag != read_u32(
            reinterpret_cast<const uint8_t*>("FORM"))) {
        return std::unexpected(
            make_error(ErrorCode::InvalidFormat, "Not a VQA file (no FORM)"));
    }

    r.skip(4);  // FORM size

    auto wvqa_tag = r.read_u32();
    if (!wvqa_tag || *wvqa_tag != read_u32(
            reinterpret_cast<const uint8_t*>("WVQA"))) {
        return std::unexpected(
            make_error(ErrorCode::InvalidFormat, "Not a VQA file (no WVQA)"));
    }

    // VQHD chunk
    auto vqhd_tag = r.read_u32();
    if (!vqhd_tag || *vqhd_tag != read_u32(
            reinterpret_cast<const uint8_t*>("VQHD"))) {
        return std::unexpected(
            make_error(ErrorCode::InvalidFormat, "Missing VQHD chunk"));
    }

    auto vqhd_size = r.read_u32be();
    if (!vqhd_size) {
        return std::unexpected(vqhd_size.error());
    }

    auto result = parse_vqhd(impl.info.header, r);
    if (!result) return result;

    // Fill in audio info from header
    impl.info.audio.sample_rate = impl.info.header.sample_rate;
    impl.info.audio.channels = impl.info.header.channels;
    impl.info.audio.bits = impl.info.header.bits;
    impl.info.audio.has_audio = (impl.info.header.channels > 0);

    // Scan for audio chunk type
    scan_audio_chunks(impl.info.audio, data);

    impl.info.file_size = data.size();

    return {};
}

Result<std::unique_ptr<VqaReader>> VqaReader::open(const std::string& path) {
    auto data = load_file(path);
    if (!data) return std::unexpected(data.error());

    auto reader = std::unique_ptr<VqaReader>(new VqaReader());
    reader->impl_->data = std::move(*data);

    auto result = parse_vqa(*reader->impl_,
                            std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

Result<std::unique_ptr<VqaReader>> VqaReader::open(
    std::span<const uint8_t> data)
{
    auto reader = std::unique_ptr<VqaReader>(new VqaReader());
    reader->impl_->data.assign(data.begin(), data.end());

    auto result = parse_vqa(*reader->impl_,
                            std::span(reader->impl_->data));
    if (!result) return std::unexpected(result.error());

    return reader;
}

} // namespace wwd
