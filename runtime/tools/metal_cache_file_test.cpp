// SPDX-License-Identifier: MPL-2.0
// The Metal recipe file's version header, load policy and rewrite (gfx/metal_cache_file.h).
#include "gfx/metal_cache_file.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gfx::mtlcache;

static std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static void append(FILE* f, uint32_t type, const std::vector<uint8_t>& raw) {
    Record r;
    assert(make_record(type, raw, r));
    assert(write_compressed(f, r));
}

static std::vector<Record> read_all(const std::string& path, bool* damaged = nullptr) {
    std::vector<Record> out;
    FILE* f = fopen(path.c_str(), "rb");
    assert(f);
    Record r;
    long at = 0;
    while ((at = ftell(f), read_record(f, r))) out.push_back(r);
    fseek(f, 0, SEEK_END);
    if (damaged) *damaged = ftell(f) != at;
    fclose(f);
    return out;
}

static std::string payload(const Record& r) {
    std::vector<uint8_t> raw;
    assert(inflate(r, raw));
    return std::string(raw.begin(), raw.end());
}

int main() {
    // header round trip and rejection
    const Header ours = current(168, "macOS 26.0 | Apple M4");
    Header back;
    auto enc = encode_header(ours);
    assert(decode_header(enc.data(), enc.size(), back));
    assert(back.schema == kSchema && back.shaderVersion == kShaderRecordVersion && back.pipelineVersion == kPipelineRecordVersion);
    assert(back.pipelineSize == 168 && back.fingerprint == ours.fingerprint);
    assert(!decode_header(enc.data(), enc.size() - 1, back));
    enc[0] ^= 1;
    assert(!decode_header(enc.data(), enc.size(), back));

    // policy
    Policy p = policy(false, Header{}, ours);
    assert(p.shaders && p.pipelines && p.writable && p.rewrite);  // legacy file: kept, header added
    p = policy(true, ours, ours);
    assert(p.shaders && p.pipelines && p.writable && !p.rewrite && !p.reason);
    Header h = ours;
    h.fingerprint = "macOS 15.6 | Apple M1";
    p = policy(true, h, ours);
    assert(p.shaders && p.pipelines && p.rewrite);  // other system: recipes kept
    h = ours;
    h.pipelineVersion = kPipelineRecordVersion + 1;
    p = policy(true, h, ours);
    assert(p.shaders && !p.pipelines && p.rewrite);
    h = ours;
    h.pipelineSize = 160;
    p = policy(true, h, ours);
    assert(p.shaders && !p.pipelines && p.rewrite);
    h = ours;
    h.shaderVersion = 0;
    p = policy(true, h, ours);
    assert(!p.shaders && p.pipelines && p.rewrite);
    h = ours;
    h.schema = kSchema + 1;
    p = policy(true, h, ours);
    assert(!p.shaders && !p.pipelines && !p.writable && !p.rewrite);  // newer release's file: untouched
    h = ours;
    h.schema = kSchema - 1;
    p = policy(true, h, ours);
    assert(!p.shaders && !p.pipelines && p.writable && p.rewrite);

    // tombstones
    std::vector<uint64_t> hashes{1, 0xfedcba9876543210ull};
    assert(decode_tombstone(encode_tombstone(hashes)) == hashes);

    // rewrite: a legacy file with a repeated row, a stale header in the middle, a tombstone, a dropped row
    // and a torn end
    char dir[] = "/tmp/metal_cache_file_testXXXXXX";
    assert(mkdtemp(dir));
    const std::string path = std::string(dir) + "/shaders.bin";
    FILE* f = fopen(path.c_str(), "wb");
    append(f, kRecShader, bytes("shader A"));         // 0
    append(f, kRecPipeline, bytes("pipeline 1"));     // 1
    append(f, kRecShader, bytes("shader A"));         // 2 repeated
    append(f, kRecHeader, encode_header(h));          // 3 a merged-in header: dropped
    append(f, kRecPipeline, bytes("pipeline drop"));  // 4 the caller drops it
    append(f, kRecTombstone, encode_tombstone({7}));  // 5
    append(f, 99, bytes("future type"));              // 6 unknown types are the caller's decision
    append(f, kRecPipeline, bytes("pipeline 2"));     // 7
    fwrite("\x01\x00\x00\x00\x10", 1, 5, f);          // torn
    fclose(f);
    std::vector<size_t> seen;
    RewriteStats st;
    assert(rewrite(path, ours, [&](const Record& r, size_t index) {
        seen.push_back(index);
        return payload(r) != "pipeline drop";
    }, st));
    assert((seen == std::vector<size_t>{0, 1, 2, 4, 6, 7}));  // indices count every record
    assert(st.kept == 4 && st.duplicates == 1 && st.dropped == 1 && st.markers == 2 && st.damaged);
    bool damaged = true;
    auto rows = read_all(path, &damaged);
    assert(!damaged && rows.size() == 5);
    assert(rows[0].type == kRecHeader);
    std::vector<uint8_t> raw;
    assert(inflate(rows[0], raw) && decode_header(raw.data(), raw.size(), back) && back.fingerprint == ours.fingerprint);
    assert(payload(rows[1]) == "shader A" && payload(rows[2]) == "pipeline 1" && payload(rows[3]) == "future type" &&
           payload(rows[4]) == "pipeline 2");
    // the rewritten file is stable: a second rewrite keeps it byte for byte
    FILE* a = fopen(path.c_str(), "rb");
    std::vector<char> before(1 << 16);
    before.resize(fread(before.data(), 1, before.size(), a));
    fclose(a);
    assert(rewrite(path, ours, [](const Record&, size_t) { return true; }, st));
    assert(st.kept == 4 && st.duplicates == 0 && st.markers == 1 && !st.damaged);
    a = fopen(path.c_str(), "rb");
    std::vector<char> after(1 << 16);
    after.resize(fread(after.data(), 1, after.size(), a));
    fclose(a);
    assert(before == after);
    // a missing file becomes a file with the header alone
    const std::string fresh = std::string(dir) + "/fresh.bin";
    assert(rewrite(fresh, ours, [](const Record&, size_t) { return true; }, st));
    assert(read_all(fresh).size() == 1 && st.kept == 0);
    remove(path.c_str());
    remove(fresh.c_str());
    rmdir(dir);
    puts("metal_cache_file_test: ok");
    return 0;
}
