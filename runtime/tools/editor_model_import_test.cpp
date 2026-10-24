// The world editor's model import (runtime/src/editor/model_import.cpp, issue #280): small GLBs made here, each with one
// thing the game's model rules don't take as it is, come out as static_models::validate_glb takes them, with a note.
#include "editor/model_import.h"
#include "editor/water.h"
#include "mods/mod_json.h"
#include "mods/static_model_validation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using mods::json::Value;
namespace mi = editor::model_import;

namespace {
// A 16 x 8 grey JPEG (made with an image library for this test; no game data)
const std::vector<uint8_t> kJpeg = {0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0xFF,0xDB,0x00,0x43,0x00,0x10,0x0B,0x0C,0x0E,0x0C,0x0A,0x10,0x0E,0x0D,0x0E,0x12,0x11,0x10,0x13,0x18,0x28,0x1A,0x18,0x16,0x16,0x18,0x31,0x23,0x25,0x1D,0x28,0x3A,0x33,0x3D,0x3C,0x39,0x33,0x38,0x37,0x40,0x48,0x5C,0x4E,0x40,0x44,0x57,0x45,0x37,0x38,0x50,0x6D,0x51,0x57,0x5F,0x62,0x67,0x68,0x67,0x3E,0x4D,0x71,0x79,0x70,0x64,0x78,0x5C,0x65,0x67,0x63,0xFF,0xC0,0x00,0x0B,0x08,0x00,0x08,0x00,0x10,0x01,0x01,0x11,0x00,0xFF,0xC4,0x00,0x15,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xC4,0x00,0x14,0x10,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xDA,0x00,0x08,0x01,0x01,0x00,0x00,0x3F,0x00,0x98,0xFF,0xD9};

Value arr() {
    Value v;
    v.type = Value::Array;
    return v;
}
// A GLB built piece by piece: accessors, meshes, nodes, materials and embedded images
struct Builder {
    Value doc;
    std::vector<uint8_t> bin;
    Builder() {
        doc["asset"]["version"] = "2.0";
        for (const char* k : {"bufferViews", "accessors", "meshes", "nodes", "materials", "textures", "images"}) doc[k] = arr();
    }
    double view(const std::vector<uint8_t>& bytes) {
        while (bin.size() % 4) bin.push_back(0);
        Value v;
        v["buffer"] = 0.0;
        v["byteOffset"] = double(bin.size());
        v["byteLength"] = double(bytes.size());
        bin.insert(bin.end(), bytes.begin(), bytes.end());
        doc["bufferViews"].array.push_back(v);
        return double(doc["bufferViews"].array.size() - 1);
    }
    double floats(const std::vector<float>& data, const char* type, size_t components) {
        std::vector<uint8_t> b(data.size() * 4);
        std::memcpy(b.data(), data.data(), b.size());
        Value a;
        a["bufferView"] = view(b);
        a["componentType"] = 5126.0;
        a["count"] = double(data.size() / components);
        a["type"] = type;
        doc["accessors"].array.push_back(a);
        return double(doc["accessors"].array.size() - 1);
    }
    double indices(const std::vector<uint16_t>& data) {
        std::vector<uint8_t> b(data.size() * 2);
        std::memcpy(b.data(), data.data(), b.size());
        Value a;
        a["bufferView"] = view(b);
        a["componentType"] = 5123.0;
        a["count"] = double(data.size());
        a["type"] = "SCALAR";
        doc["accessors"].array.push_back(a);
        return double(doc["accessors"].array.size() - 1);
    }
    double image(const std::vector<uint8_t>& bytes, const char* mime = "image/png") {
        Value i;
        i["bufferView"] = view(bytes);
        i["mimeType"] = mime;
        doc["images"].array.push_back(i);
        Value t;
        t["source"] = double(doc["images"].array.size() - 1);
        doc["textures"].array.push_back(t);
        return double(doc["textures"].array.size() - 1);
    }
    double material(const std::string& name, double texture, const char* alpha = nullptr) {
        Value m;
        m["name"] = name;
        if (texture >= 0) m["pbrMetallicRoughness"]["baseColorTexture"]["index"] = texture;
        if (alpha) m["alphaMode"] = alpha;
        doc["materials"].array.push_back(m);
        return double(doc["materials"].array.size() - 1);
    }
    // a primitive of the points `pos`; normals (up), UVs and indices as asked
    Value primitive(const std::vector<float>& pos, double material, int mode = 4, const std::vector<uint16_t>& idx = {}, bool normals = true, bool uvs = true) {
        Value p;
        const size_t n = pos.size() / 3;
        p["attributes"]["POSITION"] = floats(pos, "VEC3", 3);
        if (normals) {
            std::vector<float> up;
            for (size_t i = 0; i < n; ++i) up.insert(up.end(), {0.f, 1.f, 0.f});
            p["attributes"]["NORMAL"] = floats(up, "VEC3", 3);
        }
        if (uvs) {
            std::vector<float> uv;
            for (size_t i = 0; i < n; ++i) uv.insert(uv.end(), {float(i % 2), float(i / 2 % 2)});
            p["attributes"]["TEXCOORD_0"] = floats(uv, "VEC2", 2);
        }
        if (!idx.empty()) p["indices"] = indices(idx);
        if (mode != 4) p["mode"] = double(mode);
        p["material"] = material;
        return p;
    }
    double mesh(const std::string& name, const std::vector<Value>& primitives) {
        Value m;
        m["name"] = name;
        m["primitives"] = arr();
        for (const auto& p : primitives) m["primitives"].array.push_back(p);
        doc["meshes"].array.push_back(m);
        return double(doc["meshes"].array.size() - 1);
    }
    double node(const std::string& name, double mesh, const std::vector<double>& translation = {}, const std::vector<double>& scale = {}) {
        Value n;
        n["name"] = name;
        if (mesh >= 0) n["mesh"] = mesh;
        if (!translation.empty()) {
            n["translation"] = arr();
            for (double t : translation) n["translation"].array.push_back(t);
        }
        if (!scale.empty()) {
            n["scale"] = arr();
            for (double t : scale) n["scale"].array.push_back(t);
        }
        doc["nodes"].array.push_back(n);
        return double(doc["nodes"].array.size() - 1);
    }
    std::vector<uint8_t> glb(std::vector<double> roots = {}) {
        if (roots.empty())
            for (size_t i = 0; i < doc["nodes"].array.size(); ++i) roots.push_back(double(i));
        Value s;
        s["nodes"] = arr();
        for (double r : roots) s["nodes"].array.push_back(r);
        doc["scenes"] = arr();
        doc["scenes"].array.push_back(s);
        doc["scene"] = 0.0;
        while (bin.size() % 4) bin.push_back(0);
        doc["buffers"] = arr();
        Value b;
        b["byteLength"] = double(bin.size());
        doc["buffers"].array.push_back(b);
        for (const char* k : {"textures", "images"})
            if (doc[k].array.empty()) doc.object.erase(k);
        std::string json = mods::json::dump(doc);
        while (json.size() % 4) json += ' ';
        std::vector<uint8_t> out;
        auto w = [&](uint32_t v) {
            for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
        };
        w(0x46546C67), w(2), w(uint32_t(28 + json.size() + bin.size())), w(uint32_t(json.size())), w(0x4E4F534A);
        out.insert(out.end(), json.begin(), json.end());
        w(uint32_t(bin.size())), w(0x004E4942);
        out.insert(out.end(), bin.begin(), bin.end());
        return out;
    }
};
std::vector<uint8_t> png(unsigned w, unsigned h, bool alpha, uint8_t transparent = 255) {
    std::vector<uint8_t> px;
    for (unsigned i = 0; i < w * h; ++i) {
        px.insert(px.end(), {uint8_t(i * 7), uint8_t(i * 3), 90});
        if (alpha) px.push_back(i == 0 ? transparent : 255);
    }
    return editor::water::png(w, h, px, alpha);
}
// a quad as two triangles, in the y = 0 plane at x0..x0+1, z 0..1
std::vector<float> quad(float x0 = 0) { return {x0, 0, 0, x0 + 1, 0, 0, x0, 0, 1, x0 + 1, 0, 1}; }
const std::vector<uint16_t> kQuad = {0, 2, 1, 1, 2, 3};
bool has(const std::vector<std::string>& notes, const char* words) {
    for (const auto& n : notes)
        if (n.find(words) != std::string::npos) return true;
    return false;
}
Value json_of(const std::vector<uint8_t>& glb) {
    uint32_t length;
    std::memcpy(&length, glb.data() + 12, 4);
    return mods::json::parse(std::string(reinterpret_cast<const char*>(glb.data() + 20), length));
}
// fixed, then taken by the game's validator as it is
mi::Result fixed(const std::vector<uint8_t>& in, static_models::ModelInfo& info, bool collision = false) {
    mi::Result r;
    std::string why;
    std::string before;
    assert(!static_models::validate_glb(in, info, before, collision));  // each case needs fixing
    if (!mi::fix(in, r, why)) std::fprintf(stderr, "fix: %s\n", why.c_str());
    assert(r.changed);
    std::string after;
    if (!static_models::validate_glb(r.glb, info, after, collision)) std::fprintf(stderr, "still refused: %s\n", after.c_str());
    assert(static_models::validate_glb(r.glb, info, after, collision));
    return r;
}
}  // namespace

int main() {
    // a file that fits is kept byte for byte
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        b.node("box", b.mesh("box", {b.primitive(quad(), m, 4, kQuad)}));
        const auto in = b.glb();
        static_models::ModelInfo info;
        std::string why;
        assert(static_models::validate_glb(in, info, why));
        mi::Result r;
        assert(mi::fix(in, r, why) && !r.changed && r.glb == in && r.notes.empty());
    }
    // several mesh objects (Blender's ".001" copies), one moved: one model, the move applied, a note naming them
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        b.node("polySurface4323", b.mesh("polySurface4323", {b.primitive(quad(), m, 4, kQuad)}));
        b.node("polySurface4323.001", b.mesh("polySurface4323.001", {b.primitive(quad(), m, 4, kQuad)}), {10, 0, 0});
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.triangle_count == 4 && info.vertex_count == 8);
        assert(std::fabs(info.minimum[0] - 0) < 1e-3 && std::fabs(info.maximum[0] - 1100) < 1e-3);  // metres x 100
        assert(has(r.notes, "polySurface4323, polySurface4323.001") && has(r.notes, "_col") && has(r.notes, "placement, rotation and scale"));
        assert(json_of(r.glb).get("meshes").array.size() == 1);
    }
    // a collision mesh: "*_col" by name, or col_* materials only; it stays collision
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        const double grass = b.material("col_grass", -1), stone = b.material("col_stone", -1);
        b.node("model", b.mesh("model", {b.primitive(quad(), m, 4, kQuad)}));
        b.node("ground", b.mesh("ground", {b.primitive(quad(2), grass, 4, kQuad, false, false)}));
        b.node("rocks", b.mesh("rocks", {b.primitive(quad(4), stone, 4, kQuad, false, false)}));
        b.node("wall_col", b.mesh("wall", {b.primitive(quad(6), stone, 4, kQuad, false, false)}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info, true);
        assert(info.triangle_count == 2);
        const Value d = json_of(r.glb);
        assert(d.get("meshes").array.size() == 2 && d.get("nodes").array[1].get("name").string() == "collision_col");
        assert(d.get("meshes").array[1].get("primitives").array.size() == 2);  // grass, stone
        assert(has(r.notes, "collision meshes ground, rocks, wall_col are merged"));
    }
    // triangle strips and fans become triangles; loose edges and points are left out
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        b.node("strip", b.mesh("strip", {b.primitive(quad(), m, 5, {0, 2, 1, 3}), b.primitive(quad(2), m, 6, {0, 2, 3, 1}),
                                          b.primitive(quad(4), m, 1, {0, 1, 1, 3}), b.primitive(quad(6), m, 0)}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.triangle_count == 4);
        assert(has(r.notes, "strips and fans") && has(r.notes, "Loose edges and points (2 parts"));
        assert(json_of(r.glb).get("meshes").array[0].get("primitives").array.size() == 1);  // one material: one primitive
    }
    // a strip keeps one winding: every triangle faces up like its normals
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        b.node("strip", b.mesh("strip", {b.primitive({0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 2, 0, 0, 2, 0, 1}, m, 5, {0, 1, 2, 3, 4, 5})}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.triangle_count == 4);
        const Value d = json_of(r.glb);
        const auto& acc = d.get("accessors").array;
        const auto& views = d.get("bufferViews").array;
        uint32_t jl;
        std::memcpy(&jl, r.glb.data() + 12, 4);
        const uint8_t* bin = r.glb.data() + 28 + jl;
        const auto& prim = d.get("meshes").array[0].get("primitives").array[0];
        auto at = [&](double accessor) { return bin + size_t(views[size_t(acc[size_t(accessor)].get("bufferView").number)].get("byteOffset").number); };
        const float* pos = reinterpret_cast<const float*>(at(prim.get("attributes").get("POSITION").number));
        const uint16_t* idx = reinterpret_cast<const uint16_t*>(at(prim.get("indices").number));
        for (int t = 0; t < 4; ++t) {
            const float *a = pos + idx[t * 3] * 3, *c1 = pos + idx[t * 3 + 1] * 3, *c2 = pos + idx[t * 3 + 2] * 3;
            const float ux = c1[0] - a[0], uz = c1[2] - a[2], vx = c2[0] - a[0], vz = c2[2] - a[2];
            assert(uz * vx - ux * vz > 0);  // the face's y normal is up
        }
    }
    // vertex colours, a second UV map and tangents are left out, never refused
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        Value p = b.primitive(quad(), m, 4, kQuad);
        p["attributes"]["COLOR_0"] = b.floats(std::vector<float>(16, 1.f), "VEC4", 4);
        p["attributes"]["TEXCOORD_1"] = b.floats(std::vector<float>(8, 0.f), "VEC2", 2);
        b.node("painted", b.mesh("painted", {p}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(has(r.notes, "COLOR_0, TEXCOORD_1"));
        assert(json_of(r.glb).get("meshes").array[0].get("primitives").array[0].get("attributes").object.size() == 3);
    }
    // blended materials: a cut-out when the texture has transparent parts, else opaque; never refused
    {
        Builder b;
        const double leaves = b.material("leaves", b.image(png(64, 8, true, 0)), "BLEND");
        const double glass = b.material("glass", b.image(png(64, 8, false)), "BLEND");
        b.node("tree", b.mesh("tree", {b.primitive(quad(), leaves, 4, kQuad), b.primitive(quad(2), glass, 4, kQuad)}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.cutout_count == 1);
        const Value d = json_of(r.glb);
        assert(d.get("materials").array[0].get("alphaMode").string() == "MASK" && d.get("materials").array[0].get("alphaCutoff").number == 0.5);
        assert(d.get("materials").array[1].get("alphaMode").type == Value::Null);
        assert(has(r.notes, "leaves is drawn as a cut-out") && has(r.notes, "glass has no transparent parts"));
    }
    // over the texture budget: scaled down together until they fit 1 MiB; odd sizes rounded to sizes the game takes
    {
        Builder b;
        std::vector<Value> parts;
        for (int i = 0; i < 4; ++i) parts.push_back(b.primitive(quad(float(i * 2)), b.material("m" + std::to_string(i), b.image(png(512, 512, false))), 4, kQuad));
        parts.push_back(b.primitive(quad(10), b.material("odd", b.image(png(100, 30, false))), 4, kQuad));
        b.node("big", b.mesh("big", parts));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.texture_bytes <= static_models::kMaxTextureBytes && info.images.size() == 5);
        unsigned at_256 = 0;  // the largest are halved first: three stay at 256, one goes to 128 to make room for the fifth
        for (int i = 0; i < 4; ++i) at_256 += info.images[size_t(i)].width == 256 && info.images[size_t(i)].height == 256;
        assert(at_256 == 3);
        assert(info.images[4].width == 128 && info.images[4].height == 32);
        assert(has(r.notes, "512 x 512 to 256 x 256") && has(r.notes, "100 x 30 to 128 x 32"));
    }
    // the sizes alone
    {
        std::vector<unsigned> w, h;
        mi::texture_sizes({2048, 64}, {1024, 64}, 1024 * 1024, w, h);
        assert(w[0] == 512 && h[0] == 256 && w[1] == 64 && h[1] == 64);  // 2048 x 1024 is first made 512 a side
        mi::texture_sizes({512, 512, 512, 512, 512}, {512, 512, 512, 512, 512}, 1024 * 1024, w, h);
        size_t total = 0;
        for (size_t i = 0; i < w.size(); ++i) total += size_t(w[i]) * h[i] * 4, assert(w[i] % 64 == 0 && h[i] % 8 == 0);
        assert(total <= 1024 * 1024);
        mi::texture_sizes({64}, {8}, 1024 * 1024, w, h);
        assert(w[0] == 64 && h[0] == 8);
    }
    // a JPEG texture is converted to a PNG of a size the game takes
    {
        Builder b;
        b.node("rock", b.mesh("rock", {b.primitive(quad(), b.material("rock", b.image(kJpeg, "image/jpeg")), 4, kQuad)}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.images[0].width == 64 && info.images[0].height == 8);
        assert(has(r.notes, "16 x 8 to 64 x 8"));
    }
    // no index list, no normals: given an index list and flat normals
    {
        Builder b;
        b.node("bare", b.mesh("bare", {b.primitive({0, 0, 0, 0, 0, 1, 1, 0, 0}, b.material("m", b.image(png(64, 8, false))), 4, {}, false)}));
        static_models::ModelInfo info;
        const auto r = fixed(b.glb(), info);
        assert(info.triangle_count == 1 && has(r.notes, "no normals") && has(r.notes, "index list"));
    }
    // a mirrored object (negative scale): its faces turned around, so they still face out
    {
        Builder b;
        const double m = b.material("wood", b.image(png(64, 8, false)));
        b.node("a", b.mesh("a", {b.primitive(quad(), m, 4, kQuad)}));
        b.node("a.001", b.mesh("a.001", {b.primitive(quad(), m, 4, kQuad)}), {}, {-1, 1, 1});
        static_models::ModelInfo info;
        fixed(b.glb(), info);
        assert(std::fabs(info.minimum[0] + 100) < 1e-3);
    }
    // what it leaves to the validator: skinned models as they are, required extensions with a reason
    {
        Builder b;
        b.node("a", b.mesh("a", {b.primitive(quad(), b.material("m", b.image(png(64, 8, false))), 4, kQuad)}));
        b.doc["skins"] = arr();
        Value skin;
        skin["joints"] = arr();
        skin["joints"].array.push_back(0.0);
        b.doc["skins"].array.push_back(skin);
        const auto in = b.glb();
        mi::Result r;
        std::string why;
        assert(mi::fix(in, r, why) && !r.changed && r.glb == in);
        Builder c;
        c.node("a", c.mesh("a", {c.primitive(quad(), c.material("m", c.image(png(64, 8, false))), 4, kQuad)}));
        c.doc["extensionsRequired"] = arr();
        c.doc["extensionsRequired"].array.push_back(Value("KHR_draco_mesh_compression"));
        c.doc["extensionsUsed"] = c.doc["extensionsRequired"];
        assert(!mi::fix(c.glb(), r, why) && why.find("KHR_draco_mesh_compression") != std::string::npos && why.find("Draco") != std::string::npos);
        // a visible face without a material can't be drawn: said, not guessed
        Builder d;
        Value p = d.primitive(quad(), 0, 4, kQuad);
        p.object.erase("material");
        d.node("a", d.mesh("a", {p}));
        d.doc["materials"].array.clear();
        assert(!mi::fix(d.glb(), r, why) && why.find("no material") != std::string::npos);
        assert(!mi::fix(std::vector<uint8_t>{1, 2, 3}, r, why) && why.find("not a GLB") != std::string::npos);
    }
    std::puts("editor_model_import: ok");
    return 0;
}
