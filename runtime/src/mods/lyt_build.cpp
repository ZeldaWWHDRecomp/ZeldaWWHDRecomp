// Story UI layouts: description (JSON) + PNG art -> layout archive (lyt_build.h). A line-by-line mirror of
// the reference generator tools/story_mods/lyt; the module and function names below are the Python ones,
// so a change there is easy to follow here. Format notes live in the Python modules.
#include "lyt_build.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <utility>

#include "guest_png.h"
#include "runtime.h"

namespace lyt_build {
namespace {

constexpr const char* kWriterVersion = "lyt_build 1";
constexpr uint32_t kVersion = 0x02020000;
constexpr size_t kMaxArchive = size_t(12) << 20;  // the largest archive (SARC, in memory) the port builds

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error(why); }

// ---------------------------------------------------------------- an ordered JSON reader
// (the description's objects are used in their written order, as Python's dicts)
struct JMember;
struct J {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<J> arr;
    std::vector<JMember> obj;  // not std::pair: libstdc++ rejects a pair of a still incomplete J
    const J* find(const std::string& k) const;
    bool has(const std::string& k) const { return find(k) != nullptr; }
    const J& at(const std::string& k) const;
};
struct JMember {
    std::string first;
    J second;
};
const J* J::find(const std::string& k) const {
    for (const auto& [key, v] : obj)
        if (key == k) return &v;
    return nullptr;
}
const J& J::at(const std::string& k) const {
    const J* v = find(k);
    if (!v) fail("missing key \"" + k + "\"");
    return *v;
}

class Reader {
public:
    explicit Reader(const std::string& t) : s(t) {}
    J parse() {
        J v = value(0);
        ws();
        if (p != s.size()) fail("description: trailing data");
        return v;
    }

private:
    const std::string& s;
    size_t p = 0;
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    }
    bool take(char c) {
        ws();
        if (p < s.size() && s[p] == c) return ++p, true;
        return false;
    }
    static void utf8(std::string& out, unsigned n) {
        if (n < 0x80) out += char(n);
        else if (n < 0x800) out += char(0xC0 | (n >> 6)), out += char(0x80 | (n & 63));
        else if (n < 0x10000) out += char(0xE0 | (n >> 12)), out += char(0x80 | ((n >> 6) & 63)), out += char(0x80 | (n & 63));
        else
            out += char(0xF0 | (n >> 18)), out += char(0x80 | ((n >> 12) & 63)), out += char(0x80 | ((n >> 6) & 63)),
                out += char(0x80 | (n & 63));
    }
    unsigned hex4() {
        if (p + 4 > s.size()) fail("description: short \\u escape");
        unsigned n = 0;
        for (int i = 0; i < 4; i++) {
            char c = s[p++];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) fail("description: bad \\u escape");
            n = n * 16 + unsigned(d);
        }
        return n;
    }
    std::string str() {
        if (!take('"')) fail("description: string expected");
        std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= s.size()) break;
            c = s[p++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned n = hex4();
                if (n >= 0xD800 && n <= 0xDBFF && p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                    p += 2;
                    unsigned lo = hex4();
                    n = 0x10000 + ((n - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, n);
                break;
            }
            default: out += c;
            }
        }
        fail("description: unterminated string");
    }
    J value(int depth) {
        if (depth > 64) fail("description: nested too deeply");
        ws();
        if (p >= s.size()) fail("description: unexpected end");
        J v;
        if (s[p] == '"') {
            v.type = J::String, v.s = str();
            return v;
        }
        if (take('{')) {
            v.type = J::Object;
            if (take('}')) return v;
            do {
                std::string k = str();
                if (!take(':')) fail("description: ':' expected");
                J x = value(depth + 1);
                bool replaced = false;  // Python: a repeated key keeps its first place, the last value
                for (auto& kv : v.obj)
                    if (kv.first == k) kv.second = std::move(x), replaced = true;
                if (!replaced) v.obj.push_back({std::move(k), std::move(x)});
            } while (take(','));
            if (!take('}')) fail("description: '}' expected");
            return v;
        }
        if (take('[')) {
            v.type = J::Array;
            if (take(']')) return v;
            do v.arr.push_back(value(depth + 1));
            while (take(','));
            if (!take(']')) fail("description: ']' expected");
            return v;
        }
        for (auto [word, type, val] : {std::tuple{"true", J::Bool, true}, std::tuple{"false", J::Bool, false}, std::tuple{"null", J::Null, false}}) {
            const size_t n = strlen(word);
            if (s.compare(p, n, word) == 0) {
                p += n, v.type = type, v.b = val;
                return v;
            }
        }
        size_t end = p;
        while (end < s.size() && (isdigit((unsigned char)s[end]) || strchr("+-.eE", s[end]))) end++;
        if (end == p) fail("description: value expected");
        v.type = J::Number, v.num = strtod(s.substr(p, end - p).c_str(), nullptr);
        p = end;
        return v;
    }
};

// Python-like accessors
double num(const J& v) {
    if (v.type == J::Number) return v.num;
    if (v.type == J::Bool) return v.b ? 1 : 0;
    fail("a number expected");
}
int64_t to_int(const J& v) { return int64_t(num(v)); }  // Python int(): truncation
double num_or(const J& o, const std::string& k, double d) {
    const J* v = o.find(k);
    return v ? num(*v) : d;
}
std::string str_or(const J& o, const std::string& k, const std::string& d) {
    const J* v = o.find(k);
    return v && v->type == J::String ? v->s : d;
}
bool truthy(const J* v) {
    if (!v) return false;
    switch (v->type) {
    case J::Null: return false;
    case J::Bool: return v->b;
    case J::Number: return v->num != 0;
    case J::String: return !v->s.empty();
    case J::Array: return !v->arr.empty();
    case J::Object: return !v->obj.empty();
    }
    return false;
}
bool bool_or(const J& o, const std::string& k, bool d) {
    const J* v = o.find(k);
    return v ? truthy(v) : d;
}
J number_list(std::initializer_list<double> xs) {
    J v;
    v.type = J::Array;
    for (double x : xs) {
        J n;
        n.type = J::Number, n.num = x;
        v.arr.push_back(n);
    }
    return v;
}
J list_or(const J& o, const std::string& k, const J& d) {  // a copy: d is often a temporary
    const J* v = o.find(k);
    return v ? *v : d;
}
std::vector<double> nums(const J& v) {
    std::vector<double> out;
    if (v.type == J::Number) return {v.num};
    for (const J& x : v.arr) out.push_back(num(x));
    return out;
}

// ---------------------------------------------------------------- binio.py
struct Buf {
    Bytes d;
    Buf& u8(uint32_t v) { return d.push_back(uint8_t(v)), *this; }
    Buf& u16(uint32_t v) { return u8(v >> 8).u8(v); }
    Buf& u32(uint32_t v) { return u16(v >> 16).u16(v); }
    Buf& i16(int32_t v) { return u16(uint32_t(v) & 0xFFFF); }
    Buf& f32(double v) {
        float f = float(v);
        uint32_t u;
        memcpy(&u, &f, 4);
        return u32(u);
    }
    Buf& add(const Bytes& b) { return d.insert(d.end(), b.begin(), b.end()), *this; }
    Buf& add(const std::string& b) { return d.insert(d.end(), b.begin(), b.end()), *this; }
    Buf& zeros(size_t n) { return d.insert(d.end(), n, 0), *this; }
    Buf& pad4() { return zeros((4 - d.size() % 4) % 4); }
    size_t size() const { return d.size(); }
};

Bytes name(const std::string& s, size_t n) {
    if (s.size() + 1 > n) fail("name \"" + s + "\" is longer than " + std::to_string(n - 1) + " bytes");
    Bytes b(s.begin(), s.end());
    b.resize(n, 0);
    return b;
}
Bytes cstr4(const std::string& s) {
    Buf b;
    b.add(s).u8(0).pad4();
    return b.d;
}
Bytes pad4(Bytes b) {
    b.resize((b.size() + 3) / 4 * 4, 0);
    return b;
}
Bytes section(const char* magic, Bytes body) {
    body = pad4(std::move(body));
    Buf b;
    b.add(std::string(magic, 4)).u32(uint32_t(8 + body.size())).add(body);
    return b.d;
}
Bytes file_header(const char* magic, const std::vector<Bytes>& secs) {
    Buf body;
    for (const Bytes& s : secs) body.add(s);
    Buf b;
    b.add(std::string(magic, 4)).u16(0xFEFF).u16(0x14).u32(kVersion).u32(uint32_t(0x14 + body.size())).u16(uint32_t(secs.size())).u16(0);
    return b.add(body.d).d;
}
Bytes string_table(const std::vector<std::string>& names) {
    Buf offs, blob;
    const size_t base = 4 * names.size();
    for (const auto& n : names) offs.u32(uint32_t(base + blob.size())), blob.add(n).u8(0);
    offs.add(blob.d);
    return pad4(offs.d);
}
double srgb_to_linear(double v) {
    double x = v / 255.0;
    x = x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    return 255.0 * x;
}
std::vector<double> padded_colour(const J& c) {
    std::vector<double> v = nums(c);
    while (v.size() < 4) v.push_back(255);
    v.resize(4);
    return v;
}
Bytes rgba_of(const std::vector<double>& c) {
    Bytes b;
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(int64_t(c[i]) & 0xFF));
    return b;
}
Bytes rgba(const J& c) { return rgba_of(padded_colour(c)); }
Bytes rgba_linear(const J& c) {
    std::vector<double> v = padded_colour(c);
    for (int i = 0; i < 3; i++) v[i] = std::nearbyint(srgb_to_linear(v[i]));  // Python round(): half to even
    return rgba_of(v);
}
Bytes utf16be(const std::string& s) {
    Bytes out;
    auto put = [&](unsigned u) { out.push_back(uint8_t(u >> 8)), out.push_back(uint8_t(u)); };
    for (size_t i = 0; i < s.size();) {
        unsigned c = (unsigned char)s[i], n = 0, len = 1;
        if (c < 0x80) n = c;
        else if ((c >> 5) == 6) n = c & 31, len = 2;
        else if ((c >> 4) == 14) n = c & 15, len = 3;
        else n = c & 7, len = 4;
        for (unsigned k = 1; k < len && i + k < s.size(); k++) n = (n << 6) | ((unsigned char)s[i + k] & 63);
        i += len;
        if (n >= 0x10000) put(0xD800 + ((n - 0x10000) >> 10)), put(0xDC00 + ((n - 0x10000) & 0x3FF));
        else put(n);
    }
    put(0);
    return out;
}

// ---------------------------------------------------------------- description.py
struct TexFormat { const char* key; uint8_t code; char suffix; uint32_t bpp; bool bc; bool described; };
// described: usable in a layout description; bc5la only for the item icons' glow (item_icons.h)
const TexFormat kFormats[] = {{"rgba8", 0x14, 'l', 32, false, true}, {"bc1", 0x15, 'o', 64, true, true}, {"bc3", 0x17, 'q', 128, true, true},
                              {"bc5la", 0x11, 't', 128, true, false}};
const TexFormat* format_of(const std::string& key, bool any = false) {
    for (const TexFormat& f : kFormats)
        if (key == f.key && (f.described || any)) return &f;
    return nullptr;
}

struct Desc {
    J root;
    std::filesystem::path art_root;
    std::string name;
    double w = 1280, h = 720, pw = 0, ph = 0;
    bool srgb = true;
    J textures, materials, panes, groups, animations, fonts;
    explicit Desc(const std::string& text, std::filesystem::path art) : root(Reader(text).parse()), art_root(std::move(art)) {
        if (root.type != J::Object) fail("description: an object expected");
        name = root.at("name").s;
        if (const J* s = root.find("size")) w = nums(*s).at(0), h = nums(*s).at(1);
        if (const J* s = root.find("part_size")) pw = nums(*s).at(0), ph = nums(*s).at(1);
        const std::string cs = str_or(root, "color_space", "srgb");
        if (cs != "srgb" && cs != "linear") fail("color_space must be srgb or linear");
        srgb = cs == "srgb";
        auto get = [&](const char* k, J::Type t) {
            const J* v = root.find(k);
            J out;
            out.type = t;
            return v ? *v : out;
        };
        textures = get("textures", J::Object), materials = get("materials", J::Object), panes = get("panes", J::Array);
        groups = get("groups", J::Object), animations = get("animations", J::Object), fonts = get("fonts", J::Array);
        validate();
    }
    std::string texture_file(const std::string& tex) const {
        const J* spec = textures.find(tex);
        if (!spec) fail("unknown texture \"" + tex + "\"");
        const std::string fmt = str_or(*spec, "format", "rgba8");
        const TexFormat* f = format_of(fmt);
        if (!f) fail("texture \"" + tex + "\": format " + fmt + " is not written by the port (rgba8, bc1, bc3)");
        return tex + "^" + f->suffix + ".bflim";
    }
    void each_pane(const J& list, const std::function<void(const J&)>& fn) const {
        for (const J& p : list.arr) {
            fn(p);
            if (const J* c = p.find("children")) each_pane(*c, fn);
        }
    }
    void validate() const;
};

std::vector<std::string> materials_of(const J& p) {  // panes.materials_of
    const std::string kind = str_or(p, "type", "null");
    if (kind == "window") {  // wnd1.materials: sorted set of the frames' and the content's "material"
        std::vector<std::string> out;
        if (const J* fr = p.find("frames"))
            for (const J& f : fr->arr)
                if (const J* m = f.find("material")) out.push_back(m->s);
        if (const J* c = p.find("content"))
            if (const J* m = c->find("material")) out.push_back(m->s);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    if (truthy(p.find("material"))) return {p.at("material").s};
    return {};
}

void Desc::validate() const {
    static const char* kTypes[] = {"null", "picture", "window", "text", "bounding", "parts"};
    std::vector<std::string> names;
    each_pane(panes, [&](const J& p) {
        const std::string t = str_or(p, "type", "null");
        if (std::find(std::begin(kTypes), std::end(kTypes), t) == std::end(kTypes)) fail("pane: unknown type " + t);
        const std::string n = p.at("name").s;
        if (std::find(names.begin(), names.end(), n) != names.end()) fail("duplicate pane name " + n);
        names.push_back(n);
    });
    auto known = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    for (const auto& [g, members] : groups.obj)
        for (const J& m : members.arr)
            if (!known(m.s)) fail("group " + g + ": unknown pane " + m.s);
    for (const auto& [a, spec] : animations.obj) {
        std::vector<std::string> bound;
        if (const J* gs = spec.find("groups"))
            for (const J& g : gs->arr) {
                const J* members = groups.find(g.s);
                if (!members) fail("animation " + a + ": unknown group " + g.s);
                for (const J& m : members->arr) bound.push_back(m.s);
            }
        std::map<std::string, std::vector<std::string>> users;
        each_pane(panes, [&](const J& p) {
            for (const auto& m : materials_of(p)) users[m].push_back(p.at("name").s);
        });
        if (const J* tracks = spec.find("tracks"))
            for (const J& t : tracks->arr) {
                std::vector<std::string> targets;
                if (const J* pn = t.find("pane")) {
                    if (!known(pn->s)) fail("animation " + a + ": unknown pane " + pn->s);
                    targets = {pn->s};
                } else if (const J* m = t.find("material")) {
                    if (!materials.has(m->s)) fail("animation " + a + ": unknown material " + m->s);
                    targets = users[m->s];
                }
                bool in = false;
                for (const auto& x : targets) in |= std::find(bound.begin(), bound.end(), x) != bound.end();
                if (!truthy(spec.find("child_binding")) && !in) fail("animation " + a + ": a track's pane is not in the animation's groups");
            }
    }
    for (const auto& [t, spec] : textures.obj) texture_file(t);
}

// ---------------------------------------------------------------- context.py
struct Ctx;
Bytes mat1_encode(const std::string& mat_name, const J& spec, Ctx& ctx);

struct Ctx {
    const Desc& desc;
    std::vector<std::string> textures, fonts;
    std::vector<std::pair<std::string, Bytes>> materials;
    std::map<std::string, size_t> material_index;
    explicit Ctx(const Desc& d) : desc(d) {}
    static size_t index_of(std::vector<std::string>& v, const std::string& x) {
        auto it = std::find(v.begin(), v.end(), x);
        if (it != v.end()) return size_t(it - v.begin());
        v.push_back(x);
        return v.size() - 1;
    }
    size_t texture(const std::string& tex) {
        const bool file = tex.size() > 6 && tex.compare(tex.size() - 6, 6, ".bflim") == 0;
        return index_of(textures, file ? tex : desc.texture_file(tex));
    }
    size_t font(const std::string& f) { return index_of(fonts, f); }
    size_t material(const std::string& n) {
        auto it = material_index.find(n);
        if (it != material_index.end()) return it->second;
        const J* spec = desc.materials.find(n);
        if (!spec) fail("unknown material " + n);
        const size_t i = materials.size();
        material_index[n] = i;
        materials.push_back({n, {}});
        materials[i].second = mat1_encode(n, *spec, *this);
        return i;
    }
    size_t add_material(const std::string& n, Bytes encoded) {
        auto it = material_index.find(n);
        if (it != material_index.end()) return it->second;
        name(n, 28);
        material_index[n] = materials.size();
        materials.push_back({n, std::move(encoded)});
        return materials.size() - 1;
    }
};

// ---------------------------------------------------------------- sections
Bytes mat1_encode(const std::string& mat_name, const J& spec, Ctx& ctx) {
    static const J kNone;
    const J texs = list_or(spec, "textures", kNone);
    const size_t n = texs.arr.size();
    if (n > 3) fail("material " + mat_name + ": at most 3 textures");
    const uint32_t flags = uint32_t(n | (n << 2) | (n << 4)) | uint32_t(int64_t(num_or(spec, "flags_extra", 0)));
    auto colour = ctx.desc.srgb ? rgba_linear : rgba;
    Buf out;
    out.add(name(mat_name, 28)).add(colour(list_or(spec, "black", number_list({0, 0, 0, 0}))))
        .add(colour(list_or(spec, "white", number_list({255, 255, 255, 255})))).u32(flags);
    for (const J& t : texs.arr) {
        std::vector<double> wrap = nums(list_or(t, "wrap", number_list({4, 4})));
        out.u16(uint32_t(ctx.texture(t.at("texture").s))).u8(uint32_t(wrap[0])).u8(uint32_t(wrap[1]));
    }
    for (const J& t : texs.arr) {
        static const J kEmpty = [] { J o; o.type = J::Object; return o; }();
        const J srt = list_or(t, "srt", kEmpty);
        std::vector<double> tr = nums(list_or(srt, "translate", number_list({0, 0}))), sc = nums(list_or(srt, "scale", number_list({1, 1})));
        out.f32(tr[0]).f32(tr[1]).f32(num_or(srt, "rotate", 0)).f32(sc[0]).f32(sc[1]);
    }
    out.zeros(8 * n);
    const std::string tail = str_or(spec, "raw_tail", "");
    for (size_t i = 0; i + 1 < tail.size(); i += 2) out.u8(uint32_t(std::stoul(tail.substr(i, 2), nullptr, 16)));
    return out.d;
}

Bytes mat1_write(const std::vector<std::pair<std::string, Bytes>>& materials) {
    Buf b;
    b.u16(uint32_t(materials.size())).u16(0);
    const size_t first = 8 + 4 + 4 * materials.size();
    Buf blob;
    for (const auto& m : materials) b.u32(uint32_t(first + blob.size())), blob.add(pad4(m.second));
    b.add(blob.d);
    return section("mat1", b.d);
}

Bytes pan1_body(const J& p, uint32_t extra = 0x02) {
    const uint32_t flags = (bool_or(p, "visible", true) ? 1 : 0) | (bool_or(p, "influence_alpha", false) ? 2 : 0);
    std::vector<double> t = nums(list_or(p, "translate", number_list({0, 0})));
    t.resize(std::max<size_t>(t.size(), 3), 0);
    std::vector<double> r;
    if (const J* rv = p.find("rotate")) r = rv->type == J::Number ? std::vector<double>{0, 0, rv->num} : nums(*rv);
    else r = {0, 0, 0};
    std::vector<double> s = nums(list_or(p, "scale", number_list({1, 1})));
    std::vector<double> size = nums(p.at("size"));
    Buf b;
    b.u8(flags).u8(uint32_t(num_or(p, "origin", 0))).u8(uint32_t(num_or(p, "alpha", 255))).u8(uint32_t(num_or(p, "part_flags", extra)));
    b.add(name(p.at("name").s, 24)).add(name(str_or(p, "user", ""), 8));
    for (int i = 0; i < 3; i++) b.f32(t[i]);
    for (int i = 0; i < 3; i++) b.f32(r[i]);
    for (double v : s) b.f32(v);
    b.f32(size.at(0)).f32(size.at(1));
    return b.d;
}

const J& full_uvs() {
    static const J v = [] {
        J a;
        a.type = J::Array;
        for (auto [u, w] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) a.arr.push_back(number_list({double(u), double(w)}));
        return a;
    }();
    return v;
}
void put_uvs(Buf& b, const J& uvs) {
    for (const J& set : uvs.arr)
        for (const J& uv : set.arr) b.f32(num(uv.arr.at(0))).f32(num(uv.arr.at(1)));
}
J repeat(const J& x, size_t n) {
    J a;
    a.type = J::Array;
    a.arr.assign(n, x);
    return a;
}
size_t material_textures(const Desc& d, const std::string& m) {
    const J* spec = d.materials.find(m);
    const J* t = spec ? spec->find("textures") : nullptr;
    return t ? t->arr.size() : 0;
}

Bytes pic1_write(const J& p, Ctx& ctx) {
    const std::string m = p.at("material").s;
    const size_t mat = ctx.material(m);
    const size_t ntex = material_textures(ctx.desc, m);
    const J uvs = list_or(p, "uvs", repeat(full_uvs(), std::max<size_t>(ntex, 1)));
    const J vc = list_or(p, "vertex_colors", repeat(number_list({255, 255, 255, 255}), 4));
    Buf b;
    b.add(pan1_body(p));
    for (const J& c : vc.arr) b.add(rgba(c));
    b.u16(uint32_t(mat)).u8(uint32_t(uvs.arr.size())).u8(0);
    put_uvs(b, uvs);
    return b.d;
}

std::vector<int64_t> sizes4(const J* v, const std::string& what, int64_t lo, int64_t hi) {
    std::vector<double> x = v ? nums(*v) : std::vector<double>{0};
    if (x.size() == 1 && (!v || v->type == J::Number)) { const double v0 = x[0]; x.assign(4, v0); }
    if (x.size() != 4) fail(what + ": 4 values expected");
    std::vector<int64_t> out;
    for (double d : x) {
        if (int64_t(d) < lo || int64_t(d) > hi) fail(what + ": out of range");
        out.push_back(int64_t(d));
    }
    return out;
}

Bytes wnd1_write(const J& p, Ctx& ctx) {
    static const char* kSuffix[] = {"LT", "RT", "LB", "RB", "L", "R", "T", "B"};
    static const std::map<std::string, uint32_t> kFlips = {{"none", 0}, {"flip_h", 1}, {"flip_v", 2}, {"rot90", 3}, {"rot180", 4}, {"rot270", 5}};
    const std::string nm = p.at("name").s;
    const J* frames = p.find("frames");
    if (!frames || (frames->arr.size() != 1 && frames->arr.size() != 4 && frames->arr.size() != 8)) fail("window " + nm + ": 1, 4 or 8 frames expected");
    const auto fsize = sizes4(p.find("frame_size"), "window " + nm + " frame_size", 0, 0xFFFF);
    static const J kEmpty = [] { J o; o.type = J::Object; return o; }();
    const J content = list_or(p, "content", kEmpty);
    const auto infl = sizes4(content.find("inflation"), "window " + nm + " inflation", -0x8000, 0x7FFF);
    auto material = [&](const J& spec, const std::string& auto_name) -> std::pair<size_t, size_t> {
        if (const J* m = spec.find("material")) return {ctx.material(m->s), material_textures(ctx.desc, m->s)};
        J m;
        m.type = J::Object;
        J texs;
        texs.type = J::Array;
        if (const J* t = spec.find("texture")) {
            J e;
            e.type = J::Object;
            e.obj.push_back({"texture", *t});
            e.obj.push_back({"wrap", list_or(spec, "wrap", number_list({4, 4}))});
            texs.arr.push_back(e);
        }
        const size_t n = texs.arr.size();
        m.obj.push_back({"textures", texs});
        m.obj.push_back({"black", list_or(spec, "black", number_list({0, 0, 0, 0}))});
        m.obj.push_back({"white", list_or(spec, "white", number_list({255, 255, 255, 255}))});
        Bytes enc = mat1_encode(auto_name, m, ctx);
        return {ctx.add_material(auto_name, std::move(enc)), n};
    };
    const auto [cmat, ctex] = material(content, nm + "C");
    std::vector<std::pair<size_t, uint32_t>> fmats;
    for (size_t i = 0; i < frames->arr.size(); i++) {
        const J& f = frames->arr[i];
        uint32_t flip = 0;
        if (const J* fl = f.find("flip")) {
            if (fl->type == J::String) {
                auto it = kFlips.find(fl->s);
                if (it == kFlips.end()) fail("window " + nm + ": flip " + fl->s);
                flip = it->second;
            } else flip = uint32_t(to_int(*fl));
        }
        if (flip > 5) fail("window " + nm + ": flip out of range");
        fmats.push_back({material(f, nm + kSuffix[i]).first, flip});
    }
    const J uvs = list_or(content, "uvs", repeat(full_uvs(), ctex));
    const J vc = list_or(content, "vertex_colors", repeat(number_list({255, 255, 255, 255}), 4));
    uint32_t flags;
    if (const J* wf = p.find("window_flags")) flags = uint32_t(to_int(*wf)) & 0xFF;
    else {
        const std::string kind = str_or(p, "kind", "around");
        if (kind != "around" && kind != "horizontal") fail("window " + nm + ": unknown kind " + kind);
        flags = 0x01 | ((kind == "horizontal" ? 2u : 0u) << 2) | (bool_or(p, "draw_content", true) ? 0 : 0x10);
    }
    const Bytes pane = pan1_body(p);
    const size_t content_off = 8 + pane.size() + 0x1C;
    Buf cb;
    for (const J& c : vc.arr) cb.add(rgba(c));
    cb.u16(uint32_t(cmat)).u8(uint32_t(uvs.arr.size())).u8(0);
    put_uvs(cb, uvs);
    const size_t table_off = content_off + cb.size(), first = table_off + 4 * fmats.size();
    Buf b;
    b.add(pane);
    for (auto v : infl) b.i16(int32_t(v));
    for (auto v : fsize) b.u16(uint32_t(v));
    b.u8(uint32_t(fmats.size())).u8(flags).u16(0).u32(uint32_t(content_off)).u32(uint32_t(table_off));
    b.add(cb.d);
    for (size_t i = 0; i < fmats.size(); i++) b.u32(uint32_t(first + 4 * i));
    for (auto [m, flip] : fmats) b.u16(uint32_t(m)).u8(flip).u8(0);
    return b.d;
}

Bytes txt1_material(const J& p) {
    static const J kEmpty = [] { J o; o.type = J::Object; return o; }();
    const J m = list_or(p, "text_material", kEmpty);
    Buf out;
    out.add(name(str_or(p, "material_name", p.at("name").s), 28));
    out.add(rgba(list_or(m, "black", number_list({0, 0, 0, 0})))).add(rgba(list_or(m, "white", number_list({255, 255, 255, 255}))));
    const J* sh = p.find("shadow");
    if (sh && sh->type != J::Null && bool_or(*sh, "enabled", true) && (sh->has("black") || sh->has("white"))) {
        out.u32(0x20000);
        std::vector<double> black = nums(list_or(*sh, "black", number_list({0, 0, 0})));
        for (int i = 0; i < 3; i++) out.u8(uint32_t(int64_t(black.at(i)) & 0xFF));
        out.add(rgba(list_or(*sh, "white", number_list({255, 255, 255, 255})))).u8(0);
    } else out.u32(0);
    return out.d;
}

Bytes txt1_write(const J& p, Ctx& ctx) {
    static const std::map<std::string, int> kH = {{"center", 0}, {"left", 1}, {"right", 2}}, kV = {{"center", 0}, {"top", 1}, {"bottom", 2}},
                                            kLine = {{"pane", 0}, {"left", 1}, {"center", 2}, {"right", 3}};
    const Bytes text = utf16be(str_or(p, "text", ""));
    const int64_t chars = int64_t(text.size() / 2) - 1;
    const int64_t capacity = std::max<int64_t>(p.has("capacity") ? to_int(p.at("capacity")) : std::max<int64_t>(chars, 32), chars);
    if (2 * (capacity + 1) > 0xFFFF) fail("text pane " + p.at("name").s + ": capacity too large");
    const size_t mat = ctx.add_material(str_or(p, "material_name", p.at("name").s), txt1_material(p));
    const size_t font = ctx.font(str_or(p, "font", "CKingMsg.bffnt"));
    const J* sh = p.find("shadow");
    const bool shadow = sh && sh->type != J::Null && bool_or(*sh, "enabled", true);
    const uint32_t flags = uint32_t(num_or(p, "text_flags", 0)) | (shadow ? 1u : 0u);
    auto lookup = [](const std::map<std::string, int>& m, const J& v) {
        if (v.type == J::String) {
            auto it = m.find(v.s);
            if (it == m.end()) fail("text pane: unknown value " + v.s);
            return it->second;
        }
        return int(to_int(v));
    };
    int pos = 0;
    if (const J* tp = p.find("text_position")) pos = tp->type == J::Array ? lookup(kH, tp->arr.at(0)) + 4 * lookup(kV, tp->arr.at(1)) : int(to_int(*tp));
    const int la = p.has("line_alignment") ? lookup(kLine, p.at("line_alignment")) : 0;
    const J colors = list_or(p, "colors", repeat(number_list({255, 255, 255, 255}), 2));
    std::vector<double> fs = nums(list_or(p, "font_size", number_list({46, 54})));
    static const J kEmpty = [] { J o; o.type = J::Object; return o; }();
    const J& s = sh && sh->type == J::Object ? *sh : kEmpty;
    const J shc = list_or(s, "colors", repeat(number_list({0, 0, 0, 255}), 2));
    const J* tid = p.find("text_id");
    const bool has_tid = truthy(tid);
    Buf b;
    b.add(pan1_body(p));
    b.u16(uint32_t(2 * (capacity + 1))).u16(uint32_t(text.size())).u16(uint32_t(mat)).u16(uint32_t(font));
    b.u8(uint32_t(pos)).u8(uint32_t(la)).u8(flags).u8(0);
    b.f32(num_or(p, "italic", 0)).u32(0xA0);
    b.add(rgba(colors.arr.at(0))).add(rgba(colors.arr.at(1)));
    b.f32(fs.at(0)).f32(fs.at(1)).f32(num_or(p, "char_space", 0)).f32(num_or(p, "line_space", 0));
    b.u32(has_tid ? uint32_t(0xA0 + text.size()) : 0);
    for (double v : nums(list_or(s, "offset", number_list({1, 1})))) b.f32(v);
    for (double v : nums(list_or(s, "scale", number_list({1, 1})))) b.f32(v);
    b.add(rgba(shc.arr.at(0))).add(rgba(shc.arr.at(1))).f32(num_or(s, "italic", 0));
    if (b.size() + 8 != 0xA0) fail("text pane: layout error");
    b.add(text);
    if (has_tid) b.add(tid->s).u8(0);
    return b.d;
}

Bytes prt1_write(const J& p) {
    std::vector<double> sc = nums(list_or(p, "part_scale", number_list({1, 1})));
    Buf b;
    b.add(pan1_body(p)).u32(0).f32(sc.at(0)).f32(sc.at(1)).add(cstr4(p.at("part").s));
    return b.d;
}

std::vector<Bytes> usd1_for_pane(const J& p) {
    struct E { std::string name; int kind; const J* value; double scalable; };
    std::vector<E> items;
    static const std::map<std::string, int> kTypes = {{"string", 0}, {"int", 1}, {"float", 2}};
    if (const J* ud = p.find("user_data"))
        for (const J& e : ud->arr) {
            auto it = kTypes.find(str_or(e, "type", "string"));
            if (it == kTypes.end()) fail("user data: unknown type");
            items.push_back({e.at("name").s, it->second, &e.at("value"), 0});
        }
    if (p.has("scalable") && std::none_of(items.begin(), items.end(), [](const E& e) { return e.name == "Scalable"; }))
        items.push_back({"Scalable", 2, nullptr, num(p.at("scalable"))});
    if (items.empty()) return {};
    Buf data;
    struct Off { size_t doff, count; int kind; };
    std::vector<Off> offs;
    for (const E& e : items) {
        if (e.kind == 0) {
            const std::string v = e.value->s;
            data.add(v).u8(0);
            offs.push_back({data.size() - v.size() - 1, v.size(), 0});
            continue;
        }
        std::vector<double> vals = e.value ? nums(*e.value) : std::vector<double>{e.scalable};
        data.pad4();
        offs.push_back({data.size(), vals.size(), e.kind});
        for (double x : vals) e.kind == 1 ? data.u32(uint32_t(int32_t(int64_t(x)))) : data.f32(x);
    }
    Buf names;
    std::vector<size_t> name_offs;
    for (const E& e : items) name_offs.push_back(names.size()), names.add(e.name).u8(0);
    const size_t head = 4 + 12 * items.size();
    Buf body;
    body.u16(uint32_t(items.size())).u16(0);
    for (size_t i = 0; i < items.size(); i++) {
        const size_t entry = 4 + 12 * i;
        body.u32(uint32_t(head + data.size() + name_offs[i] - entry)).u32(uint32_t(head + offs[i].doff - entry)).u16(uint32_t(offs[i].count)).u8(uint32_t(offs[i].kind)).u8(0);
    }
    body.add(data.d).add(names.d);
    return {section("usd1", body.d)};
}

Bytes pane_section(const J& p, Ctx& ctx) {
    const std::string t = str_or(p, "type", "null");
    if (t == "null") return section("pan1", pan1_body(p));
    if (t == "picture") return section("pic1", pic1_write(p, ctx));
    if (t == "window") return section("wnd1", wnd1_write(p, ctx));
    if (t == "text") return section("txt1", txt1_write(p, ctx));
    if (t == "bounding") return section("bnd1", pan1_body(p));
    if (t == "parts") return section("prt1", prt1_write(p));
    fail("unknown pane type " + t);
}

void pane_tree(const J& panes, Ctx& ctx, std::vector<Bytes>& out) {
    for (const J& p : panes.arr) {
        out.push_back(pane_section(p, ctx));
        for (auto& u : usd1_for_pane(p)) out.push_back(std::move(u));
        const J* ch = p.find("children");
        if (ch && !ch->arr.empty()) {
            out.push_back(section("pas1", {}));
            pane_tree(*ch, ctx, out);
            out.push_back(section("pae1", {}));
        }
    }
}

Bytes group(const std::string& n, const std::vector<std::string>& panes) {
    Buf b;
    b.add(name(n, 24)).u16(uint32_t(panes.size())).u16(0);
    for (const auto& p : panes) b.add(name(p, 24));
    return section("grp1", b.d);
}

Bytes bflyt(const Desc& d, Ctx& ctx) {
    for (const J& f : d.fonts.arr) ctx.font(f.s);
    J root;
    root.type = J::Object;
    root.obj.push_back({"name", [] { J s; s.type = J::String; s.s = "RootPane"; return s; }()});
    root.obj.push_back({"size", number_list({d.w, d.h})});
    std::vector<Bytes> tree{section("pan1", pan1_body(root, 0x00))};
    if (!d.panes.arr.empty()) {
        tree.push_back(section("pas1", {}));
        pane_tree(d.panes, ctx, tree);
        tree.push_back(section("pae1", {}));
    }
    Buf l;
    l.u8(1).u8(0).u8(0).u8(0).f32(d.w).f32(d.h).f32(d.pw).f32(d.ph).add(cstr4(d.name));
    std::vector<Bytes> secs{section("lyt1", l.d)};
    auto list = [](const char* magic, const std::vector<std::string>& names) {
        Buf b;
        b.u16(uint32_t(names.size())).u16(0).add(string_table(names));
        return section(magic, b.d);
    };
    if (!ctx.textures.empty()) secs.push_back(list("txl1", ctx.textures));
    if (!ctx.fonts.empty()) secs.push_back(list("fnl1", ctx.fonts));
    if (!ctx.materials.empty()) secs.push_back(mat1_write(ctx.materials));
    secs.insert(secs.end(), tree.begin(), tree.end());
    secs.push_back(group("RootGroup", {}));
    if (!d.groups.obj.empty()) {
        secs.push_back(section("grs1", {}));
        for (const auto& [g, members] : d.groups.obj) {
            std::vector<std::string> m;
            for (const J& x : members.arr) m.push_back(x.s);
            secs.push_back(group(g, m));
        }
        secs.push_back(section("gre1", {}));
    }
    return file_header("FLYT", secs);
}

// ---------------------------------------------------------------- bflan.py, anim/
struct Target { const char* tag; int ctype, target, curve; };
const std::map<std::string, Target>& targets() {
    static const std::map<std::string, Target> t = [] {
        std::map<std::string, Target> m = {
            {"tx", {"FLPA", 0, 0, 2}}, {"ty", {"FLPA", 0, 1, 2}}, {"tz", {"FLPA", 0, 2, 2}}, {"rx", {"FLPA", 0, 3, 2}},
            {"ry", {"FLPA", 0, 4, 2}}, {"rz", {"FLPA", 0, 5, 2}}, {"sx", {"FLPA", 0, 6, 2}}, {"sy", {"FLPA", 0, 7, 2}},
            {"w", {"FLPA", 0, 8, 2}}, {"h", {"FLPA", 0, 9, 2}}, {"alpha", {"FLVC", 0, 16, 2}}, {"visible", {"FLVI", 0, 0, 1}},
            {"tex.pattern", {"FLTP", 1, 0, 1}}};
        const char* corners[] = {"tl", "tr", "bl", "br"};
        const char* ch[] = {"r", "g", "b", "a"};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) m[std::string("vc.") + corners[i] + "." + ch[j]] = {"FLVC", 0, 4 * i + j, 2};
        for (int j = 0; j < 4; j++) m[std::string("black.") + ch[j]] = {"FLMC", 1, j, 2}, m[std::string("white.") + ch[j]] = {"FLMC", 1, 4 + j, 2};
        const char* srt[] = {"tx", "ty", "rotate", "sx", "sy"};
        for (int j = 0; j < 5; j++) m[std::string("tex.") + srt[j]] = {"FLTS", 1, j, 2};
        return m;
    }();
    return t;
}

Bytes hermite_keys(const std::vector<std::vector<double>>& keys, const std::string& interp) {
    Buf b;
    for (size_t i = 0; i < keys.size(); i++) {
        const auto& k = keys[i];
        double slope;
        if (k.size() > 2) slope = k[2];
        else if (interp == "ease" || keys.size() == 1) slope = 0.0;
        else {
            double sum = 0;
            int n = 0;
            if (i > 0) sum += (keys[i][1] - keys[i - 1][1]) / (keys[i][0] - keys[i - 1][0]), n++;
            if (i + 1 < keys.size()) sum += (keys[i + 1][1] - keys[i][1]) / (keys[i + 1][0] - keys[i][0]), n++;
            slope = sum / n;
        }
        b.f32(k[0]).f32(k[1]).f32(slope);
    }
    return b.d;
}

Bytes bflan(const std::string& anim_name, const J& spec, const Desc& d) {
    const int64_t frames = to_int(spec.at("frames")), start = int64_t(num_or(spec, "start", 0));
    std::vector<std::string> textures;
    // (owner, content type) -> [(tag, [entries])], in first-use order
    std::vector<std::pair<std::pair<std::string, int>, std::vector<std::pair<std::string, std::vector<Bytes>>>>> by_target;
    if (const J* tracks = spec.find("tracks"))
        for (const J& t : tracks->arr) {
            std::string tag;
            int ctype, target, curve, index = int(num_or(t, "index", 0));
            if (t.has("tag")) {
                tag = t.at("tag").s;
                ctype = int(num_or(t, "type", t.has("pane") ? 0 : 1));
                target = int(to_int(t.at("target")));
                curve = int(num_or(t, "curve", 2));
            } else {
                const std::string tn = t.at("target").s;
                auto it = targets().find(tn);
                if (it == targets().end()) fail("animation " + anim_name + ": unknown target " + tn);
                tag = it->second.tag, ctype = it->second.ctype, target = it->second.target, curve = it->second.curve;
            }
            if (tag.size() != 4) fail("animation " + anim_name + ": a tag has 4 letters");
            std::vector<std::vector<double>> keys;
            for (const J& k : t.at("keys").arr) {
                std::vector<double> v;
                for (size_t i = 0; i < k.arr.size(); i++) {
                    if (i == 1 && tag == "FLTP") {  // a texture pattern key names a texture
                        const std::string tex = k.arr[1].s;
                        const bool file = tex.size() > 6 && tex.compare(tex.size() - 6, 6, ".bflim") == 0;
                        v.push_back(double(Ctx::index_of(textures, file ? tex : d.texture_file(tex))));
                    } else v.push_back(num(k.arr[i]));
                }
                keys.push_back(v);
            }
            const J* tv = t.find("target");
            if (d.srgb && tv && tv->type == J::String && tv->s.size() == 7 && (tv->s.rfind("black.", 0) == 0 || tv->s.rfind("white.", 0) == 0) &&
                tv->s[6] != 'a')
                for (auto& k : keys) k.at(1) = srgb_to_linear(k[1]);
            Buf e;
            e.u8(uint32_t(index)).u8(uint32_t(target)).u8(uint32_t(curve)).u8(0).u16(uint32_t(keys.size())).u16(0).u32(0xC);
            if (curve == 2) e.add(hermite_keys(keys, str_or(t, "interp", "linear")));
            else
                for (const auto& k : keys) e.f32(k.at(0)).u16(uint32_t(int64_t(k.at(1)))).u16(0);
            const std::string owner = t.has("pane") ? t.at("pane").s : t.at("material").s;
            auto it = std::find_if(by_target.begin(), by_target.end(), [&](auto& x) { return x.first == std::pair{owner, ctype}; });
            if (it == by_target.end()) by_target.push_back({{owner, ctype}, {}}), it = by_target.end() - 1;
            auto tg = std::find_if(it->second.begin(), it->second.end(), [&](auto& x) { return x.first == tag; });
            if (tg == it->second.end()) it->second.push_back({tag, {}}), tg = it->second.end() - 1;
            tg->second.push_back(e.d);
        }
    std::vector<Bytes> contents;
    for (const auto& [key, tags] : by_target) {
        std::vector<Bytes> blocks;
        for (const auto& [tag, entries] : tags) {
            Buf h;
            h.add(tag).u8(uint32_t(entries.size())).u8(0).u8(0).u8(0);
            const size_t first = 8 + 4 * entries.size();
            Buf blob;
            for (const Bytes& e : entries) h.u32(uint32_t(first + blob.size())), blob.add(e);
            blocks.push_back(h.add(blob.d).d);
        }
        Buf c;
        c.add(name(key.first, 28)).u8(uint32_t(blocks.size())).u8(uint32_t(key.second)).u8(0).u8(0);
        const size_t first = 32 + 4 * blocks.size();
        Buf blob;
        for (const Bytes& bl : blocks) c.u32(uint32_t(first + blob.size())), blob.add(pad4(bl));
        contents.push_back(c.add(blob.d).d);
    }
    // pat1
    std::vector<std::string> groups;
    if (const J* gs = spec.find("groups"))
        for (const J& g : gs->arr) groups.push_back(g.s);
    const Bytes nm = cstr4(anim_name);
    Buf pat;
    pat.u16(uint32_t(num_or(spec, "order", 0))).u16(uint32_t(groups.size())).u32(0x1C).u32(uint32_t(0x1C + nm.size()))
        .i16(int32_t(start)).i16(int32_t(start + frames)).u8(uint32_t(num_or(spec, "child_binding", 0))).u8(0).u16(0).add(nm);
    for (const auto& g : groups) pat.add(name(g, 28));
    // pai1
    const Bytes texlist = textures.empty() ? Bytes{} : string_table(textures);
    const size_t table_off = 20 + texlist.size();
    Buf pai;
    pai.u16(uint32_t(frames)).u8(bool_or(spec, "loop", false) ? 1 : 0).u8(0).u16(uint32_t(textures.size())).u16(uint32_t(contents.size()))
        .u32(uint32_t(table_off)).add(texlist);
    const size_t first = table_off + 4 * contents.size();
    Buf blob;
    for (const Bytes& c : contents) pai.u32(uint32_t(first + blob.size())), blob.add(pad4(c));
    pai.add(blob.d);
    return file_header("FLAN", {section("pat1", pat.d), section("pai1", pai.d)});
}

// ---------------------------------------------------------------- bflim.py, gx2tile.py (RGBA8)
struct Surface { uint32_t pitch, height, size, align; };
Surface surface_size(uint32_t w, uint32_t h, uint32_t bpp, int mode) {
    if (mode == 1) {
        const uint32_t a = std::max(64u, 256u * 8 / bpp), pitch = (w + a - 1) / a * a;
        return {pitch, h, pitch * h * bpp / 8, 0x100};
    }
    if (mode == 2) {
        const uint32_t pitch = (w + 7) / 8 * 8, height = (h + 7) / 8 * 8;
        return {pitch, height, pitch * height * bpp / 8, 256};
    }
    const uint32_t pitch = (w + 31) / 32 * 32, height = (h + 15) / 16 * 16;
    return {pitch, height, pitch * height * bpp / 8, 32 * 16 * bpp / 8};
}
uint32_t address(uint32_t x, uint32_t y, uint32_t pitch, int mode, uint32_t swizzle, uint32_t bpp = 32) {  // gx2tile.addresses
    const uint32_t bytes = bpp / 8;
    if (mode == 1) return (x + pitch * y) * bytes;
    auto bit = [](uint32_t v, int i) { return (v >> i) & 1; };
    uint32_t idx;  // the element's place in its 8 x 8 micro tile (gx2tile._pixel_index)
    if (bpp == 64) idx = bit(x, 0) | bit(y, 0) << 1 | bit(x, 1) << 2 | bit(x, 2) << 3 | bit(y, 1) << 4 | bit(y, 2) << 5;
    else if (bpp == 128) idx = bit(y, 0) | bit(x, 0) << 1 | bit(x, 1) << 2 | bit(x, 2) << 3 | bit(y, 1) << 4 | bit(y, 2) << 5;
    else idx = bit(x, 0) | bit(x, 1) << 1 | bit(y, 0) << 2 | bit(x, 2) << 3 | bit(y, 1) << 4 | bit(y, 2) << 5;
    const uint32_t pix = idx * bytes;
    if (mode == 2) return pix + 64 * bytes * ((x >> 3) + (pitch >> 3) * (y >> 3));
    uint32_t pipe = ((y >> 3) ^ (x >> 3)) & 1, bank = (y >> 4) & 3;
    bank = ((bank >> 1) | (bank << 1)) & 3;
    bank = (bank ^ (x >> 3)) & 3;
    uint32_t bank_pipe = ((pipe + 2 * bank) ^ (((swizzle >> 8) & 1) + 2 * ((swizzle >> 9) & 3))) % 8;
    pipe = bank_pipe % 2, bank = bank_pipe / 2;
    const uint32_t macro_bytes = 32 * 16 * bytes, macro_off = ((x / 32) + (pitch / 32) * (y / 16)) * macro_bytes;
    uint32_t off = (macro_off >> 3) + pix;
    off = ((off & ~0xFFu) << 3) | (off & 0xFF);
    return off | (pipe << 8) | (bank << 9);
}

// ---- bc.py: BC1 / BC3 in integers (the same bytes as the generator) ----
using Px = std::array<int, 4>;
uint32_t q565(const int c[3]) {
    return uint32_t((c[0] * 31 + 127) / 255) << 11 | uint32_t((c[1] * 63 + 127) / 255) << 5 | uint32_t((c[2] * 31 + 127) / 255);
}
void x565(uint32_t v, int out[3]) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (r << 3) | (r >> 2), out[1] = (g << 2) | (g >> 4), out[2] = (b << 3) | (b >> 2);
}
int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }  // Python's //
void colour_block(const Px* px, const bool* transparent, uint8_t out[8]) {
    int use[16], n = 0;
    for (int i = 0; i < 16; i++)
        if (!(transparent && transparent[i])) use[n++] = i;
    if (transparent && !n) {
        const uint8_t all[8] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(out, all, 8);
        return;
    }
    int mn[3], mx[3], mean[3];
    for (int c = 0; c < 3; c++) {
        mn[c] = 255, mx[c] = 0;
        int sum = 0;
        for (int k = 0; k < n; k++) mn[c] = std::min(mn[c], px[use[k]][c]), mx[c] = std::max(mx[c], px[use[k]][c]), sum += px[use[k]][c];
        mean[c] = sum / n;
        const int d = (mx[c] - mn[c]) >> 4;
        mn[c] += d, mx[c] -= d;
    }
    int p = 0;
    for (int c = 1; c < 3; c++)
        if (mx[c] - mn[c] > mx[p] - mn[p]) p = c;
    int hi[3] = {mx[0], mx[1], mx[2]}, lo[3] = {mn[0], mn[1], mn[2]};
    for (int c = 0; c < 3; c++) {
        if (c == p) continue;
        long cov = 0;
        for (int k = 0; k < n; k++) cov += long(px[use[k]][p] - mean[p]) * (px[use[k]][c] - mean[c]);
        if (cov < 0) hi[c] = mn[c], lo[c] = mx[c];
    }
    const uint32_t a = q565(hi), b = q565(lo);
    const bool three = transparent && n < 16;
    const uint32_t e0 = three ? std::min(a, b) : std::max(a, b), e1 = three ? std::max(a, b) : std::min(a, b);
    int pal[4][3], count;
    x565(e0, pal[0]), x565(e1, pal[1]);
    if (three) {
        for (int c = 0; c < 3; c++) pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
        count = 3;
    } else if (e0 == e1) {
        count = 1;
    } else {
        for (int c = 0; c < 3; c++) pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3, pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
        count = 4;
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t k = 3;
        if (!(transparent && transparent[i])) {
            int best = -1;
            k = 0;
            for (int j = 0; j < count; j++) {
                int d = 0;
                for (int c = 0; c < 3; c++) d += (px[i][c] - pal[j][c]) * (px[i][c] - pal[j][c]);
                if (best < 0 || d < best) best = d, k = uint32_t(j);
            }
        }
        bits |= k << (2 * i);
    }
    out[0] = uint8_t(e0), out[1] = uint8_t(e0 >> 8), out[2] = uint8_t(e1), out[3] = uint8_t(e1 >> 8);
    for (int i = 0; i < 4; i++) out[4 + i] = uint8_t(bits >> (8 * i));
}
void channel_block(const int v[16], uint8_t out[8]) {
    int mx = 0, mn = 255;
    for (int i = 0; i < 16; i++) mx = std::max(mx, v[i]), mn = std::min(mn, v[i]);
    memset(out, 0, 8);
    out[0] = uint8_t(mx), out[1] = uint8_t(mn);
    if (mx == mn) return;
    static const int w0[8] = {7, 0, 6, 5, 4, 3, 2, 1};
    int pal[8];
    for (int j = 0; j < 8; j++) pal[j] = (mx * w0[j] + mn * (7 - w0[j]) + 3) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int best = -1;
        uint64_t k = 0;
        for (int j = 0; j < 8; j++) {
            const int d = std::abs(v[i] - pal[j]);
            if (best < 0 || d < best) best = d, k = uint64_t(j);
        }
        bits |= k << (3 * i);
    }
    for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(bits >> (8 * i));
}
// the texels as 4 x 4 blocks, row by row, edges repeated (bc.py to_blocks), each block coded
// bflim.py _to_channels for "la": luminance (0.299 / 0.587 / 0.114, float32, rounded) and alpha
int luminance(const Px& t) {
    const float l = float(t[0]) * 0.299f + float(t[1]) * 0.587f + float(t[2]) * 0.114f;
    return int(std::clamp(std::nearbyint(l), 0.0f, 255.0f));
}
Bytes bc_encode(const std::string& fmt, const Bytes& rgba, uint32_t w, uint32_t h) {
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = fmt == "bc1" ? 8 : 16;
    Bytes out(size_t(bw) * bh * bytes);
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            Px px[16];
            for (int i = 0; i < 16; i++) {
                const uint32_t x = std::min(bx * 4 + uint32_t(i % 4), w - 1), y = std::min(by * 4 + uint32_t(i / 4), h - 1);
                const uint8_t* t = &rgba[(size_t(y) * w + x) * 4];
                px[i] = {t[0], t[1], t[2], t[3]};
            }
            uint8_t* o = &out[(size_t(by) * bw + bx) * bytes];
            if (fmt == "bc5la") {  // luminance (BC4), then alpha (BC4)
                int l[16], a[16];
                for (int i = 0; i < 16; i++) l[i] = luminance(px[i]), a[i] = px[i][3];
                channel_block(l, o);
                channel_block(a, o + 8);
            } else if (fmt == "bc1") {
                bool tr[16];
                for (int i = 0; i < 16; i++) tr[i] = px[i][3] < 128;
                colour_block(px, tr, o);
            } else {  // bc3: alpha (BC4), then the colour of the visible texels (4-colour mode)
                int a[16], vis = -1, nvis = 0;
                for (int i = 0; i < 16; i++) {
                    a[i] = px[i][3];
                    if (px[i][3] > 0) nvis++, vis = vis < 0 ? i : vis;
                }
                channel_block(a, o);
                Px c[16];
                for (int i = 0; i < 16; i++) c[i] = (nvis == 0 || nvis == 16 || px[i][3] > 0) ? px[i] : px[vis];
                colour_block(c, nullptr, o + 8);
            }
        }
    return out;
}

Bytes bflim(const Bytes& rgba, uint32_t w, uint32_t h, const std::string& fmt, const std::string& tile, uint32_t swizzle) {
    const TexFormat* f = format_of(fmt, true);
    if (!f) fail("format " + fmt + " is not written by the port");
    const int mode = tile == "linear" ? 1 : tile == "1d" ? 2 : tile == "tiled" ? 4 : 0;
    if (!mode) fail("unknown tile mode " + tile);
    if (swizzle > 7 || (swizzle && mode != 4)) fail("swizzle needs the tiled mode and 0-7");
    const Bytes data = f->bc ? bc_encode(fmt, rgba, w, h) : rgba;
    const uint32_t ew = f->bc ? (w + 3) / 4 : w, eh = f->bc ? (h + 3) / 4 : h, eb = f->bpp / 8;
    const Surface s = surface_size(ew, eh, f->bpp, mode);
    Buf b;
    b.d.assign(s.size, 0);
    for (uint32_t y = 0; y < eh; y++)
        for (uint32_t x = 0; x < ew; x++) memcpy(&b.d[address(x, y, s.pitch, mode, swizzle << 8, f->bpp)], &data[(size_t(y) * ew + x) * eb], eb);
    b.add(std::string("FLIM")).u16(0xFEFF).u16(0x14).u32(kVersion).u32(s.size + 0x28).u16(1).u16(0);
    b.add(std::string("imag")).u32(0x10).u16(w).u16(h).u16(s.align).u8(f->code).u8(uint32_t(mode) | (swizzle << 5)).u32(s.size);
    return b.d;
}

// archive.py: the texture's pixels (fill or PNG at its own size)
Bytes texture_pixels(const std::string& tex, const J& spec, const Desc& d, uint32_t& w, uint32_t& h) {
    if (const J* f = spec.find("fill")) {
        w = uint32_t(to_int(spec.at("width"))), h = uint32_t(to_int(spec.at("height")));
        if (!w || !h || w > 4096 || h > 4096) fail("texture " + tex + ": bad fill size");
        auto colour = [](const J& c) {  // PIL: a 3-tuple is opaque
            std::vector<double> v = nums(c);
            if (v.size() == 3) v.push_back(255);
            return rgba_of(v);
        };
        const Bytes fill = colour(f->at("color"));
        Bytes px(size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; i++) memcpy(&px[i * 4], fill.data(), 4);
        const int64_t n = int64_t(num_or(*f, "border_width", 0));
        if (n) {
            const Bytes border = colour(f->at("border"));
            for (int64_t i = 0; i < n; i++) {
                const int64_t x0 = i, y0 = i, x1 = int64_t(w) - 1 - i, y1 = int64_t(h) - 1 - i;
                if (x1 < x0 || y1 < y0) break;
                for (int64_t y = y0; y <= y1; y++)
                    for (int64_t x = x0; x <= x1; x++)
                        if (x == x0 || x == x1 || y == y0 || y == y1) memcpy(&px[(size_t(y) * w + size_t(x)) * 4], border.data(), 4);
            }
        }
        return px;
    }
    const guestmods::hud::Pixels png = guestmods::hud::load_png(d.art_root, spec.at("png").s);
    w = png.width, h = png.height;
    const int64_t want_w = spec.has("width") ? to_int(spec.at("width")) : int64_t(w);
    const int64_t want_h = spec.has("height") ? to_int(spec.at("height")) : std::max<int64_t>(1, std::llround(double(h) * double(want_w) / double(w)));
    if (want_w != int64_t(w) || want_h != int64_t(h))
        fail("texture " + tex + ": the port doesn't resize art; give it at " + std::to_string(want_w) + "x" + std::to_string(want_h));
    return png.rgba;
}

// notebook_model.py sarc / yaz0_stored
Bytes sarc(const std::map<std::string, Bytes>& files, uint32_t align = 0x2000) {
    auto hash = [](const std::string& n) {
        uint32_t h = 0;
        for (unsigned char c : n) h = h * 0x65 + c;
        return h;
    };
    std::vector<std::pair<std::string, const Bytes*>> entries;
    for (const auto& [n, b] : files) entries.push_back({n, &b});
    std::stable_sort(entries.begin(), entries.end(), [&](auto& a, auto& b) { return hash(a.first) < hash(b.first); });
    Buf names, data, sfat;
    std::vector<uint32_t> noffs;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (auto& [n, _] : entries) noffs.push_back(uint32_t(names.size())), names.add(n).u8(0).pad4();
    for (auto& [n, b] : entries) {
        data.zeros((align - data.size() % align) % align);
        spans.push_back({uint32_t(data.size()), uint32_t(data.size() + b->size())});
        data.add(*b);
    }
    sfat.add(std::string("SFAT")).u16(0xC).u16(uint32_t(entries.size())).u32(0x65);
    for (size_t i = 0; i < entries.size(); i++) sfat.u32(hash(entries[i].first)).u32(0x01000000 | (noffs[i] / 4)).u32(spans[i].first).u32(spans[i].second);
    Buf sfnt;
    sfnt.add(std::string("SFNT")).u16(8).u16(0).add(names.d);
    const size_t head_len = 0x14 + sfat.size() + sfnt.size(), doff = (head_len + align - 1) / align * align;
    Buf out;
    out.add(std::string("SARC")).u16(0x14).u16(0xFEFF).u32(uint32_t(doff + data.size())).u32(uint32_t(doff)).u16(0x0100).u16(0);
    out.add(sfat.d).add(sfnt.d).zeros(doff - head_len).add(data.d);
    return out.d;
}
Bytes yaz0_stored(const Bytes& data) {
    Buf out;
    out.add(std::string("Yaz0")).u32(uint32_t(data.size())).zeros(8);
    for (size_t i = 0; i < data.size(); i += 8) {
        out.u8(0xFF);
        out.d.insert(out.d.end(), data.begin() + i, data.begin() + std::min(data.size(), i + 8));
    }
    return out.d;
}

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}
bool read_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

}  // namespace

std::map<std::string, Bytes> build_files(const std::string& description_json, const std::filesystem::path& art_root) {
    const Desc d(description_json, art_root);
    Ctx ctx(d);
    std::map<std::string, Bytes> files;
    files["blyt/" + d.name + ".bflyt"] = bflyt(d, ctx);
    for (const auto& [tex, spec] : d.textures.obj) {
        uint32_t w = 0, h = 0;
        const Bytes px = texture_pixels(tex, spec, d, w, h);
        files["timg/" + d.texture_file(tex)] =
            bflim(px, w, h, str_or(spec, "format", "rgba8"), str_or(spec, "tile", "linear"), uint32_t(num_or(spec, "swizzle", 0)));
    }
    for (const auto& [a, spec] : d.animations.obj) files["anim/" + d.name + "_" + a + ".bflan"] = bflan(a, spec, d);
    return files;
}

Bytes build(const std::string& description_json, const std::filesystem::path& art_root) {
    const Bytes archive = sarc(build_files(description_json, art_root));
    // The game's layout cache holds the archive in its layout heap: in game runs about 14 MB loaded and
    // 17 MB crashed the game in hdLayoutResMgr_c::cache. A description that large is refused here (the
    // page is then drawn on the HUD) instead (kMaxArchive: a safe margin under that).
    if (archive.size() > kMaxArchive)
        fail("the archive would be " + std::to_string(archive.size() >> 20) + " MB, more than the layout cache's safe " +
             std::to_string(kMaxArchive >> 20) + " MB: compress its large pictures (bc1 / bc3)");
    return yaz0_stored(archive);
}

Bytes texture(const Bytes& rgba, uint32_t w, uint32_t h, const std::string& format, const std::string& tile, uint32_t swizzle) {
    if (rgba.size() != size_t(w) * h * 4) fail("texture: expected " + std::to_string(size_t(w) * h * 4) + " RGBA bytes");
    return bflim(rgba, w, h, format, tile, swizzle);
}
Bytes sarc_of(const std::map<std::string, Bytes>& files, uint32_t align) { return sarc(files, align); }
Bytes yaz0_store(const Bytes& data) { return yaz0_stored(data); }

Bytes tile_rgba8(const Bytes& rgba, uint32_t w, uint32_t h, int mode, uint32_t swizzle) {
    if (rgba.size() != size_t(w) * h * 4 || (mode != 1 && mode != 2 && mode != 4)) return {};
    const Surface s = surface_size(w, h, 32, mode);
    Bytes out(s.size, 0);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) memcpy(&out[address(x, y, s.pitch, mode, swizzle << 8)], &rgba[(size_t(y) * w + x) * 4], 4);
    return out;
}

Bytes untile(const Bytes& surface, uint32_t ew, uint32_t eh, uint32_t bpp, int mode, uint32_t swizzle) {
    if ((mode != 1 && mode != 2 && mode != 4) || (bpp != 32 && bpp != 64 && bpp != 128) || !ew || !eh || ew > 8192 || eh > 8192)
        return {};
    const Surface s = surface_size(ew, eh, bpp, mode);
    if (surface.size() < s.size) return {};
    const uint32_t eb = bpp / 8;
    Bytes out(size_t(ew) * eh * eb);
    for (uint32_t y = 0; y < eh; y++)
        for (uint32_t x = 0; x < ew; x++) memcpy(&out[(size_t(y) * ew + x) * eb], &surface[address(x, y, s.pitch, mode, swizzle << 8, bpp)], eb);
    return out;
}

std::string layout_name(const std::string& description_json) {
    try {
        const J root = Reader(description_json).parse();
        return str_or(root, "name", "");
    } catch (const std::exception&) {
        return "";
    }
}

std::string build_cached(const std::filesystem::path& description, const std::filesystem::path& cache_dir) {
    namespace fs = std::filesystem;
    try {
        std::string text;
        if (!read_file(description, text)) {
            LOG("[lyt] %s: can't be read", description.string().c_str());
            return "";
        }
        // the hash: this writer, the description and every PNG it names (in the order written)
        uint64_t h = fnv(0xCBF29CE484222325ull, kWriterVersion, strlen(kWriterVersion));
        h = fnv(h, text.data(), text.size());
        const J root = Reader(text).parse();
        if (const J* t = root.find("textures"))
            for (const auto& [name, spec] : t->obj)
                if (const J* png = spec.find("png")) {
                    std::string bytes;
                    read_file(description.parent_path() / png->s, bytes);
                    h = fnv(h, png->s.data(), png->s.size());
                    h = fnv(h, bytes.data(), bytes.size());
                }
        char tag[17];
        snprintf(tag, sizeof tag, "%016llx", (unsigned long long)h);
        const fs::path out = cache_dir / (str_or(root, "name", "layout") + "-" + tag + ".szs");
        if (fs::is_regular_file(out)) return out.string();
        const Bytes szs = build(text, description.parent_path());
        fs::create_directories(cache_dir);
        const fs::path tmp = out.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            f.write(reinterpret_cast<const char*>(szs.data()), std::streamsize(szs.size()));
            if (!f) {
                LOG("[lyt] %s: can't be written", tmp.string().c_str());
                return "";
            }
        }
        fs::rename(tmp, out);
        LOG("[lyt] built %s (%zu bytes) from %s", out.string().c_str(), szs.size(), description.string().c_str());
        return out.string();
    } catch (const std::exception& e) {
        LOG("[lyt] %s: not built: %s", description.string().c_str(), e.what());
        return "";
    }
}

}  // namespace lyt_build
