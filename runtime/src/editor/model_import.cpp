// The world editor's model import: a static .glb made to fit the game's model rules (model_import.h, issue #280).
#include "model_import.h"
#include "water.h"  // png(): the stored-DEFLATE PNG writer
#include "../mods/mod_json.h"
#include "../mods/static_model_validation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

// stb_image for reading textures (PNG of any kind, JPEG): its functions static to this file
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../../third_party/stb/stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace editor::model_import {
namespace {
using mods::json::Value;
using Matrix = std::array<double, 16>;  // row-major, affine

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] void fail(const std::string& why) { throw Fail(why); }

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
}

Matrix identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix m{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) m[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    return m;
}
bool numbers(const Value& v, size_t n) {
    if (v.type != Value::Array || v.array.size() != n) return false;
    for (const auto& x : v.array)
        if (x.type != Value::Number || !std::isfinite(x.number)) return false;
    return true;
}
Matrix local_matrix(const Value& node) {
    Matrix m = identity();
    if (node.get("matrix").type != Value::Null) {
        if (!numbers(node.get("matrix"), 16)) fail("a node has an invalid matrix");
        const auto& a = node.get("matrix").array;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) m[r * 4 + c] = a[c * 4 + r].number;  // glTF is column-major
        return m;
    }
    double q[4] = {0, 0, 0, 1}, t[3] = {0, 0, 0}, s[3] = {1, 1, 1};
    if (node.get("rotation").type != Value::Null) {
        if (!numbers(node.get("rotation"), 4)) fail("a node has an invalid rotation");
        double n = 0;
        for (int k = 0; k < 4; ++k) n += node.get("rotation").array[k].number * node.get("rotation").array[k].number;
        n = std::sqrt(n);
        if (!(n > 0)) fail("a node has an invalid rotation");
        for (int k = 0; k < 4; ++k) q[k] = node.get("rotation").array[k].number / n;
    }
    if (node.get("translation").type != Value::Null) {
        if (!numbers(node.get("translation"), 3)) fail("a node has an invalid translation");
        for (int k = 0; k < 3; ++k) t[k] = node.get("translation").array[k].number;
    }
    if (node.get("scale").type != Value::Null) {
        if (!numbers(node.get("scale"), 3)) fail("a node has an invalid scale");
        for (int k = 0; k < 3; ++k) s[k] = node.get("scale").array[k].number;
    }
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),     2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
                         2 * (y * z - x * w),     2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int a = 0; a < 3; ++a) {
        for (int c = 0; c < 3; ++c) m[a * 4 + c] = r[a * 3 + c] * s[c];
        m[a * 4 + 3] = t[a];
    }
    return m;
}

// ---------------------------------------------------------------- reading the file
struct Glb {
    Value doc;
    std::span<const uint8_t> bin;
};
Glb read_glb(std::span<const uint8_t> b) {
    if (b.size() < 20 || std::memcmp(b.data(), "glTF", 4) || le32(b.data() + 4) != 2) fail("not a GLB file (glTF 2.0, binary)");
    const size_t total = std::min<size_t>(le32(b.data() + 8), b.size());
    const size_t json_length = le32(b.data() + 12);
    if (le32(b.data() + 16) != 0x4E4F534A || json_length > total - 20) fail("the GLB has no readable JSON chunk");
    Glb g;
    try {
        g.doc = mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), json_length));
    } catch (...) {
        fail("the GLB's JSON is invalid");
    }
    const size_t at = 20 + json_length;
    if (at + 8 <= total && le32(b.data() + at + 4) == 0x004E4942) {
        const size_t length = std::min<size_t>(le32(b.data() + at), total - at - 8);
        g.bin = b.subspan(at + 8, length);
    }
    if (g.doc.type != Value::Object) fail("the GLB's JSON is invalid");
    return g;
}

size_t component_size(unsigned type) { return type == 5120 || type == 5121 ? 1 : type == 5122 || type == 5123 ? 2 : 4; }
size_t component_count(const std::string& type) {
    return type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
}
// An accessor as numbers (normalized integers as 0..1 or -1..1)
std::vector<double> read_accessor(const Glb& g, const Value& index, size_t components, const char* what) {
    const auto& accessors = g.doc.get("accessors").array;
    if (index.type != Value::Number || index.number < 0 || size_t(index.number) >= accessors.size()) fail(std::string("a primitive's ") + what + " is missing");
    const Value& a = accessors[size_t(index.number)];
    if (a.get("sparse").type != Value::Null) fail(std::string("sparse accessors (") + what + ") are not supported");
    if (component_count(a.get("type").string()) != components) fail(std::string("a primitive's ") + what + " has the wrong type");
    const unsigned type = unsigned(a.get("componentType").number);
    if (type != 5120 && type != 5121 && type != 5122 && type != 5123 && type != 5125 && type != 5126) fail(std::string("a primitive's ") + what + " has an unknown component type");
    const size_t count = a.get("count").type == Value::Number ? size_t(a.get("count").number) : 0;
    if (!count || count > 1000000) fail(std::string("a primitive's ") + what + " is empty or too large");
    std::vector<double> out(count * components, 0.0);
    if (a.get("bufferView").type == Value::Null) return out;  // all zeros (glTF)
    const auto& views = g.doc.get("bufferViews").array;
    const size_t vi = size_t(a.get("bufferView").number);
    if (vi >= views.size()) fail(std::string("a primitive's ") + what + " is outside the file");
    const Value& v = views[vi];
    if (v.get("buffer").number != 0) fail("the GLB uses an external buffer");
    const size_t view_offset = size_t(v.get("byteOffset").number), view_length = size_t(v.get("byteLength").number);
    const size_t width = component_size(type) * components;
    const size_t stride = v.get("byteStride").type == Value::Number ? size_t(v.get("byteStride").number) : width;
    const size_t offset = view_offset + size_t(a.get("byteOffset").number);
    if (view_offset > g.bin.size() || view_length > g.bin.size() - view_offset || stride < width || offset > view_offset + view_length ||
        (count - 1) * stride + width > view_offset + view_length - offset)
        fail(std::string("a primitive's ") + what + " is outside the file");
    const bool normalized = a.get("normalized").boolean;
    for (size_t i = 0; i < count; ++i)
        for (size_t k = 0; k < components; ++k) {
            const uint8_t* p = g.bin.data() + offset + i * stride + k * component_size(type);
            double x = 0;
            switch (type) {
            case 5120: x = normalized ? std::max(int8_t(p[0]) / 127.0, -1.0) : int8_t(p[0]); break;
            case 5121: x = normalized ? p[0] / 255.0 : p[0]; break;
            case 5122: x = normalized ? std::max(int16_t(p[0] | p[1] << 8) / 32767.0, -1.0) : int16_t(p[0] | p[1] << 8); break;
            case 5123: x = normalized ? uint16_t(p[0] | p[1] << 8) / 65535.0 : uint16_t(p[0] | p[1] << 8); break;
            case 5125: x = le32(p); break;
            default: {
                const uint32_t bits = le32(p);
                float f;
                std::memcpy(&f, &bits, 4);
                x = f;
            }
            }
            if (!std::isfinite(x)) fail(std::string("a primitive's ") + what + " has a coordinate that isn't a number");
            out[i * components + k] = x;
        }
    return out;
}
std::span<const uint8_t> view_bytes(const Glb& g, const Value& index) {
    const auto& views = g.doc.get("bufferViews").array;
    if (index.type != Value::Number || size_t(index.number) >= views.size()) fail("an image is outside the file");
    const Value& v = views[size_t(index.number)];
    const size_t offset = size_t(v.get("byteOffset").number), length = size_t(v.get("byteLength").number);
    if (offset > g.bin.size() || length > g.bin.size() - offset) fail("an image is outside the file");
    return g.bin.subspan(offset, length);
}

// ---------------------------------------------------------------- textures
struct Pixels {
    unsigned width = 0, height = 0;
    bool alpha = false;
    std::vector<uint8_t> rgba;
};
// the PNG as the game takes it as it is: 8-bit RGB or RGBA, not interlaced (static_models::validate_glb_strict)
bool plain_png(std::span<const uint8_t> b) {
    return b.size() >= 33 && !std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) && !std::memcmp(b.data() + 12, "IHDR", 4) && b[24] == 8 && (b[25] == 2 || b[25] == 6) &&
           !b[26] && !b[27] && !b[28];
}
Pixels decode(std::span<const uint8_t> b, const std::string& name) {
    int w = 0, h = 0, channels = 0;
    if (b.size() > size_t(INT32_MAX)) fail("texture " + name + " is too large");
    if (!stbi_info_from_memory(b.data(), int(b.size()), &w, &h, &channels) || w <= 0 || h <= 0 || w > 8192 || h > 8192)
        fail("texture " + name + " is not a PNG or JPEG image the importer can read");
    stbi_uc* data = stbi_load_from_memory(b.data(), int(b.size()), &w, &h, &channels, 4);
    if (!data) fail("texture " + name + " is not a PNG or JPEG image the importer can read");
    Pixels p;
    p.width = unsigned(w), p.height = unsigned(h);
    p.rgba.assign(data, data + size_t(w) * size_t(h) * 4);
    stbi_image_free(data);
    p.alpha = false;
    if (channels == 2 || channels == 4)
        for (size_t i = 3; i < p.rgba.size(); i += 4)
            if (p.rgba[i] != 255) {
                p.alpha = true;
                break;
            }
    return p;
}
// Area-weighted resampling (each target pixel the average of the source pixels it covers)
Pixels resample(const Pixels& in, unsigned width, unsigned height) {
    auto pass = [](const std::vector<double>& src, unsigned sw, unsigned sh, unsigned tw, bool horizontal) {
        // resamples along x (horizontal) or y; returns the new image
        const unsigned n_src = horizontal ? sw : sh, n_dst = tw;
        const unsigned other = horizontal ? sh : sw;
        std::vector<double> out(size_t(horizontal ? tw * sh : sw * tw) * 4, 0.0);
        const double scale = double(n_src) / n_dst;
        for (unsigned d = 0; d < n_dst; ++d) {
            const double from = d * scale, to = (d + 1) * scale;
            for (unsigned o = 0; o < other; ++o) {
                double sum[4] = {0, 0, 0, 0}, weight = 0;
                for (unsigned s = unsigned(std::floor(from)); s < n_src && s < to; ++s) {
                    const double wgt = std::min<double>(s + 1, to) - std::max<double>(s, from);
                    if (wgt <= 0) continue;
                    const size_t at = horizontal ? (size_t(o) * sw + s) * 4 : (size_t(s) * sw + o) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += src[at + c] * wgt;
                    weight += wgt;
                }
                const size_t to_at = horizontal ? (size_t(o) * tw + d) * 4 : (size_t(d) * sw + o) * 4;
                for (int c = 0; c < 4; ++c) out[to_at + c] = weight > 0 ? sum[c] / weight : 0;
            }
        }
        return out;
    };
    std::vector<double> img(in.rgba.begin(), in.rgba.end());
    // premultiplied alpha, so transparent pixels don't bleed their colour into the edges of a cut-out
    for (size_t i = 0; i < img.size(); i += 4)
        for (int c = 0; c < 3; ++c) img[i + c] *= img[i + 3] / 255.0;
    img = pass(img, in.width, in.height, width, true);
    img = pass(img, width, in.height, height, false);
    Pixels out;
    out.width = width, out.height = height, out.alpha = in.alpha;
    out.rgba.resize(size_t(width) * height * 4);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
        const double a = img[i + 3];
        for (int c = 0; c < 3; ++c) out.rgba[i + c] = uint8_t(std::clamp(std::lround(a > 0 ? img[i + c] * 255.0 / a : 0.0), 0L, 255L));
        out.rgba[i + 3] = uint8_t(std::clamp(std::lround(a), 0L, 255L));
    }
    return out;
}
std::vector<uint8_t> encode(const Pixels& p) {
    if (p.alpha) return editor::water::png(p.width, p.height, p.rgba, true);
    std::vector<uint8_t> rgb;
    rgb.reserve(size_t(p.width) * p.height * 3);
    for (size_t i = 0; i < p.rgba.size(); i += 4) rgb.insert(rgb.end(), {p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]});
    return editor::water::png(p.width, p.height, rgb, false);
}

// ---------------------------------------------------------------- geometry
struct Part {           // one primitive's triangles, in the model's own space
    size_t material;    // index into the file's materials
    std::vector<std::array<double, 3>> position, normal;
    std::vector<std::array<double, 2>> uv;
    std::vector<uint32_t> index;
};
std::string join(const std::vector<std::string>& list, size_t most = 6) {
    std::string out;
    for (size_t i = 0; i < list.size() && i < most; ++i) out += (i ? ", " : "") + list[i];
    if (list.size() > most) out += " and " + std::to_string(list.size() - most) + " more";
    return out;
}
std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

bool fix_inner(std::span<const uint8_t> input, Result& out) {
    const Glb g = read_glb(input);
    const Value& doc = g.doc;
    // skinned and animated models have their own rules (skinned_models::validate_glb): not changed here
    for (const char* key : {"skins", "animations"})
        if (doc.get(key).type == Value::Array && !doc.get(key).array.empty()) return false;
    if (doc.get("extensionsRequired").type == Value::Array && !doc.get("extensionsRequired").array.empty()) {
        std::vector<std::string> names;
        for (const auto& e : doc.get("extensionsRequired").array) names.push_back(e.string());
        fail("the file needs glTF extensions the game can't draw (" + join(names) +
             "); export without them (in Blender: no Draco mesh compression, no WebP or KTX2 textures)");
    }
    std::vector<std::string>& notes = out.notes;
    const auto& nodes = doc.get("nodes").array;
    const auto& meshes = doc.get("meshes").array;
    const auto& materials = doc.get("materials").array;
    if (nodes.empty() || meshes.empty()) fail("the file has no mesh");
    if (nodes.size() > 4096) fail("the file has too many objects");
    // the default scene's nodes, with their world transforms
    const Value& scenes = doc.get("scenes");
    size_t scene = doc.get("scene").type == Value::Number ? size_t(doc.get("scene").number) : 0;
    std::vector<size_t> roots;
    if (scenes.type == Value::Array && scene < scenes.array.size())
        for (const auto& r : scenes.array[scene].get("nodes").array) roots.push_back(size_t(r.number));
    else {  // no scene: every node no other node names as its child
        std::vector<bool> child(nodes.size());
        for (const auto& n : nodes)
            for (const auto& c : n.get("children").array)
                if (size_t(c.number) < nodes.size()) child[size_t(c.number)] = true;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (!child[i]) roots.push_back(i);
    }
    struct Instance {
        size_t node, mesh;
        Matrix world;
        std::string name;
    };
    std::vector<Instance> instances;
    std::vector<int> state(nodes.size());
    std::function<void(size_t, const Matrix&, int)> walk = [&](size_t n, const Matrix& parent, int depth) {
        if (n >= nodes.size() || depth > 64) fail("the file's object hierarchy is invalid");
        if (state[n]) fail("the file's object hierarchy is invalid (an object is used twice)");
        state[n] = 1;
        const Value& node = nodes[n];
        const Matrix world = multiply(parent, local_matrix(node));
        if (node.get("mesh").type == Value::Number) {
            const size_t m = size_t(node.get("mesh").number);
            if (m >= meshes.size()) fail("an object names a mesh the file doesn't have");
            std::string name = node.get("name").string();
            const std::string mesh_name = meshes[m].get("name").string();
            if (name.empty()) name = mesh_name.empty() ? "mesh " + std::to_string(m) : mesh_name;
            instances.push_back({n, m, world, name});
        }
        for (const auto& c : node.get("children").array) walk(size_t(c.number), world, depth + 1);
    };
    for (size_t r : roots) walk(r, identity(), 0);
    if (instances.empty()) fail("the file's scene has no mesh");

    // which instances are collision: "*_col" by object or mesh name, or every material a col_* surface
    auto collision_material = [&](const Value& p) {
        const Value& m = p.get("material");
        return m.type == Value::Number && size_t(m.number) < materials.size() && static_models::collision_material(materials[size_t(m.number)].get("name").string());
    };
    std::vector<Part> render, collision;
    std::vector<std::string> render_names, collision_names, dropped_attributes;
    size_t strips = 0, loose = 0, unindexed = 0;
    bool transformed = false;
    for (const auto& inst : instances) {
        const Value& mesh = meshes[inst.mesh];
        const auto& primitives = mesh.get("primitives").array;
        if (primitives.empty()) continue;
        if (mesh.get("weights").type != Value::Null) fail("mesh " + inst.name + " has shape keys (morph targets): apply or remove them before exporting");
        const auto ends_col = [](const std::string& s) { return s.size() > 4 && s.ends_with("_col"); };
        bool is_collision = ends_col(nodes[inst.node].get("name").string()) || ends_col(mesh.get("name").string());
        if (!is_collision) is_collision = std::all_of(primitives.begin(), primitives.end(), collision_material);
        (is_collision ? collision_names : render_names).push_back(inst.name);
        for (int k = 0; k < 16; ++k)
            if (std::fabs(inst.world[k] - identity()[k]) > 1e-9) transformed = true;
        // normals: the inverse transpose of the upper 3x3; a mirrored object turns its faces around
        const Matrix& w = inst.world;
        const double cof[9] = {w[5] * w[10] - w[6] * w[9], w[6] * w[8] - w[4] * w[10], w[4] * w[9] - w[5] * w[8],
                               w[9] * w[2] - w[10] * w[1], w[10] * w[0] - w[8] * w[2], w[8] * w[1] - w[9] * w[0],
                               w[1] * w[6] - w[2] * w[5], w[2] * w[4] - w[0] * w[6], w[0] * w[5] - w[1] * w[4]};
        const double det = w[0] * cof[0] + w[1] * cof[1] + w[2] * cof[2];
        if (!std::isfinite(det) || std::fabs(det) < 1e-12) fail("object " + inst.name + " is scaled to nothing (a scale of 0)");
        for (const auto& p : primitives) {
            if (p.get("targets").type != Value::Null) fail("mesh " + inst.name + " has shape keys (morph targets): apply or remove them before exporting");
            const int mode = p.get("mode").type == Value::Number ? int(p.get("mode").number) : 4;
            if (mode < 4) {  // points, lines, line loops and strips: loose vertices and edges
                ++loose;
                continue;
            }
            if (mode > 6) fail("mesh " + inst.name + " has a primitive of an unknown kind");
            const Value& at = p.get("attributes");
            for (const auto& [key, value] : at.object)
                if (key != "POSITION" && key != "NORMAL" && key != "TEXCOORD_0" && std::find(dropped_attributes.begin(), dropped_attributes.end(), key) == dropped_attributes.end())
                    dropped_attributes.push_back(key);
            const auto pos = read_accessor(g, at.get("POSITION"), 3, "positions");
            const size_t count = pos.size() / 3;
            std::vector<double> nrm, uv;
            if (at.get("NORMAL").type != Value::Null) nrm = read_accessor(g, at.get("NORMAL"), 3, "normals");
            if (at.get("TEXCOORD_0").type != Value::Null) uv = read_accessor(g, at.get("TEXCOORD_0"), 2, "UVs");
            if ((!nrm.empty() && nrm.size() != pos.size()) || (!uv.empty() && uv.size() / 2 != count)) fail("mesh " + inst.name + " has attributes of different lengths");
            std::vector<uint32_t> order;
            if (p.get("indices").type != Value::Null) {
                const auto idx = read_accessor(g, p.get("indices"), 1, "indices");
                order.reserve(idx.size());
                for (double i : idx) {
                    if (i < 0 || i >= double(count)) fail("mesh " + inst.name + " has an index outside its vertices");
                    order.push_back(uint32_t(i));
                }
            } else {
                ++unindexed;
                for (size_t i = 0; i < count; ++i) order.push_back(uint32_t(i));
            }
            std::vector<uint32_t> triangles;
            if (mode == 4) {
                triangles.assign(order.begin(), order.end() - long(order.size() % 3));
            } else {
                ++strips;
                for (size_t i = 2; i < order.size(); ++i) {
                    uint32_t a, b, c;
                    if (mode == 5) {  // strip: every other triangle turned back to the strip's winding
                        a = order[i - 2], b = order[i - 1], c = order[i];
                        if (i % 2) std::swap(a, b);
                    } else
                        a = order[0], b = order[i - 1], c = order[i];  // fan
                    if (a == b || b == c || a == c) continue;
                    triangles.insert(triangles.end(), {a, b, c});
                }
            }
            if (triangles.empty()) continue;
            if (det < 0)
                for (size_t i = 0; i < triangles.size(); i += 3) std::swap(triangles[i + 1], triangles[i + 2]);
            Part part;
            part.material = p.get("material").type == Value::Number ? size_t(p.get("material").number) : SIZE_MAX;
            // only the vertices the triangles use
            std::map<uint32_t, uint32_t> used;
            for (uint32_t& t : triangles) {
                auto [it, fresh] = used.emplace(t, uint32_t(used.size()));
                if (fresh) {
                    const double* v = &pos[size_t(t) * 3];
                    part.position.push_back({w[0] * v[0] + w[1] * v[1] + w[2] * v[2] + w[3], w[4] * v[0] + w[5] * v[1] + w[6] * v[2] + w[7],
                                             w[8] * v[0] + w[9] * v[1] + w[10] * v[2] + w[11]});
                    if (!nrm.empty()) {
                        const double* n = &nrm[size_t(t) * 3];
                        std::array<double, 3> m = {(cof[0] * n[0] + cof[1] * n[1] + cof[2] * n[2]) / det, (cof[3] * n[0] + cof[4] * n[1] + cof[5] * n[2]) / det,
                                                   (cof[6] * n[0] + cof[7] * n[1] + cof[8] * n[2]) / det};
                        const double l = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                        if (l > 1e-12)
                            for (double& x : m) x /= l;
                        part.normal.push_back(m);
                    }
                    if (!uv.empty()) part.uv.push_back({uv[size_t(t) * 2], uv[size_t(t) * 2 + 1]});
                }
                t = it->second;
            }
            part.index = std::move(triangles);
            // a missing normal: each face's own (flat), so the model still lights
            if (part.normal.empty() && !is_collision) {
                part.normal.assign(part.position.size(), {0, 1, 0});
                for (size_t i = 0; i < part.index.size(); i += 3) {
                    const auto &a = part.position[part.index[i]], &b = part.position[part.index[i + 1]], &c = part.position[part.index[i + 2]];
                    std::array<double, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    std::array<double, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                    const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (l > 1e-12)
                        for (int k = 0; k < 3; ++k) part.normal[part.index[i + k]] = {n[0] / l, n[1] / l, n[2] / l};
                }
                notes.push_back("Mesh " + inst.name + " had no normals: it is lit with its faces' own (flat shading).");
            }
            // normals pointing into the model (against their faces' winding): kept, as they may be meant so, but said,
            // since the model is then lit from the wrong side, dark where the light falls (issue #280)
            else if (!is_collision && !part.normal.empty()) {
                std::vector<std::array<double, 3>> face(part.position.size(), {0, 0, 0});
                for (size_t i = 0; i + 2 < part.index.size(); i += 3) {
                    const auto &a = part.position[part.index[i]], &b = part.position[part.index[i + 1]], &c = part.position[part.index[i + 2]];
                    const std::array<double, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    const std::array<double, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                    for (int k = 0; k < 3; ++k)
                        for (int r = 0; r < 3; ++r) face[part.index[i + k]][r] += n[r];
                }
                size_t used = 0, inward = 0;
                for (size_t i = 0; i < face.size(); ++i) {
                    const auto &f = face[i], &n = part.normal[i];
                    if (f[0] == 0 && f[1] == 0 && f[2] == 0) continue;
                    ++used;
                    if (f[0] * n[0] + f[1] * n[1] + f[2] * n[2] < 0) ++inward;
                }
                if (used && inward * 4 > used)
                    out.warnings.push_back("Mesh " + inst.name + ": " + std::to_string(inward * 100 / used) +
                                           "% of its normals point into the model, against its faces, so it is lit from the wrong side (dark where the light falls). In Blender: "
                                           "select the mesh, Edit Mode, Mesh > Normals > Recalculate Outside, and export again.");
            }
            if (part.uv.empty() && !is_collision) part.uv.assign(part.position.size(), {0, 0});
            (is_collision ? collision : render).push_back(std::move(part));
        }
    }
    if (render.empty()) fail("the file has no visible triangles (every mesh is collision, or loose edges and points only)");

    // one primitive per material: the parts of each material together
    auto merge = [](std::vector<Part>& parts) {
        std::vector<Part> merged;
        for (auto& p : parts) {
            auto it = std::find_if(merged.begin(), merged.end(), [&](const Part& m) { return m.material == p.material; });
            if (it == merged.end()) {
                merged.push_back(std::move(p));
                continue;
            }
            const uint32_t base = uint32_t(it->position.size());
            it->position.insert(it->position.end(), p.position.begin(), p.position.end());
            it->normal.insert(it->normal.end(), p.normal.begin(), p.normal.end());
            it->uv.insert(it->uv.end(), p.uv.begin(), p.uv.end());
            for (uint32_t i : p.index) it->index.push_back(base + i);
        }
        parts = std::move(merged);
    };
    merge(render);
    merge(collision);

    // ---- materials of the visible parts, and the textures they use
    Value out_materials;
    out_materials.type = Value::Array;
    std::map<size_t, size_t> material_map;  // file material -> output material
    std::vector<size_t> image_of;           // output texture n: its file image
    std::map<size_t, size_t> image_map;     // file image -> output texture
    std::vector<std::string> blended, extra_textures, dropped_extensions;
    std::vector<size_t> blended_images;  // output textures of blended materials (MASK or OPAQUE once the alpha is known)
    std::vector<size_t> blended_material;
    const auto& textures = doc.get("textures").array;
    const auto& images = doc.get("images").array;
    auto material_name = [&](size_t m) {
        const std::string n = m < materials.size() ? materials[m].get("name").string() : std::string();
        return n.empty() ? "material " + std::to_string(m) : n;
    };
    for (const auto& part : render) {
        if (part.material == SIZE_MAX || part.material >= materials.size())
            fail("a visible mesh has no material: give every visible face a material with a base colour texture (Image Texture into Base Color)");
        if (material_map.count(part.material)) continue;
        Value m = materials[part.material];
        for (const char* key : {"normalTexture", "occlusionTexture", "emissiveTexture"})
            if (m.object.erase(key)) extra_textures.push_back(material_name(part.material));
        if (m.get("pbrMetallicRoughness").object.count("metallicRoughnessTexture")) {
            m["pbrMetallicRoughness"].object.erase("metallicRoughnessTexture");
            extra_textures.push_back(material_name(part.material));
        }
        if (m.object.erase("extensions")) dropped_extensions.push_back(material_name(part.material));
        if (m.get("pbrMetallicRoughness").get("baseColorTexture").object.count("extensions")) {  // KHR_texture_transform and the like
            m["pbrMetallicRoughness"]["baseColorTexture"].object.erase("extensions");
            dropped_extensions.push_back(material_name(part.material));
        }
        // a base colour factor of white is no factor
        if (const Value& f = m.get("pbrMetallicRoughness").get("baseColorFactor"); f.type == Value::Array && f.array.size() == 4 &&
                                                                               std::all_of(f.array.begin(), f.array.end(), [](const Value& x) { return x.type == Value::Number && std::fabs(x.number - 1) < 1e-6; }))
            m["pbrMetallicRoughness"].object.erase("baseColorFactor");
        m.object.erase("extras");
        const Value& colour = m.get("pbrMetallicRoughness").get("baseColorTexture");
        if (colour.get("index").type != Value::Number || size_t(colour.get("index").number) >= textures.size())
            fail("material " + material_name(part.material) + " has no base colour texture: connect an Image Texture to its Base Color (a game material by name is a kit model, docs/world-objects.md#game-materials)");
        if (colour.get("texCoord").type == Value::Number && colour.get("texCoord").number != 0) fail("material " + material_name(part.material) + " uses its second UV map: use the first");
        const Value& texture = textures[size_t(colour.get("index").number)];
        if (texture.get("source").type != Value::Number || size_t(texture.get("source").number) >= images.size())
            fail("material " + material_name(part.material) + "'s texture has no image (a WebP or KTX2 texture can't be read: export PNG)");
        const size_t image = size_t(texture.get("source").number);
        auto [it, fresh] = image_map.emplace(image, image_of.size());
        if (fresh) image_of.push_back(image);
        Value& out_colour = m["pbrMetallicRoughness"]["baseColorTexture"];
        out_colour.object.clear();
        out_colour["index"] = double(it->second);
        if (m.get("alphaMode").string() == "BLEND") {
            blended.push_back(material_name(part.material));
            blended_images.push_back(it->second);
            blended_material.push_back(out_materials.array.size());
        }
        if (m.get("alphaMode").string() != "MASK") m.object.erase("alphaCutoff");
        material_map[part.material] = out_materials.array.size();
        out_materials.array.push_back(m);
    }
    for (const auto& part : collision) {
        if (material_map.count(part.material)) continue;
        if (part.material == SIZE_MAX || part.material >= materials.size() || !static_models::collision_material(materials[part.material].get("name").string()))
            fail("collision mesh " + join(collision_names) + " needs col_* materials (col_grass, col_stone, col_wood ...) on all its faces");
        Value m;
        m["name"] = materials[part.material].get("name").string();
        material_map[part.material] = out_materials.array.size();
        out_materials.array.push_back(m);
    }

    // ---- the textures: read, sized as the game takes them, written as plain PNG where they change
    std::vector<Pixels> pixels(image_of.size());
    std::vector<std::span<const uint8_t>> original(image_of.size());
    std::vector<std::string> image_names(image_of.size());
    std::vector<unsigned> widths, heights;
    for (size_t t = 0; t < image_of.size(); ++t) {
        const Value& image = images[image_of[t]];
        image_names[t] = image.get("name").string();
        if (image_names[t].empty()) image_names[t] = "image " + std::to_string(image_of[t]);
        if (image.get("uri").type != Value::Null) fail("texture " + image_names[t] + " is a separate file: export glTF Binary (.glb), which embeds the textures");
        original[t] = view_bytes(g, image.get("bufferView"));
        pixels[t] = decode(original[t], image_names[t]);
        widths.push_back(pixels[t].width), heights.push_back(pixels[t].height);
    }
    std::vector<unsigned> new_widths, new_heights;
    texture_sizes(widths, heights, static_models::kMaxTextureBytes, new_widths, new_heights);
    std::vector<std::string> resized, converted;
    std::vector<std::vector<uint8_t>> png(image_of.size());
    for (size_t t = 0; t < image_of.size(); ++t) {
        const bool size_changed = new_widths[t] != widths[t] || new_heights[t] != heights[t];
        if (size_changed) {
            resized.push_back(image_names[t] + " " + std::to_string(widths[t]) + " x " + std::to_string(heights[t]) + " to " + std::to_string(new_widths[t]) + " x " +
                              std::to_string(new_heights[t]));
            pixels[t] = resample(pixels[t], new_widths[t], new_heights[t]);
        }
        if (!size_changed && plain_png(original[t])) png[t].assign(original[t].begin(), original[t].end());
        else {
            if (!size_changed) converted.push_back(image_names[t]);
            png[t] = encode(pixels[t]);
        }
    }
    // blended materials: a cut-out where the texture has transparent parts, else opaque
    std::vector<std::string> cut, opaque;
    for (size_t i = 0; i < blended.size(); ++i) {
        Value& m = out_materials.array[blended_material[i]];
        if (pixels[blended_images[i]].alpha) {
            m["alphaMode"] = "MASK";
            if (m.get("alphaCutoff").type != Value::Number) m["alphaCutoff"] = 0.5;
            cut.push_back(blended[i]);
        } else {
            m.object.erase("alphaMode");
            m.object.erase("alphaCutoff");
            opaque.push_back(blended[i]);
        }
    }

    // ---- notes
    if (render_names.size() > 1)
        notes.push_back("The " + plural(render_names.size(), "mesh object", "mesh objects") + " " + join(render_names) +
                        " are merged into one model. A mesh meant as collision is named <name>_col (or uses only col_* materials).");
    if (collision_names.size() > 1) notes.push_back("The collision meshes " + join(collision_names) + " are merged into one.");
    if (transformed)
        notes.push_back("The objects' own placement, rotation and scale in Blender are applied to their vertices (the model keeps the file's origin).");
    if (strips) notes.push_back("Triangle strips and fans are turned into triangles.");
    if (loose) notes.push_back("Loose edges and points (" + plural(loose, "part", "parts") + " that aren't faces) are left out; the game draws faces only.");
    if (unindexed) notes.push_back("Meshes without an index list are given one.");
    if (!dropped_attributes.empty())
        notes.push_back("Vertex data the game doesn't draw is left out: " + join(dropped_attributes) + " (vertex colours, a second UV map or tangents; only positions, normals and the first UV map are used).");
    if (!cut.empty()) notes.push_back("Blended transparency isn't supported, so " + join(cut) + " is drawn as a cut-out (alpha below 0.5 is see-through, the rest solid; alphaMode MASK).");
    if (!opaque.empty()) notes.push_back("Blended transparency isn't supported, and the texture of " + join(opaque) + " has no transparent parts, so it is drawn opaque.");
    if (!extra_textures.empty()) notes.push_back("Only the base colour texture is drawn: the normal, occlusion, emissive and metal/roughness textures of " + join(extra_textures) + " are left out.");
    if (!dropped_extensions.empty()) notes.push_back("Material extensions of " + join(dropped_extensions) + " (specular, IOR and similar) are left out; the game doesn't draw them.");
    if (!resized.empty())
        notes.push_back("Textures are scaled to sizes the game takes (at most 512 x 512, all of a model's together at most 1 MiB): " + join(resized, 16) + ".");
    if (!converted.empty()) notes.push_back("Textures converted to plain PNG (RGB or RGBA, 8 bits): " + join(converted, 16) + ".");
    if (!doc.get("extensionsUsed").array.empty()) notes.push_back("Optional glTF extensions are left out.");

    // ---- the new file: one visible mesh, maybe one collision mesh, plain triangles
    Value d;
    d["asset"]["version"] = "2.0";
    d["asset"]["generator"] = "Wind Waker HD world editor (model import)";
    std::vector<uint8_t> bin;
    Value& views = d["bufferViews"];
    views.type = Value::Array;
    Value& accessors = d["accessors"];
    accessors.type = Value::Array;
    auto add_view = [&](const std::vector<uint8_t>& bytes, int target) {
        while (bin.size() % 4) bin.push_back(0);
        Value v;
        v["buffer"] = 0.0;
        v["byteOffset"] = double(bin.size());
        v["byteLength"] = double(bytes.size());
        if (target) v["target"] = double(target);
        bin.insert(bin.end(), bytes.begin(), bytes.end());
        views.array.push_back(v);
        return double(views.array.size() - 1);
    };
    auto add_floats = [&](const std::vector<float>& data, size_t components, const char* type, bool bounds) {
        std::vector<uint8_t> bytes(data.size() * 4);
        std::memcpy(bytes.data(), data.data(), bytes.size());
        Value a;
        a["bufferView"] = add_view(bytes, 34962);
        a["componentType"] = 5126.0;
        a["count"] = double(data.size() / components);
        a["type"] = type;
        if (bounds) {
            Value lo, hi;
            lo.type = hi.type = Value::Array;
            for (size_t k = 0; k < components; ++k) {
                float l = INFINITY, h = -INFINITY;
                for (size_t i = k; i < data.size(); i += components) l = std::min(l, data[i]), h = std::max(h, data[i]);
                lo.array.push_back(double(l)), hi.array.push_back(double(h));
            }
            a["min"] = lo, a["max"] = hi;
        }
        accessors.array.push_back(a);
        return double(accessors.array.size() - 1);
    };
    auto add_indices = [&](const std::vector<uint32_t>& index, size_t vertices) {
        std::vector<uint8_t> bytes;
        const bool small = vertices <= 65535;
        for (uint32_t i : index) {
            if (small) bytes.insert(bytes.end(), {uint8_t(i), uint8_t(i >> 8)});
            else put32(bytes, i);
        }
        Value a;
        a["bufferView"] = add_view(bytes, 34963);
        a["componentType"] = small ? 5123.0 : 5125.0;
        a["count"] = double(index.size());
        a["type"] = "SCALAR";
        accessors.array.push_back(a);
        return double(accessors.array.size() - 1);
    };
    auto add_mesh = [&](const std::vector<Part>& parts, bool visible, const char* name) {
        Value mesh;
        mesh["name"] = name;
        mesh["primitives"].type = Value::Array;
        for (const auto& part : parts) {
            std::vector<float> p, n, u;
            for (const auto& v : part.position) p.insert(p.end(), {float(v[0]), float(v[1]), float(v[2])});
            Value prim;
            prim["attributes"]["POSITION"] = add_floats(p, 3, "VEC3", true);
            if (visible) {
                for (const auto& v : part.normal) n.insert(n.end(), {float(v[0]), float(v[1]), float(v[2])});
                for (const auto& v : part.uv) u.insert(u.end(), {float(v[0]), float(v[1])});
                prim["attributes"]["NORMAL"] = add_floats(n, 3, "VEC3", false);
                prim["attributes"]["TEXCOORD_0"] = add_floats(u, 2, "VEC2", false);
            }
            prim["indices"] = add_indices(part.index, part.position.size());
            prim["material"] = double(material_map.at(part.material));
            mesh["primitives"].array.push_back(prim);
        }
        d["meshes"].type = Value::Array;
        d["meshes"].array.push_back(mesh);
        return double(d["meshes"].array.size() - 1);
    };
    d["nodes"].type = Value::Array;
    Value node;
    node["name"] = "model";
    node["mesh"] = add_mesh(render, true, "model");
    d["nodes"].array.push_back(node);
    if (!collision.empty()) {
        Value c;
        c["name"] = "collision_col";
        c["mesh"] = add_mesh(collision, false, "collision_col");
        d["nodes"].array.push_back(c);
    }
    d["scene"] = 0.0;
    Value s;
    s["nodes"].type = Value::Array;
    for (size_t i = 0; i < d["nodes"].array.size(); ++i) s["nodes"].array.push_back(double(i));
    d["scenes"].type = Value::Array;
    d["scenes"].array.push_back(s);
    d["materials"] = out_materials;
    d["images"].type = Value::Array;
    d["textures"].type = Value::Array;
    for (size_t t = 0; t < image_of.size(); ++t) {
        Value image;
        image["name"] = image_names[t];
        image["mimeType"] = "image/png";
        image["bufferView"] = add_view(png[t], 0);
        d["images"].array.push_back(image);
        Value texture;
        texture["source"] = double(t);
        // the file's sampler (wrapping and filtering) of the first texture that showed this image
        for (const auto& tex : textures)
            if (tex.get("source").type == Value::Number && size_t(tex.get("source").number) == image_of[t] && tex.get("sampler").type == Value::Number &&
                size_t(tex.get("sampler").number) < doc.get("samplers").array.size()) {
                Value sampler = doc.get("samplers").array[size_t(tex.get("sampler").number)];
                sampler.object.erase("extensions");
                sampler.object.erase("extras");
                sampler.object.erase("name");
                d["samplers"].type = Value::Array;
                d["samplers"].array.push_back(sampler);
                texture["sampler"] = double(d["samplers"].array.size() - 1);
                break;
            }
        d["textures"].array.push_back(texture);
    }
    while (bin.size() % 4) bin.push_back(0);
    d["buffers"].type = Value::Array;
    Value buffer;
    buffer["byteLength"] = double(bin.size());
    d["buffers"].array.push_back(buffer);

    std::string json = mods::json::dump(d);
    while (json.size() % 4) json += ' ';
    std::vector<uint8_t>& glb = out.glb;
    glb.clear();
    put32(glb, 0x46546C67);
    put32(glb, 2);
    put32(glb, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    put32(glb, uint32_t(json.size()));
    put32(glb, 0x4E4F534A);
    glb.insert(glb.end(), json.begin(), json.end());
    put32(glb, uint32_t(bin.size()));
    put32(glb, 0x004E4942);
    glb.insert(glb.end(), bin.begin(), bin.end());
    out.changed = true;
    return true;
}
}  // namespace

void texture_sizes(const std::vector<unsigned>& widths, const std::vector<unsigned>& heights, size_t budget, std::vector<unsigned>& out_widths,
                   std::vector<unsigned>& out_heights) {
    const size_t n = widths.size();
    std::vector<double> scale(n, 1.0);
    auto size_of = [&](size_t i, unsigned& w, unsigned& h) {
        const double s = std::min(scale[i], 512.0 / std::max(widths[i], heights[i]));
        // the nearest sizes the game takes (width a multiple of 64, height of 8), within 64..512 and 8..512
        w = unsigned(std::clamp<long>(std::lround(widths[i] * s / 64.0) * 64, 64, 512));
        h = unsigned(std::clamp<long>(std::lround(heights[i] * s / 8.0) * 8, 8, 512));
    };
    out_widths.assign(n, 0), out_heights.assign(n, 0);
    for (int round = 0; round < 64; ++round) {
        size_t total = 0, largest = SIZE_MAX, largest_area = 0;
        for (size_t i = 0; i < n; ++i) {
            size_of(i, out_widths[i], out_heights[i]);
            const size_t area = size_t(out_widths[i]) * out_heights[i];
            total += area * 4;
            if (area > largest_area && (out_widths[i] > 64 || out_heights[i] > 8)) largest_area = area, largest = i;
        }
        if (total <= budget || largest == SIZE_MAX) return;
        scale[largest] = std::min(scale[largest], 512.0 / std::max(widths[largest], heights[largest])) / 2;
    }
}

bool fix(std::span<const uint8_t> input, Result& out, std::string& why) {
    out = Result();
    out.glb.assign(input.begin(), input.end());
    // a file the game takes as it is stays as it is
    static_models::ModelInfo info;
    std::string strict;
    if (static_models::validate_glb(input, info, strict)) {
        // kept byte for byte; what looks wrong in it is still said (its warnings, from reading it as the import does)
        try {
            Result read;
            if (fix_inner(input, read)) out.warnings = std::move(read.warnings);
        } catch (const std::exception&) {
        }
        return true;
    }
    Result fixed;
    try {
        if (!fix_inner(input, fixed)) return true;  // skinned: its own rules
    } catch (const Fail& e) {
        why = e.what();
        return false;
    } catch (const std::exception& e) {
        why = std::string("the file can't be read: ") + e.what();
        return false;
    }
    out = std::move(fixed);
    return true;
}

bool strip_material_textures(std::span<const uint8_t> input, const std::vector<bool>& strip, std::vector<uint8_t>& out, unsigned& removed, std::string& why) {
    out.assign(input.begin(), input.end());
    removed = 0;
    const auto le = [&](size_t at) { return uint32_t(input[at]) | uint32_t(input[at + 1]) << 8 | uint32_t(input[at + 2]) << 16 | uint32_t(input[at + 3]) << 24; };
    if (input.size() < 20 || std::memcmp(input.data(), "glTF", 4) || le(4) != 2) return why = "not a GLB (glTF 2.0) file", false;
    const size_t json_length = le(12);
    if (json_length > input.size() - 20 || le(16) != 0x4E4F534A) return why = "invalid GLB JSON chunk", false;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(input.data() + 20), json_length));
    } catch (...) {
        return why = "invalid GLB JSON", false;
    }
    const size_t base = 20 + json_length;
    std::span<const uint8_t> bin;
    if (input.size() >= base + 8) {
        const size_t length = le(base);
        if (le(base + 4) != 0x004E4942 || length > input.size() - base - 8) return why = "invalid GLB BIN chunk", false;
        bin = input.subspan(base + 8, length);
    }
    if (doc.get("materials").type != Value::Array) return true;
    // the marked materials' texture slots (a material's own extensions keep theirs: nothing is guessed)
    bool any = false;
    for (size_t m = 0; m < doc["materials"].array.size() && m < strip.size(); ++m) {
        if (!strip[m]) continue;
        Value& mat = doc["materials"].array[m];
        bool had = false;
        if (mat.get("pbrMetallicRoughness").type == Value::Object)
            for (const char* slot : {"baseColorTexture", "metallicRoughnessTexture"}) had |= mat["pbrMetallicRoughness"].object.erase(slot) > 0;
        for (const char* slot : {"normalTexture", "occlusionTexture", "emissiveTexture"}) had |= mat.object.erase(slot) > 0;
        removed += had;
        any |= had;
    }
    if (!any) return true;
    // what is still used: textures (any "...Texture" slot of any material), their images, and the buffer views of
    // accessors and images; everything else is dropped and the indices renumbered
    const auto walk = [](Value& v, const std::function<void(const std::string&, Value&)>& f) {
        std::function<void(Value&)> go = [&](Value& x) {
            if (x.type == Value::Object)
                for (auto& [k, c] : x.object) {
                    f(k, c);
                    go(c);
                }
            else if (x.type == Value::Array)
                for (auto& c : x.array) go(c);
        };
        go(v);
    };
    const auto texture_slot = [](const std::string& key, const Value& v) {
        return key.size() >= 7 && (key.ends_with("Texture") || key.ends_with("texture")) && v.type == Value::Object && v.get("index").type == Value::Number;
    };
    const auto renumber = [](std::vector<long>& map) {
        long next = 0;
        for (auto& x : map) x = x ? next++ : -1;
    };
    const size_t texture_count = doc.get("textures").array.size(), image_count = doc.get("images").array.size(), view_count = doc.get("bufferViews").array.size();
    std::vector<long> textures(texture_count, 0), images(image_count, 0), views(view_count, 0);
    const auto mark = [](std::vector<long>& used, const Value& index) {
        if (index.type != Value::Number || index.number < 0 || size_t(index.number) >= used.size()) throw std::runtime_error("an index outside the file");
        used[size_t(index.number)] = 1;
    };
    try {
        walk(doc["materials"], [&](const std::string& k, Value& v) {
            if (texture_slot(k, v)) mark(textures, v.get("index"));
        });
        for (size_t t = 0; t < texture_count; ++t)
            if (textures[t]) walk(doc["textures"].array[t], [&](const std::string& k, Value& v) {
                    if (k == "source") mark(images, v);
                });
        for (size_t i = 0; i < image_count; ++i)
            if (images[i] && doc["images"].array[i].get("bufferView").type != Value::Null) mark(views, doc["images"].array[i].get("bufferView"));
        for (auto& a : doc["accessors"].array) {
            if (a.get("bufferView").type != Value::Null) mark(views, a.get("bufferView"));
            for (const char* part : {"indices", "values"})
                if (a.get("sparse").get(part).type == Value::Object) mark(views, a.get("sparse").get(part).get("bufferView"));
        }
    } catch (const std::exception& e) {
        return why = std::string("the file can't be read: ") + e.what(), false;
    }
    // views of a buffer other than the GLB's own (an external .bin) are kept as they are
    for (size_t v = 0; v < view_count; ++v)
        if (doc["bufferViews"].array[v].get("buffer").number != 0) views[v] = 1;
    renumber(textures);
    renumber(images);
    renumber(views);
    walk(doc["materials"], [&](const std::string& k, Value& v) {
        if (texture_slot(k, v)) v["index"] = double(textures[size_t(v.get("index").number)]);
    });
    std::vector<Value> kept_textures, kept_images, kept_views;
    for (size_t t = 0; t < texture_count; ++t)
        if (textures[t] >= 0) {
            Value x = doc["textures"].array[t];
            walk(x, [&](const std::string& k, Value& v) {
                if (k == "source") v = double(images[size_t(v.number)]);
            });
            kept_textures.push_back(std::move(x));
        }
    for (size_t i = 0; i < image_count; ++i)
        if (images[i] >= 0) {
            Value x = doc["images"].array[i];
            if (x.get("bufferView").type == Value::Number) x["bufferView"] = double(views[size_t(x.get("bufferView").number)]);
            kept_images.push_back(std::move(x));
        }
    for (auto& a : doc["accessors"].array) {
        if (a.get("bufferView").type == Value::Number) a["bufferView"] = double(views[size_t(a.get("bufferView").number)]);
        for (const char* part : {"indices", "values"})
            if (a.get("sparse").get(part).type == Value::Object) a["sparse"][part]["bufferView"] = double(views[size_t(a["sparse"][part].get("bufferView").number)]);
    }
    // the GLB's own buffer again with only the views still used, each at a 4-byte boundary
    std::vector<uint8_t> data;
    for (size_t v = 0; v < view_count; ++v) {
        if (views[v] < 0) continue;
        Value x = doc["bufferViews"].array[v];
        if (x.get("buffer").number == 0) {
            const size_t offset = x.get("byteOffset").type == Value::Number ? size_t(x.get("byteOffset").number) : 0, length = size_t(x.get("byteLength").number);
            if (offset > bin.size() || length > bin.size() - offset) return why = "a buffer view outside the file's data", false;
            while (data.size() % 4) data.push_back(0);
            x["byteOffset"] = double(data.size());
            data.insert(data.end(), bin.begin() + long(offset), bin.begin() + long(offset + length));
        }
        kept_views.push_back(std::move(x));
    }
    while (data.size() % 4) data.push_back(0);
    const auto set_list = [&](const char* key, std::vector<Value>& list) {
        if (list.empty()) doc.object.erase(key);
        else {
            doc[key].type = Value::Array;
            doc[key].array = std::move(list);
        }
    };
    set_list("textures", kept_textures);
    set_list("images", kept_images);
    set_list("bufferViews", kept_views);
    if (doc.get("samplers").type == Value::Array && !doc.object.count("textures")) doc.object.erase("samplers");
    if (doc.get("buffers").type == Value::Array && !doc["buffers"].array.empty()) doc["buffers"].array[0]["byteLength"] = double(data.size());
    std::string text = mods::json::dump(doc);
    while (text.size() % 4) text += ' ';
    std::vector<uint8_t> glb;
    const auto word = [&](uint32_t n) {
        for (unsigned k = 0; k < 4; ++k) glb.push_back(uint8_t(n >> (8 * k)));
    };
    const bool has_bin = !bin.empty() || !data.empty();
    word(0x46546C67);
    word(2);
    word(uint32_t(12 + 8 + text.size() + (has_bin ? 8 + data.size() : 0)));
    word(uint32_t(text.size()));
    word(0x4E4F534A);
    glb.insert(glb.end(), text.begin(), text.end());
    if (has_bin) {
        word(uint32_t(data.size()));
        word(0x004E4942);
        glb.insert(glb.end(), data.begin(), data.end());
    }
    out = std::move(glb);
    return true;
}
}  // namespace editor::model_import
