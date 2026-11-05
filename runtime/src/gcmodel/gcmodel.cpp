/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
// GameCube player models in HD (gcmodel.h): a port of the fork's tools/gcmodels/convert.py and survey.py.
#include "gcmodel.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <tuple>

#include "gcm_image.h"
#include "gcm_io.h"
#include "gcm_j3d.h"

namespace gcmodel {
namespace {

#include "gcm_baseline.inc"

double base_of(const std::pair<const char*, double>* t, size_t n, const std::string& k, double dflt) {
    for (size_t i = 0; i < n; i++)
        if (k == t[i].first) return t[i].second;
    return dflt;
}
double base_geometry(const std::string& m) { return base_of(kBaseGeometry, sizeof(kBaseGeometry) / sizeof(*kBaseGeometry), m, 100); }
double base_texture(const std::string& t) { return base_of(kBaseTexture, sizeof(kBaseTexture) / sizeof(*kBaseTexture), t, 0); }

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string family(const std::string& n) { return n.substr(0, n.find('.')); }

// ---- the HD file

struct Bfres {
    Bytes d;
    uint8_t u8(size_t o) const { return d.at(o); }
    uint16_t u16(size_t o) const { return be16(&d.at(o)); }
    int16_t s16(size_t o) const { return bes16(&d.at(o)); }
    uint32_t u32(size_t o) const { return be32(&d.at(o)); }
    float f32(size_t o) const { return bef32(&d.at(o)); }
    size_t rel(size_t o) const {
        const int32_t v = (int32_t)u32(o);
        return v ? (size_t)((int64_t)o + v) : 0;
    }
    void set_rel(size_t o, size_t target) { put32(&d[o], (uint32_t)(int32_t)((int64_t)target - (int64_t)o)); }
    std::string str(size_t o) const {
        std::string s;
        if (!o) return s;
        while (o < d.size() && d[o]) s += (char)d[o++];
        return s;
    }
    std::vector<std::pair<std::string, size_t>> group(size_t o) const {
        std::vector<std::pair<std::string, size_t>> r;
        if (!o) return r;
        const uint32_t n = u32(o + 4);
        for (uint32_t i = 1; i <= n; i++) r.push_back({str(rel(o + 8 + i * 16 + 8)), rel(o + 8 + i * 16 + 12)});
        return r;
    }
    size_t section(int i) const { return rel(0x20 + i * 4); }
    size_t append(const uint8_t* data, size_t n, size_t align) {
        d.resize((d.size() + align - 1) / align * align, 0);
        const size_t at = d.size();
        d.insert(d.end(), data, data + n);
        put32(&d[0xC], (uint32_t)d.size());
        return at;
    }
    void putf(size_t o, float v) { putf32(&d[o], v); }
};

struct Bone {
    std::string name;
    int index = 0, parent = -1, smooth = -1, rigid = -1;
    Mat local{}, world{};
};

std::vector<Bone> hd_skeleton(const Bfres& f, size_t fm) {
    const size_t sk = f.rel(fm + 12);
    std::vector<Bone> bones;
    for (auto& [name, bo] : f.group(f.rel(sk + 0x10))) {
        Bone b;
        b.name = name;
        b.index = f.u16(bo + 4), b.parent = f.s16(bo + 6), b.smooth = f.s16(bo + 8), b.rigid = f.s16(bo + 0xA);
        const uint32_t flags = f.u32(bo + 0x10);
        Vec3 s{f.f32(bo + 0x14), f.f32(bo + 0x18), f.f32(bo + 0x1C)}, t{f.f32(bo + 0x30), f.f32(bo + 0x34), f.f32(bo + 0x38)};
        const double r[4] = {f.f32(bo + 0x20), f.f32(bo + 0x24), f.f32(bo + 0x28), f.f32(bo + 0x2C)};
        if (flags & 0x1000) {
            b.local = mat_srt(s, {r[0], r[1], r[2]}, t);
        } else {
            const double x = r[0], y = r[1], z = r[2], w = r[3];
            const double rot[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                                      {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                                      {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) b.local[i][j] = rot[i][j] * s[j];
                b.local[i][3] = t[i];
            }
        }
        bones.push_back(b);
    }
    std::sort(bones.begin(), bones.end(), [](const Bone& a, const Bone& b) { return a.index < b.index; });
    for (auto& b : bones)
        b.world = b.parent < 0 || b.parent >= (int)bones.size() ? b.local : mat_mul(bones[b.parent].world, b.local);
    return bones;
}

struct Attr {
    int buffer, offset;
    uint32_t format;
};

int format_count(uint32_t f, bool& isFloat) {
    isFloat = true;
    switch (f) {
        case 0x811: return 3;
        case 0x813: return 4;
        case 0x80D: return 2;
        case 0x806: return 1;
    }
    isFloat = false;
    switch (f) {
        case 0x10C: return 2;
        case 0x110: return 3;
        case 0x112: return 4;
        case 0x105: return 1;
    }
    return 0;
}

std::string gc_material_name(const std::string& hd) {  // HD ear_2_ = GameCube ear(2)
    if (hd.size() > 2 && hd.back() == '_') {
        const std::string s = hd.substr(0, hd.size() - 1);
        const size_t u = s.rfind('_');
        if (u != std::string::npos && u + 1 < s.size() && std::all_of(s.begin() + u + 1, s.end(), ::isdigit))
            return s.substr(0, u) + "(" + s.substr(u + 1) + ")";
    }
    return hd;
}

// ---- HD's own textures and meshes (for comparing)

struct HDCache {
    const Bfres& f;
    std::map<std::string, size_t> tex, models;
    std::map<std::string, std::pair<uint32_t, Image>> images;
    std::map<std::string, std::vector<Vec3>> points;
    explicit HDCache(const Bfres& b) : f(b) {
        for (auto& [n, o] : f.group(f.section(1))) tex[n] = o;
        for (auto& [n, o] : f.group(f.section(0))) models[n] = o;
    }
    const std::pair<uint32_t, Image>& image(const std::string& n) {
        auto it = images.find(n);
        if (it != images.end()) return it->second;
        const size_t o = tex.at(n);
        const int w = f.u32(o + 8), h = f.u32(o + 0xC);
        const uint32_t fm = f.u32(o + 0x18), sz = f.u32(o + 0x24), tm = f.u32(o + 0x34), sw = f.u32(o + 0x38);
        const Bytes lin = gx2_untile_level0(fm, w, h, tm, sw, &f.d[f.rel(o + 0xB0)], sz);
        return images[n] = {fm, bc_decode(fm, w, h, lin)};
    }
    const std::vector<Vec3>& model_points(const std::string& name) {
        auto it = points.find(name);
        if (it != points.end()) return it->second;
        std::vector<Vec3>& pts = points[name];
        const size_t fm = models.at(name);
        const auto bones = hd_skeleton(f, fm);
        const size_t vbl = f.rel(fm + 16);
        for (auto& [sn, so] : f.group(f.rel(fm + 20))) {
            const int skin = f.u8(so + 0x16);
            const Bone& bone = bones.at(f.u16(so + 0x10));
            const size_t vb = vbl + f.u16(so + 0x12) * 0x20, al = f.rel(vb + 0x10), bl = f.rel(vb + 0x18);
            const uint32_t cnt = f.u32(vb + 8);
            int off = -1;
            for (int k = 0; k < f.u8(vb + 4); k++)
                if (f.str(f.rel(al + k * 12)) == "_p0") off = f.u16(al + k * 12 + 6);
            if (off < 0) continue;
            const int st = f.u16(bl + 0xC);
            const size_t data = f.rel(bl + 0x14);
            for (uint32_t i = 0; i < cnt; i++) {
                const size_t q = data + (size_t)i * st + off;
                Vec3 p{f.f32(q), f.f32(q + 4), f.f32(q + 8)};
                pts.push_back(skin ? p : mat_apply(bone.world, p));
            }
        }
        return pts;
    }
};

double geometry_near(HDCache& hd, const std::string& name, const Model& gc) {
    const auto& H = hd.model_points(name);
    std::vector<Vec3> G;
    for (auto& s : gc.shapes)
        for (auto& t : s.tris)
            for (auto& v : t) G.push_back(gc.position(v));
    if (G.empty() || H.empty()) return 100.0;
    Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (auto& p : H)
        for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
    double size = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    if (size <= 0) size = 1;
    const double lim = 0.01 * size;
    const size_t step = std::max<size_t>(1, G.size() / 400);
    size_t n = 0, close = 0;
    for (size_t i = 0; i < G.size(); i += step) {
        double best = 1e300;
        for (auto& h : H) {
            const double dx = G[i][0] - h[0], dy = G[i][1] - h[1], dz = G[i][2] - h[2];
            best = std::min(best, dx * dx + dy * dy + dz * dz);
        }
        n++;
        if (std::sqrt(best) < lim) close++;
    }
    return 100.0 * close / n;
}

Image to_image(const Texture& t) {
    Image im(t.width, t.height);
    im.px = t.rgba;
    return im;
}

double texture_diff(HDCache& hd, const std::string& name, const Texture& g) {
    const auto& [fm, hp] = hd.image(name);
    const Image hs = resize_box(hp, g.width, g.height);
    const uint32_t f = fm & 0x3F;
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < (size_t)g.width * g.height; i++) {
        const uint8_t *a = &g.rgba[i * 4], *b = &hs.px[i * 4];
        if (f == 0x34 || f == 0x35) {
            sum += std::fabs((double)a[0] - b[0]), n++;
            if (f == 0x35) sum += std::fabs((double)a[3] - b[1]), n++;
        } else {
            for (int c = 0; c < 3; c++) sum += std::fabs((double)a[c] - b[c]), n++;
        }
    }
    return n ? sum / n : 0;
}

// triangles with an area (a pack that takes a part out leaves one flat triangle in it)
int visible_triangles(const Model& gc) {
    int n = 0;
    for (auto& s : gc.shapes)
        for (auto& t : s.tris) {
            // (as stored, before the joints: effect parts have joints scaled to nothing in their bind pose)
            if (t[0].pos < 0 || t[1].pos < 0 || t[2].pos < 0) continue;
            const Vec3 a = gc.pos[t[0].pos], b = gc.pos[t[1].pos], c = gc.pos[t[2].pos];
            const Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const double x = u[1] * v[2] - u[2] * v[1], y = u[2] * v[0] - u[0] * v[2], z = u[0] * v[1] - u[1] * v[0];
            if (x * x + y * y + z * z > 1e-8) n++;
        }
    return n;
}

std::set<std::string> material_textures(const Bfres& f, size_t fm) {
    std::set<std::string> used, fam;
    for (auto& [n, mo] : f.group(f.rel(fm + 24))) {
        const size_t tl = f.rel(mo + 0x28);
        for (int k = 0; k < f.u8(mo + 0x11); k++) used.insert(f.str(f.rel(tl + k * 8)));
    }
    for (auto& u : used) fam.insert(family(u));
    for (auto& [n, o] : f.group(f.section(1)))
        if (fam.count(family(n)) && family(n) != "ZBtoonEX") used.insert(n);
    return used;
}

// ---- one part's meshes

struct Log {
    std::string text;
    void operator()(const std::string& s) { text += s + "\n"; }
};

// (`casual`: the casual clothes' body texture; the body's mesh is shared, so a texel counts as see-through only
// where it is so in both: Cloud's lower arm is cut out in the hero's clothes, his arm wrap in the casual ones)
const std::vector<uint8_t>* see_through(const Model& gc, const Shape& gs, std::map<std::string, std::vector<uint8_t>>& alphaCache,
                                        const Texture* casual) {
    if (gs.material < 0 || gs.material >= (int)gc.materialTextures.size() || gc.materialTextures[gs.material].empty()) return nullptr;
    const int ti = gc.materialTextures[gs.material][0];
    if (ti >= (int)gc.textureNames.size()) return nullptr;
    const std::string& name = gc.textureNames[ti];
    auto t = gc.textures.find(name);
    if (t == gc.textures.end()) return nullptr;
    auto it = alphaCache.find(name);
    if (it == alphaCache.end()) {
        std::vector<uint8_t> a((size_t)t->second.width * t->second.height);
        bool any = false;
        const bool both = casual && name == "linktexS3TC" && casual->width == t->second.width && casual->height == t->second.height;
        for (size_t i = 0; i < a.size(); i++) {
            a[i] = t->second.rgba[i * 4 + 3];
            if (both) a[i] = std::max(a[i], casual->rgba[i * 4 + 3]);
            any |= a[i] < 128;
        }
        if (!any) a.clear();
        it = alphaCache.emplace(name, a).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

bool invisible(const std::vector<uint8_t>& alpha, int w, int h, const Model& gc, const Tri& t) {
    if (t[0].tex < 0 || t[1].tex < 0 || t[2].tex < 0) return false;
    double us[4], vs[4];
    for (int i = 0; i < 3; i++) us[i] = gc.uv[t[i].tex][0], vs[i] = gc.uv[t[i].tex][1];
    us[3] = (us[0] + us[1] + us[2]) / 3, vs[3] = (vs[0] + vs[1] + vs[2]) / 3;
    for (int i = 0; i < 4; i++) {
        const double u = std::fmod(std::fmod(us[i], 1.0) + 1.0, 1.0), v = std::fmod(std::fmod(vs[i], 1.0) + 1.0, 1.0);
        const int x = (int)(u * w) % w, y = (int)(v * h) % h;
        if (alpha[(size_t)y * w + x] >= 128) return false;
    }
    return true;
}

void convert_model(Bfres& f, size_t fm, const Model& gc, Log& log, const Texture* casual = nullptr) {
    const auto bones = hd_skeleton(f, fm);
    std::map<std::string, const Bone*> byName;
    for (auto& b : bones) byName[b.name] = &b;
    std::map<std::string, Mat> gcInv;
    for (auto& j : gc.joints) gcInv[j.name] = mat_inv(j.world);
    auto hd_bone = [&](int ji) -> const Bone& {
        while (ji >= 0 && ji < (int)gc.joints.size()) {
            auto it = byName.find(gc.joints[ji].name);
            if (it != byName.end()) return *it->second;
            ji = gc.joints[ji].parent;
        }
        return bones[0];
    };
    auto smooth_bone = [&](const Bone* b) {
        while (b->smooth < 0 && b->parent >= 0 && b->parent < (int)bones.size()) b = &bones[b->parent];
        return b;
    };
    std::map<std::string, std::vector<const Shape*>> gcByMat;
    for (auto& s : gc.shapes)
        if (s.material >= 0 && s.material < (int)gc.materialNames.size()) gcByMat[gc.materialNames[s.material]].push_back(&s);
    std::vector<std::string> mats;
    for (auto& [n, o] : f.group(f.rel(fm + 24))) mats.push_back(n);
    std::map<std::string, std::vector<uint8_t>> alphaCache;
    const size_t vbl = f.rel(fm + 16);
    for (auto& [sname, so] : f.group(f.rel(fm + 20))) {
        const std::string mat = mats.at(f.u16(so + 0xE));
        auto gsIt = gcByMat.find(gc_material_name(mat));
        if (gsIt == gcByMat.end()) {
            log(fmt("  %s (%s): no GameCube shape, kept", sname.c_str(), mat.c_str()));
            continue;
        }
        const int skin = f.u8(so + 0x16);
        const Bone& shapeBone = bones.at(f.u16(so + 0x10));
        const size_t vb = vbl + f.u16(so + 0x12) * 0x20, al = f.rel(vb + 0x10), bl = f.rel(vb + 0x18);
        std::map<std::string, Attr> attrs;
        for (int k = 0; k < f.u8(vb + 4); k++)
            attrs[f.str(f.rel(al + k * 12))] = {f.u8(al + k * 12 + 4), f.u16(al + k * 12 + 6), f.u32(al + k * 12 + 8)};
        if (f.u8(vb + 5) != 1) {
            log(fmt("  %s: %d buffers, kept", sname.c_str(), f.u8(vb + 5)));
            continue;
        }
        const int stride = f.u16(bl + 0xC);
        const size_t oldData = f.rel(bl + 0x14);
        double baseColour[4] = {1, 1, 1, 1};
        if (attrs.count("_c0"))
            for (int k = 0; k < 4; k++) baseColour[k] = f.f32(oldData + attrs["_c0"].offset + 4 * k);
        // the GameCube triangles, their vertices made unique
        std::vector<Vertex> verts;
        std::map<Vertex, uint32_t> index;
        std::vector<std::array<uint32_t, 3>> tris;
        int cut = 0, dropped = 0;
        for (const Shape* gs : gsIt->second) {
            const std::vector<uint8_t>* alpha = see_through(gc, *gs, alphaCache, casual);
            int aw = 0, ah = 0;
            if (alpha) {
                const Texture& t = gc.textures.at(gc.textureNames[gc.materialTextures[gs->material][0]]);
                aw = t.width, ah = t.height;
            }
            for (auto& t : gs->tris) {
                if (alpha && invisible(*alpha, aw, ah, gc, t)) {
                    cut++;  // (cut out on the GameCube; HD's opaque shaders would draw it black)
                    continue;
                }
                uint32_t tri[3];
                for (int i = 0; i < 3; i++) {
                    auto it = index.find(t[i]);
                    if (it == index.end()) {
                        it = index.emplace(t[i], (uint32_t)verts.size()).first;
                        verts.push_back(t[i]);
                    }
                    tri[i] = it->second;
                }
                tris.push_back({tri[0], tri[2], tri[1]});  // (GameCube fronts are clockwise, HD's counter-clockwise)
            }
        }
        Bytes out((size_t)stride * verts.size(), 0);
        Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
        std::set<int> usedBones;
        const Mat shapeInv = mat_inv(shapeBone.world);
        for (size_t vi = 0; vi < verts.size(); vi++) {
            const Vertex& v = verts[vi];
            const Vec3 p = gc.position(v), nm = gc.normal(v);
            const auto& infl = gc.draw.at(v.mtx < (int)gc.draw.size() ? v.mtx : 0);
            // onto HD's skeleton: each joint's part moved from the GameCube bind pose to HD's
            Vec3 ph{0, 0, 0}, nh{0, 0, 0};
            std::vector<std::pair<int, double>> hdInfl;  // (bone index, weight), in first-seen order
            for (auto& [j, w] : infl) {
                const Bone& hb = hd_bone(j);
                auto gi = gcInv.find(hb.name);
                if (gi == gcInv.end()) gi = gcInv.find(j < (int)gc.joints.size() ? gc.joints[j].name : "");
                const Mat g2h = gi == gcInv.end() ? mat_identity() : mat_mul(hb.world, gi->second);
                const Vec3 q = mat_apply(g2h, p), r = rot_apply(g2h, nm);
                for (int k = 0; k < 3; k++) ph[k] += w * q[k], nh[k] += w * r[k];
                auto e = std::find_if(hdInfl.begin(), hdInfl.end(), [&](auto& x) { return x.first == hb.index; });
                if (e == hdInfl.end()) hdInfl.push_back({hb.index, w});
                else e->second += w;
            }
            auto normalize = [](Vec3& n) {
                const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 0)
                    for (double& x : n) x /= l;
                else
                    n = {0, 0, 1};
            };
            normalize(nh);
            if (skin == 0) {
                ph = mat_apply(shapeInv, ph);
                nh = rot_apply(shapeInv, nh);
                normalize(nh);
            }
            for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], ph[k]), hi[k] = std::max(hi[k], ph[k]);
            const double u = v.tex >= 0 ? gc.uv[v.tex][0] : 0, vv = v.tex >= 0 ? gc.uv[v.tex][1] : 0;
            double col[4] = {baseColour[0], baseColour[1], baseColour[2], baseColour[3]};
            if (v.clr >= 0 && v.clr < (int)gc.clr.size())
                for (int k = 0; k < 4; k++) col[k] = gc.clr[v.clr][k] / 255.0;
            // weights: the strongest `skin` ones, normalised, as smooth matrix indices
            std::vector<std::pair<double, const Bone*>> ws;
            for (auto& [b, w] : hdInfl) ws.push_back({w, smooth_bone(&bones.at(b))});
            std::stable_sort(ws.begin(), ws.end(), [](auto& a, auto& b) { return a.first > b.first; });
            if (skin && (int)ws.size() > skin) dropped++;
            if ((int)ws.size() > std::max(skin, 1)) ws.resize(std::max(skin, 1));
            double tot = 0;
            for (auto& [w, b] : ws) tot += w, usedBones.insert(b->index);
            if (tot == 0) tot = 1;
            const int n = std::max(skin, 1);
            std::vector<double> sm, wt;
            for (auto& [w, b] : ws) sm.push_back(std::max(b->smooth, 0)), wt.push_back(w / tot);
            while ((int)sm.size() < n) sm.push_back(std::max(ws[0].second->smooth, 0)), wt.push_back(0.0);
            const int rig = std::max(ws[0].second->rigid, 0);
            for (auto& [name, a] : attrs) {
                bool isFloat;
                const int cnt = format_count(a.format, isFloat);
                if (!cnt) throw Error(fmt("%s: attribute %s format %x not handled", sname.c_str(), name.c_str(), a.format));
                std::vector<double> val(cnt, 0.0);
                if (name == "_p0") {
                    for (int k = 0; k < std::min(cnt, 3); k++) val[k] = ph[k];
                    if (cnt == 4) val[3] = 1.0;
                } else if (name == "_n0") {
                    for (int k = 0; k < std::min(cnt, 3); k++) val[k] = nh[k];
                } else if (name == "_u0") {
                    val[0] = u;
                    if (cnt > 1) val[1] = vv;
                } else if (name == "_c0") {
                    for (int k = 0; k < std::min(cnt, 4); k++) val[k] = col[k];
                } else if (name == "_i0" || name == "_w0") {
                    const auto& src = name == "_i0" ? sm : wt;
                    for (int k = 0; k < cnt && k < (int)src.size(); k++) val[k] = src[k];
                } else if (name == "_i1" || name == "_w1") {
                    const auto& src = name == "_i1" ? sm : wt;
                    for (int k = 0; k < cnt; k++) val[k] = (size_t)(4 + k) < src.size() ? src[4 + k] : (name == "_i1" ? sm[0] : 0.0);
                } else if (name == "_i2") {
                    for (int k = 0; k < cnt; k++) val[k] = rig;
                } else {
                    throw Error(fmt("%s: attribute %s not handled", sname.c_str(), name.c_str()));
                }
                uint8_t* dst = &out[vi * stride + a.offset];
                for (int k = 0; k < cnt; k++) {
                    if (isFloat) putf32(dst + 4 * k, (float)val[k]);
                    else put32(dst + 4 * k, (uint32_t)val[k]);
                }
            }
        }
        // the vertex buffer
        const size_t at = f.append(out.data(), out.size(), 0x100);
        put32(&f.d[vb + 8], (uint32_t)verts.size());
        put32(&f.d[bl + 4], (uint32_t)out.size());
        f.set_rel(bl + 0x14, at);
        // the index buffer (the first mesh; its sub meshes become one)
        const size_t mesh = f.rel(so + 0x24);
        const uint32_t ifmt = f.u32(mesh + 4);
        Bytes ib;
        for (auto& t : tris)
            for (uint32_t i : t) {
                if (ifmt == 9) ib.push_back((uint8_t)(i >> 24)), ib.push_back((uint8_t)(i >> 16));
                ib.push_back((uint8_t)(i >> 8)), ib.push_back((uint8_t)i);
            }
        const uint32_t count = (uint32_t)tris.size() * 3;
        const size_t iat = f.append(ib.data(), ib.size(), 0x100);
        put32(&f.d[mesh + 8], count);
        const size_t sub = f.rel(mesh + 0x10);
        put32(&f.d[sub], 0);
        put32(&f.d[sub + 4], count);
        for (int k = 1; k < f.u16(mesh + 0xC); k++) put32(&f.d[sub + 8 * k], count), put32(&f.d[sub + 8 * k + 4], 0);
        const size_t ibuf = f.rel(mesh + 0x14);
        put32(&f.d[ibuf + 4], (uint32_t)ib.size());
        f.set_rel(ibuf + 0x14, iat);
        put32(&f.d[mesh + 0x18], 0);
        // skin bones
        if (skin) {
            Bytes sb;
            for (int b : usedBones) sb.push_back((uint8_t)(b >> 8)), sb.push_back((uint8_t)b);
            const size_t sat = f.append(sb.data(), sb.size(), 4);
            f.set_rel(so + 0x28, sat);
            put16(&f.d[so + 0x14], (uint16_t)usedBones.size());
        }
        // bounds: centre and half size of all, radius round the centre
        if (verts.empty()) lo = hi = {0, 0, 0};
        double c[3], e[3];
        for (int k = 0; k < 3; k++) c[k] = (lo[k] + hi[k]) / 2, e[k] = (hi[k] - lo[k]) / 2;
        f.putf(so + 0x1C, (float)std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]));
        if (f.u16(so + 0x1A) == 0 && f.rel(so + 0x30)) {
            const size_t bb = f.rel(so + 0x30);
            const int nb = 1 + f.u16(mesh + 0xC) + 1;
            for (int k = 0; k < nb; k++)
                for (int q = 0; q < 3; q++) f.putf(bb + 24 * k + 4 * q, (float)c[q]), f.putf(bb + 24 * k + 12 + 4 * q, (float)e[q]);
        }
        if (cut) log(fmt("  %s: %d see-through triangles left out", sname.c_str(), cut));
        log(fmt("  %s (%s): %zu vertices, %zu triangles, skin %d%s", sname.c_str(), mat.c_str(), verts.size(), tris.size(), skin,
                dropped ? fmt(", %d vertices lost weights", dropped).c_str() : ""));
    }
}

// ---- textures

bool hd_opaque(HDCache& hd, const std::string& name) {
    const auto& im = hd.image(name).second;
    for (size_t i = 0; i < (size_t)im.w * im.h; i++)
        if (im.px[i * 4 + 3] < 128) return false;
    return true;
}

std::vector<std::vector<uint8_t>> uv_regions(const Model& gc, const std::string& name, int w, int h) {
    std::vector<std::vector<uint8_t>> regions;
    for (size_t mi = 0; mi < gc.materialTextures.size(); mi++) {
        const auto& ts = gc.materialTextures[mi];
        if (ts.empty() || ts[0] >= (int)gc.textureNames.size() || gc.textureNames[ts[0]] != name) continue;
        std::vector<std::array<std::array<double, 2>, 3>> uvt;
        for (auto& s : gc.shapes) {
            if (s.material != (int)mi) continue;
            for (auto& t : s.tris)
                if (t[0].tex >= 0 && t[1].tex >= 0 && t[2].tex >= 0)
                    uvt.push_back({{{gc.uv[t[0].tex][0], gc.uv[t[0].tex][1]}, {gc.uv[t[1].tex][0], gc.uv[t[1].tex][1]},
                                    {gc.uv[t[2].tex][0], gc.uv[t[2].tex][1]}}});
        }
        regions.push_back(raster_uv(uvt, w, h));
    }
    return regions;
}

bool grey_format(int f) { return f == 0 || f == 1 || f == 2 || f == 3; }  // I4, I8, IA4, IA8

void replace_texture(Bfres& f, HDCache& hd, const std::string& name, const Texture& g, const Model* owner,
                     std::map<std::string, std::array<double, 3>>& tinted, Log& log, const Texture* casual = nullptr) {
    if (!hd.tex.count(name) || name == "ZBtoonEX") return;
    const size_t o = hd.tex[name];
    const int w = f.u32(o + 8), h = f.u32(o + 0xC), mips = f.u32(o + 0x14);
    const uint32_t fm = f.u32(o + 0x18), sz = f.u32(o + 0x24), msz = f.u32(o + 0x2C), tm = f.u32(o + 0x34), sw = f.u32(o + 0x38);
    Image src = to_image(g);
    const uint32_t ff = fm & 0x3F;
    if (ff == 0x34 || ff == 0x35) {  // BC4 / BC5: intensity in red (BC5: alpha in green)
        bool grey = grey_format(g.format);
        if (!grey && (g.format == 8 || g.format == 9)) {  // C4 / C8 painted in greys
            int spread = 0;
            for (size_t i = 0; i < (size_t)src.w * src.h; i++) {
                const uint8_t* p = &src.px[i * 4];
                spread = std::max(spread, std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}));
            }
            grey = spread < 8;
        }
        std::array<double, 3> colour{0, 0, 0};
        if (!grey) {
            // a coloured texture (eyebrows painted in the hair's colour): HD's shader draws red as grey, times the
            // material colour (mat_color0): red full, the texture's colour into the materials using it, at 65 %
            // (that shader doesn't shade with the light: full colour glows in shadow)
            size_t n = 0;
            for (size_t i = 0; i < (size_t)src.w * src.h; i++)
                if (src.px[i * 4 + 3] > 128) {
                    for (int c = 0; c < 3; c++) colour[c] += src.px[i * 4 + c];
                    n++;
                }
            for (double& c : colour) c = n ? c / n / 255.0 * 0.65 : 0.65;
            tinted[name] = colour;
        }
        for (size_t i = 0; i < (size_t)src.w * src.h; i++) {
            uint8_t* p = &src.px[i * 4];
            const uint8_t red = grey ? p[0] : 255, a = p[3];
            p[0] = red, p[1] = ff == 0x35 ? a : red, p[2] = 0, p[3] = 255;
        }
    }
    // (HD's own texture fully opaque: its shaders draw every texel, so see-through ones get colours)
    bool see = false;
    for (size_t i = 0; i < (size_t)src.w * src.h; i++) see |= src.px[i * 4 + 3] < 128;
    if ((ff == 0x31 || ff == 0x32 || ff == 0x33) && see && hd_opaque(hd, name)) {
        Image under;
        const Image* up = nullptr;
        // the two clothes' body textures fill each other's see-through texels (the body's mesh is shared: parts cut
        // out in one clothes are drawn there, in the other one's colours)
        if (name == "linktexbci4" && owner) {
            auto hero = owner->textures.find("linktexS3TC");
            if (hero != owner->textures.end() && hero->second.width == src.w && hero->second.height == src.h) {
                under = to_image(hero->second);
                up = &under;
            }
        } else if (name == "linktexS3TC" && casual && casual->width == src.w && casual->height == src.h) {
            under = to_image(*casual);
            up = &under;
        }
        const auto regions = owner ? uv_regions(*owner, name == "linktexbci4" ? "linktexS3TC" : name, src.w, src.h)
                                   : std::vector<std::vector<uint8_t>>{};
        src = bleed(src, regions, up);
    }
    const Image big = upscale_sharp(src, w, h);
    Bytes linear;
    for (int m = 0; m < mips; m++) {
        const int lw = std::max(w >> m, 1), lh = std::max(h >> m, 1);
        const Bytes b = bc_encode(fm, m ? resize_lanczos(big, lw, lh) : big);
        linear.insert(linear.end(), b.begin(), b.end());
    }
    Bytes image, mip;
    gx2_tile(fm, w, h, mips, tm, sw, linear, image, mip, sz, msz);
    memcpy(&f.d[f.rel(o + 0xB0)], image.data(), sz);
    if (msz) memcpy(&f.d[f.rel(o + 0xB4)], mip.data(), msz);
    log(fmt("  texture %s: %dx%d -> %dx%d, %d mips", name.c_str(), g.width, g.height, w, h, mips));
}

void set_mat_colour(Bfres& f, size_t fm, const std::string& texture, const std::array<double, 3>& colour, Log& log) {
    const std::string fam = family(texture);
    for (auto& [mn, mo] : f.group(f.rel(fm + 24))) {
        if (!f.u8(mo + 0x11)) continue;
        const std::string first = f.str(f.rel(f.rel(mo + 0x28)));
        if (family(first) != fam) continue;
        const size_t pl = f.rel(mo + 0x34), pd = f.rel(mo + 0x3C);
        for (int k = 0; k < f.u16(mo + 0x12); k++) {
            const size_t e = pl + k * 0x14;
            if (f.str(f.rel(e + 0x10)) == "mat_color0" && f.u8(e) == 0xF) {
                const size_t at = pd + f.u16(e + 2);
                for (int c = 0; c < 3; c++) f.putf(at + 4 * c, (float)colour[c]);
                f.putf(at + 12, 1.0f);
                log(fmt("  material %s: colour %.2f %.2f %.2f", mn.c_str(), colour[0], colour[1], colour[2]));
            }
        }
    }
}

}  // namespace

std::string gc_material(const std::string& hd) { return gc_material_name(hd); }

std::vector<PartInfo> describe(const std::vector<uint8_t>& linkBfres) {
    Bfres f;
    f.d = linkBfres;
    if (f.d.size() < 0x60 || memcmp(f.d.data(), "FRES", 4)) throw Error("not a BFRES");
    std::vector<PartInfo> out;
    for (auto& [n, fm] : f.group(f.section(0))) {
        PartInfo p;
        p.name = n;
        for (auto& b : hd_skeleton(f, fm)) p.bones.push_back({b.name, b.parent, {b.world[0][3], b.world[1][3], b.world[2][3]}});
        for (auto& [mn, mo] : f.group(f.rel(fm + 24))) {
            p.materials.push_back(mn);
            std::vector<std::string> ts;
            const size_t tl = f.rel(mo + 0x28);
            for (int k = 0; k < f.u8(mo + 0x11); k++) ts.push_back(f.str(f.rel(tl + k * 8)));
            p.materialTextures.push_back(ts);
        }
        for (auto& [sn, so] : f.group(f.rel(fm + 20))) p.shapes.push_back({sn, f.u16(so + 0xE), f.u16(so + 0x10), f.u8(so + 0x16)});
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<uint8_t> assemble(const std::vector<uint8_t>& pack, const std::vector<uint8_t>& linkSzs, std::string& error) {
    try {
        return sarc_replace(pack, {{"Link.szs", linkSzs}});
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

std::vector<uint8_t> link_szs(const std::vector<uint8_t>& pack) {
    try {
        auto files = sarc_read(pack);
        auto it = files.find("Link.szs");
        return it == files.end() ? std::vector<uint8_t>{} : it->second;
    } catch (const std::exception&) {
        return {};
    }
}

Result convert(const std::vector<uint8_t>& arcBytes, const std::vector<uint8_t>& pack, std::vector<uint8_t>& outSzs,
               const std::function<void(int, const std::string&)>& progress) {
    Result res;
    Log log;
    auto step = [&](int pc, const std::string& s) {
        if (progress) progress(pc, s);
    };
    try {
        step(2, "reading");
        const auto arc = rarc_read(arcBytes);
        auto packFiles = sarc_read(pack);
        auto szsIt = packFiles.find("Link.szs");
        if (szsIt == packFiles.end()) throw Error("permanent_3d.pack without Link.szs");
        const Bytes& szs = szsIt->second;
        const Bytes link = szs.size() >= 4 && !memcmp(szs.data(), "Yaz0", 4) ? yaz0_decode(szs) : szs;
        auto linkFiles = sarc_read(link);
        auto bfIt = linkFiles.find("Link.bfres");
        if (bfIt == linkFiles.end()) throw Error("Link.szs without Link.bfres");
        Bfres f;
        f.d = bfIt->second;
        if (f.d.size() < 0x60 || memcmp(f.d.data(), "FRES", 4)) throw Error("Link.bfres is not a BFRES");

        std::map<std::string, size_t> models;
        std::vector<std::string> modelOrder;
        for (auto& [n, o] : f.group(f.section(0))) models[n] = o, modelOrder.push_back(n);
        auto gcKey = [&](const std::string& n) -> std::string {
            if (arc.count(n + ".bdl")) return n + ".bdl";
            if (arc.count(n + ".bmd")) return n + ".bmd";
            return "";
        };
        if (gcKey("cl").empty()) throw Error("Link.arc has no cl.bdl (not a player model)");

        // ---- what the pack changed
        step(5, "comparing");
        HDCache hd(f);
        std::map<std::string, std::unique_ptr<Model>> gcModels;
        std::set<std::string> emptied;  // parts the pack took out (Link's casual hair, in most packs)
        std::map<std::string, double> geo, tex;
        std::map<std::string, std::set<std::string>> own;
        int mi = 0;
        for (auto& n : modelOrder) {
            step(5 + 35 * mi++ / (int)modelOrder.size(), "comparing " + n);
            const std::string key = gcKey(n);
            if (key.empty()) continue;
            std::unique_ptr<Model> m;
            try {
                m = std::make_unique<Model>(arc.at(key));
            } catch (const std::exception& e) {
                log(fmt("skipped %s: %s", n.c_str(), e.what()));
                continue;
            }
            geo[n] = geometry_near(hd, n, *m);
            if (visible_triangles(*m) <= 2 && hd.model_points(n).size() > 8) emptied.insert(n);  // (not the tiny effect parts)
            for (auto& [tn, t] : m->textures)
                if (hd.tex.count(tn) && !tex.count(tn)) tex[tn] = texture_diff(hd, tn, t);
            own[n] = material_textures(f, models[n]);
            gcModels[n] = std::move(m);
        }
        std::set<std::string> textures, otherLayout;
        for (auto& [t, v] : tex) {
            if (t == "ZBtoonEX") continue;
            if (v > base_texture(t) + 15) textures.insert(t);
            if (base_texture(t) > 15) otherLayout.insert(t);
        }
        std::map<std::string, std::string> why;
        for (auto& [n, ts] : own) {
            const double g0 = base_geometry(n);
            std::vector<std::string> changed;
            for (auto& t : ts)
                if (textures.count(t)) changed.push_back(t);
            if (emptied.count(n)) why[n] = "taken out by the pack";
            else if (g0 >= 50 && geo[n] < g0 - 15) why[n] = fmt("shape %.0f%% (original %.0f%%)", geo[n], g0);
            else if (g0 < 50 && !changed.empty()) why[n] = "texture " + changed[0] + " (HD remodelled the part)";
            else if (n == "cl") why[n] = "the body";
        }
        for (bool more = true; more;) {
            more = false;
            for (auto& [n, ts] : own) {
                if (why.count(n)) continue;
                for (auto& t : ts)
                    if (textures.count(t) && otherLayout.count(t)) {
                        why[n] = "texture " + t + " in a layout of its own";
                        more = true;
                        break;
                    }
            }
            for (auto& [n, w] : why)
                for (auto& t : own[n])
                    if (otherLayout.count(t) && tex.count(t) && !textures.count(t)) textures.insert(t), more = true;
        }
        for (auto& t : textures) log(fmt("texture %-20s %.0f (original %.0f)", t.c_str(), tex[t], base_texture(t)));
        for (auto& [n, w] : why) log(fmt("mesh    %-20s %s", n.c_str(), w.c_str()));

        // ---- meshes, then each texture from the first part (meshes first) whose GameCube model has it
        std::vector<std::string> sources;
        for (auto& n : modelOrder)
            if (why.count(n)) sources.push_back(n);
        for (auto& n : modelOrder)
            if (!why.count(n) && gcModels.count(n)) sources.push_back(n);
        std::set<std::string> done;
        int si = 0;
        for (auto& n : sources) {
            step(40 + 50 * si++ / (int)sources.size(), "converting " + n);
            const Model& gc = *gcModels[n];
            if (why.count(n)) {
                log("model " + n);
                Texture casual;
                const bool hasCasual = n == "cl" && arc.count("linktexbci4.bti");
                if (hasCasual) casual = decode_bti_file(arc.at("linktexbci4.bti"));
                convert_model(f, models[n], gc, log, hasCasual ? &casual : nullptr);
            }
            std::map<std::string, std::array<double, 3>> tinted;
            for (auto& [tn, t] : gc.textures)
                if (textures.count(tn) && !done.count(tn)) {
                    done.insert(tn);
                    Texture casual;
                    const bool hasCasual = n == "cl" && tn == "linktexS3TC" && arc.count("linktexbci4.bti");
                    if (hasCasual) casual = decode_bti_file(arc.at("linktexbci4.bti"));
                    replace_texture(f, hd, tn, t, &gc, tinted, log, hasCasual ? &casual : nullptr);
                }
            std::set<std::string> fams;
            for (auto& [tn, c] : tinted)
                if (fams.insert(family(tn)).second) set_mat_colour(f, models[n], tn, c, log);
        }
        // the casual clothes' body texture: a file of its own next to the models, with the body's mesh
        auto loose = arc.find("linktexbci4.bti");
        if (why.count("cl") && loose != arc.end() && !done.count("linktexbci4")) {
            std::map<std::string, std::array<double, 3>> tinted;
            replace_texture(f, hd, "linktexbci4", decode_bti_file(loose->second), gcModels["cl"].get(), tinted, log);
        }

        // ---- back into the pack
        step(92, "packing");
        std::map<std::string, Bytes> repl{{"Link.bfres", f.d}};
        const Bytes newLink = sarc_replace(link, repl);
        const uint32_t align = szs.size() >= 12 && !memcmp(szs.data(), "Yaz0", 4) ? be32(&szs[8]) : 0;
        outSzs = yaz0_encode(newLink, align);
        step(100, "done");
    } catch (const std::exception& e) {
        res.error = e.what();
    }
    res.report = log.text;
    return res;
}

}  // namespace gcmodel
