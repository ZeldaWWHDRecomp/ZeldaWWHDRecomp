// SPDX-License-Identifier: MPL-2.0
// Link pose icons: reading Link's model out of the player's Link.bfres and drawing one pose of LkAnm.bfres
// (link_poses.h). Wii U BFRES version 3: big-endian, every offset relative to the field holding it. The
// skeleton and the animation curves are story_resident_walk_clip.h's readers; textures are untiled by
// lyt_build and decoded by game_layouts (BC1-BC5, RGBA8); the archive by item_icons (SARC, Yaz0).
//
// Idea from the GreenNaugahyde Android fork (MPL-2.0), its link_icons.cpp; reimplemented on our readers.
#include "link_poses.h"

#include <cstring>
#include <map>
#include <stdexcept>

#include "game_layouts.h"
#include "item_icons.h"
#include "lyt_build.h"
#include "story_resident_walk_clip.h"

namespace link_poses {
namespace wc = story_resident_walk_clip;

struct Shape {
    std::string name;
    int texture = -1;               // Model::textures, -1: a flat colour
    int overlay = -1;               // the eyes: the pupil (sampler _a1, "hitomi") over the eye's white
    std::array<uint8_t, 3> flat{200, 200, 200};
    int bone = 0;                   // the shape's own bone (skin count 0)
    int skin = 0;                   // vertex skin count
    std::vector<V3> pos, nrm;
    std::vector<float> uv;          // 2 per vertex
    std::vector<std::array<uint8_t, 4>> idx;   // bones (already mapped from smooth matrices)
    std::vector<std::array<float, 4>> weight;
    std::vector<uint32_t> tris;
};
struct Part {  // one model of Link.bfres: "cl" (Link) or a piece of his gear
    std::vector<wc::Bone> bones;
    std::vector<M34> rest_world, inv_rest;
    std::vector<Shape> shapes;
};
struct Model {
    Part link;
    Part sword, grip, shield;
    bool has_sword = false, has_grip = false, has_shield = false;
    int left_hand = -1, right_hand = -1;
    std::vector<Texture> textures;
};

namespace {

M34 to_m34(const wc::M3& r, const std::array<double, 3>& t) {
    M34 m;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) m.m[i][j] = float(r.m[i][j]);
        m.m[i][3] = float(t[i]);
    }
    return m;
}
std::vector<M34> world_of(const std::vector<wc::Bone>& bones, const wc::Pose& p) {
    const wc::World w = wc::fk(bones, p);
    std::vector<M34> out;
    for (size_t i = 0; i < bones.size(); i++) out.push_back(to_m34(w.rot[i], w.pos[i]));
    return out;
}

// one vertex attribute as up to 4 floats (the formats Link's model uses, and the common others)
bool attribute(wc::Reader& r, uint64_t at, uint32_t fmt, float out[4]) {
    out[0] = out[1] = out[2] = 0, out[3] = 1;
    auto f16 = [](uint16_t h) {
        const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
        float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? INFINITY : std::ldexp(float(m | 1024), int(e) - 25);
        return s ? -v : v;
    };
    switch (fmt) {
    case 0x811: for (int k = 0; k < 3; k++) out[k] = float(r.f32(at + 4 * k)); return true;
    case 0x813: for (int k = 0; k < 4; k++) out[k] = float(r.f32(at + 4 * k)); return true;
    case 0x80D: for (int k = 0; k < 2; k++) out[k] = float(r.f32(at + 4 * k)); return true;
    case 0x806: out[0] = float(r.f32(at)); return true;
    case 0x808: for (int k = 0; k < 2; k++) out[k] = f16(r.u16(at + 2 * k)); return true;
    case 0x80F: for (int k = 0; k < 4; k++) out[k] = f16(r.u16(at + 2 * k)); return true;
    case 0x000: out[0] = r.u8(at) / 255.0f; return true;
    case 0x004: for (int k = 0; k < 2; k++) out[k] = r.u8(at + k) / 255.0f; return true;
    case 0x00A: for (int k = 0; k < 4; k++) out[k] = r.u8(at + k) / 255.0f; return true;
    case 0x100: out[0] = r.u8(at); return true;
    case 0x104: for (int k = 0; k < 2; k++) out[k] = r.u8(at + k); return true;
    case 0x10A: for (int k = 0; k < 4; k++) out[k] = r.u8(at + k); return true;
    case 0x105: out[0] = float(r.u32(at)); return true;                                   // uint 32
    case 0x10C: for (int k = 0; k < 2; k++) out[k] = float(r.u32(at + 4 * k)); return true;  // uint 32 x 2 (bone indices)
    case 0x110: for (int k = 0; k < 3; k++) out[k] = float(r.u32(at + 4 * k)); return true;  // ... x 3
    case 0x112: for (int k = 0; k < 4; k++) out[k] = float(r.u32(at + 4 * k)); return true;  // ... x 4
    case 0x007: for (int k = 0; k < 2; k++) out[k] = r.u16(at + 2 * k) / 65535.0f; return true;
    case 0x207: for (int k = 0; k < 2; k++) out[k] = std::max(-1.0f, int16_t(r.u16(at + 2 * k)) / 32767.0f); return true;
    case 0x20A: for (int k = 0; k < 4; k++) out[k] = std::max(-1.0f, int8_t(r.u8(at + k)) / 127.0f); return true;
    case 0x20B: {  // snorm 10_10_10_2, x in the low bits
        const uint32_t v = r.u32(at);
        for (int k = 0; k < 3; k++) {
            int c = int((v >> (10 * k)) & 0x3FF);
            if (c & 0x200) c -= 0x400;
            out[k] = std::max(-1.0f, c / 511.0f);
        }
        return true;
    }
    default: return false;
    }
}

// a BFRES texture (FTEX), its first level as RGBA; false for a format we don't decode
bool read_texture(wc::Reader& r, uint64_t ftex, Texture& out) {
    if (!r.in(ftex, 0xC0) || std::memcmp(&r.b[ftex], "FTEX", 4)) return false;
    const uint64_t s = ftex + 4;
    const uint32_t w = r.u32(s + 4), h = r.u32(s + 8), fmt = r.u32(s + 0x14), len = r.u32(s + 0x20), mode = r.u32(s + 0x30), swz = r.u32(s + 0x34);
    const uint64_t data = r.rel(ftex + 0xB0);
    if (!w || !h || w > 2048 || h > 2048 || !data || !r.in(data, len)) return false;
    int bc = 0;
    switch (fmt & 0xFF) {
    case 0x1A: bc = 0; break;
    case 0x31: bc = 1; break;
    case 0x32: bc = 2; break;
    case 0x33: bc = 3; break;
    case 0x34: bc = 4; break;
    case 0x35: bc = 5; break;
    default: return false;
    }
    const uint32_t bpp = bc == 0 ? 32 : (bc == 1 || bc == 4) ? 64 : 128;
    const uint32_t ew = bc ? (w + 3) / 4 : w, eh = bc ? (h + 3) / 4 : h;
    const Bytes lin = lyt_build::untile(Bytes(r.b.begin() + data, r.b.begin() + data + len), ew, eh, bpp, int(mode), (swz >> 8) & 7);
    if (lin.empty()) return false;
    Bytes px = bc ? game_layouts::decode_bc(bc, lin, w, h) : lin;
    // the texture's channel selection (GX2 component selectors: 0-3 R G B A, 4 zero, 5 one)
    const uint8_t sel[4] = {r.u8(ftex + 0x88), r.u8(ftex + 0x89), r.u8(ftex + 0x8A), r.u8(ftex + 0x8B)};
    if (!(sel[0] == 0 && sel[1] == 1 && sel[2] == 2 && sel[3] == 3))
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            const uint8_t c[4] = {px[i], px[i + 1], px[i + 2], px[i + 3]};
            for (int k = 0; k < 4; k++) px[i + k] = sel[k] < 4 ? c[sel[k]] : sel[k] == 4 ? 0 : 255;
        }
    out.w = int(w), out.h = int(h), out.rgba = std::move(px);
    return true;
}

// the texture of a material's sampler (e.g. "_a0", its colour), else (fallback) its first texture
std::string sampler_texture(wc::Reader& r, uint64_t mat, const char* want, bool fallback) {
    const unsigned n = r.u8(mat + 0x10);
    const uint64_t refs = r.rel(mat + 0x28);
    std::vector<std::string> names;
    for (unsigned i = 0; i < n && refs && r.ok; i++) names.push_back(r.str(r.rel(refs + 8 * i)));
    const uint64_t first = r.rel(mat + 0x2C);
    for (const auto& [sampler, at] : r.group(mat + 0x30)) {
        const uint64_t index = first && at >= first ? (at - first) / 24 : ~0ull;
        if (sampler == want && index < names.size()) return names[size_t(index)];
    }
    return names.empty() || !fallback ? std::string() : names[0];
}

// a model of the file with its skeleton and shapes (every shape's first, most detailed mesh)
bool read_part(wc::Reader& r, const std::string& name, Model& m, std::map<std::string, int>& tex_index, Part& out, std::string& why,
               bool (*keep)(const std::string& shape) = nullptr) {
    if (!wc::skeleton(r.b, name, out.bones, why)) return false;
    const uint64_t fmdl = wc::find(r.group(0x20), name);
    const uint64_t sk = r.rel(fmdl + 0xC), bones = r.rel(sk + 0x14);
    std::vector<int> smooth_bone;  // smooth matrix index -> bone
    for (size_t i = 0; i < out.bones.size(); i++) {
        const int16_t s = int16_t(r.u16(bones + 0x40 * i + 8));
        if (s >= 0) {
            if (size_t(s) >= smooth_bone.size()) smooth_bone.resize(size_t(s) + 1, 0);
            smooth_bone[size_t(s)] = int(i);
        }
    }
    out.rest_world = world_of(out.bones, wc::rest(out.bones));
    for (const M34& w : out.rest_world) out.inv_rest.push_back(w.rigid_inverse());
    const uint64_t vertex_array = r.rel(fmdl + 0x10);
    const auto mats = r.group(fmdl + 0x18);
    // the file's textures by name (index group 1)
    const auto files = r.group(0x24);
    for (const auto& [sname, at] : r.group(fmdl + 0x14)) {
        if (!r.ok) break;
        if (keep && !keep(sname)) continue;
        Shape s;
        s.name = sname;
        const unsigned mi = r.u16(at + 0xE);
        s.bone = r.u16(at + 0x10);
        s.skin = r.u8(at + 0x16);
        if (s.bone >= int(out.bones.size()) || s.skin > 4) continue;
        auto texture = [&](const std::string& tn) {
            if (tn.empty()) return -1;
            auto it = tex_index.find(tn);
            if (it == tex_index.end()) {
                Texture t;
                const uint64_t ftex = wc::find(files, tn);
                const int index = ftex && read_texture(r, ftex, t) ? int(m.textures.size()) : -1;
                if (index >= 0) m.textures.push_back(std::move(t));
                it = tex_index.emplace(tn, index).first;
            }
            return it->second;
        };
        if (mi < mats.size()) {
            s.texture = texture(sampler_texture(r, mats[mi].second, "_a0", true));
            const std::string second = sampler_texture(r, mats[mi].second, "_a1", false);
            if (second == "hitomi") s.overlay = texture(second);
        }
        const uint64_t vtx = vertex_array + 0x20 * uint64_t(r.u16(at + 0x12));
        const unsigned nattr = r.u8(vtx + 4), nbuf = r.u8(vtx + 5);
        const uint32_t count = r.u32(vtx + 8);
        if (!count || count > 200000) continue;
        const uint64_t attrs = r.rel(vtx + 0x10), bufs = r.rel(vtx + 0x18);
        struct Col { uint64_t data = 0; uint32_t stride = 0, offset = 0, fmt = 0; bool ok = false; };
        std::map<std::string, Col> cols;
        for (unsigned i = 0; i < nattr && r.ok; i++) {
            const uint64_t a = attrs + 12 * uint64_t(i);
            Col c;
            const unsigned bi = r.u8(a + 4);
            c.offset = r.u16(a + 6), c.fmt = r.u32(a + 8);
            if (bi >= nbuf) continue;
            const uint64_t buf = bufs + 0x18 * uint64_t(bi);
            c.stride = r.u16(buf + 0xC), c.data = r.rel(buf + 0x14);
            c.ok = c.data && c.stride && r.in(c.data, uint64_t(c.stride) * count);
            cols[r.str(r.rel(a))] = c;
        }
        if (!cols["_p0"].ok) continue;
        auto read = [&](const char* an, uint32_t k, float v[4]) {
            const Col& c = cols[an];
            return c.ok && attribute(r, c.data + uint64_t(c.stride) * k + c.offset, c.fmt, v);
        };
        float v[4];
        for (uint32_t k = 0; k < count && r.ok; k++) {
            read("_p0", k, v);
            s.pos.push_back({v[0], v[1], v[2]});
            if (read("_n0", k, v)) s.nrm.push_back(normalize({v[0], v[1], v[2]}));
            else s.nrm.push_back({0, 0, 1});
            if (read("_u0", k, v)) s.uv.push_back(v[0]), s.uv.push_back(v[1]);
            else s.uv.push_back(0), s.uv.push_back(0);
            std::array<uint8_t, 4> bi{};
            std::array<float, 4> bw{};
            if (s.skin >= 1 && read("_i0", k, v)) {
                for (int j = 0; j < s.skin; j++) {
                    const int si = int(v[j]);
                    bi[size_t(j)] = uint8_t(si >= 0 && size_t(si) < smooth_bone.size() ? smooth_bone[size_t(si)] : 0);
                }
                if (s.skin == 1) bw[0] = 1;
                else if (read("_w0", k, v)) for (int j = 0; j < s.skin; j++) bw[size_t(j)] = v[j];
                else bw[0] = 1;
            }
            s.idx.push_back(bi), s.weight.push_back(bw);
        }
        // the first mesh: an indexed triangle list
        const uint64_t mesh = r.rel(at + 0x24);
        const uint32_t prim = r.u32(mesh), ifmt = r.u32(mesh + 4), n = r.u32(mesh + 8), first = r.u32(mesh + 0x18);
        const uint64_t ib = r.rel(r.rel(mesh + 0x14) + 0x14);
        if (prim != 4 || (ifmt != 4 && ifmt != 9) || !ib || n > 600000 || !r.in(ib, uint64_t(n) * (ifmt == 9 ? 4 : 2))) continue;
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            uint32_t t[3];
            for (int j = 0; j < 3; j++) t[j] = (ifmt == 9 ? r.u32(ib + 4 * uint64_t(i + j)) : r.u16(ib + 2 * uint64_t(i + j))) + first;
            if (t[0] < count && t[1] < count && t[2] < count) s.tris.insert(s.tris.end(), t, t + 3);
        }
        if (!s.tris.empty()) out.shapes.push_back(std::move(s));
    }
    if (!r.ok) return why = "bad model " + name, false;
    if (out.shapes.empty()) return why = "no shapes in " + name, false;
    return true;
}

// Link's own shapes: not the hurt eyes / brows (the "dam" variants the game shows when he is hit)
bool link_shape(const std::string& s) { return s.find("dam") == std::string::npos; }

struct Posed {
    std::vector<V3> pos, nrm;
};
Posed skin(const Part& p, const Shape& s, const std::vector<M34>& world, const M34& base) {
    Posed out;
    out.pos.resize(s.pos.size()), out.nrm.resize(s.pos.size());
    std::vector<M34> skin_m(p.bones.size());
    for (size_t b = 0; b < p.bones.size(); b++) skin_m[b] = base * world[b] * p.inv_rest[b];
    const M34 rigid = base * world[size_t(s.bone)];
    for (size_t k = 0; k < s.pos.size(); k++) {
        M34 m;
        if (s.skin == 0) {
            m = rigid;  // the vertices are in the shape's bone's space
        } else if (s.skin == 1) {
            m = base * world[s.idx[k][0]];  // one bone: its vertices are in that bone's space
        } else {
            m = M34::blend(skin_m[s.idx[k][0]], s.weight[k][0], skin_m[s.idx[k][1]], s.weight[k][1]);
            for (int j = 2; j < s.skin; j++) m = M34::blend(m, 1, skin_m[s.idx[k][size_t(j)]], s.weight[k][size_t(j)]);
        }
        out.pos[k] = m.point(s.pos[k]);
        out.nrm[k] = normalize(m.dir(s.nrm[k]));
    }
    return out;
}

}  // namespace

bool read_pack(const Bytes& pack, Bytes& link_bfres, Bytes& anim_bfres, std::string& why) {
    try {
        const auto files = item_icons::read_sarc(pack);
        auto inner = [&](const char* arc, const char* file, Bytes& out) {
            const auto it = files.find(arc);
            if (it == files.end()) throw std::runtime_error(std::string(arc) + " not in the pack");
            const auto sarc = item_icons::read_sarc(item_icons::yaz0_decode(it->second));
            const auto f = sarc.find(file);
            if (f == sarc.end()) throw std::runtime_error(std::string(file) + " not in " + arc);
            out = f->second;
        };
        inner("Link.szs", "Link.bfres", link_bfres);
        inner("LkAnm.szs", "LkAnm.bfres", anim_bfres);
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
}

std::shared_ptr<const Model> load(const Bytes& link_bfres, std::string& why) {
    auto m = std::make_shared<Model>();
    wc::Reader r{link_bfres};
    if (!wc::fres(r)) return why = "Link.bfres is not a BFRES file", nullptr;
    std::map<std::string, int> tex;
    if (!read_part(r, "cl", *m, tex, m->link, why, link_shape)) return nullptr;
    m->left_hand = wc::index_of(m->link.bones, "cl_LhandA");
    m->right_hand = wc::index_of(m->link.bones, "cl_RhandA");
    std::string ignored;
    // his gear (optional: a pose without it still draws)
    m->has_sword = read_part(r, "swa", *m, tex, m->sword, ignored);
    m->has_grip = read_part(r, "swgripa", *m, tex, m->grip, ignored);
    m->has_shield = read_part(r, "sha", *m, tex, m->shield, ignored);
    return m;
}

bool render(const Model& m, const Bytes& anim_bfres, const PoseSpec& pose, Bytes& rgba, std::string& why, int size) {
    wc::Clip clip;
    if (!wc::clip(anim_bfres, pose.anim, clip, why)) return false;
    std::vector<int> track(m.link.bones.size(), -1);
    for (size_t i = 0; i < m.link.bones.size(); i++)
        for (size_t j = 0; j < clip.bones.size(); j++)
            if (clip.bones[j].name == m.link.bones[i].name) track[i] = int(j);
    const double frame = std::clamp(double(pose.frame), 0.0, double(clip.frames > 0 ? clip.frames - 1 : 0));
    const std::vector<M34> world = world_of(m.link.bones, wc::sample(m.link.bones, clip, track, frame));
    const M34 cam = camera(pose.yaw, pose.pitch);
    // everything posed into camera space
    struct Item {
        const Shape* shape;
        Posed posed;
        int texture;
    };
    std::vector<Item> items;
    for (const Shape& s : m.link.shapes) items.push_back({&s, skin(m.link, s, world, cam), s.texture});
    auto attach = [&](const Part& p, bool has, int hand) {
        if (!has || hand < 0) return;
        const M34 base = cam * world[size_t(hand)];
        for (const Shape& s : p.shapes) items.push_back({&s, skin(p, s, p.rest_world, base), s.texture});
    };
    if (pose.gear == Gear::kSword || pose.gear == Gear::kSwordShield) {
        attach(m.sword, m.has_sword, m.left_hand);
        attach(m.grip, m.has_grip, m.left_hand);
    }
    if (pose.gear == Gear::kSwordShield) attach(m.shield, m.has_shield, m.right_hand);
    // framing: the pose's bounds fill the picture with a margin, centred
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (const Item& it : items)
        for (const V3& p : it.posed.pos) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
    if (!(x1 > x0) || !(y1 > y0)) return why = "empty pose", false;
    const int px = size * kSupersample;
    const float margin = px * 0.06f;
    const float scale = (px - 2 * margin) / std::max(x1 - x0, y1 - y0);
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    const V3 light = normalize({-0.45f, 0.65f, 0.6f});  // from the upper left, in front
    Canvas c(px, px);
    for (const Item& it : items) {
        const Shape& s = *it.shape;
        const Texture* tex = it.texture >= 0 && size_t(it.texture) < m.textures.size() ? &m.textures[size_t(it.texture)] : nullptr;
        std::vector<ScreenVertex> sv(s.pos.size());
        for (size_t k = 0; k < sv.size(); k++) {
            const V3& p = it.posed.pos[k];
            sv[k] = {px / 2 + (p.x - cx) * scale, px / 2 - (p.y - cy) * scale, p.z, s.uv[2 * k], s.uv[2 * k + 1], dot(it.posed.nrm[k], light)};
        }
        const Texture* over = s.overlay >= 0 && size_t(s.overlay) < m.textures.size() ? &m.textures[size_t(s.overlay)] : nullptr;
        for (size_t t = 0; t + 2 < s.tris.size(); t += 3)
            draw_triangle(c, sv[s.tris[t]], sv[s.tris[t + 1]], sv[s.tris[t + 2]], tex, s.flat, over);
    }
    rgba = downsample(c, kSupersample);
    outline(rgba, size, size, size / 64.0f);
    return true;
}

}  // namespace link_poses
