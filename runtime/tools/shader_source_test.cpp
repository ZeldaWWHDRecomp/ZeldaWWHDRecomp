// SPDX-License-Identifier: MPL-2.0
#include "gfx/vulkan/shader_disk_cache.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <tuple>
#include <unordered_map>
using namespace gfxvk::vk;
namespace dc = gfxvk::vk::diskcache;
namespace {
std::vector<uint32_t> module(uint32_t marker) {
    // Header plus one two-word instruction carrying a marker word.
    return {0x07230203, 0x00010300, 0, 16, 0, (2u << 16) | 0u, marker};
}
dc::Entry entry(const std::string& text, bool vertex, uint32_t marker, uint32_t usage = 0) {
    dc::Entry e;
    e.source = shader_source_id(text);
    e.words = std::make_shared<const std::vector<uint32_t>>(module(marker));
    e.vertex = vertex;
    e.usage = usage;
    return e;
}
// A WWVKSC02 file as older builds wrote it: exact GLSL text per record.
std::vector<uint8_t> legacy_file(const std::vector<std::tuple<uint64_t, bool, std::string, uint32_t>>& records,
                                 uint64_t fingerprint) {
    std::vector<uint8_t> payload;
    for (const auto& [key, vertex, text, marker] : records) {
        auto words = module(marker);
        dc::put(payload, key, 8); dc::put(payload, vertex, 4);
        dc::put(payload, text.size(), 4); dc::put(payload, words.size(), 4);
        payload.insert(payload.end(), text.begin(), text.end());
        for (auto w : words) dc::put(payload, w, 4);
    }
    std::vector<uint8_t> file{'W','W','V','K','S','C','0','2'};
    dc::put(file, fingerprint, 8); dc::put(file, payload.size(), 8);
    dc::put(file, dc::checksum(payload.data(), payload.size()), 8);
    dc::put(file, records.size(), 8);
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}
}
int main() {
    // Identity: equal text, equal id; any byte or length change differs.
    const std::string a(4096, 'a');
    std::string b = a; b[2048] = 'b';
    assert(shader_source_id(a) == shader_source_id(std::string(4096, 'a')));
    assert(!(shader_source_id(a) == shader_source_id(b)));
    assert(!(shader_source_id(a) == shader_source_id(a.substr(0, 4095))));
    assert(shader_source_id(std::string("abc\0xyz", 7)).length == 7);
    assert(!(shader_source_id(std::string("abc\0xyz", 7)) == shader_source_id(std::string("abc\0xyw", 7))));

    // WWVKSC03 round trip keeps identity, stage, usage and the exact words.
    const uint64_t fp = 0x1234;
    dc::Snapshot snapshot{{1, entry(a, true, 7, 3)}, {2, entry(b, false, 9)}};
    auto file = dc::encode(snapshot, fp);
    assert(file.size() == dc::kHeaderBytes + 2 * dc::record_bytes(7));
    assert(!memcmp(file.data(), "WWVKSC03", 8));
    dc::Map map;
    auto decoded = dc::decode(file, fp, map);
    assert(decoded.ok && !decoded.legacy && decoded.bytes == file.size() && map.size() == 2);
    assert(map.at(1).vertex && map.at(1).source == shader_source_id(a) && map.at(1).usage == 3);
    assert(!map.at(2).vertex && map.at(2).source == shader_source_id(b) && map.at(2).fromDisk);
    assert(*map.at(1).words == module(7) && *map.at(2).words == module(9));
    // The file never contains the GLSL text.
    assert(std::search(file.begin(), file.end(), a.begin(), a.begin() + 64) == file.end());

    // Rejected without touching the destination: other fingerprint, damage, truncation.
    dc::Map untouched{{5, entry("keep", true, 1)}};
    assert(!dc::decode(file, fp + 1, untouched).ok && untouched.size() == 1);
    auto damaged = file; damaged.back() ^= 1;
    assert(!dc::decode(damaged, fp, untouched).ok && untouched.size() == 1);
    auto truncated = file; truncated.resize(file.size() - 4);
    assert(!dc::decode(truncated, fp, untouched).ok && untouched.size() == 1);
    auto unknown = file; unknown[7] = '9';
    assert(!dc::decode(unknown, fp, untouched).ok && untouched.size() == 1);
    assert(!dc::decode({}, fp, untouched).ok);

    // WWVKSC02 migration: text hashed once, same keys, smaller re-encoded file.
    auto old = legacy_file({{1, true, a, 7}, {2, false, b, 9}}, fp);
    dc::Map migrated;
    auto legacy = dc::decode(old, fp, migrated);
    assert(legacy.ok && legacy.legacy && migrated.size() == 2);
    assert(migrated.at(1).source == shader_source_id(a) && migrated.at(2).source == shader_source_id(b));
    assert(*migrated.at(1).words == module(7) && migrated.at(1).usage == 0);
    dc::Snapshot again(migrated.begin(), migrated.end());
    auto rewritten = dc::encode(again, fp);
    assert(rewritten.size() == legacy.bytes && rewritten.size() < old.size());
    dc::Map reloaded;
    assert(dc::decode(rewritten, fp, reloaded).ok && reloaded.size() == 2 &&
           reloaded.at(1).source == migrated.at(1).source);
    auto oldDamaged = old; oldDamaged[60] ^= 1;
    assert(!dc::decode(oldDamaged, fp, reloaded).ok);

    // A record without SPIR-V is never written.
    dc::Snapshot invalid{{3, entry(a, true, 1)}};
    invalid[0].second.words.reset();
    assert(dc::encode(invalid, fp).empty());

    // Pending compilations: an older binary's publication keeps a newer lease.
    using Entry = std::pair<ShaderSourceId, std::weak_ptr<int>>;
    std::unordered_map<unsigned, Entry> pending;
    auto oldBinary = std::make_shared<int>(1), newBinary = std::make_shared<int>(2);
    pending.emplace(7, Entry{shader_source_id(a), oldBinary});
    pending[7] = Entry{shader_source_id(b), newBinary};
    release_pending_shader_source(pending, 7u, oldBinary);
    assert(pending.size() == 1 && pending.at(7).first == shader_source_id(b));
    release_pending_shader_source(pending, 7u, newBinary);
    assert(pending.empty());
}
