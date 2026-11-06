// SPDX-License-Identifier: MPL-2.0
// Bounds-checked little-endian readers and writers for the online protocol (docs/online-server.md).
// Every read past the end sets `bad` and returns zero; callers check `ok()` once at the end of a message.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace online::proto {

using Bytes = std::vector<uint8_t>;

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    explicit Reader(const Bytes& b) : d_(b.data()), n_(b.size()) {}

    bool ok() const { return !bad_; }
    bool done() const { return !bad_ && at_ == n_; }
    size_t left() const { return bad_ ? 0 : n_ - at_; }
    size_t pos() const { return at_; }
    const uint8_t* here() const { return d_ + at_; }

    uint8_t u8() { return need(1) ? d_[at_++] : 0; }
    int8_t s8() { return static_cast<int8_t>(u8()); }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(d_[at_] | (d_[at_ + 1] << 8));
        at_ += 2;
        return v;
    }
    int16_t s16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = uint32_t(d_[at_]) | (uint32_t(d_[at_ + 1]) << 8) | (uint32_t(d_[at_ + 2]) << 16) |
                     (uint32_t(d_[at_ + 3]) << 24);
        at_ += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32();
        uint64_t hi = u32();
        return lo | (hi << 32);
    }
    float f32() {
        uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    bool bytes(uint8_t* out, size_t n) {
        if (!need(n)) return false;
        std::memcpy(out, d_ + at_, n);
        at_ += n;
        return true;
    }
    // a view of the next n bytes (valid while the buffer lives); empty and bad if short
    std::string_view view(size_t n) {
        if (!need(n)) return {};
        std::string_view v(reinterpret_cast<const char*>(d_ + at_), n);
        at_ += n;
        return v;
    }
    // u8 length + text, at most `cap` bytes
    std::string str8(size_t cap) {
        size_t n = u8();
        if (n > cap) {
            bad_ = true;
            return {};
        }
        return std::string(view(n));
    }
    void skip(size_t n) { need(n) ? void(at_ += n) : void(); }
    void fail() { bad_ = true; }

private:
    bool need(size_t n) {
        if (bad_ || n > n_ - at_) {
            bad_ = true;
            return false;
        }
        return true;
    }
    const uint8_t* d_;
    size_t n_, at_ = 0;
    bool bad_ = false;
};

class Writer {
public:
    explicit Writer(Bytes& out) : o_(out) {}
    void u8(uint8_t v) { o_.push_back(v); }
    void s8(int8_t v) { u8(uint8_t(v)); }
    void u16(uint16_t v) {
        o_.push_back(uint8_t(v));
        o_.push_back(uint8_t(v >> 8));
    }
    void s16(int16_t v) { u16(uint16_t(v)); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) o_.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void f32(float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        u32(u);
    }
    void bytes(const void* d, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        o_.insert(o_.end(), p, p + n);
    }
    void str8(std::string_view s) {
        size_t n = s.size() > 255 ? 255 : s.size();
        u8(uint8_t(n));
        bytes(s.data(), n);
    }
    size_t size() const { return o_.size(); }
    // patch a u16 written earlier at `at`
    void put_u16_at(size_t at, uint16_t v) {
        o_[at] = uint8_t(v);
        o_[at + 1] = uint8_t(v >> 8);
    }

private:
    Bytes& o_;
};

}  // namespace online::proto
