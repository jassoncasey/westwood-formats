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

    while (src < src_end) {
        uint8_t cmd = *src++;

        if (cmd == 0x80) {
            // End marker
            break;
        }

        if (cmd < 0x80) {
            if (cmd < 0x40) {
                // 0x00-0x3F: Literal copy
                size_t count = (cmd & 0x3F) + 1;
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
            } else {
                // 0x40-0x7F: Short back-reference (always relative)
                size_t count = ((cmd & 0x70) >> 4) + 3;
                if (src >= src_end) {
                    return std::unexpected(
                        make_error(ErrorCode::UnexpectedEof, "LCW short ref"));
                }
                size_t offset = ((cmd & 0x0F) << 8) | *src++;
                if (dst - offset < dst_start) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW bad offset"));
                }
                if (dst + count > dst_end) {
                    return std::unexpected(
                        make_error(ErrorCode::OutputOverflow, "LCW short ref"));
                }
                copy_overlap(dst, dst - offset, count);
                dst += count;
            }
        } else if (cmd < 0xC0) {
            // 0x81-0xBF: Long back-reference
            if (src >= src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW long ref count"));
            }
            size_t count = ((cmd & 0x3F) << 8) | *src++;
            if (count == 0) {
                // Extended: 0x80 0x00 is end marker (handled above)
                break;
            }
            if (src + 2 > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW long ref offset"));
            }
            uint16_t raw_offset = read_u16(src);
            src += 2;

            const uint8_t* copy_src;
            if (relative) {
                if (dst - raw_offset < dst_start) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW bad rel offset"));
                }
                copy_src = dst - raw_offset;
            } else {
                if (dst_start + raw_offset >= dst) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW bad abs offset"));
                }
                copy_src = dst_start + raw_offset;
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW long ref"));
            }
            copy_overlap(dst, copy_src, count);
            dst += count;
        } else if (cmd < 0xFE) {
            // 0xC0-0xFD: Short fill
            size_t count = (cmd & 0x3F) + 3;
            if (src >= src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW short fill"));
            }
            uint8_t value = *src++;
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW short fill"));
            }
            std::memset(dst, value, count);
            dst += count;
        } else if (cmd == 0xFE) {
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
        } else {
            // 0xFF: Long copy with separate count
            if (src + 4 > src_end) {
                return std::unexpected(
                    make_error(ErrorCode::UnexpectedEof, "LCW 0xFF"));
            }
            size_t count = read_u16(src);
            src += 2;
            uint16_t raw_offset = read_u16(src);
            src += 2;

            const uint8_t* copy_src;
            if (relative) {
                if (dst - raw_offset < dst_start) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW 0xFF bad rel"));
                }
                copy_src = dst - raw_offset;
            } else {
                if (dst_start + raw_offset >= dst) {
                    return std::unexpected(
                        make_error(ErrorCode::CorruptData, "LCW 0xFF bad abs"));
                }
                copy_src = dst_start + raw_offset;
            }
            if (dst + count > dst_end) {
                return std::unexpected(
                    make_error(ErrorCode::OutputOverflow, "LCW 0xFF"));
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

} // namespace wwd
