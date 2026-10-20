// SPDX-License-Identifier: MPL-2.0
#include "demo_stb.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace demo_stb {
namespace {
uint32_t be32(const Bytes& d, size_t p) { return uint32_t(d[p]) << 24 | uint32_t(d[p + 1]) << 16 | uint32_t(d[p + 2]) << 8 | d[p + 3]; }
uint16_t be16(const Bytes& d, size_t p) { return uint16_t(d[p] << 8 | d[p + 1]); }
void put32(Bytes& d, uint32_t v) { for (int i = 3; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i))); }
void put16(Bytes& d, uint16_t v) { d.push_back(uint8_t(v >> 8)); d.push_back(uint8_t(v)); }
void set32(Bytes& d, size_t p, uint32_t v) { for (int i = 0; i < 4; ++i) d[p + i] = uint8_t(v >> (24 - 8 * i)); }
void pad4(Bytes& d) { while (d.size() % 4) d.push_back(0); }
size_t al4(size_t n) { return (n + 3) & ~size_t(3); }
float as_float(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
uint32_t as_word(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
Bytes floats(std::initializer_list<float> values) { Bytes b; for (float f : values) put32(b, as_word(f)); return b; }

// A paragraph at p, inside [p, end). Returns the next position or 0 on error.
size_t read_paragraph(const Bytes& d, size_t p, size_t end, Paragraph& out) {
    if (p + 4 > end) return 0;
    uint32_t size = be16(d, p), type = be16(d, p + 2);
    size_t content = p + 4;
    out.long_header = (size & 0x8000) != 0;
    if (out.long_header) {
        if (p + 8 > end) return 0;
        size = ((size & 0x7FFF) << 16) | type;
        type = be32(d, p + 4);
        content = p + 8;
    }
    if (content + size > end) return 0;
    out.type = type;
    out.data.assign(d.begin() + long(content), d.begin() + long(content + size));
    const size_t next = content + al4(size);
    return next <= end ? next : 0;
}
void write_paragraph(Bytes& d, const Paragraph& p) {
    const uint32_t size = uint32_t(p.data.size());
    if (p.long_header || size >= 0x8000 || p.type >= 0x10000) {
        put16(d, uint16_t(0x8000 | (size >> 16))); put16(d, uint16_t(size)); put32(d, p.type);
    } else {
        put16(d, uint16_t(size)); put16(d, uint16_t(p.type));
    }
    d.insert(d.end(), p.data.begin(), p.data.end());
    pad4(d);
}
bool fail(std::string& why, const std::string& text) { why = text; return false; }

bool parse_fvb(const Bytes& d, size_t at, size_t end, File& f, std::string& why) {
    if (end - at < 16 || std::memcmp(&d[at], "FVB", 4) != 0 || be16(d, at + 4) != 0xFEFF) return fail(why, "bad FVB header");
    f.fvb_version = be16(d, at + 6);
    const uint32_t count = be32(d, at + 12);
    if (count > 4096) return fail(why, "too many FVB functions");
    size_t p = at + 16;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 8 > end) return fail(why, "FVB function outside its block");
        const uint32_t size = be32(d, p);
        if (size < 8 || p + size > end || size % 4) return fail(why, "bad FVB function size");
        Function fn;
        fn.type = be16(d, p + 4);
        const uint16_t id_size = be16(d, p + 6);
        if (8 + al4(id_size) > size) return fail(why, "bad FVB id");
        fn.id.assign(d.begin() + long(p + 8), d.begin() + long(p + 8 + id_size));
        size_t q = p + 8 + al4(id_size);
        while (q < p + size) {
            Paragraph para;
            const size_t next = read_paragraph(d, q, p + size, para);
            if (!next) return fail(why, "bad FVB paragraph");
            fn.paragraphs.push_back(std::move(para));
            q = next;
        }
        f.functions.push_back(std::move(fn));
        p += size;
    }
    if (p != end) return fail(why, "FVB block has trailing bytes");
    return true;
}
bool parse_object(const Bytes& d, size_t at, size_t end, Object& o, std::string& why) {
    if (end - at < 12) return fail(why, "object block too short");
    o.type = be32(d, at + 4);
    o.flag = be16(d, at + 8);
    const uint16_t id_size = be16(d, at + 10);
    if (at + 12 + al4(id_size) > end) return fail(why, "object id outside its block");
    o.id.assign(d.begin() + long(at + 12), d.begin() + long(at + 12 + id_size));
    size_t p = at + 12 + al4(id_size);
    while (p < end) {
        if (p + 4 > end) return fail(why, "sequence item outside its block");
        const uint32_t head = be32(d, p);
        Item it;
        it.type = uint8_t(head >> 24);
        it.param = head & 0xFFFFFF;
        p += 4;
        if (it.type >= 0x80) {
            if (p + it.param > end) return fail(why, "paragraphs outside their block");
            const size_t stop = p + it.param;
            while (p < stop) {
                Paragraph para;
                const size_t next = read_paragraph(d, p, stop, para);
                if (!next) return fail(why, "bad paragraph");
                it.paragraphs.push_back(std::move(para));
                p = next;
            }
        }
        o.sequence.push_back(std::move(it));
    }
    return true;
}
}  // namespace

std::string Object::name() const {
    std::string s;
    for (uint8_t c : id) { if (!c) break; s += char(c); }
    return s;
}

bool parse(const Bytes& d, File& f, std::string& why) {
    f = {};
    if (d.size() < 0x20 || std::memcmp(d.data(), "STB", 4) != 0 || be16(d, 4) != 0xFEFF) return fail(why, "not an STB file");
    f.version = be16(d, 6);
    if (f.version < 1 || f.version > 3) return fail(why, "unsupported STB version");
    if (be32(d, 8) != d.size()) return fail(why, "STB size field does not match the data");
    const uint32_t blocks = be32(d, 12);
    f.target.assign(d.begin() + 16, d.begin() + 32);
    size_t p = 0x20;
    for (uint32_t i = 0; i < blocks; ++i) {
        if (p + 8 > d.size()) return fail(why, "block outside the file");
        const uint32_t size = be32(d, p), type = be32(d, p + 4);
        if (size < 8 || p + size > d.size() || size % 4) return fail(why, "bad block size");
        if (type == fourcc("JFVB")) {
            if (f.has_fvb) return fail(why, "two JFVB blocks");
            f.has_fvb = true;
            f.fvb_position = f.objects.size();
            if (!parse_fvb(d, p + 8, p + size, f, why)) return false;
        } else {
            Object o;
            if (!parse_object(d, p, p + size, o, why)) return false;
            f.objects.push_back(std::move(o));
        }
        p += size;
    }
    if (p != d.size()) return fail(why, "STB has trailing bytes");
    return true;
}

Bytes write(const File& f) {
    Bytes out{'S', 'T', 'B', 0};
    put16(out, 0xFEFF); put16(out, f.version);
    put32(out, 0); put32(out, uint32_t(f.objects.size() + (f.has_fvb ? 1 : 0)));
    Bytes target = f.target; target.resize(16);
    out.insert(out.end(), target.begin(), target.end());
    auto fvb = [&] {
        Bytes b{'F', 'V', 'B', 0};
        put16(b, 0xFEFF); put16(b, f.fvb_version); put32(b, 0); put32(b, uint32_t(f.functions.size()));
        for (const auto& fn : f.functions) {
            Bytes blk; put32(blk, 0); put16(blk, fn.type); put16(blk, uint16_t(fn.id.size()));
            blk.insert(blk.end(), fn.id.begin(), fn.id.end()); pad4(blk);
            for (const auto& para : fn.paragraphs) write_paragraph(blk, para);
            set32(blk, 0, uint32_t(blk.size()));
            b.insert(b.end(), blk.begin(), blk.end());
        }
        set32(b, 8, uint32_t(b.size()));
        Bytes blk; put32(blk, uint32_t(8 + b.size())); put32(blk, fourcc("JFVB"));
        blk.insert(blk.end(), b.begin(), b.end());
        out.insert(out.end(), blk.begin(), blk.end());
    };
    for (size_t i = 0; i <= f.objects.size(); ++i) {
        if (f.has_fvb && i == f.fvb_position) fvb();
        if (i == f.objects.size()) break;
        const auto& o = f.objects[i];
        Bytes blk; put32(blk, 0); put32(blk, o.type); put16(blk, o.flag); put16(blk, uint16_t(o.id.size()));
        blk.insert(blk.end(), o.id.begin(), o.id.end()); pad4(blk);
        for (const auto& it : o.sequence) {
            if (it.type < 0x80) { put32(blk, uint32_t(it.type) << 24 | (it.param & 0xFFFFFF)); continue; }
            Bytes paras;
            for (const auto& para : it.paragraphs) write_paragraph(paras, para);
            put32(blk, uint32_t(it.type) << 24 | uint32_t(paras.size()));
            blk.insert(blk.end(), paras.begin(), paras.end());
        }
        set32(blk, 0, uint32_t(blk.size()));
        out.insert(out.end(), blk.begin(), blk.end());
    }
    set32(out, 8, uint32_t(out.size()));
    return out;
}

namespace {
const char* code_name(uint32_t object, uint32_t code) {
    static const std::map<uint32_t, const char*> act = {{9, "TX"}, {10, "TY"}, {11, "TZ"}, {12, "TXYZ"}, {13, "RX"}, {14, "RY"}, {15, "RZ"}, {16, "RXYZ"}, {17, "SX"}, {18, "SY"}, {19, "SZ"}, {20, "SXYZ"},
        {48, "PARENT"}, {49, "PARENT_NODE"}, {50, "PARENT_ENABLE"}, {51, "RELATION"}, {52, "RELATION_NODE"}, {53, "RELATION_ENABLE"}, {57, "SHAPE"}, {58, "ANIMATION"}, {59, "ANIM_FRAME"},
        {67, "ANIM_MODE"}, {75, "TEXANIM_FRAME"}, {76, "TEXANIM"}, {78, "TEXANIM_MODE"}};
    static const std::map<uint32_t, const char*> cam = {{21, "EYE_X"}, {22, "EYE_Y"}, {23, "EYE_Z"}, {24, "EYE_XYZ"}, {25, "TGT_X"}, {26, "TGT_Y"}, {27, "TGT_Z"}, {28, "TGT_XYZ"},
        {38, "ROLL"}, {39, "FOVY"}, {40, "NEAR"}, {41, "FAR"}, {42, "NEARFAR"}, {48, "PARENT"}, {49, "PARENT_NODE"}, {50, "PARENT_ENABLE"}};
    static const std::map<uint32_t, const char*> snd = {{21, "X"}, {22, "Y"}, {23, "Z"}, {24, "XYZ"}, {46, "VOLUME"}, {47, "PAN"}, {56, "LOCATED"}, {60, "SOUND"}, {61, "PITCH"}, {62, "TEMPO"}, {63, "ECHO"}, {64, "DOLBY"}, {65, "DIRECT"}};
    static const std::map<uint32_t, const char*> ptc = {{9, "TX"}, {10, "TY"}, {11, "TZ"}, {12, "TXYZ"}, {13, "RX"}, {14, "RY"}, {15, "RZ"}, {16, "RXYZ"}, {17, "SX"}, {18, "SY"}, {19, "SZ"}, {20, "SXYZ"},
        {29, "R"}, {30, "G"}, {31, "B"}, {32, "A"}, {33, "RGB"}, {34, "RGBA"}, {48, "PARENT"}, {49, "PARENT_NODE"}, {50, "PARENT_ENABLE"}, {68, "PARTICLE"}};
    const std::map<uint32_t, const char*>* table = object == kActor ? &act : object == kCamera ? &cam : object == kSound ? &snd : object == kParticle ? &ptc : nullptr;
    if (object == kMessage && code == 66) return "MESSAGE";
    if (table) { auto it = table->find(code); if (it != table->end()) return it->second; }
    return nullptr;
}
std::string hex(const Bytes& b) { std::string s; char t[4]; for (uint8_t c : b) { std::snprintf(t, sizeof t, "%02x", c); s += t; } return s; }
std::string number(float f) { char t[32]; std::snprintf(t, sizeof t, "%g", double(f)); return t; }
std::string value_text(const Paragraph& p) {
    const uint32_t op = p.type & 0x1F;
    std::string s;
    const auto words = [&](bool as_floats) {
        for (size_t i = 0; i + 4 <= p.data.size(); i += 4) {
            const uint32_t w = be32(p.data, i);
            s += (i ? "," : "");
            s += as_floats ? number(as_float(w)) : std::to_string(w);
        }
    };
    if (op == kImmediate) {
        // a word below 0x10000 that isn't a plausible float is an integer (animation mode, ids)
        const bool word = p.data.size() == 4 && be32(p.data, 0) && be32(p.data, 0) < 0x10000;
        s = word ? "int " : "";
        words(!word);
    } else if (op == kTime) { s = "rate "; words(true); }
    else if (op == kCurveIndex) { s = "curves "; words(false); }
    else if (op == kName) { s = "name "; for (uint8_t c : p.data) { if (!c) break; s += char(c); } }
    else if (op == kValue) { s = "u32 "; words(false); }
    else s = "raw " + hex(p.data);
    return s;
}
}  // namespace

std::string dump(const File& f) {
    std::string out = "STB v" + std::to_string(f.version) + " objects " + std::to_string(f.objects.size()) + " functions " + std::to_string(f.functions.size()) + "\n";
    for (size_t i = 0; i < f.functions.size(); ++i) {
        const auto& fn = f.functions[i];
        out += "fn #" + std::to_string(i) + " type " + std::to_string(fn.type);
        for (const auto& p : fn.paragraphs) {
            if (p.type == 0) continue;
            out += " p" + std::to_string(p.type) + ":";
            if (p.type == 1 && (fn.type == 5 || fn.type == 6) && p.data.size() >= 4) {
                const uint32_t head = be32(p.data, 0);
                const uint32_t count = fn.type == 6 ? (head & 0xFFFFFFF) : head, stride = fn.type == 6 ? (head >> 28) : 2;
                out += "n=" + std::to_string(count) + "/" + std::to_string(stride) + "[";
                for (size_t k = 4; k + 4 <= p.data.size(); k += 4) out += (k > 4 ? "," : "") + number(as_float(be32(p.data, k)));
                out += "]";
            } else if (p.data.size() % 4 == 0 && p.data.size() <= 16 && p.type != 0x16 && p.type != 0x13 && p.type != 0x14 && p.type != 0x15) {
                for (size_t k = 0; k < p.data.size(); k += 4) out += (k ? "," : "") + number(as_float(be32(p.data, k)));
            } else out += hex(p.data);
        }
        out += "\n";
    }
    for (const auto& o : f.objects) {
        char t[5] = {char(o.type >> 24), char(o.type >> 16), char(o.type >> 8), char(o.type), 0};
        out += "== " + std::string(o.type == kControl ? "CTRL" : t) + " \"" + o.name() + "\"\n";
        uint32_t frame = 0;
        for (const auto& it : o.sequence) {
            if (it.type == 0) { out += "  f" + std::to_string(frame) + " end\n"; continue; }
            if (it.type == 2) { frame += it.param; continue; }
            if (it.type < 0x80) { out += "  f" + std::to_string(frame) + " seq " + std::to_string(it.type) + " " + std::to_string(it.param) + "\n"; continue; }
            for (const auto& p : it.paragraphs) {
                if (p.type < 0x100) { out += "  f" + std::to_string(frame) + " reserved " + std::to_string(p.type) + " " + hex(p.data) + "\n"; continue; }
                const char* n = code_name(o.type, p.type >> 5);
                out += "  f" + std::to_string(frame) + " " + (n ? n : ("code" + std::to_string(p.type >> 5)).c_str()) + " = " + value_text(p) + "\n";
            }
        }
    }
    return out;
}

// ---- builder ----
uint32_t Builder::constant(float value) {
    Function fn; fn.type = 2;
    fn.paragraphs.push_back({1, floats({value})});
    fn.paragraphs.push_back({0, {}});
    functions_.push_back(std::move(fn));
    return uint32_t(functions_.size() - 1);
}
uint32_t Builder::list(const std::vector<std::pair<float, float>>& keys, Interpolation interpolation) {
    Function fn; fn.type = 5;
    fn.paragraphs.push_back({0x12, floats({keys.front().first, keys.back().first})});
    Bytes interp; put32(interp, uint32_t(interpolation));
    fn.paragraphs.push_back({0x16, interp});
    Bytes data; put32(data, uint32_t(keys.size()));
    for (const auto& [t, v] : keys) { put32(data, as_word(t)); put32(data, as_word(v)); }
    fn.paragraphs.push_back({1, data});
    fn.paragraphs.push_back({0, {}});
    functions_.push_back(std::move(fn));
    return uint32_t(functions_.size() - 1);
}
uint32_t Builder::hermite(const std::vector<HermiteKey>& keys) {
    Function fn; fn.type = 6;
    fn.paragraphs.push_back({0x12, floats({keys.front().time, keys.back().time})});
    Bytes data; put32(data, uint32_t(keys.size()) | (4u << 28));
    for (const auto& k : keys) for (float v : {k.time, k.value, k.in, k.out}) put32(data, as_word(v));
    fn.paragraphs.push_back({1, data});
    fn.paragraphs.push_back({0, {}});
    functions_.push_back(std::move(fn));
    return uint32_t(functions_.size() - 1);
}
size_t Builder::object(uint32_t type, const std::string& name) { objects_.push_back({type, name, {}, 0}); return objects_.size() - 1; }
void Builder::add(size_t object, uint32_t frame, Paragraph p) { objects_[object].events.push_back({frame, order_++, false, std::move(p)}); }
void Builder::immediate(size_t o, uint32_t frame, uint32_t code, std::initializer_list<float> values) { add(o, frame, {code << 5 | kImmediate, floats(values)}); }
void Builder::immediate_word(size_t o, uint32_t frame, uint32_t code, uint32_t value) { Bytes b; put32(b, value); add(o, frame, {code << 5 | kImmediate, b}); }
void Builder::value(size_t o, uint32_t frame, uint32_t code, uint32_t v) { Bytes b; put32(b, v); add(o, frame, {code << 5 | kValue, b}); }
void Builder::time(size_t o, uint32_t frame, uint32_t code, float rate) { add(o, frame, {code << 5 | kTime, floats({rate})}); }
void Builder::curves(size_t o, uint32_t frame, uint32_t code, std::initializer_list<uint32_t> functions) { Bytes b; for (uint32_t f : functions) put32(b, f); add(o, frame, {code << 5 | kCurveIndex, b}); }
void Builder::name(size_t o, uint32_t frame, uint32_t code, const std::string& text) { Bytes b(text.begin(), text.end()); b.push_back(0); add(o, frame, {code << 5 | kName, b}); }
void Builder::data(size_t o, uint32_t frame, uint32_t id, const Bytes& payload) {
    Bytes b; put16(b, 0); put16(b, 4); put32(b, id); b.insert(b.end(), payload.begin(), payload.end());
    add(o, frame, {0x81, b});
}
void Builder::end(size_t o, uint32_t frame) { objects_[o].end = std::max(objects_[o].end, frame); }
void Builder::suspend(uint32_t frame) { suspends_.push_back(frame); }

Bytes Builder::finish() const {
    File f;
    f.has_fvb = !functions_.empty();
    f.functions = functions_;
    f.fvb_position = 0;
    auto sequence = [](std::vector<Event> events, uint32_t end) {
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.frame != b.frame ? a.frame < b.frame : a.order < b.order; });
        std::vector<Item> seq;
        uint32_t now = 0;
        for (size_t i = 0; i < events.size();) {
            const uint32_t frame = events[i].frame;
            if (frame > now) { seq.push_back({2, frame - now, {}}); now = frame; }
            if (events[i].suspend) {
                // control object: one suspend item per point
                seq.push_back({4, 1, {}});
                ++i;
                continue;
            }
            Item group{0x80, 0, {}};
            for (; i < events.size() && events[i].frame == frame && !events[i].suspend; ++i) group.paragraphs.push_back(events[i].paragraph);
            seq.push_back(std::move(group));
        }
        if (end > now) seq.push_back({2, end - now, {}});
        seq.push_back({0, 0, {}});
        return seq;
    };
    for (const auto& p : objects_) {
        Object o; o.type = p.type; o.id.assign(p.name.begin(), p.name.end()); o.id.push_back(0);
        o.sequence = sequence(p.events, p.end);
        f.objects.push_back(std::move(o));
    }
    if (!suspends_.empty()) {
        Object c; c.type = kControl; const char n[] = "control"; c.id.assign(n, n + sizeof n);
        std::vector<Event> events;
        uint32_t k = 0;
        for (uint32_t s : suspends_) events.push_back({s, k++, true, {}});
        c.sequence = sequence(events, 0);
        f.objects.push_back(std::move(c));
    }
    return write(f);
}
}  // namespace demo_stb
