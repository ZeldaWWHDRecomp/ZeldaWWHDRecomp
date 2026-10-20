// SPDX-License-Identifier: MPL-2.0
// World editor: the water tool's geometry (water.h).
#include "water.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <map>
#include <tuple>

// The same bytes everywhere: no fused multiply-add where the compiler would otherwise contract (every output is
// rounded to whole 1/1024 metres anyway; this keeps the values before rounding the same too).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace editor::water {
namespace {
constexpr int kUnknown = INT_MIN;

// ---------------------------------------------------------------- small vector maths (game units)
struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) {
    const double l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1 / l) : V3{};
}
V3 at(XZ p, double y) { return {double(p.x), y, double(p.z)}; }

// 1/1024 metre = 100/1024 game units: whole steps of the output grid
int64_t grid(double units) { return std::llround(units * 1024.0 / 100.0); }
int64_t grid_uv(double uv) { return std::llround(uv * 1024.0); }

// ---------------------------------------------------------------- meshes before they are written
struct Prim {
    std::string material;  // the material's name
    bool visible = false;
    bool smooth = false;   // normals: averaged over the faces meeting at a place (a bank), else each face's own
    std::map<std::array<int64_t, 3>, V3> normal_sum;
    std::vector<int64_t> pos, nrm, uv;  // grid steps (positions, normals and UV: k / 1024)
    std::vector<uint16_t> idx;
    std::map<std::vector<int64_t>, uint16_t> weld;
    bool full = false;  // over 65535 vertices (never reached by the limits)
    unsigned vertices() const { return unsigned(pos.size() / 3); }
    unsigned triangles() const { return unsigned(idx.size() / 3); }
    uint16_t vertex(const int64_t p[3], const int64_t n[3], const int64_t t[2]) {
        std::vector<int64_t> key(p, p + 3);
        if (visible) key.insert(key.end(), {n[0], n[1], n[2], t[0], t[1]});
        if (const auto f = weld.find(key); f != weld.end()) return f->second;
        if (pos.size() / 3 >= 65535) {
            full = true;
            return 0;
        }
        const auto i = uint16_t(pos.size() / 3);
        pos.insert(pos.end(), p, p + 3);
        if (visible) nrm.insert(nrm.end(), n, n + 3), uv.insert(uv.end(), t, t + 2);
        weld.emplace(std::move(key), i);
        return i;
    }
    // a triangle facing `want` (its winding turned when it faces away); a triangle that the grid collapses is left out
    void triangle(V3 a, V3 b, V3 c, V3 want, const double ta[2] = nullptr, const double tb[2] = nullptr, const double tc[2] = nullptr) {
        int64_t pa[3] = {grid(a.x), grid(a.y), grid(a.z)}, pb[3] = {grid(b.x), grid(b.y), grid(b.z)}, pc[3] = {grid(c.x), grid(c.y), grid(c.z)};
        // the facing on the grid itself (exact integers)
        const int64_t u[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]}, v[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
        const int64_t n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        if (!n[0] && !n[1] && !n[2]) return;
        const double facing = double(n[0]) * want.x + double(n[1]) * want.y + double(n[2]) * want.z;
        const double zero[2] = {0, 0};
        const double* t[3] = {ta ? ta : zero, tb ? tb : zero, tc ? tc : zero};
        int64_t* p[3] = {pa, pb, pc};
        if (facing < 0) std::swap(p[1], p[2]), std::swap(t[1], t[2]);
        // the normal: the face's own, on the grid (k / 1024 of a unit vector); a smooth prim sums them per place
        const V3 fn = unit({double(facing < 0 ? -n[0] : n[0]), double(facing < 0 ? -n[1] : n[1]), double(facing < 0 ? -n[2] : n[2])});
        const int64_t qn[3] = {smooth ? 0 : grid_uv(fn.x), smooth ? 0 : grid_uv(fn.y), smooth ? 0 : grid_uv(fn.z)};
        for (int k = 0; k < 3; k++) {
            const int64_t qt[2] = {grid_uv(t[k][0]), grid_uv(t[k][1])};
            idx.push_back(vertex(p[k], qn, qt));
            if (smooth) {
                V3& sum = normal_sum[{p[k][0], p[k][1], p[k][2]}];
                sum = sum + fn;
            }
        }
    }
    // a smooth prim's normals from its faces (once every triangle is in)
    void finish() {
        if (!smooth || !visible) return;
        for (size_t i = 0; i < pos.size() / 3; i++) {
            const V3 n = unit(normal_sum[{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]}]);
            nrm[i * 3] = grid_uv(n.x), nrm[i * 3 + 1] = grid_uv(n.y), nrm[i * 3 + 2] = grid_uv(n.z);
        }
    }
    void quad(V3 a, V3 b, V3 c, V3 d, V3 want, const double ta[2] = nullptr, const double tb[2] = nullptr, const double tc[2] = nullptr,
              const double td[2] = nullptr) {
        triangle(a, b, c, want, ta, tb, tc);
        triangle(a, c, d, want, ta, tc, td);
    }
};
struct Material {
    std::string name;
    int image = -1;  // -1: a collision material (no texture)
    bool mask = false;
    std::string extras;  // the material's glTF extras (JSON), "" none
};
struct Mesh {
    std::string name;
    std::vector<Prim> prims;
};

// ---------------------------------------------------------------- bytes
void le32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; k++) b.push_back(uint8_t(v >> (8 * k)));
}
void be32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 3; k >= 0; k--) b.push_back(uint8_t(v >> (8 * k)));
}
uint32_t float_bits(int64_t k) {
    const float f = float(double(k) / 1024.0);  // exact: |k| < 2^24
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
std::string integer(int64_t v) { return std::to_string(v); }
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// The GLB: one node per mesh (the first is the visible one), one buffer view per accessor and image.
std::vector<uint8_t> write_glb(const std::vector<Mesh>& meshes, const std::vector<Material>& materials, const std::vector<std::vector<uint8_t>>& images,
                               const std::string& extras) {
    std::vector<uint8_t> bin;
    std::string views, accessors, mesh_json, nodes;
    unsigned nview = 0, naccessor = 0;
    auto align = [&] {
        while (bin.size() % 4) bin.push_back(0);
    };
    auto add_view = [&](size_t start) {
        if (nview) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + integer(int64_t(start)) + ",\"byteLength\":" + integer(int64_t(bin.size() - start)) + "}";
        return nview++;
    };
    auto add_floats = [&](const std::vector<int64_t>& values, unsigned width, const char* type, bool bounds) {
        align();
        const size_t start = bin.size();
        for (int64_t v : values) le32(bin, float_bits(v));
        const unsigned view = add_view(start);
        if (naccessor) accessors += ",";
        accessors += "{\"bufferView\":" + integer(view) + ",\"componentType\":5126,\"count\":" + integer(int64_t(values.size() / width)) + ",\"type\":\"" + type + "\"";
        if (bounds) {
            std::string lo, hi;
            for (unsigned c = 0; c < width; c++) {
                int64_t a = INT64_MAX, b = INT64_MIN;
                for (size_t i = c; i < values.size(); i += width) a = std::min(a, values[i]), b = std::max(b, values[i]);
                lo += (c ? "," : "") + metres(a);
                hi += (c ? "," : "") + metres(b);
            }
            accessors += ",\"min\":[" + lo + "],\"max\":[" + hi + "]";
        }
        accessors += "}";
        return naccessor++;
    };
    auto add_indices = [&](const std::vector<uint16_t>& values) {
        align();
        const size_t start = bin.size();
        for (uint16_t v : values) bin.push_back(uint8_t(v)), bin.push_back(uint8_t(v >> 8));
        const unsigned view = add_view(start);
        if (naccessor) accessors += ",";
        accessors += "{\"bufferView\":" + integer(view) + ",\"componentType\":5123,\"count\":" + integer(int64_t(values.size())) + ",\"type\":\"SCALAR\"}";
        return naccessor++;
    };
    std::string mesh_list, node_list, roots;
    for (size_t m = 0; m < meshes.size(); m++) {
        std::string prims;
        for (const auto& p : meshes[m].prims) {
            if (p.idx.empty()) continue;
            const unsigned position = add_floats(p.pos, 3, "VEC3", true);
            std::string attributes = "\"POSITION\":" + integer(position);
            if (p.visible) {
                const unsigned normal = add_floats(p.nrm, 3, "VEC3", false), texcoord = add_floats(p.uv, 2, "VEC2", false);
                attributes += ",\"NORMAL\":" + integer(normal) + ",\"TEXCOORD_0\":" + integer(texcoord);
            }
            const unsigned indices = add_indices(p.idx);
            int material = 0;
            for (size_t i = 0; i < materials.size(); i++)
                if (materials[i].name == p.material) material = int(i);
            prims += std::string(prims.empty() ? "" : ",") + "{\"attributes\":{" + attributes + "},\"indices\":" + integer(indices) + ",\"material\":" + integer(material) + ",\"mode\":4}";
        }
        mesh_list += std::string(m ? "," : "") + "{\"name\":" + quoted(meshes[m].name) + ",\"primitives\":[" + prims + "]}";
        node_list += std::string(m ? "," : "") + "{\"mesh\":" + integer(int64_t(m)) + ",\"name\":" + quoted(meshes[m].name) + "}";
        roots += std::string(m ? "," : "") + integer(int64_t(m));
    }
    std::string image_list, texture_list, material_list;
    for (size_t i = 0; i < images.size(); i++) {
        align();
        const size_t start = bin.size();
        bin.insert(bin.end(), images[i].begin(), images[i].end());
        const unsigned view = add_view(start);
        image_list += std::string(i ? "," : "") + "{\"bufferView\":" + integer(view) + ",\"mimeType\":\"image/png\"}";
        texture_list += std::string(i ? "," : "") + "{\"source\":" + integer(int64_t(i)) + "}";
    }
    for (size_t i = 0; i < materials.size(); i++) {
        const auto& m = materials[i];
        std::string body = "\"name\":" + quoted(m.name);
        if (m.image >= 0) body += ",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":" + integer(m.image) + "}}";
        if (m.mask) body += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
        if (!m.extras.empty()) body += ",\"extras\":" + m.extras;
        material_list += std::string(i ? "," : "") + "{" + body + "}";
    }
    const size_t used = bin.size();
    align();
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"WWHD editor water tool\"" + (extras.empty() ? std::string() : ",\"extras\":{\"wwhd_water\":" + extras + "}") +
                       "},\"scene\":0,\"scenes\":[{\"nodes\":[" + roots + "]}],\"nodes\":[" + node_list + "],\"meshes\":[" + mesh_list + "],\"materials\":[" + material_list + "]";
    if (!images.empty()) json += ",\"textures\":[" + texture_list + "],\"images\":[" + image_list + "]";
    json += ",\"accessors\":[" + accessors + "],\"bufferViews\":[" + views + "],\"buffers\":[{\"byteLength\":" + integer(int64_t(used)) + "}]}";
    while (json.size() % 4) json += ' ';
    std::vector<uint8_t> out;
    le32(out, 0x46546C67);
    le32(out, 2);
    le32(out, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    le32(out, uint32_t(json.size()));
    le32(out, 0x4E4F534A);
    out.insert(out.end(), json.begin(), json.end());
    le32(out, uint32_t(bin.size()));
    le32(out, 0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

// ---------------------------------------------------------------- textures (deterministic patterns, no game data)
uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }
// a ground colour with a little grain (64 x 64)
std::vector<uint8_t> ground_texture(Colour c, uint32_t seed) {
    std::vector<uint8_t> px;
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 64; x++) {
            const int n = int(hash(seed * 4096 + y * 64 + x) % 21) - 10;
            px.push_back(clamp8(c.r + n)), px.push_back(clamp8(c.g + n)), px.push_back(clamp8(c.b + n));
        }
    return png(64, 64, px, false);
}
// foam: soft white patches, cut out (alpha 0 or 255): value noise on an 8-pixel lattice (wrapping, integer maths),
// thick at the shore (v = 0, the top rows) and thinning out towards the water
std::vector<uint8_t> foam_texture() {
    auto corner = [](unsigned cx, unsigned cy) { return int(hash(0xF0A3u + (cy % 8) * 8 + (cx % 8)) % 256); };
    std::vector<uint8_t> px;
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 64; x++) {
            const unsigned cx = x / 8, cy = y / 8;
            const int fx = int(x % 8), fy = int(y % 8);
            const int n = (corner(cx, cy) * (8 - fx) * (8 - fy) + corner(cx + 1, cy) * fx * (8 - fy) + corner(cx, cy + 1) * (8 - fx) * fy + corner(cx + 1, cy + 1) * fx * fy) / 64;
            const bool on = n + int(y) * 3 < 200;
            const int g = 232 + int(hash(y * 64 + x) % 16);
            px.push_back(clamp8(g)), px.push_back(clamp8(g + 4)), px.push_back(255), px.push_back(on ? 255 : 0);
        }
    return png(64, 64, px, true);
}
// the waterfall's rock (left half) and its splash pad (right half), 128 x 64
std::vector<uint8_t> waterfall_texture(Colour c) {
    std::vector<uint8_t> px;
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 128; x++) {
            const int n = int(hash(0x5A11u + y * 128 + x) % 25) - 12;
            if (x < 64) px.push_back(clamp8(c.r * 3 / 5 + n)), px.push_back(clamp8(c.g * 3 / 5 + n)), px.push_back(clamp8(c.b * 3 / 5 + n));
            else px.push_back(clamp8(205 + n)), px.push_back(clamp8(222 + n)), px.push_back(clamp8(232 + n));
        }
    return png(128, 64, px, false);
}
// the surface model's placeholder (the game supplies the water)
std::vector<uint8_t> placeholder_texture() {
    std::vector<uint8_t> px(64 * 8 * 3, 0);
    for (size_t i = 0; i < px.size(); i += 3) px[i] = 40, px[i + 1] = 110, px[i + 2] = 200;
    return png(64, 8, px, false);
}

// ---------------------------------------------------------------- polygons (exact integer predicates)
int64_t orient(XZ a, XZ b, XZ c) { return int64_t(b.x - a.x) * (c.z - a.z) - int64_t(b.z - a.z) * (c.x - a.x); }
int sign(int64_t v) { return (v > 0) - (v < 0); }
bool on_segment(XZ a, XZ b, XZ p) {
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.z, b.z) <= p.z && p.z <= std::max(a.z, b.z);
}
bool segments_meet(XZ a, XZ b, XZ c, XZ d) {
    const int o1 = sign(orient(a, b, c)), o2 = sign(orient(a, b, d)), o3 = sign(orient(c, d, a)), o4 = sign(orient(c, d, b));
    if (o1 != o2 && o3 != o4) return true;
    return (!o1 && on_segment(a, b, c)) || (!o2 && on_segment(a, b, d)) || (!o3 && on_segment(c, d, a)) || (!o4 && on_segment(c, d, b));
}
// p inside or on the triangle a, b, c of orientation s
bool in_triangle(XZ a, XZ b, XZ c, XZ p, int s) {
    return sign(orient(a, b, p)) * s >= 0 && sign(orient(b, c, p)) * s >= 0 && sign(orient(c, a, p)) * s >= 0;
}

// the inner ring of a polygon (orientation s = sign(area2)): each point moved `d` units inwards along its corner's
// bisector (a miter, at most three times d); rounded to whole units. Empty when that is not a simple polygon of the
// same orientation whose ring quads all face the same way.
std::vector<XZ> inset(const std::vector<XZ>& poly, double d) {
    const size_t n = poly.size();
    const int s = sign(area2(poly));
    std::vector<XZ> out;
    for (size_t i = 0; i < n; i++) {
        const XZ a = poly[(i + n - 1) % n], b = poly[i], c = poly[(i + 1) % n];
        const V3 e1 = unit({double(b.x - a.x), 0, double(b.z - a.z)}), e2 = unit({double(c.x - b.x), 0, double(c.z - b.z)});
        // inwards: left of each edge for a positive orientation, right for a negative one
        const V3 n1 = s > 0 ? V3{-e1.z, 0, e1.x} : V3{e1.z, 0, -e1.x}, n2 = s > 0 ? V3{-e2.z, 0, e2.x} : V3{e2.z, 0, -e2.x};
        V3 m = unit(n1 + n2);
        double c1 = dot(m, n1);
        if (c1 < 1.0 / 3) {
            if (dot(n1 + n2, n1 + n2) < 1e-9) m = n1;
            c1 = std::max(dot(m, n1), 1.0 / 3);
        }
        const V3 p = V3{double(b.x), 0, double(b.z)} + m * (d / c1);
        out.push_back({int(std::lround(p.x)), int(std::lround(p.z))});
    }
    if (!simple_polygon(out) || sign(area2(out)) != s) return {};
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        if (sign(orient(poly[i], poly[j], out[j])) != s || sign(orient(poly[i], out[j], out[i])) != s) return {};
    }
    return out;
}
std::string place_words(const XZ& origin, XZ p) {
    return "near " + std::to_string(p.x + origin.x) + ", " + std::to_string(p.z + origin.z);
}
int ground_at(const std::vector<Sample>& g, XZ world) {
    for (const auto& s : g)
        if (s.x == world.x && s.z == world.z) return s.h;
    return kUnknown;
}
std::string hex(Colour c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}
bool colour_of(const std::string& s, Colour& c) {
    if (s.size() != 7 || s[0] != '#') return false;
    unsigned v[3];
    for (int i = 0; i < 3; i++) {
        v[i] = 0;
        for (int k = 0; k < 2; k++) {
            const char ch = s[size_t(1 + i * 2 + k)];
            const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0) return false;
            v[i] = v[i] * 16 + unsigned(d);
        }
    }
    c = {uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2])};
    return true;
}
bool whole(const Value& v, int lo, int hi, int& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi) return false;
    out = int(std::lround(v.number));
    return true;
}
// river centre-line samples: local places, the section each starts, and its distance along the line
struct RiverSample {
    XZ at;
    int section = 0;
    double along = 0;
};
std::vector<RiverSample> river_samples(const Params& p) {
    std::vector<RiverSample> out;
    double along = 0;
    for (size_t i = 0; i + 1 < p.points.size(); i++) {
        const XZ a = p.points[i], b = p.points[i + 1];
        const double len = std::sqrt(double(b.x - a.x) * (b.x - a.x) + double(b.z - a.z) * (b.z - a.z));
        const int m = std::max(1, int(std::ceil(len / kFineSpacing)));
        for (int j = 0; j < m; j++) {
            const double t = double(j) / m;
            out.push_back({{int(std::lround(a.x + (b.x - a.x) * t)), int(std::lround(a.z + (b.z - a.z) * t))}, int(i), along + len * t});
        }
        along += len;
    }
    if (!p.points.empty()) out.push_back({p.points.back(), int(p.points.size()) - 2, along});
    return out;
}
// the waterfall's foot: in front of the top edge's middle, 150 units downstream
XZ waterfall_foot(const Params& p) {
    const XZ a = p.points[0], b = p.points[1];
    const V3 u = unit({double(b.x - a.x), 0, double(b.z - a.z)});
    const V3 d = V3{u.z, 0, -u.x} * double(p.side);
    return {int(std::lround((a.x + b.x) / 2.0 + d.x * 150)), int(std::lround((a.z + b.z) / 2.0 + d.z * 150))};
}

// a river sample's sideways direction (to the right of the flow), mitred at the line's corners
V3 river_right(const std::vector<RiverSample>& fine, size_t k) {
    const size_t n = fine.size();
    const XZ prev = fine[k ? k - 1 : 0].at, next = fine[k + 1 < n ? k + 1 : k].at, here = fine[k].at;
    const V3 e1 = unit({double(here.x - prev.x), 0, double(here.z - prev.z)}), e2 = unit({double(next.x - here.x), 0, double(next.z - here.z)});
    const V3 r1{e1.z, 0, -e1.x}, r2{e2.z, 0, -e2.x};
    const V3 r = unit(k == 0 ? r2 : k + 1 == n ? r1 : r1 + r2);
    const double c = std::max(dot(r, k == 0 ? r2 : r1), 0.5);
    return r * (1 / c);
}

// ---------------------------------------------------------------- banks
// A bank is a natural rim: from the water's edge (under water: down to the floor) up to a crest a little above the
// water, then a slope down to where it meets the island's ground. Its crest and slope wander a little from place to
// place (a fixed pattern, the same everywhere), and its texture is the island's own ground there (island_texture.py).
constexpr int kToes = 5;
constexpr double kBankRun = 240;  // the farthest a bank's slope runs out from its crest to the ground (a raised pond's mound)
constexpr int kToe[kToes] = {40, 120, 260, 480, 760};  // how far beyond the crest the ground is asked
uint32_t wander(uint32_t seed, uint32_t i) { return hash(seed * 7919u + i * 104729u + 17u); }
XZ offset_xz(XZ q, V3 dir, double by) { return {int(std::lround(q.x + dir.x * by)), int(std::lround(q.z + dir.z * by))}; }

// one cross-section of a bank, outwards from the water's edge; heights relative to the water
struct Section {
    double crest = 8, crest_out = 25, shoulder_out = 60, shoulder = 4, toe_out = 80, toe = -10, skirt = 0;  // outs: from the edge
    bool slope = true;  // a slope down to the ground (false: the ground is as high as the crest there)
};
// edge_ground: the ground at the water's edge (relative); toe_ground: at crest_out + kToe[k] (kUnknown: not known)
// bed: the river bed there (relative; a pond: none); the ground at the edge far below it is a drop beside the water
Section bank_section(double rim, double edge_ground, const int toe_ground[kToes], uint32_t seed, uint32_t i, double bed = -1e9) {
    Section b;
    const uint32_t w = wander(seed, i);
    b.crest_out = 22 + double(w % 23);
    // rise over run: 19 to 27 degrees; a high bank (water standing well above the ground) steepens so that it reaches the
    // ground within kBankRun of its crest: a raised pond keeps a footprint near its outline (on a crowded island the
    // bank of a gentle slope would spread far over the ground round it)
    double slope = 0.35 + double((w >> 8) % 18) / 100.0;
    bool known = false;
    for (int k = 0; k < kToes; k++) known = known || toe_ground[k] != kUnknown;
    if (known && edge_ground > bed - kStepDrop) slope = std::max(slope, (std::max({rim, 8.0, std::min(edge_ground + 3, 30.0)}) - edge_ground) / kBankRun);
    // a little above the ground at the water's edge, but no wall: where the edge runs into a bank higher than that
    // (a cliff beside the water) the ground itself is the bank
    b.crest = std::max({rim, 8.0, std::min(edge_ground + 3, 30.0)});
    if (edge_ground < bed - kStepDrop) {
        // the water's edge hangs over a drop (the lip of a step): a short rounded lip, then straight down to the ground
        b.toe_out = b.crest_out + 20;
        b.toe = b.crest - 12;
        b.shoulder_out = b.crest_out + 6;
        b.shoulder = b.crest - 2;
        b.skirt = edge_ground - 8;
        return b;
    }
    // the first ground the slope line meets
    double prev_t = 0, prev_gap = 1;
    bool found = false;
    for (int k = 0; k < kToes; k++) {
        if (toe_ground[k] == kUnknown) continue;
        const double t = kToe[k];
        if (toe_ground[k] < edge_ground - kStepDrop) {
            // the ground falls away (a cliff): the bank ends before it, where it is, without going down it
            b.toe_out = b.crest_out + std::max(prev_t, 20.0);
            b.toe = std::max(b.crest - slope * std::max(prev_t, 20.0), edge_ground) - 4;
            found = true;
            break;
        }
        const double gap = (b.crest - slope * t) - toe_ground[k];
        if (gap <= 0) {
            const double f = prev_gap > 0 ? prev_gap / (prev_gap - gap) : 0;
            const double at = prev_t + (t - prev_t) * f;
            b.toe_out = b.crest_out + at;
            b.toe = b.crest - slope * at - 6;
            found = true;
            if (at < 4) b.slope = false;
            break;
        }
        prev_t = t, prev_gap = gap;
    }
    if (!found) {
        // no ground within reach (or none known): the slope runs its full length and a short skirt goes on down
        double last = kUnknown;
        for (int k = kToes - 1; k >= 0 && last == kUnknown; k--) last = toe_ground[k];
        if (last == kUnknown) {
            // nothing known there (the game found no ground: off a cliff, over the sea): a short slope and a short skirt
            b.toe_out = b.crest_out + kToe[0];
            b.toe = b.crest - slope * kToe[0];
            b.skirt = b.toe - 30;
        } else {
            b.toe_out = b.crest_out + kToe[kToes - 1];
            b.toe = b.crest - slope * kToe[kToes - 1];
            b.skirt = std::min(b.toe, double(last)) - 8;
        }
    }
    // a rounded top: the shoulder a third of the way down, nearer the crest
    b.shoulder_out = b.crest_out + (b.toe_out - b.crest_out) * 0.3;
    b.shoulder = b.crest - (b.crest - b.toe) * 0.18;
    return b;
}

// a pond's outline in rows a bank is made of: the outline (turned to negative orientation), each edge cut into pieces
// of at most 200 units; each point's outward direction (mitred)
struct Ring {
    std::vector<XZ> q;
    std::vector<V3> out;
    std::vector<double> along;  // the perimeter up to each point
};
Ring pond_ring(const Params& p) {
    std::vector<XZ> poly = p.points;
    if (area2(poly) > 0) std::reverse(poly.begin(), poly.end());
    // a pond has no corners: each is cut twice (a quarter of each edge, at most 150 units), the outline rounding off
    for (int pass = 0; pass < 2 && poly.size() < 64; pass++) {
        std::vector<XZ> cut;
        const size_t n = poly.size();
        for (size_t i = 0; i < n; i++) {
            const XZ a = poly[i], b = poly[(i + 1) % n];
            const double len = std::sqrt(double(b.x - a.x) * (b.x - a.x) + double(b.z - a.z) * (b.z - a.z));
            const double f = len > 0 ? std::min(0.25, 150.0 / len) : 0.25;
            cut.push_back({int(std::lround(a.x + (b.x - a.x) * f)), int(std::lround(a.z + (b.z - a.z) * f))});
            cut.push_back({int(std::lround(b.x + (a.x - b.x) * f)), int(std::lround(b.z + (a.z - b.z) * f))});
        }
        poly.swap(cut);
    }
    Ring r;
    const size_t n = poly.size();
    double along = 0;
    for (size_t i = 0; i < n; i++) {
        const XZ a = poly[i], b = poly[(i + 1) % n];
        const double len = std::sqrt(double(b.x - a.x) * (b.x - a.x) + double(b.z - a.z) * (b.z - a.z));
        const int m = std::max(1, int(std::ceil(len / 150)));
        for (int j = 0; j < m; j++) {
            r.q.push_back({int(std::lround(a.x + (b.x - a.x) * double(j) / m)), int(std::lround(a.z + (b.z - a.z) * double(j) / m))});
            r.along.push_back(along + len * double(j) / m);
        }
        along += len;
    }
    r.along.push_back(along);  // the closing point's
    const size_t m = r.q.size();
    for (size_t i = 0; i < m; i++) {
        const XZ a = r.q[(i + m - 1) % m], b = r.q[i], c = r.q[(i + 1) % m];
        const V3 e1 = unit({double(b.x - a.x), 0, double(b.z - a.z)}), e2 = unit({double(c.x - b.x), 0, double(c.z - b.z)});
        // negative orientation: the inside is on the right of each edge, outward on the left
        const V3 n1{-e1.z, 0, e1.x}, n2{-e2.z, 0, e2.x};
        V3 o = unit(n1 + n2);
        if (dot(n1 + n2, n1 + n2) < 1e-9) o = n1;
        const double cosine = std::max(dot(o, n1), 0.5);
        r.out.push_back(o * (1 / cosine));
    }
    return r;
}
// the places a bank asks the ground at: beyond the crest's furthest wander (crest_out <= 44)
XZ toe_place(XZ q, V3 out, int k) { return offset_xz(q, out, 44 + kToe[k]); }

XZ river_toe(const std::vector<RiverSample>& fine, size_t k, double width, int side, int t) {
    return offset_xz(fine[k].at, river_right(fine, k), side * (width / 2 + 44 + kToe[t]));
}

// the bank's texture: the island's own ground where its slope meets it (the converter reads it; this model's own
// texture stays for a game without that room)
std::string island_extras(int room, const std::vector<XZ>& world) {
    if (room < 1 || room > 49 || world.empty()) return "";
    std::string pts;
    const size_t step = std::max<size_t>(1, world.size() / 64);
    for (size_t i = 0; i < world.size(); i += step) pts += (pts.empty() ? "[" : ",[") + std::to_string(world[i].x) + "," + std::to_string(world[i].z) + "]";
    return "{\"wwhd_island\":{\"room\":" + std::to_string(room) + ",\"points\":[" + pts + "]}}";
}
int ground_rel(const Params& p, const XZ& origin, int origin_y, XZ local) {
    const int h = ground_at(p.ground, {local.x + origin.x, local.z + origin.z});
    return h == kUnknown ? kUnknown : h - origin_y;
}

// ---------------------------------------------------------------- the three bodies
struct Out {
    std::vector<Mesh> main_meshes, surface_meshes;
    std::vector<Material> main_materials, surface_materials;
    std::vector<std::vector<uint8_t>> main_images;
};
double planar(double v) { return v / 300; }  // the bank texture: one repeat every 3 m, from above

bool build_pond(const Params& p, const XZ& origin, int origin_y, Out& o, std::vector<Problem>& problems) {
    const Ring ring = pond_ring(p);
    const std::vector<XZ>& q = ring.q;
    const size_t n = q.size();
    if (!simple_polygon(q)) return problems.push_back({"the outline crosses itself", "points"}), false;
    std::vector<unsigned> tris;
    if (!triangulate(q, tris)) return problems.push_back({"the outline can't be filled (it crosses itself)", "points"}), false;
    const double area = double(-area2(q)) / 2;
    const double D = p.depth, R = p.rim;
    // the inner ring of the surface (where it is fully there) and its foam: 50 units in, less for a small pond
    double d = std::clamp(std::sqrt(area) * 0.1, 15.0, 50.0);
    std::vector<XZ> inner;
    for (int tries = 0; tries < 4 && inner.empty(); tries++, d /= 2) inner = inset(q, d);
    if (inner.empty()) return problems.push_back({"the outline is too narrow or its corners too sharp for a shore", "points"}), false;
    std::vector<unsigned> inner_tris;
    if (!triangulate(inner, inner_tris)) return problems.push_back({"the outline is too narrow for a shore", "points"}), false;
    // the floor: the outline drawn in by most of the depth (a bowl), less where it is narrow
    double df = std::clamp(D * 0.9, 20.0, std::sqrt(area) * 0.3);
    std::vector<XZ> floor_ring;
    for (int tries = 0; tries < 5 && floor_ring.empty(); tries++, df /= 2) floor_ring = inset(q, df);
    if (floor_ring.empty()) floor_ring = inner;
    std::vector<unsigned> floor_tris;
    if (!triangulate(floor_ring, floor_tris)) floor_ring = inner, floor_tris = inner_tris;
    const V3 up{0, 1, 0};
    // the sections of the bank, each from its own ground
    std::vector<Section> sec(n);
    // the bank in up to four arcs, each painted with the island's ground where its slope meets it (grass on one side,
    // sand on the other); the bowl under the water with the ground at the shore
    const size_t arcs = std::min<size_t>(4, n);
    auto arc_of = [&](size_t i) { return i * arcs / n; };
    std::vector<std::vector<XZ>> toes(arcs);
    std::vector<XZ> shore;
    for (size_t i = 0; i < n; i++) {
        int toe[kToes];
        for (int k = 0; k < kToes; k++) toe[k] = ground_rel(p, origin, origin_y, toe_place(q[i], ring.out[i], k));
        const int edge = ground_rel(p, origin, origin_y, q[i]);
        sec[i] = bank_section(R, edge == kUnknown ? -D : double(edge), toe, 1, uint32_t(i));
        const XZ meets = offset_xz(q[i], ring.out[i], sec[i].toe_out + 10);
        toes[arc_of(i)].push_back({meets.x + origin.x, meets.z + origin.z});
        shore.push_back({q[i].x + origin.x, q[i].z + origin.z});
    }
    auto at_out = [&](size_t i, double out, double y) { return V3{q[i].x + ring.out[i].x * out, y, q[i].z + ring.out[i].z * out}; };
    auto uv_of = [](V3 v, double t[2]) { t[0] = planar(v.x), t[1] = planar(v.z); };
    Prim bed, water, sand, grass, stone;
    std::vector<Prim> banks(arcs);
    for (size_t k = 0; k < arcs; k++) banks[k].material = "bank" + std::to_string(k + 1), banks[k].visible = true, banks[k].smooth = true;
    bed.material = "bed", bed.visible = true, bed.smooth = true;
    water.material = "col_water", sand.material = "col_sand", grass.material = "col_grass", stone.material = "col_stone";
    auto band = [&](Prim& visible, Prim* col, V3 a, V3 b, V3 c, V3 e) {
        double ta[2], tb[2], tc[2], te[2];
        uv_of(a, ta), uv_of(b, tb), uv_of(c, tc), uv_of(e, te);
        visible.quad(a, b, c, e, up, ta, tb, tc, te);
        if (col) col->quad(a, b, c, e, up);
    };
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        const Section &si = sec[i], &sj = sec[j];
        const V3 fi = at(floor_ring[i], -D), fj = at(floor_ring[j], -D);
        const V3 ei = at(q[i], 0), ej = at(q[j], 0);
        Prim& bank = banks[arc_of(i)];
        // under water: the bowl from the floor's edge up to the water's edge
        band(bed, &sand, fi, fj, ej, ei);
        // the lip above the water, the rounded top, the slope down to the island's ground (and a skirt where it can't reach)
        const V3 ci = at_out(i, si.crest_out, si.crest), cj = at_out(j, sj.crest_out, sj.crest);
        band(bank, &grass, ei, ej, cj, ci);
        const V3 hi = at_out(i, si.shoulder_out, si.shoulder), hj = at_out(j, sj.shoulder_out, sj.shoulder);
        band(bank, &grass, ci, cj, hj, hi);
        const V3 ti = at_out(i, si.toe_out, si.toe), tj = at_out(j, sj.toe_out, sj.toe);
        band(bank, &grass, hi, hj, tj, ti);
        if (si.skirt != 0 || sj.skirt != 0) {
            const V3 ki = at_out(i, si.toe_out + 6, si.skirt != 0 ? si.skirt : si.toe - 10), kj = at_out(j, sj.toe_out + 6, sj.skirt != 0 ? sj.skirt : sj.toe - 10);
            band(bank, &stone, ti, tj, kj, ki);
        }
    }
    for (size_t t = 0; t < floor_tris.size(); t += 3) {
        const V3 a = at(floor_ring[floor_tris[t]], -D), b = at(floor_ring[floor_tris[t + 1]], -D), c = at(floor_ring[floor_tris[t + 2]], -D);
        double ta[2], tb[2], tc[2];
        uv_of(a, ta), uv_of(b, tb), uv_of(c, tc);
        bed.triangle(a, b, c, up, ta, tb, tc);
        sand.triangle(a, b, c, up);
    }
    for (size_t t = 0; t < tris.size(); t += 3) water.triangle(at(q[tris[t]], 0), at(q[tris[t + 1]], 0), at(q[tris[t + 2]], 0), up);
    // the water (the blue of the island's shallows, a little under the level), the wave lines on it (fading out
    // towards the shore) and the coast foam along the shore, a little above it
    Prim surface, waves, foam;
    surface.material = "surface", surface.visible = true;
    waves.material = "waves", waves.visible = true;
    foam.material = "edge", foam.visible = true;
    auto uv_s = [](XZ v, double t[2]) { t[0] = v.x / 50.0, t[1] = v.z / 50.0; };
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        double a[2], b[2], c[2], e[2];
        uv_s(q[i], a), uv_s(q[j], b), uv_s(inner[j], c), uv_s(inner[i], e);
        surface.quad(at(q[i], -2), at(q[j], -2), at(inner[j], -2), at(inner[i], -2), up, a, b, c, e);
    }
    // the wave lines: rings round the shore (U along it, V the distance from it: the lines lie along the shore and roll
    // in), and the coast foam on the outermost band; each ring as far in as the outline allows
    std::vector<std::vector<XZ>> rings{q};
    std::vector<double> dist{0};
    for (double want : {60.0, 150.0, 260.0}) {
        std::vector<XZ> r;
        for (double dd = want; dd > dist.back() + 10 && r.empty(); dd *= 0.7) {
            r = inset(q, dd);
            if (!r.empty()) dist.push_back(dd);
        }
        if (r.empty()) break;
        rings.push_back(std::move(r));
    }
    for (size_t k = 0; k + 1 < rings.size(); k++)
        for (size_t i = 0; i < n; i++) {
            const size_t j = (i + 1) % n;
            const double u0 = ring.along[i] / 400, u1 = ring.along[i + 1] / 400, v0 = dist[k] / 100, v1 = dist[k + 1] / 100;
            const double w0[2] = {u0, v0}, w1[2] = {u1, v0}, w2[2] = {u1, v1}, w3[2] = {u0, v1};
            waves.quad(at(rings[k][i], 0), at(rings[k][j], 0), at(rings[k + 1][j], 0), at(rings[k + 1][i], 0), up, w0, w1, w2, w3);
            if (k == 0) {  // the foam: V 0 at the shore, 1 out on the water (as on an island's coast)
                const double f0[2] = {ring.along[i] / 1000, 0}, f1[2] = {ring.along[i + 1] / 1000, 0}, f2[2] = {ring.along[i + 1] / 1000, 1}, f3[2] = {ring.along[i] / 1000, 1};
                foam.quad(at(rings[0][i], 0.8), at(rings[0][j], 0.8), at(rings[1][j], 0.8), at(rings[1][i], 0.8), up, f0, f1, f2, f3);
            }
        }
    for (size_t t = 0; t < inner_tris.size(); t += 3) {
        double a[2], b[2], c[2];
        uv_s(inner[inner_tris[t]], a), uv_s(inner[inner_tris[t + 1]], b), uv_s(inner[inner_tris[t + 2]], c);
        surface.triangle(at(inner[inner_tris[t]], -2), at(inner[inner_tris[t + 1]], -2), at(inner[inner_tris[t + 2]], -2), up, a, b, c);
    }
    o.main_images.push_back(ground_texture(p.floor, 1));
    Mesh visible{"pond", {}};
    bed.finish();
    o.main_materials.push_back({"bed", 0, false, island_extras(p.room, shore)});
    visible.prims.push_back(std::move(bed));
    for (size_t k = 0; k < arcs; k++) {
        if (banks[k].idx.empty()) continue;
        banks[k].finish();
        o.main_materials.push_back({banks[k].material, 0, false, island_extras(p.room, toes[k])});
        visible.prims.push_back(std::move(banks[k]));
    }
    for (const char* m : {"col_water", "col_sand", "col_grass", "col_stone"}) o.main_materials.push_back({m});
    o.main_meshes.push_back(std::move(visible));
    Mesh col{"pond_col", {}};
    for (Prim* c : {&water, &sand, &grass, &stone})
        if (!c->idx.empty()) col.prims.push_back(std::move(*c));
    o.main_meshes.push_back(std::move(col));
    Mesh water_mesh{"pond_surface", {std::move(surface), std::move(waves)}};
    o.surface_materials = {{"surface", 0}, {"waves", 0}};
    if (p.foam) water_mesh.prims.push_back(std::move(foam)), o.surface_materials.push_back({"edge", 0});  // the coast foam along the shore
    o.surface_meshes.push_back(std::move(water_mesh));
    return true;
}

bool build_river(const Params& p, const XZ& origin, int origin_y, Out& o, Built& b, std::vector<Problem>& problems) {
    // the ground every kFineSpacing units along the centre line (where it drops is found to that), the water drawn
    // in rows every kSampleSpacing units and at both sides of each step
    const auto fine = river_samples(p);
    const size_t n = fine.size();
    std::vector<double> g(n);
    for (size_t k = 0; k < n; k++) {
        const int h = ground_at(p.ground, {fine[k].at.x + origin.x, fine[k].at.z + origin.z});
        if (h == kUnknown) return problems.push_back({"no ground under the river " + place_words(origin, fine[k].at), "points"}), false;
        g[k] = h - origin_y;
    }
    auto shallow = [&](int section) { return size_t(section) < p.shallow.size() && p.shallow[size_t(section)]; };
    // the water's level: the ground plus the section's water; never up the flow; a drop of more than kStepDrop a step
    std::vector<double> level(n);
    std::vector<bool> step(n, false);
    bool warned = false;
    for (size_t k = 0; k < n; k++) {
        const double want = g[k] + (shallow(fine[k].section) ? kShallowDepth : p.depth);
        if (!k) {
            level[k] = want;
            continue;
        }
        if (want >= level[k - 1]) {
            level[k] = level[k - 1];
            if (g[k] + 10 > level[k] && !warned)
                problems.push_back({"the river runs uphill " + place_words(origin, fine[k].at) + ": its water is under the ground there", "points", "warning"}), warned = true;
        } else {
            level[k] = want;
            step[k] = level[k - 1] - want > kStepDrop;
        }
    }
    // drops close together (a ledge in a cliff) are one step: the water falls past the ledge
    std::vector<bool> inside(n, false);
    for (size_t i = 1; i < n; i++)
        if (step[i])
            for (size_t j = i + 1; j < n && j <= i + 4; j++)
                if (step[j]) {
                    for (size_t m = i; m < j; m++) inside[m] = true;
                    step[i] = false;
                    i = j - 1;
                    break;
                }
    // the rows: every kSampleSpacing / kFineSpacing samples, the centre line's own points, both sides of a step
    std::vector<size_t> rows;
    const size_t every = size_t(kSampleSpacing / kFineSpacing);
    for (size_t k = 0; k < n; k++) {
        if (inside[k]) continue;
        const bool corner = k == 0 || k + 1 == n || fine[k].section != fine[k - 1].section;
        if (corner || k % every == 0 || step[k] || (k + 1 < n && (step[k + 1] || inside[k + 1]))) rows.push_back(k);
    }
    for (size_t r : rows) b.levels.push_back(int(std::lround(level[r])));
    auto right_of = [&](size_t k) { return river_right(fine, k); };
    const double W = p.width;
    const double across[4] = {-0.5, -0.3, 0.3, 0.5};
    auto place = [&](size_t k, double f, double y) {
        const XZ c = fine[k].at;
        const V3 r = right_of(k);
        return V3{c.x + r.x * W * f, y, c.z + r.z * W * f};
    };
    // a bank's section at a row, on one side (+1 right, -1 left of the flow)
    auto section = [&](size_t k, int side) {
        int toe[kToes];
        for (int t = 0; t < kToes; t++) {  // relative to the water there, as the edge
            toe[t] = ground_rel(p, origin, origin_y, river_toe(fine, k, W, side, t));
            if (toe[t] != kUnknown) toe[t] -= int(std::lround(level[k]));
        }
        const int edge = ground_rel(p, origin, origin_y, offset_xz(fine[k].at, right_of(k), side * W / 2));
        Section s = bank_section(0, (edge == kUnknown ? g[k] : double(edge)) - level[k], toe, side > 0 ? 2u : 3u, uint32_t(k), g[k] - level[k]);
        s.crest = std::max(s.crest, 8.0);
        return s;
    };
    auto bank_at = [&](size_t k, int side, double out, double y) {  // `out` beyond the water's edge, y relative to the level
        const XZ c = fine[k].at;
        const V3 r = right_of(k);
        const double f = side * (W / 2 + out);
        return V3{c.x + r.x * f, level[k] + y, c.z + r.z * f};
    };
    const V3 up{0, 1, 0};
    Prim surface, foam, water, wade, sand, grass, stone;
    surface.material = "surface", surface.visible = true;
    foam.material = "edge", foam.visible = true;
    water.material = "col_water", wade.material = "col_shallow", sand.material = "col_sand", grass.material = "col_grass", stone.material = "col_stone";
    // the banks of each section are painted with the island's ground along that section (grass up there, sand on the beach)
    const size_t sections = p.points.size() - 1;
    std::vector<Prim> banks(sections);
    std::vector<std::vector<XZ>> toes(sections);
    for (size_t i = 0; i < sections; i++) banks[i].material = "bank" + std::to_string(i + 1), banks[i].visible = true, banks[i].smooth = true;
    auto uv_of = [](V3 v, double t[2]) { t[0] = planar(v.x), t[1] = planar(v.z); };
    auto band = [&](Prim& visible, Prim* col, V3 a, V3 bb, V3 c, V3 e) {
        double ta[2], tb[2], tc[2], te[2];
        uv_of(a, ta), uv_of(bb, tb), uv_of(c, tc), uv_of(e, te);
        visible.quad(a, bb, c, e, up, ta, tb, tc, te);
        if (col) col->quad(a, bb, c, e, up);
    };
    for (size_t j = 1; j < rows.size(); j++) {
        const size_t a = rows[j - 1], k = rows[j];
        const int sect = fine[a].section;
        if (step[k]) {
            // the drop: no water across it (the waterfall goes there), foam across the lip; the step from the upper row
            const V3 l = place(a, -0.5, 0), r = place(a, 0.5, 0);
            b.steps.push_back({{int(std::lround(l.x)), int(std::lround(l.z))}, {int(std::lround(r.x)), int(std::lround(r.z))}, int(std::lround(level[a])),
                               int(std::lround(level[k])), int(std::lround(g[k])), sect});
            const V3 back = unit({double(fine[a].at.x - fine[k].at.x), 0, double(fine[a].at.z - fine[k].at.z)}) * 45;
            const double f0[2] = {0, 1}, f1[2] = {W / 300, 1}, f2[2] = {W / 300, 0}, f3[2] = {0, 0};
            foam.quad(place(a, -0.5, level[a] + 0.8), place(a, 0.5, level[a] + 0.8), place(a, 0.5, level[a] + 0.8) + back, place(a, -0.5, level[a] + 0.8) + back, up, f0, f1, f2, f3);
            // and where the water lands: foam across the lower water, strongest at the fall, fading downstream
            const V3 ahead = back * (-50.0 / 45);
            for (int band = 0; band < 3; band++) {  // three lines of it, one after the other
                const V3 from = ahead * (band * 0.8);
                const double g0[2] = {band * 0.37, 0}, g1[2] = {band * 0.37 + W / 300, 0}, g2[2] = {band * 0.37 + W / 300, 1}, g3[2] = {band * 0.37, 1};
                foam.quad(place(k, -0.5, level[k] + 1.2) + from, place(k, 0.5, level[k] + 1.2) + from, place(k, 0.5, level[k] + 1.2) + from + ahead,
                          place(k, -0.5, level[k] + 1.2) + from + ahead, up, g0, g1, g2, g3);
            }
            continue;
        }
        const double y0 = level[a], y1 = level[k];
        for (int c = 0; c < 3; c++) {
            // every 200 units across and along (V grows downstream), as the game's own streams
            const double ta[2] = {(across[c] + 0.5) * W / 200, fine[a].along / 200}, tb[2] = {(across[c + 1] + 0.5) * W / 200, fine[a].along / 200},
                         tc[2] = {(across[c + 1] + 0.5) * W / 200, fine[k].along / 200}, td[2] = {(across[c] + 0.5) * W / 200, fine[k].along / 200};
            surface.quad(place(a, across[c], y0), place(a, across[c + 1], y0), place(k, across[c + 1], y1), place(k, across[c], y1), up, ta, tb, tc, td);
        }
        // foam along both banks, on the water
        for (int side : {-1, 1}) {
            const double e = side * 0.5, i = side * (0.5 - std::min(45.0, W * 0.2) / W);
            const double f0[2] = {fine[a].along / 300, 0}, f1[2] = {fine[k].along / 300, 0}, f2[2] = {fine[k].along / 300, 1}, f3[2] = {fine[a].along / 300, 1};
            foam.quad(place(a, e, y0 + 0.8), place(k, e, y1 + 0.8), place(k, i, y1 + 0.8), place(a, i, y0 + 0.8), up, f0, f1, f2, f3);
        }
        // the bed: the ground the game reported, a few units up (its highest fine sample between the rows: a bump between
        // them would stand above a straight bed), narrower than the water by its inner banks
        double ga = g[a], gk = g[k];
        for (size_t i = a + 1; i < k; i++) {
            const double t = double(i - a) / double(k - a), lin = ga * (1 - t) + gk * t;
            if (g[i] > lin) ga += g[i] - lin, gk += g[i] - lin;
        }
        // always under the water (where the ground rises above it the island's own ground shows)
        ga = std::min(ga, y0 - kBedLift - 6), gk = std::min(gk, y1 - kBedLift - 6);
        const double ina = std::min((y0 - ga) * 0.8, W * 0.3) / W, ink = std::min((y1 - gk) * 0.8, W * 0.3) / W;
        Prim& bank = banks[size_t(sect)];
        // next to a step the banks stop where they are (no skirt down the cliff)
        const bool near_step = (j + 1 < rows.size() && step[rows[j + 1]]) || (j >= 2 && step[a]);
        band(bank, shallow(sect) ? &wade : &sand, place(a, -0.5 + ina, ga + kBedLift), place(a, 0.5 - ina, ga + kBedLift), place(k, 0.5 - ink, gk + kBedLift),
             place(k, -0.5 + ink, gk + kBedLift));
        if (!shallow(sect)) water.quad(place(a, -0.5, y0), place(a, 0.5, y0), place(k, 0.5, y1), place(k, -0.5, y1), up);
        // the banks: under water from the bed's edge up to the water's edge, the lip, the slope to the island's ground
        for (int side : {-1, 1}) {
            const Section sa = section(a, side), sk = section(k, side);
            const double fa = side * (0.5 - ina), fk = side * (0.5 - ink);
            const V3 ba = place(a, fa, ga + kBedLift), bk = place(k, fk, gk + kBedLift);
            const V3 ea = bank_at(a, side, 0, 0), ek = bank_at(k, side, 0, 0);
            const V3 ca = bank_at(a, side, sa.crest_out, sa.crest), ck = bank_at(k, side, sk.crest_out, sk.crest);
            const V3 ha = bank_at(a, side, sa.shoulder_out, sa.shoulder), hk = bank_at(k, side, sk.shoulder_out, sk.shoulder);
            const V3 ta = bank_at(a, side, sa.toe_out, sa.toe), tk = bank_at(k, side, sk.toe_out, sk.toe);
            band(bank, &sand, ba, bk, ek, ea);
            band(bank, &grass, ea, ek, ck, ca);
            band(bank, &grass, ca, ck, hk, ha);
            band(bank, &grass, ha, hk, tk, ta);
            if ((sa.skirt != 0 || sk.skirt != 0) && !near_step) {
                const V3 ka = bank_at(a, side, sa.toe_out + 6, sa.skirt != 0 ? sa.skirt : sa.toe - 10), kk = bank_at(k, side, sk.toe_out + 6, sk.skirt != 0 ? sk.skirt : sk.toe - 10);
                band(bank, &stone, ta, tk, kk, ka);
            }
            const XZ toe = offset_xz(fine[a].at, right_of(a), side * (W / 2 + sa.toe_out + 10));
            toes[size_t(sect)].push_back({toe.x + origin.x, toe.z + origin.z});
        }
    }
    o.main_images.push_back(ground_texture(p.floor, 2));
    Mesh visible{"river", {}};
    for (size_t i = 0; i < sections; i++) {
        if (banks[i].idx.empty()) continue;
        banks[i].finish();
        o.main_materials.push_back({banks[i].material, 0, false, island_extras(p.room, toes[i])});
        visible.prims.push_back(std::move(banks[i]));
    }
    if (visible.prims.empty()) return problems.push_back({"nothing to build: the river has no length", "points"}), false;
    for (const char* m : {"col_water", "col_shallow", "col_sand", "col_grass", "col_stone"}) o.main_materials.push_back({m});
    o.main_meshes.push_back(std::move(visible));
    Mesh col{"river_col", {}};
    for (Prim* c : {&water, &wade, &sand, &grass, &stone})
        if (!c->idx.empty()) col.prims.push_back(std::move(*c));
    o.main_meshes.push_back(std::move(col));
    Mesh shore{"river_surface", {std::move(surface)}};
    o.surface_materials = {{"surface", 0}};
    if (!foam.idx.empty()) shore.prims.push_back(std::move(foam)), o.surface_materials.push_back({"edge", 0});
    o.surface_meshes.push_back(std::move(shore));
    return true;
}

bool build_waterfall(const Params& p, const XZ& origin, Out& o) {
    const XZ a = p.points[0], bp = p.points[1];
    const V3 u = unit({double(bp.x - a.x), 0, double(bp.z - a.z)});
    const V3 d = V3{u.z, 0, -u.x} * double(p.side);  // downstream: where the water falls to
    const double L = std::sqrt(double(bp.x - a.x) * (bp.x - a.x) + double(bp.z - a.z) * (bp.z - a.z));
    const double top = p.top + 2, bottom = p.bottom, H = top - bottom;
    const V3 A = at(a, 0), B = at(bp, 0), up{0, 1, 0};
    // the sheet: thin, leaving the lip a little forward and falling straight; V grows downwards (Forest Haven's falls)
    Prim sheet, lip;
    sheet.material = "surface", sheet.visible = true;
    lip.material = "edge", lip.visible = true;
    const double cols[3] = {0, 0.5, 1};
    const double rows[4] = {0, 0.06, 0.5, 1};
    auto sheet_at = [&](double f, double r) {
        const double forward = r < 0.06 ? 4 + 14 * (r / 0.06) : 18;  // the water curls over the lip, then falls
        return A + (B - A) * f + d * forward + V3{0, top - H * r, 0};
    };
    for (int c = 0; c < 2; c++)
        for (int r = 0; r < 3; r++) {
            const double ta[2] = {cols[c] * L / 500, rows[r] * H / 700}, tb[2] = {cols[c + 1] * L / 500, rows[r] * H / 700},
                         tc[2] = {cols[c + 1] * L / 500, rows[r + 1] * H / 700}, td[2] = {cols[c] * L / 500, rows[r + 1] * H / 700};
            sheet.quad(sheet_at(cols[c], rows[r]), sheet_at(cols[c + 1], rows[r]), sheet_at(cols[c + 1], rows[r + 1]), sheet_at(cols[c], rows[r + 1]), d, ta, tb, tc, td);
        }
    // foam where the water leaves the lip: a band over it
    {
        const double f0[2] = {0, 0}, f1[2] = {L / 300, 0}, f2[2] = {L / 300, 1}, f3[2] = {0, 1};
        lip.quad(A - d * 30 + V3{0, top + 1, 0}, B - d * 30 + V3{0, top + 1, 0}, B + d * 10 + V3{0, top - 2, 0}, A + d * 10 + V3{0, top - 2, 0}, V3{0, 1, 0} + d * 0.3, f0, f1, f2, f3);
    }
    // the splash at its foot: a soft ring of foam on the water round where it lands
    {
        const V3 centre = (A + B) * 0.5 + d * 18 + V3{0, bottom + 3, 0};
        const double rx = L / 2 + 50, rz = 110;
        const int segments = 12;
        for (int i = 0; i < segments; i++) {
            // an ellipse along the sheet, in fixed steps (exact: no trigonometry, a polygon of 12 points)
            static const double cs[13][2] = {{1, 0}, {0.866, 0.5}, {0.5, 0.866}, {0, 1}, {-0.5, 0.866}, {-0.866, 0.5}, {-1, 0}, {-0.866, -0.5}, {-0.5, -0.866}, {0, -1}, {0.5, -0.866}, {0.866, -0.5}, {1, 0}};
            auto ring_at = [&](int k, double s) { return centre + u * (cs[k][0] * rx * s) + d * (cs[k][1] * rz * s); };
            // the coast foam's texture: strongest (V 0) where the water lands, fading outwards
            const double t0[2] = {i / 12.0 * 4, 0}, t1[2] = {(i + 1) / 12.0 * 4, 0}, t2[2] = {(i + 1) / 12.0 * 4, 1}, t3[2] = {i / 12.0 * 4, 1};
            lip.quad(ring_at(i, 0.35), ring_at(i + 1, 0.35), ring_at(i + 1, 1), ring_at(i, 1), up, t0, t1, t2, t3);
        }
    }
    // the rock behind it (the island's own ground texture; mostly inside the cliff), its collision a wall; the splash
    // ground under it has collision only
    Prim rock, pad, wall;
    rock.material = "bank", rock.visible = true;
    pad.material = "col_waterfall", wall.material = "col_wall";
    const V3 back = d * -8;
    const double r0[2] = {0, top / 300}, r1[2] = {L / 300, top / 300}, r2[2] = {L / 300, (bottom - 20) / 300}, r3[2] = {0, (bottom - 20) / 300};
    rock.quad(A + back + V3{0, top, 0}, B + back + V3{0, top, 0}, B + back + V3{0, bottom - 20, 0}, A + back + V3{0, bottom - 20, 0}, d, r0, r1, r2, r3);
    wall.quad(A + back + V3{0, top, 0}, B + back + V3{0, top, 0}, B + back + V3{0, bottom - 20, 0}, A + back + V3{0, bottom - 20, 0}, d);
    const V3 pa = A - u * 40 + V3{0, bottom + 1, 0}, pb = B + u * 40 + V3{0, bottom + 1, 0};
    pad.quad(pa, pb, pb + d * 220, pa + d * 220, up);
    const XZ foot = waterfall_foot(p);
    o.main_images.push_back(ground_texture(p.floor, 3));
    o.main_materials.push_back({"bank", 0, false, island_extras(p.room, {{a.x + origin.x, a.z + origin.z}, {bp.x + origin.x, bp.z + origin.z}, {foot.x + origin.x, foot.z + origin.z}})});
    for (const char* m : {"col_waterfall", "col_wall"}) o.main_materials.push_back({m});
    o.main_meshes.push_back({"waterfall", {std::move(rock)}});
    o.main_meshes.push_back({"waterfall_col", {std::move(pad), std::move(wall)}});
    o.surface_meshes.push_back({"waterfall_surface", {std::move(sheet), std::move(lip)}});
    o.surface_materials = {{"surface", 0}, {"edge", 0}};
    return true;
}
}  // namespace

// ---------------------------------------------------------------- pieces
std::string metres(int64_t k) {
    const bool negative = k < 0;
    const uint64_t a = negative ? uint64_t(-k) : uint64_t(k);
    std::string s = (negative ? "-" : "") + std::to_string(a / 1024);
    uint64_t frac = (a % 1024) * 9765625ULL;  // 10^10 / 1024: ten decimal places, exact
    if (frac) {
        std::string digits = std::to_string(frac);
        digits.insert(digits.begin(), 10 - digits.size(), '0');
        while (!digits.empty() && digits.back() == '0') digits.pop_back();
        s += "." + digits;
    }
    return s == "-0" ? "0" : s;
}
uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & uint32_t(-int32_t(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}
std::vector<uint8_t> png(unsigned width, unsigned height, const std::vector<uint8_t>& pixels, bool alpha) {
    const unsigned channels = alpha ? 4 : 3;
    std::vector<uint8_t> raw;  // each row: filter 0, then the row
    for (unsigned y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels.begin() + long(y * width * channels), pixels.begin() + long((y + 1) * width * channels));
    }
    // zlib: stored blocks (no compression: the same bytes from every zlib), then Adler-32
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size() || raw.empty(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(len)), z.push_back(uint8_t(len >> 8)), z.push_back(uint8_t(~len)), z.push_back(uint8_t(~len >> 8));
        z.insert(z.end(), raw.begin() + long(at), raw.begin() + long(at + len));
        if (raw.empty()) break;
    }
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t c : raw) s1 = (s1 + c) % 65521, s2 = (s2 + s1) % 65521;
    be32(z, (s2 << 16) | s1);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& body) {
        be32(out, uint32_t(body.size()));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), body.begin(), body.end());
        be32(out, crc32(out.data() + start, out.size() - start));
    };
    std::vector<uint8_t> ihdr;
    be32(ihdr, width), be32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, uint8_t(alpha ? 6 : 2), 0, 0, 0});
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}
int64_t area2(const std::vector<XZ>& poly) {
    int64_t a = 0;
    for (size_t i = 0; i < poly.size(); i++) {
        const XZ p = poly[i], q = poly[(i + 1) % poly.size()];
        a += int64_t(p.x) * q.z - int64_t(q.x) * p.z;
    }
    return a;
}
bool simple_polygon(const std::vector<XZ>& poly) {
    const size_t n = poly.size();
    if (n < 3) return false;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (poly[i] == poly[j]) return false;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            if (j == i + 1 || (i == 0 && j == n - 1)) {
                // neighbours share one point: they must not fold back onto each other
                const XZ a = poly[i], b = poly[(i + 1) % n], c = poly[j], d = poly[(j + 1) % n];
                const XZ shared = j == i + 1 ? b : a, other1 = j == i + 1 ? a : b, other2 = j == i + 1 ? d : c;
                if (!orient(other1, shared, other2) && (int64_t(other1.x - shared.x) * (other2.x - shared.x) + int64_t(other1.z - shared.z) * (other2.z - shared.z)) > 0) return false;
                continue;
            }
            if (segments_meet(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n])) return false;
        }
    return area2(poly) != 0;
}
bool triangulate(const std::vector<XZ>& poly, std::vector<unsigned>& out) {
    out.clear();
    const size_t n = poly.size();
    if (n < 3) return false;
    const int s = sign(area2(poly));
    if (!s) return false;
    std::vector<unsigned> v(n);
    for (size_t i = 0; i < n; i++) v[i] = unsigned(i);
    size_t guard = 0;
    while (v.size() > 3) {
        if (++guard > n * n + 16) return false;
        bool clipped = false;
        for (size_t i = 0; i < v.size(); i++) {
            const unsigned a = v[(i + v.size() - 1) % v.size()], b = v[i], c = v[(i + 1) % v.size()];
            const int o = sign(orient(poly[a], poly[b], poly[c]));
            if (o != s) {
                if (o == 0 && !(poly[a] == poly[c])) {  // a straight corner: drop the point, nothing to fill
                    // only when nothing else lies on it (the polygon is simple: it can't)
                    v.erase(v.begin() + long(i));
                    clipped = true;
                    break;
                }
                continue;
            }
            bool ear = true;
            for (unsigned w : v) {
                if (w == a || w == b || w == c) continue;
                if (in_triangle(poly[a], poly[b], poly[c], poly[w], s)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;
            out.insert(out.end(), {a, b, c});
            v.erase(v.begin() + long(i));
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (sign(orient(poly[v[0]], poly[v[1]], poly[v[2]])) == s) out.insert(out.end(), {v[0], v[1], v[2]});
    return !out.empty();
}

// ---------------------------------------------------------------- parameters
namespace {
bool point_of(const Value& v, XZ& out) {
    double x, z;
    if (v.type == Value::Array && v.array.size() >= 2 && v.array[0].type == Value::Number && v.array[1].type == Value::Number) x = v.array[0].number, z = v.array[1].number;
    else if (v.type == Value::Object && v.get("x").type == Value::Number && v.get("z").type == Value::Number) x = v.get("x").number, z = v.get("z").number;
    else return false;
    if (!std::isfinite(x) || !std::isfinite(z) || std::fabs(x) > 1e7 || std::fabs(z) > 1e7) return false;
    out = {int(std::lround(x)), int(std::lround(z))};
    return true;
}
}  // namespace

bool parse(const std::string& kind, const Value& p, const XZ& origin, int origin_y, Params& out, std::string& why) {
    Params r;
    r.kind = kind;
    if (kind != "pond" && kind != "river" && kind != "waterfall") return why = "a body of water is a pond, a river or a waterfall", false;
    const Value& pts = p.get("points");
    if (pts.type != Value::Array) return why = "\"points\" is a list of {x, z} places", false;
    for (const auto& v : pts.array) {
        XZ q;
        if (!point_of(v, q)) return why = "each point is {x, z} (or {x, y, z}) in world units", false;
        r.points.push_back({q.x - origin.x, q.z - origin.z});
    }
    auto number = [&](const char* key, int lo, int hi, int& field) {
        const Value& v = p.get(key);
        if (v.type == Value::Null) return true;
        if (!whole(v, lo, hi, field)) return why = std::string("\"") + key + "\" is a number from " + std::to_string(lo) + " to " + std::to_string(hi), false;
        return true;
    };
    if (!number("depth", kMinDepth, kMaxDepth, r.depth) || !number("rim", 0, kMaxRim, r.rim) || !number("width", kMinWidth, kMaxWidth, r.width)) return false;
    if (p.get("foam").type == Value::Bool) r.foam = p.get("foam").boolean;
    else if (p.get("foam").type != Value::Null) return why = "\"foam\" is true or false", false;
    if (p.get("floor").type == Value::String) {
        if (!colour_of(p.get("floor").text, r.floor)) return why = "\"floor\" is a colour \"#rrggbb\"", false;
    } else if (p.get("floor").type != Value::Null)
        return why = "\"floor\" is a colour \"#rrggbb\"", false;
    if (p.get("shallow").type == Value::Array) {
        for (const auto& v : p.get("shallow").array) {
            if (v.type != Value::Bool) return why = "\"shallow\" is a list of true / false, one per section", false;
            r.shallow.push_back(v.boolean);
        }
    } else if (p.get("shallow").type == Value::Bool)
        r.shallow.assign(r.points.size() > 1 ? r.points.size() - 1 : 0, p.get("shallow").boolean);
    else if (p.get("shallow").type != Value::Null)
        return why = "\"shallow\" is a list of true / false, one per section", false;
    if (kind == "river") r.shallow.resize(r.points.size() > 1 ? r.points.size() - 1 : 0, false);
    else r.shallow.clear();
    if (kind == "waterfall") {
        int top = origin_y, bottom = origin_y - 600;
        if (!number("top", -100000, 100000, top) || !number("bottom", -100000, 100000, bottom)) return false;
        r.top = top - origin_y, r.bottom = bottom - origin_y;
        if (p.get("side").type != Value::Null && !(p.get("side").type == Value::Number && (p.get("side").number == 1 || p.get("side").number == -1)))
            return why = "\"side\" is 1 (the water falls to the right of the edge, seen from its first point) or -1", false;
        if (p.get("side").type == Value::Number) r.side = int(p.get("side").number);
    }
    if (p.get("ground").type == Value::Array)
        for (const auto& g : p.get("ground").array)
            if (g.type == Value::Array && g.array.size() == 3 && g.array[0].type == Value::Number && g.array[1].type == Value::Number && g.array[2].type == Value::Number)
                r.ground.push_back({int(g.array[0].number), int(g.array[1].number), int(g.array[2].number)});
    out = std::move(r);
    return true;
}
Value to_value(const Params& p, const XZ& origin, int origin_y) {
    Value v;
    v["kind"] = p.kind;
    v["points"].type = Value::Array;
    for (const auto& q : p.points) {
        Value pt;
        pt["x"] = double(q.x + origin.x), pt["z"] = double(q.z + origin.z);
        v["points"].array.push_back(pt);
    }
    v["floor"] = hex(p.floor);
    if (p.kind == "pond") v["level"] = double(origin_y), v["depth"] = double(p.depth), v["rim"] = double(p.rim), v["foam"] = p.foam;
    if (p.kind == "river") {
        v["width"] = double(p.width), v["depth"] = double(p.depth);
        v["shallow"].type = Value::Array;
        for (bool b : p.shallow) v["shallow"].array.push_back(Value(b));
    }
    if (p.kind == "waterfall") v["top"] = double(p.top + origin_y), v["bottom"] = double(p.bottom + origin_y), v["side"] = double(p.side);
    return v;
}
std::string params_text(const Params& p) {
    std::string s = "{\"version\":1,\"kind\":" + quoted(p.kind) + ",\"points\":[";
    for (size_t i = 0; i < p.points.size(); i++) s += (i ? ",[" : "[") + std::to_string(p.points[i].x) + "," + std::to_string(p.points[i].z) + "]";
    s += "],\"depth\":" + std::to_string(p.depth) + ",\"rim\":" + std::to_string(p.rim) + ",\"width\":" + std::to_string(p.width) + ",\"top\":" + std::to_string(p.top) +
         ",\"bottom\":" + std::to_string(p.bottom) + ",\"side\":" + std::to_string(p.side) + ",\"foam\":" + (p.foam ? "true" : "false") + ",\"floor\":" + quoted(hex(p.floor)) + ",\"shallow\":[";
    for (size_t i = 0; i < p.shallow.size(); i++) s += std::string(i ? "," : "") + (p.shallow[i] ? "true" : "false");
    s += "],\"ground\":[";
    for (size_t i = 0; i < p.ground.size(); i++) s += (i ? ",[" : "[") + std::to_string(p.ground[i].x) + "," + std::to_string(p.ground[i].z) + "," + std::to_string(p.ground[i].h) + "]";
    return s + "]}";
}
bool params_from_text(const std::string& text, Params& out, std::string& why) {
    Value v;
    try {
        v = mods::json::parse(text);
    } catch (...) {
        return why = "unreadable water parameters", false;
    }
    if (v.get("version").number != 1) return why = "water parameters of another version", false;
    Params r;
    if (!parse(v.get("kind").string(), v, {}, 0, r, why)) return false;
    // stored values are local already (and the waterfall's top / bottom relative)
    r.top = int(v.get("top").number), r.bottom = int(v.get("bottom").number);
    r.shallow.clear();
    for (const auto& b : v.get("shallow").array) r.shallow.push_back(b.boolean);
    if (r.kind == "river") r.shallow.resize(r.points.size() > 1 ? r.points.size() - 1 : 0, false);
    out = std::move(r);
    return true;
}
bool params_from_glb(const std::vector<uint8_t>& b, Params& out, std::string& why) {
    if (b.size() < 28 || std::memcmp(b.data(), "glTF", 4)) return why = "not a GLB", false;
    const uint32_t jl = uint32_t(b[12]) | uint32_t(b[13]) << 8 | uint32_t(b[14]) << 16 | uint32_t(b[15]) << 24;
    if (jl > b.size() - 20) return why = "not a GLB", false;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), jl));
    } catch (...) {
        return why = "not a GLB", false;
    }
    const Value& w = doc.get("asset").get("extras").get("wwhd_water");
    if (w.type != Value::Object) return why = "not made by the water tool", false;
    return params_from_text(mods::json::dump(w), out, why);
}

// ---------------------------------------------------------------- geometry
std::vector<XZ> ground_places(const Params& p, const XZ& origin) {
    std::vector<XZ> out;
    auto add = [&](XZ q) { out.push_back({q.x + origin.x, q.z + origin.z}); };
    if (p.kind == "pond" && p.points.size() >= 3) {
        const Ring ring = pond_ring(p);
        for (size_t i = 0; i < ring.q.size(); i++) {
            add(ring.q[i]);
            for (int k = 0; k < kToes; k++) add(toe_place(ring.q[i], ring.out[i], k));
        }
    }
    if (p.kind == "river" && p.points.size() >= 2) {
        const auto fine = river_samples(p);
        for (const auto& s : fine) add(s.at);  // first: the centre line (its first point sets the river's height)
        for (size_t k = 0; k < fine.size(); k++)
            for (int side : {-1, 1}) {
                add(offset_xz(fine[k].at, river_right(fine, k), side * p.width / 2.0));
                for (int t = 0; t < kToes; t++) add(river_toe(fine, k, p.width, side, t));
            }
    }
    if (p.kind == "waterfall" && p.points.size() == 2) {
        for (const auto& q : p.points) add(q);
        add(waterfall_foot(p));
    }
    return out;
}

std::vector<XZ> shore_places(const Params& p, const XZ& origin) {
    std::vector<XZ> out;
    if (p.kind == "pond" && p.points.size() >= 3)
        for (const XZ& q : pond_ring(p).q) out.push_back({q.x + origin.x, q.z + origin.z});
    return out;
}

std::vector<Problem> check(const Params& p) {
    std::vector<Problem> out;
    auto error = [&](const std::string& m, const char* field) { out.push_back({m, field}); };
    const size_t n = p.points.size();
    if (p.kind == "pond") {
        if (n < 3) error("a pond's outline needs at least 3 points", "points");
        else if (n > kMaxPondPoints) error("a pond's outline has at most " + std::to_string(kMaxPondPoints) + " points", "points");
        else if (!simple_polygon(p.points)) error("the outline crosses or touches itself (or has a point twice)", "points");
        else if (std::llabs(area2(p.points)) < 2 * 100 * 100) error("the pond is too small (at least about 1 m x 1 m)", "points");
    } else if (p.kind == "river") {
        if (n < 2) error("a river needs at least 2 points (its flow goes from the first to the last)", "points");
        else if (n > kMaxRiverPoints) error("a river has at most " + std::to_string(kMaxRiverPoints) + " points", "points");
        for (size_t i = 0; i + 1 < n; i++)
            if (std::hypot(double(p.points[i + 1].x - p.points[i].x), double(p.points[i + 1].z - p.points[i].z)) < 50) {
                error("points " + std::to_string(i + 1) + " and " + std::to_string(i + 2) + " are less than 50 units apart", "points");
                break;
            }
        for (size_t i = 1; i + 1 < n; i++) {  // a turn of more than 120 degrees folds the banks over each other
            const XZ a = p.points[i - 1], b = p.points[i], c = p.points[i + 1];
            const double ux = b.x - a.x, uz = b.z - a.z, vx = c.x - b.x, vz = c.z - b.z;
            if ((ux * vx + uz * vz) < -0.5 * std::hypot(ux, uz) * std::hypot(vx, vz)) {
                error("the river turns too sharply at point " + std::to_string(i + 1) + " (at most 120 degrees)", "points");
                break;
            }
        }
    } else if (p.kind == "waterfall") {
        if (n != 2) error("a waterfall's top edge is 2 points", "points");
        else {
            const double len = std::hypot(double(p.points[1].x - p.points[0].x), double(p.points[1].z - p.points[0].z));
            if (len < 100 || len > 4000) error("a waterfall is 100 to 4000 units wide", "points");
        }
        if (p.top - p.bottom < 80) error("a waterfall falls at least 80 units (its top is above the ground below)", "bottom");
        if (p.top - p.bottom > kMaxFall) error("a waterfall falls at most " + std::to_string(kMaxFall) + " units", "bottom");
    } else
        error("a body of water is a pond, a river or a waterfall", "kind");
    if (n) {
        int lo[2] = {INT_MAX, INT_MAX}, hi[2] = {INT_MIN, INT_MIN};
        for (const auto& q : p.points) lo[0] = std::min(lo[0], q.x), lo[1] = std::min(lo[1], q.z), hi[0] = std::max(hi[0], q.x), hi[1] = std::max(hi[1], q.z);
        const int margin = p.kind == "river" ? p.width : 600;
        if (int64_t(hi[0]) - lo[0] + margin > 19000 || int64_t(hi[1]) - lo[1] + margin > 19000) error("too large: a body of water spans at most about 18,000 units", "points");
    }
    return out;
}

bool build(const Params& p, const XZ& origin, int origin_y, Built& b, std::vector<Problem>& problems) {
    b = Built();
    for (auto& e : check(p)) problems.push_back(std::move(e));
    for (const auto& e : problems)
        if (e.severity == "error") return false;
    Out o;
    bool ok = p.kind == "pond" ? build_pond(p, origin, origin_y, o, problems) : p.kind == "river" ? build_river(p, origin, origin_y, o, b, problems) : build_waterfall(p, origin, o);
    if (!ok) return false;
    b.material = p.kind == "pond" ? kPondMaterial : p.kind;
    // counts and bounds
    int64_t lo[3] = {INT64_MAX, INT64_MAX, INT64_MAX}, hi[3] = {INT64_MIN, INT64_MIN, INT64_MIN};
    for (size_t m = 0; m < o.main_meshes.size(); m++)
        for (const auto& prim : o.main_meshes[m].prims) {
            if (prim.full) return problems.push_back({"too many vertices", "points"}), false;
            for (size_t i = 0; i < prim.pos.size(); i++) lo[i % 3] = std::min(lo[i % 3], prim.pos[i]), hi[i % 3] = std::max(hi[i % 3], prim.pos[i]);
            (prim.visible ? b.main_vertices : b.collision_vertices) += prim.vertices();
            (prim.visible ? b.main_triangles : b.collision_triangles) += prim.triangles();
        }
    for (const auto& prim : o.surface_meshes[0].prims) b.surface_vertices += prim.vertices(), b.surface_triangles += prim.triangles();
    for (int k = 0; k < 3; k++) b.min[k] = int(std::floor(double(lo[k]) * 100 / 1024)), b.max[k] = int(std::ceil(double(hi[k]) * 100 / 1024));
    for (const auto& img : o.main_images) b.texture_bytes += uint64_t(img[16] << 24 | img[17] << 16 | img[18] << 8 | img[19]) * (img[20] << 24 | img[21] << 16 | img[22] << 8 | img[23]) * 4;
    if (!b.surface_triangles || !b.main_triangles || !b.collision_triangles) return problems.push_back({"nothing to build: the shape has no area", "points"}), false;
    if (b.surface_triangles > kMaxSurfaceTriangles)
        problems.push_back({"the water surface has " + std::to_string(b.surface_triangles) + " triangles; at most " + std::to_string(kMaxSurfaceTriangles) + " (use fewer points)", "points"});
    if (b.collision_vertices > kMaxCollisionVertices || b.collision_triangles > kMaxCollisionTriangles)
        problems.push_back({"the collision has " + std::to_string(b.collision_vertices) + " vertices and " + std::to_string(b.collision_triangles) + " triangles; at most 4,096 each (use fewer points)", "points"});
    if (b.main_vertices > 4096 || b.main_triangles > 8192) problems.push_back({"the model has too many vertices (at most 4,096)", "points"});
    for (const auto& e : problems)
        if (e.severity == "error") return false;
    // the collision the game gets (the same welded vertices the GLB holds), for the placement check
    for (const auto& mesh : o.main_meshes)
        for (const auto& prim : mesh.prims) {
            if (prim.visible) continue;
            const auto base = b.collision_points.size();
            for (size_t i = 0; i + 2 < prim.pos.size(); i += 3)
                b.collision_points.push_back({float(double(prim.pos[i]) * 100 / 1024), float(double(prim.pos[i + 1]) * 100 / 1024), float(double(prim.pos[i + 2]) * 100 / 1024)});
            for (size_t t = 0; t + 2 < prim.idx.size(); t += 3) {
                b.collision_faces.push_back({uint16_t(base + prim.idx[t]), uint16_t(base + prim.idx[t + 1]), uint16_t(base + prim.idx[t + 2])});
                b.collision_materials.push_back(prim.material);
            }
        }
    const std::string extras = params_text(p);
    b.main = write_glb(o.main_meshes, o.main_materials, o.main_images, extras);
    // the surface's materials are its parts (surface, waves, edge, splash): a part with no triangles (no wave rings in a
    // small or narrow pond) is left out with its material, as the converter reads each material as a part it has
    std::vector<Material> used;
    for (const auto& m : o.surface_materials)
        for (const auto& mesh : o.surface_meshes)
            for (const auto& prim : mesh.prims)
                if (prim.material == m.name && !prim.idx.empty() && (used.empty() || used.back().name != m.name)) used.push_back(m);
    b.surface = write_glb(o.surface_meshes, used, {placeholder_texture()}, "");
    return true;
}
}  // namespace editor::water
