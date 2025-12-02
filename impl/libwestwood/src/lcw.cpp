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

// LCW / Format80 decompression
//
// Command byte encoding (based on C&C source and replicant project):
//
// 0x00-0x7F: Short relative copy
//   Bits: 0cccpppp pppppppp  (2 bytes total)
//   count = ((cmd & 0x70) >> 4) + 3  (3-10 bytes)
//   pos = ((cmd & 0x0F) << 8) | next_byte  (relative back-reference)
//
// 0x80-0xBF: Literal bytes
//   Bits: 10cccccc [data...]
//   count = cmd & 0x3F  (0-63 bytes, 0 means end marker)
//   Copy count bytes from source
//
// 0xC0-0xFD: Medium copy (absolute or relative mode)
//   Bits: 11cccccc pppppppp pppppppp  (3 bytes)
//   count = (cmd & 0x3F) + 3  (3-66 bytes)
//   pos = uint16 from next 2 bytes
//
// 0xFE: Long fill
//   Bytes: 0xFE cccc CCCC vvvv  (4 bytes)
//   count = uint16, value = byte
//
// 0xFF: Long copy (absolute or relative mode)
//   Bytes: 0xFF cccc CCCC pppp PPPP  (5 bytes)
//   count = uint16, pos = uint16

Result<size_t> lcw_decompress(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    bool relative)
{
    if (input.empty()) {
        return std::unexpected(
            make_error(ErrorCode::DecompressError, "Empty input"));
    }

    const uint8_t* src = input.data();
    const uint8_t* src_end = src + input.size();
    uint8_t* dst = output.data();
    uint8_t* dst_start = dst;
    uint8_t* dst_end = dst + output.size();

    // Check for relative mode indicator: first byte == 0x00
    bool use_relative = relative;
    if (src < src_end && *src == 0x00) {
        use_relative = true;
        src++;  // Skip mode indicator byte
    }

    while (src < src_end) {
        uint8_t cmd = *src++;

        if (cmd < 0x80) {
            // 0x00-0x7F: Short relative copy (always relative)
            size_t count = ((cmd & 0x70) >> 4) + 3;
            if (src >= src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW short copy"));
            }
            size_t offset = ((cmd & 0x0F) << 8) | *src++;
            if (offset == 0 || dst - offset < dst_start) {
                return std::unexpected(
                    make_error(ErrorCode::CorruptData, "LCW bad short offset"));
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW short copy"));
            }
            copy_overlap(dst, dst - offset, count);
            dst += count;
        }
        else if (cmd < 0xC0) {
            // 0x80-0xBF: Literal bytes
            size_t count = cmd & 0x3F;
            if (count == 0) {
                // End marker (0x80)
                break;
            }
            if (src + count > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW literal"));
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW literal"));
            }
            std::memcpy(dst, src, count);
            src += count;
            dst += count;
        }
        else if (cmd < 0xFE) {
            // 0xC0-0xFD: Medium copy
            size_t count = (cmd & 0x3F) + 3;
            if (src + 2 > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW medium copy"));
            }
            uint16_t pos = read_u16(src);
            src += 2;

            const uint8_t* copy_src;
            if (use_relative) {
                if (pos == 0 || dst - pos < dst_start) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW bad rel pos"));
                }
                copy_src = dst - pos;
            } else {
                if (dst_start + pos > dst) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW bad abs pos"));
                }
                copy_src = dst_start + pos;
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW medium copy"));
            }
            copy_overlap(dst, copy_src, count);
            dst += count;
        }
        else if (cmd == 0xFE) {
            // 0xFE: Long fill
            if (src + 3 > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW long fill"));
            }
            size_t count = read_u16(src);
            src += 2;
            uint8_t value = *src++;
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW long fill"));
            }
            std::memset(dst, value, count);
            dst += count;
        }
        else {
            // 0xFF: Long copy
            if (src + 4 > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW long copy"));
            }
            size_t count = read_u16(src);
            src += 2;
            uint16_t pos = read_u16(src);
            src += 2;

            const uint8_t* copy_src;
            if (use_relative) {
                if (pos == 0 || dst - pos < dst_start) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW 0xFF bad rel"));
                }
                copy_src = dst - pos;
            } else {
                if (dst_start + pos > dst) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW 0xFF bad abs"));
                }
                copy_src = dst_start + pos;
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW long copy"));
            }
            copy_overlap(dst, copy_src, count);
            dst += count;
        }
    }

    return static_cast<size_t>(dst - dst_start);
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
            for (uint8_t i = 0; i < cmd && src < src_end && dst < dst_end; ++i) {
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
                // LONGDUMP: XOR next (word & 0x3FFF) bytes from source
                size_t count = word & 0x3FFF;
                for (size_t i = 0; i < count && src < src_end && dst < dst_end; ++i) {
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
