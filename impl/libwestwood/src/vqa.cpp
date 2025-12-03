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

// Read u16 into field, return error on failure
static Result<void> read_field_u16(SpanReader& r, uint16_t& field) {
    auto v = r.read_u16();
    if (!v) return std::unexpected(v.error());
    field = *v;
    return {};
}

// Read u8 into field, return error on failure
static Result<void> read_field_u8(SpanReader& r, uint8_t& field) {
    auto v = r.read_u8();
    if (!v) return std::unexpected(v.error());
    field = *v;
    return {};
}

// Read video header fields (version through max_blocks)
static Result<void> read_vqhd_video(SpanReader& r, VqaHeader& hdr) {
    Result<void> res;
    if (!(res = read_field_u16(r, hdr.version))) return res;
    if (!(res = read_field_u16(r, hdr.flags))) return res;
    if (!(res = read_field_u16(r, hdr.frame_count))) return res;
    if (!(res = read_field_u16(r, hdr.width))) return res;
    if (!(res = read_field_u16(r, hdr.height))) return res;
    if (!(res = read_field_u8(r, hdr.block_w))) return res;
    if (!(res = read_field_u8(r, hdr.block_h))) return res;
    if (!(res = read_field_u8(r, hdr.frame_rate))) return res;
    if (!(res = read_field_u8(r, hdr.cb_parts))) return res;
    if (!(res = read_field_u16(r, hdr.colors))) return res;
    if (!(res = read_field_u16(r, hdr.max_blocks))) return res;
    return {};
}

// Read offset and audio fields
static Result<void> read_vqhd_audio(SpanReader& r, VqaHeader& hdr) {
    Result<void> res;
    if (!(res = read_field_u16(r, hdr.offset_x))) return res;
    if (!(res = read_field_u16(r, hdr.offset_y))) return res;
    if (!(res = read_field_u16(r, hdr.max_vpt_size))) return res;
    if (!(res = read_field_u16(r, hdr.sample_rate))) return res;
    if (!(res = read_field_u8(r, hdr.channels))) return res;
    if (!(res = read_field_u8(r, hdr.bits))) return res;
    return {};
}

static Result<void> parse_vqhd(VqaHeader& hdr, SpanReader& r) {
    auto res = read_vqhd_video(r, hdr);
    if (!res) return res;
    return read_vqhd_audio(r, hdr);
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
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767
};

static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static int16_t ima_decode_sample(
    uint8_t nibble, int16_t& predictor, int& step_index)
{
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
                    int nibble = (n == 0)
                        ? (delta & 0x0F) : ((delta >> 4) & 0x0F);

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

// Process full codebook chunk (CBF0/CBFZ)
static void process_codebook_full(
    uint32_t tag,
    std::span<const uint8_t> chunk_data,
    std::vector<uint8_t>& codebook)
{
    if (tag == make_tag("CBFZ")) {
        auto decomp = lcw_decompress(chunk_data, codebook.size());
        if (decomp) {
            size_t n = std::min(decomp->size(), codebook.size());
            std::memcpy(codebook.data(), decomp->data(), n);
        }
    } else {
        size_t n = std::min(chunk_data.size(), codebook.size());
        std::memcpy(codebook.data(), chunk_data.data(), n);
    }
}

// Process partial codebook chunk (CBP0/CBPZ)
static void process_codebook_partial(
    uint32_t tag,
    std::span<const uint8_t> chunk_data,
    std::vector<uint8_t>& codebook)
{
    if (chunk_data.size() < 4) return;

    uint32_t offset = chunk_data[0] | (chunk_data[1] << 8) |
                      (chunk_data[2] << 16) | (chunk_data[3] << 24);
    if (offset >= codebook.size()) return;

    auto payload = chunk_data.subspan(4);

    if (tag == make_tag("CBPZ")) {
        size_t decomp_size = codebook.size() - offset;
        auto decomp = lcw_decompress(payload, decomp_size);
        if (decomp) {
            size_t n = std::min(decomp->size(), codebook.size() - offset);
            std::memcpy(codebook.data() + offset, decomp->data(), n);
        }
    } else {
        size_t n = std::min(payload.size(), codebook.size() - offset);
        std::memcpy(codebook.data() + offset, payload.data(), n);
    }
}

// Convert 6-bit palette to 8-bit Color array
static void convert_palette_6to8(
    std::span<const uint8_t> src,
    std::array<Color, 256>& palette)
{
    int sz = static_cast<int>(src.size());
    for (int i = 0; i < 256 && (i * 3 + 2) < sz; ++i) {
        uint8_t r = src[i * 3];
        uint8_t g = src[i * 3 + 1];
        uint8_t b = src[i * 3 + 2];
        palette[i] = {
            static_cast<uint8_t>((r << 2) | (r >> 4)),
            static_cast<uint8_t>((g << 2) | (g >> 4)),
            static_cast<uint8_t>((b << 2) | (b >> 4))
        };
    }
}

// Process palette chunk (CPL0/CPLZ)
static void process_palette(
    uint32_t tag,
    std::span<const uint8_t> chunk_data,
    std::array<Color, 256>& palette)
{
    if (tag == make_tag("CPLZ")) {
        auto decomp = lcw_decompress(chunk_data, 768);
        if (decomp && decomp->size() >= 768) {
            convert_palette_6to8(*decomp, palette);
        }
    } else if (chunk_data.size() >= 768) {
        convert_palette_6to8(chunk_data, palette);
    }
}

// Render a single block to frame buffer (uniform color)
static void render_block_uniform(
    int bx, int by,
    const VqaHeader& hdr,
    uint8_t color,
    const std::array<Color, 256>& palette,
    std::vector<uint8_t>& frame_buffer)
{
    for (int py = 0; py < hdr.block_h; ++py) {
        for (int px = 0; px < hdr.block_w; ++px) {
            int fx = bx * hdr.block_w + px;
            int fy = by * hdr.block_h + py;
            if (fx >= hdr.width || fy >= hdr.height) continue;
            size_t dst = (fy * hdr.width + fx) * 3;
            frame_buffer[dst] = palette[color].r;
            frame_buffer[dst + 1] = palette[color].g;
            frame_buffer[dst + 2] = palette[color].b;
        }
    }
}

// Render a single block to frame buffer (indexed color)
static void render_block_indexed(
    int bx, int by,
    const VqaHeader& hdr,
    const uint8_t* cb_block,
    const std::array<Color, 256>& palette,
    std::vector<uint8_t>& frame_buffer)
{
    for (int py = 0; py < hdr.block_h; ++py) {
        for (int px = 0; px < hdr.block_w; ++px) {
            int fx = bx * hdr.block_w + px;
            int fy = by * hdr.block_h + py;
            if (fx >= hdr.width || fy >= hdr.height) continue;
            size_t dst = (fy * hdr.width + fx) * 3;
            size_t src = py * hdr.block_w + px;
            uint8_t idx = cb_block[src];
            frame_buffer[dst] = palette[idx].r;
            frame_buffer[dst + 1] = palette[idx].g;
            frame_buffer[dst + 2] = palette[idx].b;
        }
    }
}

// Render a single block to frame buffer (hicolor RGB555)
static void render_block_hicolor(
    int bx, int by,
    const VqaHeader& hdr,
    const uint8_t* cb_block,
    std::vector<uint8_t>& frame_buffer)
{
    for (int py = 0; py < hdr.block_h; ++py) {
        for (int px = 0; px < hdr.block_w; ++px) {
            int fx = bx * hdr.block_w + px;
            int fy = by * hdr.block_h + py;
            if (fx >= hdr.width || fy >= hdr.height) continue;
            size_t dst = (fy * hdr.width + fx) * 3;
            size_t src = (py * hdr.block_w + px) * 2;
            uint16_t pixel = cb_block[src] | (cb_block[src + 1] << 8);
            frame_buffer[dst] = ((pixel >> 10) & 0x1F) << 3;
            frame_buffer[dst + 1] = ((pixel >> 5) & 0x1F) << 3;
            frame_buffer[dst + 2] = (pixel & 0x1F) << 3;
        }
    }
}

// Decode V1 VPT entry (16-bit with special encoding)
struct VptEntry {
    bool uniform;
    uint8_t color;
    uint16_t cb_idx;
};

static VptEntry decode_vpt_v1(uint8_t lo, uint8_t hi) {
    VptEntry e{};
    if (hi == 0xff) {
        e.uniform = true;
        e.color = lo;
    } else {
        e.uniform = false;
        e.cb_idx = static_cast<uint16_t>((hi * 256 + lo) / 8);
    }
    return e;
}

// Process a single V1 block from VPT
static size_t process_block_v1(
    std::span<const uint8_t> vpt,
    size_t idx,
    int bx, int by,
    const VqaHeader& hdr,
    size_t block_size,
    const std::vector<uint8_t>& codebook,
    const std::array<Color, 256>& palette,
    std::vector<uint8_t>& fb)
{
    if (idx + 1 >= vpt.size()) return idx;
    auto e = decode_vpt_v1(vpt[idx], vpt[idx + 1]);
    if (e.uniform) {
        render_block_uniform(bx, by, hdr, e.color, palette, fb);
    } else if (e.cb_idx < hdr.max_blocks) {
        const uint8_t* cb = codebook.data() + e.cb_idx * block_size;
        render_block_indexed(bx, by, hdr, cb, palette, fb);
    }
    return idx + 2;
}

// Process a single V2 hicolor block from VPT
static size_t process_block_hicolor(
    std::span<const uint8_t> vpt,
    size_t idx,
    int bx, int by,
    const VqaHeader& hdr,
    size_t block_size,
    const std::vector<uint8_t>& codebook,
    std::vector<uint8_t>& fb)
{
    if (idx + 1 >= vpt.size()) return idx;
    uint16_t cb_idx = vpt[idx] | (vpt[idx + 1] << 8);
    if (cb_idx < hdr.max_blocks) {
        const uint8_t* cb = codebook.data() + cb_idx * block_size;
        render_block_hicolor(bx, by, hdr, cb, fb);
    }
    return idx + 2;
}

// Process a single V2 indexed block from VPT
static size_t process_block_indexed(
    std::span<const uint8_t> vpt,
    size_t idx,
    int bx, int by,
    const VqaHeader& hdr,
    size_t block_size,
    const std::vector<uint8_t>& codebook,
    const std::array<Color, 256>& palette,
    std::vector<uint8_t>& fb)
{
    if (idx >= vpt.size()) return idx;
    uint16_t cb_idx = vpt[idx];
    if (cb_idx < hdr.max_blocks) {
        const uint8_t* cb = codebook.data() + cb_idx * block_size;
        render_block_indexed(bx, by, hdr, cb, palette, fb);
    }
    return idx + 1;
}

// Check if VPT tag is compressed
static bool is_vpt_compressed(uint32_t tag) {
    return tag == make_tag("VPTZ") || tag == make_tag("VPRZ");
}

// Calculate expected VPT size
static size_t calc_vpt_size(const VqaHeader& hdr, bool hicolor) {
    int blocks_x = hdr.width / hdr.block_w;
    int blocks_y = hdr.height / hdr.block_h;
    size_t vpt_size = static_cast<size_t>(blocks_x) * blocks_y;
    if (hdr.version == 1 || hicolor) vpt_size *= 2;
    return vpt_size;
}

// Process VPT data and render all blocks for a frame
static void process_vpt_blocks(
    std::span<const uint8_t> vpt,
    const VqaHeader& hdr,
    bool hicolor,
    size_t block_size,
    const std::vector<uint8_t>& codebook,
    const std::array<Color, 256>& palette,
    std::vector<uint8_t>& fb)
{
    int blocks_x = hdr.width / hdr.block_w;
    int blocks_y = hdr.height / hdr.block_h;
    bool is_v1 = (hdr.version == 1);
    size_t idx = 0;

    for (int by = 0; by < blocks_y && idx < vpt.size(); ++by) {
        for (int bx = 0; bx < blocks_x && idx < vpt.size(); ++bx) {
            if (is_v1) {
                idx = process_block_v1(vpt, idx, bx, by, hdr,
                                       block_size, codebook, palette, fb);
            } else if (hicolor) {
                idx = process_block_hicolor(vpt, idx, bx, by, hdr,
                                            block_size, codebook, fb);
            } else {
                idx = process_block_indexed(vpt, idx, bx, by, hdr,
                                            block_size, codebook, palette, fb);
            }
        }
    }
}

// Add completed frame to output
static void emit_frame(
    const VqaHeader& hdr,
    const std::vector<uint8_t>& fb,
    std::vector<VqaFrame>& frames,
    int& frame_idx)
{
    VqaFrame frame;
    frame.rgb = fb;
    frame.width = hdr.width;
    frame.height = hdr.height;
    frames.push_back(std::move(frame));
    frame_idx++;
}

// Video decode state
struct VqaDecodeState {
    std::vector<uint8_t> frame_buffer;
    std::vector<uint8_t> codebook;
    std::array<Color, 256> palette{};
    size_t block_size;
    bool hicolor;
};

// Initialize decode state from header
static VqaDecodeState init_decode_state(const VqaHeader& hdr, bool hicolor) {
    VqaDecodeState st;
    st.frame_buffer.resize(hdr.width * hdr.height * 3, 0);
    st.block_size = static_cast<size_t>(hdr.block_w) * hdr.block_h;
    st.hicolor = hicolor;
    if (hicolor) st.block_size *= 2;
    st.codebook.resize(static_cast<size_t>(hdr.max_blocks) * st.block_size, 0);
    return st;
}

// Skip to first chunk after VQHD
static void skip_to_first_chunk(SpanReader& r) {
    r.seek(12);  // Skip FORM header
    while (r.remaining() >= 8) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;
        if (*tag == make_tag("VQHD")) {
            r.skip(*size + (*size & 1));
            break;
        }
        r.skip(*size + (*size & 1));
    }
}

// Handle VPT chunk - returns true if frame was produced
static bool handle_vpt_chunk(
    uint32_t tag,
    std::span<const uint8_t> data,
    const VqaHeader& hdr,
    VqaDecodeState& st,
    std::vector<VqaFrame>& frames,
    int& frame_idx)
{
    std::vector<uint8_t> decompressed;
    std::span<const uint8_t> vpt;

    if (is_vpt_compressed(tag)) {
        auto decomp = lcw_decompress(data, calc_vpt_size(hdr, st.hicolor));
        if (!decomp) return false;
        decompressed = std::move(*decomp);
        vpt = std::span<const uint8_t>(decompressed);
    } else {
        vpt = data;
    }

    process_vpt_blocks(vpt, hdr, st.hicolor, st.block_size,
                       st.codebook, st.palette, st.frame_buffer);
    emit_frame(hdr, st.frame_buffer, frames, frame_idx);
    return true;
}

// Check if chunk is a VPT chunk
static bool is_vpt_chunk(uint32_t t) {
    return t == make_tag("VPT0") || t == make_tag("VPTZ") ||
           t == make_tag("VPTR") || t == make_tag("VPRZ");
}

// Process a single video chunk
static bool process_video_chunk(
    uint32_t t,
    SpanReader& r,
    uint32_t size,
    const VqaHeader& hdr,
    VqaDecodeState& st,
    std::vector<VqaFrame>& frames,
    int& frame_idx)
{
    if (t == make_tag("FINF")) return true;
    if (t == make_tag("VQFR") || t == make_tag("VQFL")) return false;

    auto chunk_data = r.read_bytes(size);
    if (!chunk_data) return true;

    if (t == make_tag("CBF0") || t == make_tag("CBFZ")) {
        process_codebook_full(t, *chunk_data, st.codebook);
    } else if (t == make_tag("CBP0") || t == make_tag("CBPZ")) {
        process_codebook_partial(t, *chunk_data, st.codebook);
    } else if (t == make_tag("CPL0") || t == make_tag("CPLZ")) {
        process_palette(t, *chunk_data, st.palette);
    } else if (is_vpt_chunk(t)) {
        handle_vpt_chunk(t, *chunk_data, hdr, st, frames, frame_idx);
    }
    return true;
}

Result<std::vector<VqaFrame>> VqaReader::decode_video() const {
    const auto& hdr = impl_->info.header;
    std::vector<VqaFrame> frames;
    frames.reserve(hdr.frame_count);

    auto st = init_decode_state(hdr, is_hicolor());
    SpanReader r(impl_->data);
    skip_to_first_chunk(r);

    int frame_idx = 0;
    while (r.remaining() >= 8 && frame_idx < hdr.frame_count) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;

        size_t chunk_start = r.pos();
        bool skip = process_video_chunk(
            *tag, r, *size, hdr, st, frames, frame_idx);

        if (skip) {
            size_t consumed = r.pos() - chunk_start;
            size_t aligned = *size + (*size & 1);
            if (consumed < aligned) r.skip(aligned - consumed);
        }
    }

    while (frames.size() < static_cast<size_t>(hdr.frame_count)) {
        emit_frame(hdr, st.frame_buffer, frames, frame_idx);
    }

    return frames;
}

// Decode SND0 (uncompressed PCM) chunk
static void decode_snd0(
    std::span<const uint8_t> data,
    uint8_t bits,
    std::vector<int16_t>& out)
{
    if (bits == 16) {
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            out.push_back(static_cast<int16_t>(data[i] | (data[i + 1] << 8)));
        }
    } else {
        for (uint8_t b : data) {
            out.push_back((static_cast<int16_t>(b) - 128) << 8);
        }
    }
}

// IMA ADPCM decode state
struct ImaState {
    int16_t pred_l = 0, pred_r = 0;
    int idx_l = 0, idx_r = 0;
};

// Parse IMA ADPCM header (mono or stereo)
static std::pair<const uint8_t*, size_t> parse_ima_header(
    std::span<const uint8_t> data,
    uint8_t channels,
    ImaState& st)
{
    const uint8_t* src = data.data();
    size_t sz = data.size();
    if (channels == 2 && sz >= 8) {
        st.pred_l = static_cast<int16_t>(src[0] | (src[1] << 8));
        st.idx_l = src[2];
        st.pred_r = static_cast<int16_t>(src[4] | (src[5] << 8));
        st.idx_r = src[6];
        return {src + 8, sz - 8};
    } else if (sz >= 4) {
        st.pred_l = static_cast<int16_t>(src[0] | (src[1] << 8));
        st.idx_l = src[2];
        return {src + 4, sz - 4};
    }
    return {src, sz};
}

// Decode IMA ADPCM samples (stereo)
static void decode_ima_stereo(
    const uint8_t* src,
    size_t sz,
    ImaState& st,
    std::vector<int16_t>& out)
{
    for (size_t i = 0; i < sz; ++i) {
        uint8_t b = src[i];
        out.push_back(ima_decode_sample(b & 0x0F, st.pred_l, st.idx_l));
        out.push_back(ima_decode_sample(b >> 4, st.pred_r, st.idx_r));
    }
}

// Decode IMA ADPCM samples (mono)
static void decode_ima_mono(
    const uint8_t* src,
    size_t sz,
    ImaState& st,
    std::vector<int16_t>& out)
{
    for (size_t i = 0; i < sz; ++i) {
        uint8_t b = src[i];
        out.push_back(ima_decode_sample(b & 0x0F, st.pred_l, st.idx_l));
        out.push_back(ima_decode_sample(b >> 4, st.pred_l, st.idx_l));
    }
}

// Decode SND2 (IMA ADPCM) chunk
static void decode_snd2(
    std::span<const uint8_t> data,
    uint8_t channels,
    ImaState& st,
    std::vector<int16_t>& out)
{
    auto [src, sz] = parse_ima_header(data, channels, st);
    if (channels == 2) {
        decode_ima_stereo(src, sz, st, out);
    } else {
        decode_ima_mono(src, sz, st, out);
    }
}

Result<std::vector<int16_t>> VqaReader::decode_audio() const {
    if (!impl_->info.audio.has_audio) return std::vector<int16_t>{};

    std::vector<int16_t> samples;
    const auto& audio = impl_->info.audio;
    SpanReader r(impl_->data);
    r.seek(12);
    ImaState ima;

    while (r.remaining() >= 8) {
        auto tag = r.read_u32();
        auto size = r.read_u32be();
        if (!tag || !size) break;
        uint32_t t = *tag;

        if (t == make_tag("SND0")) {
            auto d = r.read_bytes(*size);
            if (d) decode_snd0(*d, audio.bits, samples);
        } else if (t == make_tag("SND1")) {
            auto d = r.read_bytes(*size);
            if (d && !d->empty())
                decode_westwood_adpcm(d->data(), d->size(), samples);
        } else if (t == make_tag("SND2")) {
            auto d = r.read_bytes(*size);
            if (d && d->size() >= 4)
                decode_snd2(*d, audio.channels, ima, samples);
        } else {
            r.skip(*size + (*size & 1));
            continue;
        }
        if (*size & 1) r.skip(1);
    }

    return samples;
}

} // namespace wwd
