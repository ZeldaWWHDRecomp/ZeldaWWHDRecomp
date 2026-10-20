// SPDX-License-Identifier: MPL-2.0
#include "demo_event_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace demo_event_image {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kNone = 0xFFFFFFFFu, kMaxFlag = 10240;
constexpr uint32_t kEvent = 0x40, kStaff = 0xF0;
void put(Bytes& b, size_t p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (24 - 8 * i)); }
uint32_t get(const Bytes& b, size_t p) { return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3]; }
void putf(Bytes& b, size_t p, float f) { uint32_t v; std::memcpy(&v, &f, 4); put(b, p, v); }
float getf(const Bytes& b, size_t p) { const uint32_t v = get(b, p); float f; std::memcpy(&f, &v, 4); return f; }
void name(Bytes& b, size_t p, const std::string& s) { std::memcpy(b.data() + p, s.data(), s.size()); }
std::string name_at(const Bytes& b, size_t p) { const char* s = reinterpret_cast<const char*>(b.data() + p); return std::string(s, strnlen(s, std::min<size_t>(31, b.size() - p))); }
bool fail(std::string& why, const char* text) { why = text; return false; }
bool plain(const std::string& s, size_t max) {
    if (s.empty() || s.size() >= max) return false;
    for (char c : s) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    return true;
}
bool check(const Spec& s, std::string& why) {
    if (!plain(s.event, 32)) return fail(why, "demo event name must be 1..31 plain characters");
    if (!plain(s.file, 32)) return fail(why, "demo file name must be 1..31 plain characters");
    if (s.origin) {
        const auto& o = *s.origin;
        for (float v : {o.x, o.y, o.z, o.angle_y}) if (!std::isfinite(v) || std::fabs(v) > 1e6f) return fail(why, "demo origin must be finite");
    }
    if (s.light.size() > kMaxLightCuts) return fail(why, "a demo has at most 8 light changes");
    std::vector<uint32_t> flags{0, 5};  // the PLAY cut's end flag, the event's finish flag
    if (!s.light.empty()) flags.push_back(s.light_wait_flag);
    for (const auto& l : s.light) {
        if (l.hour && (!std::isfinite(*l.hour) || *l.hour < 0 || *l.hour >= 24)) return fail(why, "light hour must be 0..24");
        flags.push_back(l.start_flag);
        flags.push_back(l.end_flag);
    }
    std::sort(flags.begin(), flags.end());
    if (std::adjacent_find(flags.begin(), flags.end()) != flags.end() || flags.back() >= kMaxFlag) return fail(why, "light flags must be distinct event flags");
    return true;
}
struct Layout {
    uint32_t staffs = 0, cuts = 0, properties = 0, floats = 0, ints = 0, cut_at = 0, property_at = 0, float_at = 0, int_at = 0, string_at = 0, string_bytes = 0, size = 0;
};
Layout layout(const Spec& s) {
    Layout l;
    const bool light = !s.light.empty();
    l.staffs = light ? 2 : 1;
    l.cuts = 1 + (light ? 1 + uint32_t(s.light.size()) : 0);
    l.properties = s.origin ? 3 : 1;
    l.floats = s.origin ? 4 : 0;
    for (const auto& c : s.light) { l.properties += (c.hour ? 1 : 0) + (c.rain ? 1 : 0); l.floats += c.hour ? 1 : 0; l.ints += c.rain ? 1 : 0; }
    l.cut_at = kStaff + l.staffs * 0x50;
    l.property_at = l.cut_at + l.cuts * 0x50;
    l.float_at = l.property_at + l.properties * 0x40;
    l.int_at = l.float_at + l.floats * 4;
    l.string_at = l.int_at + l.ints * 4;
    l.string_bytes = uint32_t((s.file.size() + 1 + 15) & ~size_t(15));
    l.size = l.string_at + l.string_bytes;
    return l;
}
void staff(Bytes& b, uint32_t at, const char* n, uint32_t index, uint32_t type, uint32_t first_cut) {
    name(b, at, n);
    put(b, at + 0x24, index); put(b, at + 0x2C, type); put(b, at + 0x30, first_cut); put(b, at + 0x38, kNone); put(b, at + 0x3C, kNone);
}
void cut(Bytes& b, uint32_t at, const char* n, uint32_t index, uint32_t start_flag, uint32_t end_flag, uint32_t first_property, uint32_t next) {
    name(b, at, n);
    put(b, at + 0x20, index);
    put(b, at + 0x28, start_flag);
    for (unsigned j = 1; j < 3; ++j) put(b, at + 0x28 + j * 4, kNone);
    put(b, at + 0x34, end_flag); put(b, at + 0x38, first_property); put(b, at + 0x3C, next);
}
Bytes build(const Spec& s) {
    const Layout l = layout(s);
    Bytes b(l.size, 0);
    // pools: event, staff, cut, property, float, int, string
    const uint32_t offsets[7] = {kEvent, kStaff, l.cut_at, l.property_at, l.floats ? l.float_at : 0, l.ints ? l.int_at : 0, l.string_at};
    const uint32_t counts[7] = {1, l.staffs, l.cuts, l.properties, l.floats, l.ints, l.string_bytes};
    for (unsigned i = 0; i < 7; ++i) { put(b, i * 8, offsets[i]); put(b, i * 8 + 4, counts[i]); }
    name(b, kEvent, s.event);
    for (unsigned i = 0; i < 20; ++i) put(b, kEvent + 0x2C + i * 4, i < l.staffs ? i : kNone);
    put(b, kEvent + 0x7C, l.staffs);
    put(b, kEvent + 0x88, 5); put(b, kEvent + 0x8C, kNone); put(b, kEvent + 0x90, kNone);
    staff(b, kStaff, "PACKAGE", 0, 11, 0);
    cut(b, l.cut_at, "PLAY", 0, kNone, 0, 0, kNone);
    const bool origin = s.origin.has_value();
    struct Prop { const char* name; uint32_t type, index, count, next; };
    auto property = [&](uint32_t i, const Prop& p) {
        const size_t at = l.property_at + i * 0x40;
        name(b, at, p.name);
        put(b, at + 0x20, i); put(b, at + 0x24, p.type); put(b, at + 0x28, p.index); put(b, at + 0x2C, p.count); put(b, at + 0x30, p.next);
    };
    const Prop props[3] = {{"FileName", 4, 0, uint32_t(s.file.size() + 1), origin ? 1u : kNone}, {"OffsetPos", 1, 0, 1, 2}, {"OffsetAngY", 0, 3, 1, kNone}};
    uint32_t prop = 0;
    for (; prop < (origin ? 3u : 1u); ++prop) property(prop, props[prop]);
    if (origin) { putf(b, l.float_at, s.origin->x); putf(b, l.float_at + 4, s.origin->y); putf(b, l.float_at + 8, s.origin->z); putf(b, l.float_at + 12, s.origin->angle_y); }
    if (!s.light.empty()) {
        staff(b, kStaff + 0x50, "LIGHT", 1, 9, 1);
        cut(b, l.cut_at + 0x50, "WAIT", 1, kNone, s.light_wait_flag, kNone, 2);
        uint32_t floats = origin ? 4 : 0, ints = 0;
        for (size_t i = 0; i < s.light.size(); ++i) {
            const auto& c = s.light[i];
            const uint32_t index = 2 + uint32_t(i), first = (c.hour || c.rain) ? prop : kNone;
            cut(b, l.cut_at + index * 0x50, "CHANGE", index, c.start_flag, c.end_flag, first, i + 1 < s.light.size() ? index + 1 : kNone);
            if (c.hour) {
                putf(b, l.float_at + floats * 4, *c.hour);
                property(prop, {"Hour", 0, floats++, 1, c.rain ? prop + 1 : kNone});
                ++prop;
            }
            if (c.rain) {
                put(b, l.int_at + ints * 4, 0);
                property(prop, {"Weather", 3, ints++, 1, kNone});
                ++prop;
            }
        }
    }
    name(b, l.string_at, s.file);
    return b;
}
}  // namespace

bool make(const Spec& spec, std::vector<uint8_t>& out, std::string& why) {
    out.clear(); why.clear();
    if (!check(spec, why)) return false;
    out = build(spec);
    if (out.size() > kMaxImageBytes) { out.clear(); return fail(why, "demo event image too large"); }
    return true;
}

bool validate(const std::vector<uint8_t>& b, Spec& out, std::string& why) {
    out = {}; why.clear();
    if (b.size() < kStaff + 0x50 + 0x50 + 0x40 + 16 || b.size() > kMaxImageBytes) return fail(why, "wrong demo event image extent");
    Spec s;
    s.event = name_at(b, kEvent);
    const uint32_t staffs = get(b, 1 * 8 + 4), cuts = get(b, 2 * 8 + 4), properties = get(b, 3 * 8 + 4);
    const uint32_t cut_at = get(b, 2 * 8), property_at = get(b, 3 * 8), float_at = get(b, 4 * 8), int_at = get(b, 5 * 8), string_at = get(b, 6 * 8);
    if (staffs != 1 && staffs != 2) return fail(why, "demo event staff count");
    if (cuts < 1 || cuts > 2 + kMaxLightCuts || (staffs == 2) != (cuts > 1)) return fail(why, "demo event cut count");
    if (properties < 1 || properties > 3 + 2 * kMaxLightCuts) return fail(why, "demo event property count");
    if (uint64_t(cut_at) + cuts * 0x50 > b.size() || uint64_t(property_at) + properties * 0x40 > b.size() || string_at >= b.size()) return fail(why, "demo event pools");
    s.file = name_at(b, string_at);
    // the PLAY cut's property chain: FileName, then OffsetPos/OffsetAngY with an origin
    if (get(b, property_at + 0x30) != kNone) {
        if (uint64_t(float_at) + 16 > b.size()) return fail(why, "demo event float pool");
        s.origin = Origin{getf(b, float_at), getf(b, float_at + 4), getf(b, float_at + 8), getf(b, float_at + 12)};
    }
    if (cuts > 1) {
        s.light_wait_flag = get(b, cut_at + 0x50 + 0x34);
        for (uint32_t i = 2; i < cuts; ++i) {
            const uint32_t at = cut_at + i * 0x50;
            LightCut c;
            c.start_flag = get(b, at + 0x28);
            c.end_flag = get(b, at + 0x34);
            uint32_t n = 0;
            for (uint32_t p = get(b, at + 0x38); p != kNone && n < 2; ++n) {
                if (p >= properties) return fail(why, "demo event property index");
                const uint32_t pa = property_at + p * 0x40, index = get(b, pa + 0x28);
                const std::string pn = name_at(b, pa);
                if (pn == "Hour") { if (uint64_t(float_at) + (uint64_t(index) + 1) * 4 > b.size()) return fail(why, "demo event float pool"); c.hour = getf(b, float_at + index * 4); }
                else if (pn == "Weather") c.rain = true;
                else return fail(why, "demo event light property");
                p = get(b, pa + 0x30);
            }
            s.light.push_back(c);
        }
    }
    if (!check(s, why)) return false;
    if (b != build(s)) return fail(why, "noncanonical demo event image");
    out = s;
    return true;
}
}  // namespace demo_event_image
