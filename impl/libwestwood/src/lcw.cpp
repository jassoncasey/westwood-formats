#include <westwood/lcw.h>
#include <westwood/io.h>

#include <algorithm>
#include <cstring>

namespace wwd {
namespace {

// Copy with overlap handling (for back-references)
void copy_overlap(uint8_t* dst, const uint8_t* src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

} // namespace

// LCW decode state for cleaner function signatures
struct LcwState {
    const uint8_t* src;
    const uint8_t* src_end;
    uint8_t* dst;
    uint8_t* dst_start;
    uint8_t* dst_end;
    bool relative;
};

// Handle short relative copy (0x00-0x7F)
Result<void> lcw_short_copy(LcwState& st, uint8_t cmd) {
    size_t count = ((cmd & 0x70) >> 4) + 3;
    if (st.src >= st.src_end)
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "LCW short"));
    size_t off = ((cmd & 0x0F) << 8) | *st.src++;
    if (off == 0 || st.dst - off < st.dst_start)
        return std::unexpected(
            make_error(ErrorCode::CorruptData, "LCW short off"));
    if (st.dst + count > st.dst_end)
        return std::unexpected(
            make_error(ErrorCode::OutputOverflow, "LCW short"));
    copy_overlap(st.dst, st.dst - off, count);
    st.dst += count;
    return {};
}

// Handle literal bytes (0x80-0xBF), returns true if end marker
Result<bool> lcw_literal(LcwState& st, uint8_t cmd) {
    size_t count = cmd & 0x3F;
    if (count == 0) return true;  // End marker
    if (st.src + count > st.src_end)
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "LCW lit"));
    if (st.dst + count > st.dst_end)
        return std::unexpected(
            make_error(ErrorCode::OutputOverflow, "LCW lit"));
    std::memcpy(st.dst, st.src, count);
    st.src += count;
    st.dst += count;
    return false;
}

// Resolve copy source pointer
Result<const uint8_t*> lcw_resolve_src(
    const LcwState& st, uint16_t pos, const char* ctx)
{
    if (st.relative) {
        if (pos == 0 || st.dst - pos < st.dst_start)
            return std::unexpected(make_error(ErrorCode::CorruptData, ctx));
        return st.dst - pos;
    }
    if (st.dst_start + pos > st.dst)
        return std::unexpected(make_error(ErrorCode::CorruptData, ctx));
    return st.dst_start + pos;
}

// Handle medium copy (0xC0-0xFD)
Result<void> lcw_medium_copy(LcwState& st, uint8_t cmd) {
    size_t count = (cmd & 0x3F) + 3;
    if (st.src + 2 > st.src_end)
        return std::unexpected(make_error(ErrorCode::UnexpectedEof, "LCW med"));
    uint16_t pos = read_u16(st.src);
    st.src += 2;
    auto cs = lcw_resolve_src(st, pos, "LCW med pos");
    if (!cs) return std::unexpected(cs.error());
    if (st.dst + count > st.dst_end)
        return std::unexpected(
            make_error(ErrorCode::OutputOverflow, "LCW med"));
    copy_overlap(st.dst, *cs, count);
    st.dst += count;
    return {};
}

// Handle long fill (0xFE)
Result<void> lcw_long_fill(LcwState& st) {
    if (st.src + 3 > st.src_end)
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "LCW fill"));
    size_t count = read_u16(st.src);
    st.src += 2;
    uint8_t value = *st.src++;
    if (st.dst + count > st.dst_end)
        return std::unexpected(
            make_error(ErrorCode::OutputOverflow, "LCW fill"));
    std::memset(st.dst, value, count);
    st.dst += count;
    return {};
}

// Handle long copy (0xFF)
Result<void> lcw_long_copy(LcwState& st) {
    if (st.src + 4 > st.src_end)
        return std::unexpected(
            make_error(ErrorCode::UnexpectedEof, "LCW long"));
    size_t count = read_u16(st.src);
    st.src += 2;
    uint16_t pos = read_u16(st.src);
    st.src += 2;
    auto cs = lcw_resolve_src(st, pos, "LCW long pos");
    if (!cs) return std::unexpected(cs.error());
    if (st.dst + count > st.dst_end)
        return std::unexpected(
            make_error(ErrorCode::OutputOverflow, "LCW long"));
    copy_overlap(st.dst, *cs, count);
    st.dst += count;
    return {};
}

// LCW / Format80 decompression
Result<size_t> lcw_decompress(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    bool relative)
{
    if (input.empty())
        return std::unexpected(make_error(ErrorCode::DecompressError, "Empty"));

    LcwState st{input.data(), input.data() + input.size(),
                output.data(), output.data(), output.data() + output.size(),
                relative};

    if (st.src < st.src_end && *st.src == 0x00) {
        st.relative = true;
        st.src++;
    }

    while (st.src < st.src_end) {
        uint8_t cmd = *st.src++;
        Result<void> r;

        if (cmd < 0x80) {
            r = lcw_short_copy(st, cmd);
        } else if (cmd < 0xC0) {
            auto lit = lcw_literal(st, cmd);
            if (!lit) return std::unexpected(lit.error());
            if (*lit) break;  // End marker
            continue;
        } else if (cmd < 0xFE) {
            r = lcw_medium_copy(st, cmd);
        } else if (cmd == 0xFE) {
            r = lcw_long_fill(st);
        } else {
            r = lcw_long_copy(st);
        }
        if (!r) return std::unexpected(r.error());
    }

    return static_cast<size_t>(st.dst - st.dst_start);
}

Result<std::vector<uint8_t>> lcw_decompress(
    std::span<const uint8_t> input,
    size_t output_size,
    bool relative)
{
    std::vector<uint8_t> output(output_size);
    auto result = lcw_decompress(input, std::span(output), relative);
    if (!result) {
        return std::unexpected(result.error());
    }
    output.resize(*result);
    return output;
}

// Format40 / XOR Delta decompression
//
// Command encoding (from EA/Westwood XORDELTA.ASM):
//
// 0x00: SHORTRUN - XOR fill
//   Next two bytes: count, value
//   XOR next 'count' bytes with 'value'
//
// 0x01-0x7F: SHORTDUMP - XOR copy
//   XOR next 'cmd' bytes from source
//
// 0x80: Long command - read uint16
//   If uint16 == 0: END marker
//   Else if bit15 == 0: LONGSKIP - skip (word & 0x7FFF) bytes
//   Else if bit14 == 0: LONGDUMP - XOR next (word & 0x3FFF) bytes from source
//   Else: LONGRUN - XOR (word & 0x3FFF) bytes with next byte value
//
// 0x81-0xFF: SHORTSKIP - skip (cmd & 0x7F) bytes

Result<size_t> format40_decompress(
    std::span<const uint8_t> input,
    std::span<uint8_t> buffer)
{
    if (input.empty()) {
        return buffer.size();
    }

    const uint8_t* src = input.data();
    const uint8_t* src_end = src + input.size();
    uint8_t* dst = buffer.data();
    uint8_t* dst_end = dst + buffer.size();

    while (src < src_end && dst < dst_end) {
        uint8_t cmd = *src++;

        if (cmd == 0x00) {
            // SHORTRUN: XOR fill
            if (src + 2 > src_end) break;
            uint8_t count = *src++;
            uint8_t value = *src++;
            for (uint8_t i = 0; i < count && dst < dst_end; ++i) {
                *dst++ ^= value;
            }
        }
        else if (cmd < 0x80) {
            // SHORTDUMP: XOR copy next 'cmd' bytes
            for (uint8_t i = 0; i < cmd; ++i) {
                if (src >= src_end || dst >= dst_end) break;
                *dst++ ^= *src++;
            }
        }
        else if (cmd == 0x80) {
            // Long command - read uint16
            if (src + 2 > src_end) break;
            uint16_t word = read_u16(src);
            src += 2;

            if (word == 0) {
                // END marker
                break;
            }
            else if ((word & 0x8000) == 0) {
                // LONGSKIP: skip (word & 0x7FFF) bytes
                size_t skip = word & 0x7FFF;
                if (dst + skip > dst_end) {
                    dst = dst_end;  // Clamp to end
                } else {
                    dst += skip;
                }
            }
            else if ((word & 0x4000) == 0) {
                // LONGDUMP: XOR next (word & 0x3FFF) bytes
                size_t count = word & 0x3FFF;
                for (size_t i = 0; i < count; ++i) {
                    if (src >= src_end || dst >= dst_end) break;
                    *dst++ ^= *src++;
                }
            }
            else {
                // LONGRUN: XOR (word & 0x3FFF) bytes with value
                if (src >= src_end) break;
                size_t count = word & 0x3FFF;
                uint8_t value = *src++;
                for (size_t i = 0; i < count && dst < dst_end; ++i) {
                    *dst++ ^= value;
                }
            }
        }
        else {
            // SHORTSKIP: skip (cmd & 0x7F) bytes
            size_t skip = cmd & 0x7F;
            if (dst + skip > dst_end) {
                dst = dst_end;  // Clamp to end
            } else {
                dst += skip;
            }
        }
    }

    return buffer.size();
}

} // namespace wwd
