// SPDX-License-Identifier: MPL-2.0
// The Metal recipe file (shaders.bin, metal_draw.mm "persistent shader cache"): a stream of records
// (u32 type, u32 raw size, u32 compressed size, zlib payload). Since the cache is versioned it starts with
// a header record (type 7): a schema version, a version per record type and the OS + GPU it was written
// on. Rows of another record version are stale and dropped; another schema starts a new file (an older
// one) or leaves the file alone (a newer one). Pipeline recipes that can no longer be resolved (their
// shaders are gone) get a tombstone record (type 8) and are left out when the file is next rewritten.
// Rewrites copy the kept rows' compressed bytes as they are, once each (duplicates go), header first.
// Readers before the header existed skip types 7 and 8 (they only know 1 and 2).
// Idea after aurora's pipeline cache (MIT; lib/gfx/pipeline_cache.cpp, schema table, per-type config_version
// and pruning of old versions); no code taken.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <zlib.h>

namespace gfx::mtlcache {

constexpr uint32_t kRecShader = 1, kRecPipeline = 2, kRecHeader = 7, kRecTombstone = 8;
constexpr uint32_t kSchema = 1;
constexpr uint32_t kShaderRecordVersion = 1;
// bump with the PipelineRecipe layout and with the derivation of the keys a recipe names its shaders
// and fetch shader by (metal_draw.mm wide_key, get_fetch_shader)
constexpr uint32_t kPipelineRecordVersion = 1;
constexpr uint32_t kRecordLimit = 16u << 20;  // as tools/shaderprep.py RECORD_LIMIT
constexpr char kMagic[8] = {'W', 'W', 'M', 'T', 'L', 'H', 'D', 'R'};

struct Header {
    uint32_t schema = 0, shaderVersion = 0, pipelineVersion = 0, pipelineSize = 0;
    std::string fingerprint;  // OS version and GPU name
};

inline Header current(uint32_t pipelineSize, std::string fingerprint) {
    return Header{kSchema, kShaderRecordVersion, kPipelineRecordVersion, pipelineSize, std::move(fingerprint)};
}

inline std::vector<uint8_t> encode_header(const Header& h) {
    std::vector<uint8_t> v(kMagic, kMagic + 8);
    auto put = [&](uint32_t x) { v.insert(v.end(), (const uint8_t*)&x, (const uint8_t*)&x + 4); };
    put(h.schema);
    put(h.shaderVersion);
    put(h.pipelineVersion);
    put(h.pipelineSize);
    put((uint32_t)h.fingerprint.size());
    v.insert(v.end(), h.fingerprint.begin(), h.fingerprint.end());
    return v;
}

inline bool decode_header(const uint8_t* p, size_t n, Header& h) {
    if (n < 28 || memcmp(p, kMagic, 8)) return false;
    uint32_t w[5];
    memcpy(w, p + 8, sizeof w);
    if (w[4] > 4096 || 28 + (size_t)w[4] != n) return false;
    h = Header{w[0], w[1], w[2], w[3], std::string((const char*)p + 28, w[4])};
    return true;
}

// what a file's header (or its absence) means for its rows
struct Policy {
    bool shaders = true, pipelines = true;  // replay (and keep) these rows
    bool writable = true;                   // append to the file
    bool rewrite = false;                   // rewrite it with the current header
    const char* reason = nullptr;           // for the log, when rows are dropped or the file is rewritten
};

inline Policy policy(bool hasHeader, const Header& file, const Header& ours) {
    Policy p;
    if (!hasHeader) {
        p.rewrite = true;
        p.reason = "no version header (older release)";
        return p;
    }
    if (file.schema > ours.schema) {  // a newer release's file: leave it alone
        p.shaders = p.pipelines = p.writable = false;
        p.reason = "written by a newer release (left as it is, not recorded into)";
        return p;
    }
    if (file.schema < ours.schema) {
        p.shaders = p.pipelines = false;
        p.rewrite = true;
        p.reason = "older cache schema (started again)";
        return p;
    }
    if (file.shaderVersion != ours.shaderVersion) {
        p.shaders = false;
        p.rewrite = true;
        p.reason = "shader records of another version (dropped)";
    }
    if (file.pipelineVersion != ours.pipelineVersion || file.pipelineSize != ours.pipelineSize) {
        p.pipelines = false;
        p.rewrite = true;
        p.reason = p.shaders ? "pipeline records of another version (dropped)" : "records of another version (dropped)";
    }
    if (!p.rewrite && file.fingerprint != ours.fingerprint) {
        p.rewrite = true;  // recipes are guest state and stay valid; the header records the new system
        p.reason = "macOS or GPU changed (recipes kept, compiled again)";
    }
    return p;
}

struct Record {
    uint32_t type = 0, raw = 0;
    std::vector<uint8_t> comp;
};

// the next record; false at the end or at a torn or implausible record (the rest of the file is unusable)
inline bool read_record(FILE* f, Record& r) {
    uint32_t hdr[3];
    if (fread(hdr, sizeof hdr, 1, f) != 1) return false;
    if (!hdr[1] || hdr[1] > kRecordLimit || !hdr[2] || hdr[2] > kRecordLimit) return false;
    r.type = hdr[0];
    r.raw = hdr[1];
    r.comp.resize(hdr[2]);
    return fread(r.comp.data(), 1, hdr[2], f) == hdr[2];
}

inline bool inflate(const Record& r, std::vector<uint8_t>& out) {
    out.resize(r.raw);
    uLongf size = r.raw;
    return uncompress(out.data(), &size, r.comp.data(), r.comp.size()) == Z_OK && size == r.raw;
}

inline bool write_compressed(FILE* f, const Record& r) {
    uint32_t hdr[3] = {r.type, r.raw, (uint32_t)r.comp.size()};
    return fwrite(hdr, sizeof hdr, 1, f) == 1 && fwrite(r.comp.data(), 1, r.comp.size(), f) == r.comp.size();
}

inline bool make_record(uint32_t type, const std::vector<uint8_t>& raw, Record& r) {
    uLongf size = compressBound(raw.size());
    r.type = type;
    r.raw = (uint32_t)raw.size();
    r.comp.resize(size);
    if (compress2(r.comp.data(), &size, raw.data(), raw.size(), 6) != Z_OK) return false;
    r.comp.resize(size);
    return true;
}

inline uint64_t fnv(const void* data, size_t n, uint64_t h = 0xcbf29ce484222325ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
// identity of a row for de-duplication: its type and compressed bytes (every writer uses zlib level 6)
inline uint64_t row_id(const Record& r) { return fnv(r.comp.data(), r.comp.size(), fnv(&r.type, 4)); }

inline std::vector<uint64_t> decode_tombstone(const std::vector<uint8_t>& raw) {
    std::vector<uint64_t> v(raw.size() / 8);
    if (!v.empty()) memcpy(v.data(), raw.data(), v.size() * 8);
    return v;
}
inline std::vector<uint8_t> encode_tombstone(const std::vector<uint64_t>& hashes) {
    std::vector<uint8_t> v(hashes.size() * 8);
    if (!v.empty()) memcpy(v.data(), hashes.data(), v.size());
    return v;
}

struct RewriteStats {
    size_t kept = 0, duplicates = 0, dropped = 0, markers = 0;
    bool damaged = false;  // a torn or implausible tail was left out
};

// Rewrites path: header h first, then every row keep(row, index) accepts, each distinct row once; header and
// tombstone records are not copied. index counts every record of the file in order (as the loader numbers them).
// Writes a temporary file next to it and renames it over the original only when everything was written.
inline bool rewrite(const std::string& path, const Header& h, const std::function<bool(const Record&, size_t)>& keep,
                    RewriteStats& st) {
    st = RewriteStats{};
    const std::string tmp = path + ".rewrite";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out) return false;
    Record hr;
    bool ok = make_record(kRecHeader, encode_header(h), hr) && write_compressed(out, hr);
    if (FILE* in = fopen(path.c_str(), "rb")) {
        std::unordered_set<uint64_t> seen;
        Record r;
        size_t index = 0;
        long at = 0;
        for (; ok; index++) {
            at = ftell(in);
            if (!read_record(in, r)) break;
            if (r.type == kRecHeader || r.type == kRecTombstone) { st.markers++; continue; }
            if (!keep(r, index)) { st.dropped++; continue; }
            if (!seen.insert(row_id(r)).second) { st.duplicates++; continue; }
            ok = write_compressed(out, r);
            st.kept++;
        }
        fseek(in, 0, SEEK_END);
        st.damaged = ftell(in) != at;
        fclose(in);
    }
    ok = fflush(out) == 0 && ok;
    ok = fclose(out) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace gfx::mtlcache
