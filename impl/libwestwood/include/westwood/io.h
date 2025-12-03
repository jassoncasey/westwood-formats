#pragma once

#include <westwood/error.h>

#include <cstdint>
#include <span>
#include <vector>

namespace wwd {

// Little-endian readers (Westwood standard)
inline uint16_t read_u16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t read_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline int16_t read_i16(const uint8_t* p) {
    return static_cast<int16_t>(read_u16(p));
}

inline int32_t read_i32(const uint8_t* p) {
    return static_cast<int32_t>(read_u32(p));
}

// Big-endian readers (IFF chunk sizes)
inline uint32_t read_u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Read 4-byte ASCII tag
inline uint32_t read_tag(const uint8_t* p) {
    return read_u32(p);  // stored as-is, compared as uint32
}

// Check if tag matches ASCII string
inline bool tag_eq(uint32_t tag, const char* s) {
    return tag == read_u32(reinterpret_cast<const uint8_t*>(s));
}

// Safe span accessor
class SpanReader {
public:
    explicit SpanReader(std::span<const uint8_t> data)
        : data_(data), pos_(0) {}

    size_t pos() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }
    bool at_end() const { return pos_ >= data_.size(); }

    bool skip(size_t n) {
        if (pos_ + n > data_.size()) return false;
        pos_ += n;
        return true;
    }

    bool seek(size_t pos) {
        if (pos > data_.size()) return false;
        pos_ = pos;
        return true;
    }

    const uint8_t* ptr() const {
        return data_.data() + pos_;
    }

    std::span<const uint8_t> span(size_t n) const {
        if (pos_ + n > data_.size()) return {};
        return data_.subspan(pos_, n);
    }

    Result<uint8_t> read_u8() {
        if (pos_ >= data_.size()) {
            return std::unexpected(
                make_error(ErrorCode::UnexpectedEof, "read_u8"));
        }
        return data_[pos_++];
    }

    Result<uint16_t> read_u16() {
        if (pos_ + 2 > data_.size()) {
            return std::unexpected(
                make_error(ErrorCode::UnexpectedEof, "read_u16"));
        }
        auto v = wwd::read_u16(data_.data() + pos_);
        pos_ += 2;
        return v;
    }

    Result<uint32_t> read_u32() {
        if (pos_ + 4 > data_.size()) {
            return std::unexpected(
                make_error(ErrorCode::UnexpectedEof, "read_u32"));
        }
        auto v = wwd::read_u32(data_.data() + pos_);
        pos_ += 4;
        return v;
    }

    Result<uint32_t> read_u32be() {
        if (pos_ + 4 > data_.size()) {
            return std::unexpected(
                make_error(ErrorCode::UnexpectedEof, "read_u32be"));
        }
        auto v = wwd::read_u32be(data_.data() + pos_);
        pos_ += 4;
        return v;
    }

    Result<std::span<const uint8_t>> read_bytes(size_t n) {
        if (pos_ + n > data_.size()) {
            return std::unexpected(
                make_error(ErrorCode::UnexpectedEof, "read_bytes"));
        }
        auto sp = data_.subspan(pos_, n);
        pos_ += n;
        return sp;
    }

private:
    std::span<const uint8_t> data_;
    size_t pos_;
};

// File loading
WWD_API Result<std::vector<uint8_t>> load_file(const std::string& path);

// Load from stdin (reads until EOF)
WWD_API Result<std::vector<uint8_t>> load_stdin();

} // namespace wwd
