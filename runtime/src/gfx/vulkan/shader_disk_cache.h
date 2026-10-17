// SPDX-License-Identifier: MPL-2.0
#pragma once
// SPIR-V disk cache file format (vulkan-shaders/spirv.bin). No Vulkan, guest
// memory or renderer state: the decoder/encoder run on immutable data only.
#include "shader_source.h"
#include <bit>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include "../../../third_party/xxhash/xxhash.h"
namespace gfxvk::vk {
inline ShaderSourceId shader_source_id(std::string_view text) {
    const XXH128_hash_t hash = XXH3_128bits(text.data(), text.size());
    return {hash.low64, hash.high64, uint32_t(text.size())};
}
namespace diskcache {
// WWVKSC03 stores the source identity (XXH3-128 + length) instead of the GLSL
// text. WWVKSC02 files (exact GLSL per record) still load: their text is
// hashed once and dropped, and the next save writes WWVKSC03. Both share the
// same 40-byte header: 8-byte schema, fingerprint, payload length, checksum,
// record count.
constexpr char kMagic[] = "WWVKSC03", kLegacyMagic[] = "WWVKSC02";
constexpr size_t kMaxBytes = 128u * 1024u * 1024u;
constexpr size_t kMaxSourceBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxSpirvWords = 1024u * 1024u;
constexpr size_t kMaxEntries = 65536;
constexpr size_t kHeaderBytes = 40;
// key 8, stage 4, source length 4, source hash 16, usage 4, word count 4
constexpr size_t kRecordBytes = 40;
struct Entry {
    ShaderSourceId source;
    SpirvWords words;
    bool vertex = false;
    bool fromDisk = false;
    uint32_t usage = 0; // reserved for per-entry session bookkeeping
};
using Map = std::unordered_map<uint64_t, Entry>;
using Snapshot = std::vector<std::pair<uint64_t, Entry>>;
inline uint64_t checksum(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
inline void put(std::vector<uint8_t>& out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) out.push_back(uint8_t(value >> (i * 8)));
}
inline bool take(const std::vector<uint8_t>& in, size_t& offset, uint64_t& value, size_t bytes) {
    if (offset > in.size() || bytes > in.size() - offset) return false;
    value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= uint64_t(in[offset++]) << (i * 8);
    return true;
}
inline bool spirv_valid(const std::vector<uint32_t>& words) {
    if (words.size() < 5 || words.size() > kMaxSpirvWords || words[0] != 0x07230203 ||
        words[1] != 0x00010300 || !words[3] || words[3] > 0x400000 || words[4] != 0) return false;
    for (size_t i = 5; i < words.size();) {
        size_t length = words[i] >> 16;
        if (!length || length > words.size() - i) return false;
        i += length;
    }
    return words.size() > 5;
}
inline size_t record_bytes(size_t wordCount) { return kRecordBytes + wordCount * 4; }
struct DecodeResult {
    bool ok = false;
    bool legacy = false; // loaded from WWVKSC02; rewrite to shrink the file
    size_t bytes = 0;    // WWVKSC03 size of the decoded records, header included
};
inline DecodeResult decode(const std::vector<uint8_t>& file, uint64_t fingerprint, Map& result) {
    DecodeResult out;
    if (file.size() < kHeaderBytes || file.size() > kMaxBytes) return out;
    const bool legacy = !memcmp(file.data(), kLegacyMagic, 8);
    if (!legacy && memcmp(file.data(), kMagic, 8)) return out;
    size_t pos = 8; uint64_t stored, length, sum, count;
    if (!take(file, pos, stored, 8) || stored != fingerprint ||
        !take(file, pos, length, 8) || length != file.size() - kHeaderBytes ||
        !take(file, pos, sum, 8) || sum != checksum(file.data() + kHeaderBytes, file.size() - kHeaderBytes) ||
        !take(file, pos, count, 8) || count > kMaxEntries) return out;
    Map decoded;
    size_t bytes = kHeaderBytes;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key, stage, sourceLength, wordCount, low = 0, high = 0, usage = 0;
        if (!take(file, pos, key, 8) || !take(file, pos, stage, 4) || stage > 1 ||
            !take(file, pos, sourceLength, 4) || !sourceLength || sourceLength > kMaxSourceBytes) return out;
        if (!legacy && (!take(file, pos, low, 8) || !take(file, pos, high, 8) || !take(file, pos, usage, 4)))
            return out;
        if (!take(file, pos, wordCount, 4) || wordCount < 5 || wordCount > kMaxSpirvWords) return out;
        Entry entry;
        entry.vertex = stage != 0;
        entry.fromDisk = true;
        entry.usage = uint32_t(usage);
        if (legacy) {
            if (sourceLength > file.size() - pos) return out;
            entry.source = shader_source_id(std::string_view(
                reinterpret_cast<const char*>(file.data() + pos), size_t(sourceLength)));
            pos += sourceLength;
        } else entry.source = {low, high, uint32_t(sourceLength)};
        if (wordCount > (file.size() - pos) / 4) return out;
        std::vector<uint32_t> words(static_cast<size_t>(wordCount));
        memcpy(words.data(), file.data() + pos, size_t(wordCount) * 4);
        // Little-endian on disk; every supported host is little-endian too.
        static_assert(std::endian::native == std::endian::little);
        pos += size_t(wordCount) * 4;
        if (!spirv_valid(words)) return out;
        entry.words = std::make_shared<const std::vector<uint32_t>>(std::move(words));
        bytes += record_bytes(size_t(wordCount));
        if (!decoded.emplace(key, std::move(entry)).second) return out;
    }
    if (pos != file.size()) return out;
    result = std::move(decoded);
    out.ok = true; out.legacy = legacy; out.bytes = bytes;
    return out;
}
// Empty result: the snapshot does not fit the file limit.
inline std::vector<uint8_t> encode(const Snapshot& snapshot, uint64_t fingerprint) {
    size_t expected = kHeaderBytes;
    for (const auto& [key, entry] : snapshot) {
        const size_t bytes = record_bytes(entry.words ? entry.words->size() : 0);
        if (!entry.words || bytes > kMaxBytes - expected) return {};
        expected += bytes;
    }
    std::vector<uint8_t> file;
    file.reserve(expected);
    file.resize(kHeaderBytes);
    for (const auto& [key, entry] : snapshot) {
        put(file, key, 8); put(file, entry.vertex, 4); put(file, entry.source.length, 4);
        put(file, entry.source.low, 8); put(file, entry.source.high, 8);
        put(file, entry.usage, 4); put(file, entry.words->size(), 4);
        const size_t at = file.size();
        file.resize(at + entry.words->size() * 4);
        memcpy(file.data() + at, entry.words->data(), entry.words->size() * 4);
    }
    std::vector<uint8_t> header;
    header.insert(header.end(), kMagic, kMagic + 8);
    put(header, fingerprint, 8); put(header, file.size() - kHeaderBytes, 8);
    put(header, checksum(file.data() + kHeaderBytes, file.size() - kHeaderBytes), 8);
    put(header, snapshot.size(), 8);
    memcpy(file.data(), header.data(), kHeaderBytes);
    return file;
}
}
}
