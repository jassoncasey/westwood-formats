#include <westwood/vqa.h>
#include <westwood/io.h>
#include <westwood/lcw.h>

#include <algorithm>

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
    // HiColor is indicated by flag 0x10 OR colors == 0
    const auto& hdr = impl_->info.header;
    return (hdr.flags & 0x10) != 0 || hdr.colors == 0;
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
            audio.codec_id = 0;  // Raw PCM
            break;
        }
        if (t == read_u32(reinterpret_cast<const uint8_t*>("SND1"))) {
            audio.has_audio = true;
            audio.compressed = true;
            audio.codec_id = 1;  // Westwood ADPCM
            break;
        }
        if (t == read_u32(reinterpret_cast<const uint8_t*>("SND2"))) {
            audio.has_audio = true;
            audio.compressed = true;
            audio.codec_id = 2;  // IMA ADPCM
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
    // V1 files may have 0 values that need defaults
    impl.info.audio.sample_rate = impl.info.header.sample_rate;
    impl.info.audio.channels = impl.info.header.channels;
    impl.info.audio.bits = impl.info.header.bits;

    // Apply V1 defaults if needed
    if (impl.info.header.version == 1) {
        if (impl.info.audio.sample_rate == 0) {
            impl.info.audio.sample_rate = 22050;
        }
        if (impl.info.audio.channels == 0) {
            impl.info.audio.channels = 1;  // mono
        }
        if (impl.info.audio.bits == 0) {
            impl.info.audio.bits = 8;
        }
    }

    impl.info.audio.has_audio = (impl.info.audio.channels > 0 ||
                                  impl.info.header.flags & 0x01);

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

// IMA ADPCM step table
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static int16_t ima_decode_sample(uint8_t nibble, int16_t& predictor, int& step_index) {
    int step = ima_step_table[step_index];
    int diff = step >> 3;

    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    int new_pred = predictor + diff;
    if (new_pred < -32768) new_pred = -32768;
    if (new_pred > 32767) new_pred = 32767;
    predictor = static_cast<int16_t>(new_pred);

    int new_idx = step_index + ima_index_table[nibble];
    if (new_idx < 0) new_idx = 0;
    if (new_idx > 88) new_idx = 88;
    step_index = new_idx;

    return predictor;
}

// Westwood ADPCM index adjustment table (uses only lower 3 bits)
static const int8_t ws_index_adjust[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

// Decode Westwood ADPCM chunk (SND1) - used in VQA v1 files
static void decode_westwood_adpcm(const uint8_t* src, size_t src_size,
                                   std::vector<int16_t>& samples) {
    if (src_size < 4) return;

    size_t pos = 0;
    int sample = 0;
    int index = 0;

    while (pos < src_size) {
        uint8_t count = src[pos++];

        if (count & 0x80) {
            // Delta encoded block
            count &= 0x7F;
            if (count == 0) {
                // Large count follows in next byte
                if (pos >= src_size) break;
                count = src[pos++];
                if (count == 0) continue;
            }

            // Read delta samples
            for (int i = 0; i < count && pos < src_size; ++i) {
                uint8_t delta = src[pos++];

                // 4-bit deltas (two per byte)
                for (int n = 0; n < 2; ++n) {
                    int nibble = (n == 0) ? (delta & 0x0F) : ((delta >> 4) & 0x0F);

                    // Apply step using IMA step table
                    int step = ima_step_table[index];
                    int diff = 0;

                    if (nibble & 4) diff += step;
                    if (nibble & 2) diff += step >> 1;
                    if (nibble & 1) diff += step >> 2;
                    diff += step >> 3;

                    if (nibble & 8) sample -= diff;
                    else sample += diff;

                    if (sample < -32768) sample = -32768;
                    if (sample > 32767) sample = 32767;
                    samples.push_back(static_cast<int16_t>(sample));

                    index += ws_index_adjust[nibble & 0x07];
                    if (index < 0) index = 0;
                    if (index > 88) index = 88;
                }
            }
        } else {
            // Raw sample block
            if (count == 0) continue;

            for (int i = 0; i < count && pos < src_size; ++i) {
                // 8-bit unsigned to 16-bit signed
                uint8_t raw = src[pos++];
                sample = (static_cast<int>(raw) - 128) << 8;
                samples.push_back(static_cast<int16_t>(sample));
            }
            index = 0;
        }
    }
}

// Helper to make tag from string
static uint32_t make_tag(const char* s) {
    return read_u32(reinterpret_cast<const uint8_t*>(s));
}

Result<std::vector<VqaFrame>> VqaReader::decode_video() const {
    const auto& hdr = impl_->info.header;
    std::vector<VqaFrame> frames;
    frames.reserve(hdr.frame_count);

    // Current frame buffer (palette indices or RGB555)
    std::vector<uint8_t> frame_buffer(hdr.width * hdr.height * 3, 0);

    // Codebook (vector table) - each block is block_w * block_h bytes (or *2 for hicolor)
    size_t block_size = static_cast<size_t>(hdr.block_w) * hdr.block_h;
    bool hicolor = is_hicolor();
    if (hicolor) block_size *= 2;

    std::vector<uint8_t> codebook(static_cast<size_t>(hdr.max_blocks) * block_size, 0);

    // Palette (for indexed color)
    std::array<Color, 256> palette{};

    // Parse chunks
    SpanReader r(impl_->data);
    r.seek(12);  // Skip FORM header

    // Skip to first chunk after VQHD
    while (r.remaining() >= 8) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;

        if (*tag == make_tag("VQHD")) {
            // Skip header (already parsed)
            r.skip(*size + (*size & 1));
            break;
        }
        r.skip(*size + (*size & 1));
    }

    // Current frame index
    int frame_idx = 0;
    int blocks_x = hdr.width / hdr.block_w;
    int blocks_y = hdr.height / hdr.block_h;

    // Process chunks
    while (r.remaining() >= 8 && frame_idx < hdr.frame_count) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;

        size_t chunk_start = r.pos();
        uint32_t t = *tag;

        if (t == make_tag("FINF")) {
            // Frame info - skip
        }
        else if (t == make_tag("VQFR") || t == make_tag("VQFL")) {
            // V3 container chunks - contains sub-chunks
            // Don't skip the data, just continue parsing sub-chunks inside
            continue;
        }
        else if (t == make_tag("CBF0") || t == make_tag("CBFZ")) {
            // Full codebook (uncompressed or LCW)
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data) {
                if (t == make_tag("CBFZ")) {
                    // LCW compressed codebook
                    auto decomp = lcw_decompress(*chunk_data, codebook.size());
                    if (decomp) {
                        size_t copy_size = std::min(decomp->size(), codebook.size());
                        std::memcpy(codebook.data(), decomp->data(), copy_size);
                    }
                } else {
                    // Uncompressed codebook
                    size_t copy_size = std::min(chunk_data->size(), codebook.size());
                    std::memcpy(codebook.data(), chunk_data->data(), copy_size);
                }
            }
        }
        else if (t == make_tag("CBP0") || t == make_tag("CBPZ")) {
            // Partial codebook update
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data && chunk_data->size() >= 4) {
                // First 4 bytes: offset into codebook (little-endian)
                uint32_t cb_offset = (*chunk_data)[0] |
                                     ((*chunk_data)[1] << 8) |
                                     ((*chunk_data)[2] << 16) |
                                     ((*chunk_data)[3] << 24);

                if (t == make_tag("CBPZ")) {
                    // LCW compressed partial codebook
                    std::span<const uint8_t> compressed(
                        chunk_data->data() + 4, chunk_data->size() - 4);
                    size_t decomp_size = codebook.size() - cb_offset;
                    auto decomp = lcw_decompress(compressed, decomp_size);
                    if (decomp && cb_offset < codebook.size()) {
                        size_t copy_size = std::min(decomp->size(),
                                                    codebook.size() - cb_offset);
                        std::memcpy(codebook.data() + cb_offset,
                                    decomp->data(), copy_size);
                    }
                } else {
                    // Uncompressed partial codebook (CBP0)
                    if (cb_offset < codebook.size()) {
                        size_t copy_size = std::min(chunk_data->size() - 4,
                                                    codebook.size() - cb_offset);
                        std::memcpy(codebook.data() + cb_offset,
                                    chunk_data->data() + 4, copy_size);
                    }
                }
            }
        }
        else if (t == make_tag("CPL0")) {
            // Palette (uncompressed)
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data && chunk_data->size() >= 768) {
                for (int i = 0; i < 256; ++i) {
                    uint8_t r_val = (*chunk_data)[i * 3];
                    uint8_t g_val = (*chunk_data)[i * 3 + 1];
                    uint8_t b_val = (*chunk_data)[i * 3 + 2];
                    // 6-bit to 8-bit
                    palette[i] = {
                        static_cast<uint8_t>((r_val << 2) | (r_val >> 4)),
                        static_cast<uint8_t>((g_val << 2) | (g_val >> 4)),
                        static_cast<uint8_t>((b_val << 2) | (b_val >> 4))
                    };
                }
            }
        }
        else if (t == make_tag("CPLZ")) {
            // Palette (LCW compressed)
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data) {
                // Decompress to 768 bytes (256 colors * 3 bytes each)
                auto decomp = lcw_decompress(*chunk_data, 768);
                if (decomp && decomp->size() >= 768) {
                    for (int i = 0; i < 256; ++i) {
                        uint8_t r_val = (*decomp)[i * 3];
                        uint8_t g_val = (*decomp)[i * 3 + 1];
                        uint8_t b_val = (*decomp)[i * 3 + 2];
                        // 6-bit to 8-bit
                        palette[i] = {
                            static_cast<uint8_t>((r_val << 2) | (r_val >> 4)),
                            static_cast<uint8_t>((g_val << 2) | (g_val >> 4)),
                            static_cast<uint8_t>((b_val << 2) | (b_val >> 4))
                        };
                    }
                }
            }
        }
        else if (t == make_tag("VPT0") || t == make_tag("VPTZ") ||
                 t == make_tag("VPTR") || t == make_tag("VPRZ")) {
            // Vector pointer table - defines which codebook entry goes where
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data) {
                // Decompress if VPTZ or VPRZ (LCW compressed)
                std::vector<uint8_t> decompressed;
                std::span<const uint8_t> vpt_data;

                if (t == make_tag("VPTZ") || t == make_tag("VPRZ")) {
                    // LCW compressed - calculate expected size
                    // V1 uses 2 bytes per block, V2 uses 1 byte (or 2 for hicolor)
                    size_t vpt_size = static_cast<size_t>(blocks_x) * blocks_y;
                    if (hdr.version == 1 || hicolor) vpt_size *= 2;
                    auto decomp = lcw_decompress(*chunk_data, vpt_size);
                    if (decomp) {
                        decompressed = std::move(*decomp);
                        vpt_data = std::span<const uint8_t>(decompressed);
                    } else {
                        // Decompression failed, skip this chunk
                        continue;
                    }
                } else {
                    // Uncompressed (VPT0 or VPTR)
                    vpt_data = std::span<const uint8_t>(*chunk_data);
                }

                // Each byte/word is an index into the codebook
                // V1: 16-bit values, index = (HiVal*256+LoVal)/8, HiVal=0xff for uniform
                // V2: single bytes (or 16-bit for hicolor)
                size_t vpt_idx = 0;
                bool is_v1 = (hdr.version == 1);
                for (int by = 0; by < blocks_y && vpt_idx < vpt_data.size(); ++by) {
                    for (int bx = 0; bx < blocks_x && vpt_idx < vpt_data.size(); ++bx) {
                        uint16_t cb_idx;
                        bool uniform_block = false;
                        uint8_t uniform_color = 0;

                        if (is_v1 && vpt_idx + 1 < vpt_data.size()) {
                            // V1: uses 16-bit values with special encoding
                            uint8_t lo_val = vpt_data[vpt_idx];
                            uint8_t hi_val = vpt_data[vpt_idx + 1];
                            vpt_idx += 2;

                            if (hi_val == 0xff) {
                                // Uniform color block - lo_val is the palette index
                                uniform_block = true;
                                uniform_color = lo_val;
                                cb_idx = 0;  // Not used
                            } else {
                                // Block index = (HiVal*256+LoVal)/8
                                cb_idx = static_cast<uint16_t>((hi_val * 256 + lo_val) / 8);
                            }
                        } else if (hicolor && vpt_idx + 1 < vpt_data.size()) {
                            // V2 HiColor: 16-bit index
                            cb_idx = vpt_data[vpt_idx] | (vpt_data[vpt_idx + 1] << 8);
                            vpt_idx += 2;
                        } else {
                            // V2 indexed: single byte
                            cb_idx = vpt_data[vpt_idx++];
                        }

                        if (!uniform_block && cb_idx >= hdr.max_blocks) continue;

                        // Copy block from codebook to frame (or fill with uniform color)
                        const uint8_t* cb_block = uniform_block ? nullptr :
                            codebook.data() + cb_idx * block_size;

                        for (int py = 0; py < hdr.block_h; ++py) {
                            for (int px = 0; px < hdr.block_w; ++px) {
                                int fx = bx * hdr.block_w + px;
                                int fy = by * hdr.block_h + py;
                                if (fx >= hdr.width || fy >= hdr.height) continue;

                                size_t dst_idx = (fy * hdr.width + fx) * 3;

                                if (uniform_block) {
                                    // V1 uniform color block - fill with palette color
                                    frame_buffer[dst_idx] = palette[uniform_color].r;
                                    frame_buffer[dst_idx + 1] = palette[uniform_color].g;
                                    frame_buffer[dst_idx + 2] = palette[uniform_color].b;
                                } else if (hicolor) {
                                    // RGB555
                                    size_t src_idx = (py * hdr.block_w + px) * 2;
                                    uint16_t pixel = cb_block[src_idx] | (cb_block[src_idx + 1] << 8);
                                    frame_buffer[dst_idx] = ((pixel >> 10) & 0x1F) << 3;
                                    frame_buffer[dst_idx + 1] = ((pixel >> 5) & 0x1F) << 3;
                                    frame_buffer[dst_idx + 2] = (pixel & 0x1F) << 3;
                                } else {
                                    // Indexed color
                                    size_t src_idx = py * hdr.block_w + px;
                                    uint8_t pal_idx = cb_block[src_idx];
                                    frame_buffer[dst_idx] = palette[pal_idx].r;
                                    frame_buffer[dst_idx + 1] = palette[pal_idx].g;
                                    frame_buffer[dst_idx + 2] = palette[pal_idx].b;
                                }
                            }
                        }
                    }
                }

                // Frame complete - add to output
                VqaFrame frame;
                frame.rgb = frame_buffer;
                frame.width = hdr.width;
                frame.height = hdr.height;
                frames.push_back(std::move(frame));
                frame_idx++;
            }
        }

        // Align to word boundary and skip to end of chunk
        size_t consumed = r.pos() - chunk_start;
        size_t aligned_size = *size + (*size & 1);
        if (consumed < aligned_size) {
            r.skip(aligned_size - consumed);
        }
    }

    // If we didn't get all frames, add placeholder frames
    while (frames.size() < static_cast<size_t>(hdr.frame_count)) {
        VqaFrame frame;
        frame.rgb = frame_buffer;
        frame.width = hdr.width;
        frame.height = hdr.height;
        frames.push_back(std::move(frame));
    }

    return frames;
}

Result<std::vector<int16_t>> VqaReader::decode_audio() const {
    if (!impl_->info.audio.has_audio) {
        return std::vector<int16_t>{};
    }

    std::vector<int16_t> samples;
    const auto& audio = impl_->info.audio;

    // Parse chunks looking for SND0/SND1/SND2
    SpanReader r(impl_->data);
    r.seek(12);  // Skip FORM header

    int16_t predictor_l = 0, predictor_r = 0;
    int step_index_l = 0, step_index_r = 0;

    while (r.remaining() >= 8) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;

        uint32_t t = *tag;

        if (t == make_tag("SND0")) {
            // Uncompressed PCM
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data) {
                if (audio.bits == 16) {
                    for (size_t i = 0; i + 1 < chunk_data->size(); i += 2) {
                        int16_t sample = static_cast<int16_t>((*chunk_data)[i] | ((*chunk_data)[i + 1] << 8));
                        samples.push_back(sample);
                    }
                } else {
                    // 8-bit unsigned to 16-bit signed
                    for (size_t i = 0; i < chunk_data->size(); ++i) {
                        int16_t sample = (static_cast<int16_t>((*chunk_data)[i]) - 128) << 8;
                        samples.push_back(sample);
                    }
                }
            }
            // Skip padding byte if chunk size is odd (IFF alignment)
            if (*size & 1) r.skip(1);
        }
        else if (t == make_tag("SND1")) {
            // Westwood ADPCM compressed (VQA v1)
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data && chunk_data->size() > 0) {
                decode_westwood_adpcm(chunk_data->data(), chunk_data->size(), samples);
            }
            // Skip padding byte if chunk size is odd (IFF alignment)
            if (*size & 1) r.skip(1);
        }
        else if (t == make_tag("SND2")) {
            // IMA ADPCM compressed
            auto chunk_data = r.read_bytes(*size);
            if (chunk_data && chunk_data->size() >= 4) {
                const uint8_t* src = chunk_data->data();
                size_t src_size = chunk_data->size();

                // First 4 bytes: initial predictor and step index for each channel
                if (audio.channels == 2 && src_size >= 8) {
                    predictor_l = static_cast<int16_t>(src[0] | (src[1] << 8));
                    step_index_l = src[2];
                    predictor_r = static_cast<int16_t>(src[4] | (src[5] << 8));
                    step_index_r = src[6];
                    src += 8;
                    src_size -= 8;
                } else if (src_size >= 4) {
                    predictor_l = static_cast<int16_t>(src[0] | (src[1] << 8));
                    step_index_l = src[2];
                    src += 4;
                    src_size -= 4;
                }

                // Decode IMA ADPCM
                if (audio.channels == 2) {
                    // Stereo: alternating samples
                    for (size_t i = 0; i < src_size; ++i) {
                        uint8_t byte = src[i];
                        // Each byte = 2 samples
                        samples.push_back(ima_decode_sample(byte & 0x0F, predictor_l, step_index_l));
                        samples.push_back(ima_decode_sample(byte >> 4, predictor_r, step_index_r));
                    }
                } else {
                    // Mono
                    for (size_t i = 0; i < src_size; ++i) {
                        uint8_t byte = src[i];
                        samples.push_back(ima_decode_sample(byte & 0x0F, predictor_l, step_index_l));
                        samples.push_back(ima_decode_sample(byte >> 4, predictor_l, step_index_l));
                    }
                }
            }
            // Skip padding byte if chunk size is odd (IFF alignment)
            if (*size & 1) r.skip(1);
        }
        else {
            // Skip other chunks (including padding)
            r.skip(*size + (*size & 1));
        }
    }

    return samples;
}

} // namespace wwd
