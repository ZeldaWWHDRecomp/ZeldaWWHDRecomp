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
#include <zlib.h>
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
// WWVKSC04 stores each distinct SPIR-V module once (zlib) and names it by index from every record: translator
// variants share words, so a whole-game cache (the setup's model sweep: ~7,900 records, ~1,400 distinct modules)
// is ~11 MB instead of ~134 MB, and decoded records share one copy in memory.
// WWVKSC03 (words per record) and WWVKSC02 (exact GLSL per record) files still load and are rewritten as
// WWVKSC04 at the next save. All share the same 40-byte header: 8-byte schema, fingerprint, payload length,
// checksum, record count.
constexpr char kMagic[] = "WWVKSC04", kRecordsMagic[] = "WWVKSC03", kLegacyMagic[] = "WWVKSC02";
// Limits count distinct words (the memory the cache holds), not the packed file.
constexpr size_t kMaxBytes = 128u * 1024u * 1024u;
constexpr size_t kMaxSourceBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxSpirvWords = 1024u * 1024u;
constexpr size_t kMaxEntries = 65536;
constexpr size_t kHeaderBytes = 40;
// key 8, stage 4, source length 4, source hash 16, usage 4, word count (WWVKSC03) or module index 4
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
// Bytes a record adds: its fields, plus its words when no other record holds the same module.
inline size_t record_bytes(size_t wordCount, bool newWords = true) { return kRecordBytes + (newWords ? wordCount * 4 : 0); }
// One shared copy per distinct module (full word comparison; the digest only finds candidates).
class WordPool {
    std::unordered_multimap<uint64_t, SpirvWords> entries;
public:
    static uint64_t digest(const std::vector<uint32_t>& words) {
        return checksum(reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4);
    }
    // the pooled copy of these words; fresh: they were not pooled before (now they are)
    SpirvWords intern(const SpirvWords& words, bool& fresh) {
        fresh = false;
        if (!words) return words;
        const uint64_t h = digest(*words);
        auto [first, last] = entries.equal_range(h);
        for (auto it = first; it != last; ++it)
            if (it->second == words || *it->second == *words) return it->second;
        entries.emplace(h, words);
        fresh = true;
        return words;
    }
    size_t size() const { return entries.size(); }
};
struct DecodeResult {
    bool ok = false;
    bool legacy = false; // loaded from WWVKSC02/03; rewrite to shrink the file
    size_t bytes = 0;    // accounted size of the decoded records: header, records, distinct words
};
inline bool unpack(const uint8_t* packed, size_t size, std::vector<uint32_t>& words) {
    uLongf length = uLongf(words.size()) * 4;
    return uncompress(reinterpret_cast<Bytef*>(words.data()), &length, packed, uLong(size)) == Z_OK &&
           length == uLongf(words.size()) * 4;
}
// Words in the result are shared between records with the same module (and with pool, when given).
inline DecodeResult decode(const std::vector<uint8_t>& file, uint64_t fingerprint, Map& result, WordPool* pool = nullptr) {
    DecodeResult out;
    if (file.size() < kHeaderBytes || file.size() > kMaxBytes) return out;
    const bool legacy = !memcmp(file.data(), kLegacyMagic, 8), records = !memcmp(file.data(), kRecordsMagic, 8);
    const bool indexed = !memcmp(file.data(), kMagic, 8);
    if (!legacy && !records && !indexed) return out;
    // Little-endian on disk; every supported host is little-endian too.
    static_assert(std::endian::native == std::endian::little);
    size_t pos = 8; uint64_t stored, length, sum, count;
    if (!take(file, pos, stored, 8) || stored != fingerprint ||
        !take(file, pos, length, 8) || length != file.size() - kHeaderBytes ||
        !take(file, pos, sum, 8) || sum != checksum(file.data() + kHeaderBytes, file.size() - kHeaderBytes) ||
        !take(file, pos, count, 8) || count > kMaxEntries) return out;
    WordPool local;
    WordPool& words = pool ? *pool : local;
    std::vector<SpirvWords> modules;
    size_t bytes = kHeaderBytes;
    if (indexed) {
        uint64_t moduleCount;
        if (!take(file, pos, moduleCount, 8) || moduleCount > kMaxEntries) return out;
        bytes += 8;
        modules.reserve(size_t(moduleCount));
        for (uint64_t i = 0; i < moduleCount; ++i) {
            uint64_t wordCount, packed;
            if (!take(file, pos, wordCount, 4) || wordCount < 5 || wordCount > kMaxSpirvWords ||
                !take(file, pos, packed, 4) || !packed || packed > file.size() - pos) return out;
            const size_t padded = (size_t(packed) + 3) & ~size_t(3);
            if (padded > file.size() - pos) return out;
            std::vector<uint32_t> code(static_cast<size_t>(wordCount));
            if (!unpack(file.data() + pos, size_t(packed), code) || !spirv_valid(code)) return out;
            pos += padded;
            bool fresh;
            modules.push_back(words.intern(std::make_shared<const std::vector<uint32_t>>(std::move(code)), fresh));
            bytes += size_t(wordCount) * 4;
            if (bytes > kMaxBytes) return out;
        }
    }
    Map decoded;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key, stage, sourceLength, wordCount, low = 0, high = 0, usage = 0;
        if (!take(file, pos, key, 8) || !take(file, pos, stage, 4) || stage > 1 ||
            !take(file, pos, sourceLength, 4) || !sourceLength || sourceLength > kMaxSourceBytes) return out;
        if (!legacy && (!take(file, pos, low, 8) || !take(file, pos, high, 8) || !take(file, pos, usage, 4)))
            return out;
        Entry entry;
        entry.vertex = stage != 0;
        entry.fromDisk = true;
        entry.usage = uint32_t(usage);
        if (indexed) {
            uint64_t index;
            if (!take(file, pos, index, 4) || index >= modules.size()) return out;
            entry.source = {low, high, uint32_t(sourceLength)};
            entry.words = modules[size_t(index)];
            bytes += kRecordBytes;
        } else {
            if (!take(file, pos, wordCount, 4) || wordCount < 5 || wordCount > kMaxSpirvWords) return out;
            if (legacy) {
                if (sourceLength > file.size() - pos) return out;
                entry.source = shader_source_id(std::string_view(
                    reinterpret_cast<const char*>(file.data() + pos), size_t(sourceLength)));
                pos += sourceLength;
            } else entry.source = {low, high, uint32_t(sourceLength)};
            if (wordCount > (file.size() - pos) / 4) return out;
            std::vector<uint32_t> code(static_cast<size_t>(wordCount));
            memcpy(code.data(), file.data() + pos, size_t(wordCount) * 4);
            pos += size_t(wordCount) * 4;
            if (!spirv_valid(code)) return out;
            bool fresh;
            entry.words = words.intern(std::make_shared<const std::vector<uint32_t>>(std::move(code)), fresh);
            bytes += record_bytes(size_t(wordCount), fresh);
        }
        if (!decoded.emplace(key, std::move(entry)).second) return out;
    }
    if (pos != file.size()) return out;
    result = std::move(decoded);
    out.ok = true; out.legacy = legacy || records; out.bytes = bytes;
    return out;
}
// Empty result: the snapshot does not fit the limit (distinct words, kMaxBytes).
inline std::vector<uint8_t> encode(const Snapshot& snapshot, uint64_t fingerprint) {
    WordPool pool;
    std::vector<SpirvWords> modules;
    std::unordered_map<const std::vector<uint32_t>*, uint32_t> index;
    std::vector<uint32_t> recordModule;
    recordModule.reserve(snapshot.size());
    size_t accounted = kHeaderBytes + 8;
    for (const auto& [key, entry] : snapshot) {
        if (!entry.words || entry.words->size() < 5 || entry.words->size() > kMaxSpirvWords) return {};
        bool fresh;
        const SpirvWords shared = pool.intern(entry.words, fresh);
        auto [it, added] = index.emplace(shared.get(), uint32_t(modules.size()));
        if (added) modules.push_back(shared);
        recordModule.push_back(it->second);
        const size_t bytes = record_bytes(entry.words->size(), added);
        if (bytes > kMaxBytes - accounted) return {};
        accounted += bytes;
    }
    std::vector<uint8_t> file;
    file.resize(kHeaderBytes);
    put(file, modules.size(), 8);
    for (const auto& words : modules) {
        uLongf size = compressBound(uLong(words->size() * 4));
        std::vector<uint8_t> packed(size);
        // Level 1: ~0.2 ms per module on the saving thread, ~3x smaller than the words.
        if (compress2(packed.data(), &size, reinterpret_cast<const Bytef*>(words->data()), uLong(words->size() * 4), 1) != Z_OK)
            return {};
        put(file, words->size(), 4); put(file, size, 4);
        file.insert(file.end(), packed.begin(), packed.begin() + size);
        while (file.size() % 4) file.push_back(0);
    }
    size_t at = 0;
    for (const auto& [key, entry] : snapshot) {
        put(file, key, 8); put(file, entry.vertex, 4); put(file, entry.source.length, 4);
        put(file, entry.source.low, 8); put(file, entry.source.high, 8);
        put(file, entry.usage, 4); put(file, recordModule[at++], 4);
    }
    if (file.size() > kMaxBytes) return {};
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
