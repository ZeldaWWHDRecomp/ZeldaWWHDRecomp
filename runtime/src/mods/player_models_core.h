/* SPDX-License-Identifier: MPL-2.0
 * GameCube player models in the game's memory (player_models.h), the part without the game: what a converted
 * Link.bfres (runtime/src/gcmodel) changes against the game's own, and writing it in and out.
 *
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/mods/player_models.cpp,
 * taken over as it is; here behind a memory accessor so tests can run it without the game.
 *
 * Why this works: the converter keeps every structure of Link.bfres where it was (shapes, materials, bones,
 * texture headers); it replaces texture data in place and appends new vertex, index and skin-bone data at the
 * file's end, pointed at by relative offsets. The game, loading the file, writes absolute data pointers into the
 * vertex/index buffer and texture headers and binds the materials to their shaders: none of those words is one
 * the converter changes. So a model goes in as
 *   - the words where its Link.bfres differs from the game's (structure fields and texture data),
 *   - its appended part copied to an area of its own (kArea), the relative offsets that point into it aimed there
 *     (32-bit offsets reach any guest address),
 *   - each vertex/index buffer's data pointer (its +0, set by the game at load) aimed at its data there,
 * and goes out by writing the game's words back (a data pointer: the file's address plus its own offset).
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace player_models {

// the area for a model's appended part: unused guest memory between MEM2 (ends 0x50000000) and the runtime's
// objects (0x60000000); Metal maps it for the GPU (metal_main.mm), save states keep its used part
inline constexpr uint32_t kArea = 0x58000000;
inline constexpr uint32_t kAreaSize = 0x00400000;  // 4 MiB (the biggest of the fork's 62 test models adds 1.5 MiB)
inline constexpr uint32_t kHeader = 0x1000;        // "WWPM", its size, the model's id; the appended part after it
inline constexpr uint32_t kMagic = 0x4D505757;     // "WWPM" as stored (host order, as the fork)
inline constexpr uint32_t kStep = 0x100;

using Bytes = std::vector<uint8_t>;
// a guest address to host memory (the game's: mem::ptr; tests: a buffer of their own)
using Memory = std::function<uint8_t*(uint32_t)>;

inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline void put_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }

// a Link.bfres (version 3, big-endian) read as far as needed here
struct Bfres {
    const Bytes& d;
    explicit Bfres(const Bytes& b) : d(b) {}
    bool in(uint64_t o, uint64_t n = 4) const { return o + n <= d.size(); }
    uint32_t u32(uint32_t o) const { return in(o) ? be32(&d[o]) : 0; }
    uint16_t u16(uint32_t o) const { return in(o, 2) ? (uint16_t)(d[o] << 8 | d[o + 1]) : 0; }
    uint8_t u8(uint32_t o) const { return in(o, 1) ? d[o] : 0; }
    uint32_t rel(uint32_t o) const {
        int32_t x = (int32_t)u32(o);
        return x ? (uint32_t)((int64_t)o + x) : 0;
    }
    std::vector<std::pair<std::string, uint32_t>> group(uint32_t o) const {
        std::vector<std::pair<std::string, uint32_t>> out;
        if (!o || !in(o + 8)) return out;
        uint32_t n = u32(o + 4);
        for (uint32_t i = 1; i <= n && in(o + 8 + i * 16 + 16); i++) {
            uint32_t s = rel(o + 8 + i * 16 + 8);
            std::string name;
            while (s && s < d.size() && d[s] && name.size() < 128) name += (char)d[s++];
            out.push_back({name, rel(o + 8 + i * 16 + 12)});
        }
        return out;
    }
    uint32_t section(int i) const { return rel(0x20 + i * 4); }
};

struct Buffer { uint32_t at, data, size; };  // a vertex/index buffer struct, its data's offset and size
struct Layout {
    std::vector<Buffer> buffers;
    std::vector<std::pair<std::string, std::array<uint32_t, 4>>> textures;  // name, image/mip offset and size
    std::vector<std::pair<uint32_t, uint32_t>> data;                       // data ranges (textures, buffers)
};

inline Layout layout(const Bytes& d) {
    Bfres f(d);
    Layout l;
    for (auto& [name, o] : f.group(f.section(1))) {
        std::array<uint32_t, 4> t{f.rel(o + 0xB0), f.u32(o + 0x24), f.rel(o + 0xB4), f.u32(o + 0x2C)};
        l.textures.push_back({name, t});
        l.data.push_back({t[0], t[1]});
        if (t[3]) l.data.push_back({t[2], t[3]});
    }
    for (auto& [mname, fm] : f.group(f.section(0))) {
        uint32_t vbl = f.rel(fm + 16);
        for (uint32_t i = 0, n = f.u16(fm + 32); i < n; i++) {
            uint32_t o = vbl + i * 0x20, bl = f.rel(o + 0x18);
            for (uint32_t k = 0; k < f.u8(o + 5); k++) {
                uint32_t bb = bl + k * 0x18;
                l.buffers.push_back({bb, f.rel(bb + 0x14), f.u32(bb + 4)});
            }
        }
        for (auto& [sname, so] : f.group(f.rel(fm + 20))) {
            uint32_t mesh0 = f.rel(so + 0x24);
            for (uint32_t k = 0; k < f.u8(so + 0x17); k++) {
                uint32_t ib = f.rel(mesh0 + k * 0x1C + 0x14);
                l.buffers.push_back({ib, f.rel(ib + 0x14), f.u32(ib + 4)});
            }
        }
    }
    for (auto& b : l.buffers) l.data.push_back({b.data, b.size});
    return l;
}

struct Run { uint32_t off, len, at; };  // file offset, length, place in Model::bytes
// a word aimed into the appended part (target: offset in the model's file); a data pointer (not rel) goes back to the
// file's address plus `game`, the game's own data offset
struct Fix { uint32_t off, target; bool rel; uint32_t game; };
struct Probe { uint32_t off, model, game; };  // a word telling the model from the game's (raw big-endian)

struct Model {
    std::string id;
    std::string key;  // its Link.szs's path, size and time (imported again: made again)
    std::vector<Run> runs;
    Bytes bytes;   // the model's words of the runs
    Bytes revert;  // the game's words of the runs
    std::vector<Fix> fixes;
    Bytes tail;          // the appended part
    uint32_t tail0 = 0;  // its offset in the model's file
    std::vector<Probe> probes;
};

// what the model changes against the game's file; null and `why` if it doesn't fit the game's
inline std::shared_ptr<Model> make_model(const std::string& id, const Bytes& game, const Layout& gl, const Bytes& m, std::string& why) {
    const uint32_t n = (uint32_t)game.size();
    auto fail = [&](const char* w) {
        why = w;
        return std::shared_ptr<Model>();
    };
    if (m.size() < n) return fail("smaller than the game's Link.bfres");
    if (n < 0x40 || memcmp(m.data(), game.data(), 4)) return fail("not a Link.bfres");
    Layout ml = layout(m);
    if (ml.textures.size() != gl.textures.size() || ml.buffers.size() != gl.buffers.size()) return fail("not made from this game's Link");
    for (size_t i = 0; i < gl.textures.size(); i++)
        if (ml.textures[i] != gl.textures[i]) return fail("its textures aren't where the game's are");
    for (size_t i = 0; i < gl.buffers.size(); i++)
        if (ml.buffers[i].at != gl.buffers[i].at) return fail("its buffers aren't where the game's are");
    auto model = std::make_shared<Model>();
    model->id = id;
    model->tail0 = n & ~(kStep - 1);  // (the data's alignment kept)
    if (m.size() - model->tail0 > kAreaSize - kHeader) return fail("too big");
    model->tail.assign(m.begin() + model->tail0, m.end());
    // the game's data ranges: no relative offsets there (texture bytes may look like one)
    std::vector<uint8_t> isData((n + 3) / 4, 0);
    for (auto [o, s] : gl.data)
        for (uint64_t w = o / 4; w < ((uint64_t)o + s + 3) / 4 && w < isData.size(); w++) isData[w] = 1;
    std::vector<uint8_t> isPtr((n + 3) / 4, 0);
    for (size_t i = 0; i < ml.buffers.size(); i++) {
        const Buffer& b = ml.buffers[i];
        if (b.data != gl.buffers[i].data) {
            if (b.data < n) return fail("a buffer moved inside the file");
            if (b.at / 4 >= isPtr.size()) return fail("a buffer outside the file");
            model->fixes.push_back({b.at, b.data, false, gl.buffers[i].data});
            isPtr[b.at / 4] = 1;
        }
    }
    // the differing words (the header's file size stays the game's)
    std::vector<uint32_t> structural;
    for (uint32_t o = 0; o + 4 <= n; o += 4) {
        if (o == 0xC || memcmp(&m[o], &game[o], 4) == 0) continue;
        if (isPtr[o / 4]) continue;  // (the data pointers: Fix)
        if (!model->runs.empty() && model->runs.back().off + model->runs.back().len == o) model->runs.back().len += 4;
        else model->runs.push_back({o, 4, (uint32_t)model->bytes.size()});
        model->bytes.insert(model->bytes.end(), &m[o], &m[o] + 4);
        model->revert.insert(model->revert.end(), &game[o], &game[o] + 4);
        if (isData[o / 4]) continue;
        int32_t x = (int32_t)be32(&m[o]);
        int64_t t = (int64_t)o + x;
        if (x && (x & 3) == 0 && t >= n && t < (int64_t)m.size()) model->fixes.push_back({o, (uint32_t)t, true, 0});
        else structural.push_back(o);
    }
    // sample words to tell it in memory: structure fields, spread over the file
    for (size_t k = 0, want = std::min<size_t>(64, structural.size()); k < want; k++) {
        uint32_t o = structural[k * structural.size() / want];
        model->probes.push_back({o, be32(&m[o]), be32(&game[o])});
    }
    return model;
}

// the file at `base` holds the model (true) or the game's words (false) there
inline bool holds(const Memory& mem, uint32_t base, const Model& m, bool model) {
    for (const Probe& p : m.probes)
        if (be32(mem(base + p.off)) != (model ? p.model : p.game)) return false;
    for (const Fix& f : m.fixes)
        if (!f.rel && be32(mem(base + f.off)) != (model ? kArea + kHeader + (f.target - m.tail0) : base + f.game)) return false;
    return true;
}

inline void write_out(const Memory& mem, uint32_t base, const Model& m) {
    for (const Run& r : m.runs) memcpy(mem(base + r.off), &m.revert[r.at], r.len);
    for (const Fix& f : m.fixes)
        if (!f.rel) put_be32(mem(base + f.off), base + f.game);
    memset(mem(kArea), 0, kHeader);  // (the area's header: nothing in)
}

inline void write_in(const Memory& mem, uint32_t base, const Model& m) {
    uint8_t* h = mem(kArea);
    memset(h, 0, kHeader);
    memcpy(mem(kArea + kHeader), m.tail.data(), m.tail.size());
    for (const Run& r : m.runs) memcpy(mem(base + r.off), &m.bytes[r.at], r.len);
    for (const Fix& f : m.fixes) {
        uint32_t to = kArea + kHeader + (f.target - m.tail0);
        put_be32(mem(base + f.off), f.rel ? to - (base + f.off) : to);
    }
    put_be32(h + 4, (uint32_t)m.tail.size());
    snprintf((char*)h + 8, 120, "%s", m.id.c_str());
    memcpy(h, &kMagic, 4);
}

// the model id the area's header notes ("" none)
inline std::string noted(const Memory& mem) {
    const uint8_t* h = mem(kArea);
    if (memcmp(h, &kMagic, 4) != 0) return {};
    return std::string((const char*)h + 8, strnlen((const char*)h + 8, 120));
}

// a new game in a save file: no event of the story done yet (the file select's fresh slot; the game's
// two set from the start excepted)
inline bool new_game(const uint8_t* events /* save + 0x624, 0x100 bytes */) {
    for (int i = 0; i < 0x100; i++)
        if (events[i] && i != 0x7E && i != 0xBE) return false;
    return true;
}

}  // namespace player_models
