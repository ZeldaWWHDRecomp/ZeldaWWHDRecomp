// Mod items: the extended item icon archive (item_icons.h). Mirrors tools/story_mods/lyt/item_icons.py.
#include "item_icons.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>

#include "guest_png.h"
#include "lyt_build.h"
#include "runtime.h"
#include "story_notebook_art.h"

namespace item_icons {
namespace {

constexpr const char* kAnim = "anim/BtnItemIcon_00_IconChange.bflan";
constexpr const char* kMember = "BtnItemIcon_00.szs";
constexpr int kVersion = 1;  // bump when the output changes (the cache key)

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error(why); }

uint16_t u16(const Bytes& b, size_t o) {
    if (o + 2 > b.size()) fail("truncated data");
    return uint16_t(b[o] << 8 | b[o + 1]);
}
uint32_t u32(const Bytes& b, size_t o) {
    if (o + 4 > b.size()) fail("truncated data");
    return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
}
void put16(Bytes& b, uint32_t v) { b.push_back(uint8_t(v >> 8)), b.push_back(uint8_t(v)); }
void put32(Bytes& b, uint32_t v) { put16(b, v >> 16), put16(b, v & 0xFFFF); }
void pad4(Bytes& b) { b.resize((b.size() + 3) / 4 * 4, 0); }
void add(Bytes& b, const Bytes& x) { b.insert(b.end(), x.begin(), x.end()); }
void add(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }

std::string two(int n) {
    char s[8];
    snprintf(s, sizeof s, "%02d", n);
    return s;
}
std::string icon_name(int n) { return "Icon128_" + two(n) + "^l.bflim"; }
std::string float_name(int n) { return "Float128_" + two(n) + "^t.bflim"; }

// ---- the glow (item_icons.py glow) ----
std::vector<int> dilate(const std::vector<int>& m, int w, int h, int r) {
    std::vector<std::pair<int, int>> offs;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r) offs.push_back({dx, dy});
    std::vector<int> out(size_t(w) * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = 0;
            for (auto [dx, dy] : offs) {
                const int xx = x + dx, yy = y + dy;
                if (xx >= 0 && xx < w && yy >= 0 && yy < h) v = std::max(v, m[size_t(yy) * w + xx]);
            }
            out[size_t(y) * w + x] = v;
        }
    return out;
}
std::vector<int> box(const std::vector<int>& m, int w, int h, int r) {
    const int n = (2 * r + 1) * (2 * r + 1);
    std::vector<int> out(size_t(w) * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int s = 0;
            for (int yy = std::max(0, y - r); yy <= std::min(h - 1, y + r); yy++)
                for (int xx = std::max(0, x - r); xx <= std::min(w - 1, x + r); xx++) s += m[size_t(yy) * w + xx];
            out[size_t(y) * w + x] = (2 * s + n) / (2 * n);
        }
    return out;
}

// ---- the animation (item_icons.py parse_pai1 / write_pai1) ----
struct Key { float frame; uint32_t value; float slope; };  // step: value; hermite: value as float bits, slope
struct Entry { uint8_t index, target, curve; std::vector<Key> keys; };
struct Tag { std::string magic; std::vector<Entry> entries; };
struct Content { std::string name; uint8_t type; std::vector<Tag> tags; };
struct Pai1 { uint16_t frames; uint8_t loop; std::vector<std::string> textures; std::vector<Content> contents; };

float f32(uint32_t bits) {
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
uint32_t bits_of(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
std::string cstr(const Bytes& b, size_t o, size_t max = 256) {
    std::string s;
    while (o < b.size() && b[o] && s.size() < max) s += char(b[o++]);
    return s;
}

Pai1 parse_pai1(const Bytes& s) {
    Pai1 p;
    p.frames = u16(s, 8), p.loop = s.at(10);
    const uint16_t ntex = u16(s, 12), ncont = u16(s, 14);
    const uint32_t table = u32(s, 16);
    for (uint32_t i = 0; i < ntex; i++) p.textures.push_back(cstr(s, 20 + u32(s, 20 + 4 * i)));
    for (uint32_t ci = 0; ci < ncont; ci++) {
        const size_t c = u32(s, table + 4 * ci);
        Content ct;
        ct.name = cstr(s, c, 28), ct.type = s.at(c + 29);
        for (uint32_t ti = 0; ti < s.at(c + 28); ti++) {
            const size_t t = c + u32(s, c + 32 + 4 * ti);
            Tag tag;
            tag.magic.assign(s.begin() + t, s.begin() + t + 4);
            for (uint32_t ei = 0; ei < s.at(t + 4); ei++) {
                const size_t e = t + u32(s, t + 8 + 4 * ei);
                Entry en{s.at(e), s.at(e + 1), s.at(e + 2), {}};
                const uint16_t nk = u16(s, e + 4);
                const uint32_t ko = u32(s, e + 8);
                for (uint32_t k = 0; k < nk; k++) {
                    if (en.curve == 1) en.keys.push_back({f32(u32(s, e + ko + 8 * k)), u16(s, e + ko + 8 * k + 4), 0});
                    else en.keys.push_back({f32(u32(s, e + ko + 12 * k)), u32(s, e + ko + 12 * k + 4), f32(u32(s, e + ko + 12 * k + 8))});
                }
                tag.entries.push_back(std::move(en));
            }
            ct.tags.push_back(std::move(tag));
        }
        p.contents.push_back(std::move(ct));
    }
    return p;
}

Bytes section(const char* magic, const Bytes& body) {
    Bytes out;
    add(out, std::string(magic, 4));
    put32(out, uint32_t(body.size() + 8));
    add(out, body);
    return out;
}
Bytes string_table(const std::vector<std::string>& names) {
    Bytes offs, blob;
    for (const std::string& n : names) put32(offs, uint32_t(4 * names.size() + blob.size())), add(blob, n), blob.push_back(0);
    add(offs, blob);
    pad4(offs);
    return offs;
}
Bytes write_pai1(const Pai1& p) {
    std::vector<Bytes> contents;
    for (const Content& c : p.contents) {
        std::vector<Bytes> blocks;
        for (const Tag& t : c.tags) {
            std::vector<Bytes> entries;
            for (const Entry& e : t.entries) {
                Bytes b{e.index, e.target, e.curve, 0};
                put16(b, uint32_t(e.keys.size())), put16(b, 0), put32(b, 0xC);
                for (const Key& k : e.keys) {
                    put32(b, bits_of(k.frame));
                    if (e.curve == 1) put16(b, k.value), put16(b, 0);
                    else put32(b, k.value), put32(b, bits_of(k.slope));
                }
                entries.push_back(std::move(b));
            }
            Bytes head;
            add(head, t.magic);
            head.push_back(uint8_t(entries.size())), head.push_back(0), head.push_back(0), head.push_back(0);
            uint32_t first = uint32_t(head.size() + 4 * entries.size());
            Bytes blob;
            for (const Bytes& e : entries) put32(head, first + uint32_t(blob.size())), add(blob, e);
            add(head, blob);
            blocks.push_back(std::move(head));
        }
        Bytes head(28, 0);
        memcpy(head.data(), c.name.data(), std::min<size_t>(c.name.size(), 27));
        head.push_back(uint8_t(blocks.size())), head.push_back(c.type), head.push_back(0), head.push_back(0);
        const uint32_t first = uint32_t(head.size() + 4 * blocks.size());
        Bytes blob;
        for (Bytes& b : blocks) put32(head, first + uint32_t(blob.size())), pad4(b), add(blob, b);
        add(head, blob);
        contents.push_back(std::move(head));
    }
    const Bytes texlist = p.textures.empty() ? Bytes{} : string_table(p.textures);
    const uint32_t table = uint32_t(8 + 12 + texlist.size());
    Bytes body;
    put16(body, p.frames), body.push_back(p.loop), body.push_back(0);
    put16(body, uint32_t(p.textures.size())), put16(body, uint32_t(contents.size())), put32(body, table);
    add(body, texlist);
    const uint32_t first = table + uint32_t(4 * contents.size());
    Bytes blob;
    for (Bytes& c : contents) put32(body, first + uint32_t(blob.size())), pad4(c), add(blob, c);
    add(body, blob);
    return section("pai1", body);
}

std::vector<Bytes> sections_of(const Bytes& flan, size_t& head) {
    head = u16(flan, 6);
    std::vector<Bytes> out;
    size_t o = head;
    for (uint32_t i = 0, n = u16(flan, 0x10); i < n; i++) {
        const uint32_t size = u32(flan, o + 4);
        if (o + size > flan.size()) fail("truncated animation");
        out.emplace_back(flan.begin() + o, flan.begin() + o + size);
        o += size;
    }
    return out;
}

uint32_t name_hash(const std::string& n) {
    uint32_t h = 0;
    for (unsigned char c : n) h = h * 0x65 + c;
    return h;
}

Bytes sarc_with(const std::map<std::string, Bytes>& files, const std::function<uint32_t(const std::string&, const Bytes&)>& align_of,
                uint32_t data_align = 0x2000) {
    std::vector<std::string> names;
    for (const auto& [n, _] : files) names.push_back(n);
    std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return name_hash(a) < name_hash(b); });
    Bytes sfnt, data, sfat;
    std::vector<uint32_t> offs;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const std::string& n : names) offs.push_back(uint32_t(sfnt.size())), add(sfnt, n), sfnt.push_back(0), pad4(sfnt);
    for (const std::string& n : names) {
        const Bytes& f = files.at(n);
        const uint32_t a = align_of(n, f);
        data.resize((data.size() + a - 1) / a * a, 0);
        spans.push_back({uint32_t(data.size()), uint32_t(data.size() + f.size())});
        add(data, f);
    }
    add(sfat, std::string("SFAT")), put16(sfat, 0xC), put16(sfat, uint32_t(names.size())), put32(sfat, 0x65);
    for (size_t i = 0; i < names.size(); i++)
        put32(sfat, name_hash(names[i])), put32(sfat, 0x01000000 | (offs[i] / 4)), put32(sfat, spans[i].first), put32(sfat, spans[i].second);
    Bytes nt;
    add(nt, std::string("SFNT")), put16(nt, 8), put16(nt, 0), add(nt, sfnt);
    const size_t head = 0x14 + sfat.size() + nt.size(), start = (head + data_align - 1) / data_align * data_align;
    Bytes out;
    add(out, std::string("SARC")), put16(out, 0x14), put16(out, 0xFEFF), put32(out, uint32_t(start + data.size())), put32(out, uint32_t(start));
    put16(out, 0x0100), put16(out, 0);
    add(out, sfat), add(out, nt);
    out.resize(start, 0);
    add(out, data);
    return out;
}

}  // namespace

Bytes glow(const Bytes& rgba) {
    const int w = kIconSize, h = kIconSize;
    if (rgba.size() != size_t(w) * h * 4) fail("an icon must be 128 x 128 RGBA");
    std::vector<int> m(size_t(w) * h);
    for (size_t i = 0; i < m.size(); i++) m[i] = rgba[4 * i + 3] >= 128 ? 255 : 0;
    m = dilate(m, w, h, 2);
    for (int i = 0; i < 3; i++) m = box(m, w, h, 2);
    Bytes out(m.size() * 4);
    for (size_t i = 0; i < m.size(); i++) out[4 * i] = out[4 * i + 1] = out[4 * i + 2] = 255, out[4 * i + 3] = uint8_t(m[i]);
    return out;
}

Bytes extend_animation(const Bytes& flan, unsigned count) {
    if (flan.size() < 0x14 || memcmp(flan.data(), "FLAN", 4)) fail("not an animation");
    size_t head;
    std::vector<Bytes> secs = sections_of(flan, head);
    for (Bytes& s : secs) {
        if (memcmp(s.data(), "pai1", 4)) continue;
        Pai1 p = parse_pai1(s);
        const unsigned last = p.frames;
        for (Content& c : p.contents)
            for (Tag& t : c.tags)
                for (Entry& e : t.entries) {
                    if (t.magic != "FLTP" || e.curve != 1 || e.keys.empty()) continue;
                    if (e.keys.back().value >= p.textures.size()) fail("bad texture pattern key");
                    const bool is_float = p.textures[e.keys.back().value].rfind("Float128_", 0) == 0;
                    for (unsigned i = 0; i < count; i++) {
                        const int n = int(last + 1 + i);
                        const std::string tex = is_float ? float_name(n) : icon_name(n);
                        auto it = std::find(p.textures.begin(), p.textures.end(), tex);
                        if (it == p.textures.end()) p.textures.push_back(tex), it = p.textures.end() - 1;
                        e.keys.push_back({float(n), uint32_t(it - p.textures.begin()), 0});
                    }
                }
        p.frames = uint16_t(last + count);
        s = write_pai1(p);
    }
    Bytes out(flan.begin(), flan.begin() + head);
    size_t size = head;
    for (const Bytes& s : secs) size += s.size();
    out[0x0C] = uint8_t(size >> 24), out[0x0D] = uint8_t(size >> 16), out[0x0E] = uint8_t(size >> 8), out[0x0F] = uint8_t(size);
    for (const Bytes& s : secs) add(out, s);
    return out;
}

std::map<std::string, Bytes> extend(const std::map<std::string, Bytes>& files, const std::vector<Bytes>& icons) {
    auto anim = files.find(kAnim);
    if (anim == files.end()) fail("no IconChange animation");
    size_t head;
    unsigned frames = 0;
    for (const Bytes& s : sections_of(anim->second, head))
        if (!memcmp(s.data(), "pai1", 4)) frames = u16(s, 8) + 1u;
    if (int(frames) != kGameFrames) fail("unexpected IconChange: " + std::to_string(frames) + " frames");
    std::map<std::string, Bytes> out = files;
    std::vector<Bytes> pictures{Bytes(size_t(kIconSize) * kIconSize * 4, 0)};
    pictures.insert(pictures.end(), icons.begin(), icons.end());
    for (size_t i = 0; i < pictures.size(); i++) {
        if (pictures[i].size() != size_t(kIconSize) * kIconSize * 4) fail("an icon must be 128 x 128 RGBA");
        const int n = kGameFrames + int(i);
        out["timg/" + icon_name(n)] = lyt_build::texture(pictures[i], kIconSize, kIconSize, "rgba8", "tiled", 0);
        out["timg/" + float_name(n)] = lyt_build::texture(glow(pictures[i]), kIconSize, kIconSize, "bc5la", "tiled", 0);
    }
    out[kAnim] = extend_animation(anim->second, unsigned(pictures.size()));
    return out;
}

Bytes archive(const std::map<std::string, Bytes>& files) {
    return sarc_with(files, [](const std::string& n, const Bytes& d) -> uint32_t {
        if (n.size() > 6 && n.compare(n.size() - 6, 6, ".bflim") == 0 && d.size() >= 0x28 && !memcmp(&d[d.size() - 0x28], "FLIM", 4))
            return std::max<uint32_t>(4, u16(d, d.size() - 0x28 + 0x20));
        return 4;
    });
}

std::map<std::string, Bytes> read_sarc(const Bytes& d) {
    if (d.size() < 0x20 || memcmp(d.data(), "SARC", 4)) fail("not a SARC");
    const uint32_t head = u16(d, 4), start = u32(d, 12), n = u16(d, head + 6);
    const size_t names = head + 12 + 16 * size_t(n) + 8;
    std::map<std::string, Bytes> out;
    for (uint32_t i = 0; i < n; i++) {
        const size_t e = head + 12 + 16 * size_t(i);
        const uint32_t attr = u32(d, e + 4), s = u32(d, e + 8), en = u32(d, e + 12);
        if (en < s || size_t(start) + en > d.size()) fail("bad SARC entry");
        out[cstr(d, names + (attr & 0xFFFF) * 4)] = Bytes(d.begin() + start + s, d.begin() + start + en);
    }
    return out;
}

Bytes yaz0_decode(const Bytes& d) {
    if (d.size() < 16 || memcmp(d.data(), "Yaz0", 4)) return d;
    const uint32_t size = u32(d, 4);
    Bytes out;
    out.reserve(size);
    size_t i = 16;
    while (out.size() < size) {
        if (i >= d.size()) fail("truncated Yaz0");
        const uint8_t code = d[i++];
        for (int b = 0; b < 8 && out.size() < size; b++) {
            if (code & (0x80 >> b)) {
                if (i >= d.size()) fail("truncated Yaz0");
                out.push_back(d[i++]);
            } else {
                if (i + 1 >= d.size()) fail("truncated Yaz0");
                const size_t dist = (size_t(d[i] & 15) << 8 | d[i + 1]) + 1;
                size_t n = d[i] >> 4;
                i += 2;
                if (n == 0) {
                    if (i >= d.size()) fail("truncated Yaz0");
                    n = size_t(d[i++]) + 0x12;
                } else {
                    n += 2;
                }
                if (dist > out.size()) fail("bad Yaz0 back reference");
                for (size_t k = 0; k < n; k++) out.push_back(out[out.size() - dist]);
            }
        }
    }
    return out;
}

Bytes yaz0_compress(const Bytes& data) {
    constexpr size_t kWindow = 0x1000, kMax = 0x111;
    constexpr int kChain = 64;
    const size_t n = data.size();
    Bytes out{'Y', 'a', 'z', '0'};
    put32(out, uint32_t(n)), out.resize(16, 0);
    // chains of earlier positions by a 16-bit hash of their 3 bytes (walked newest first; only positions
    // with the same 3 bytes count, as the Python twin's exact-key lists)
    std::vector<int32_t> head(1 << 16, -1), prev(n, -1);
    auto key = [&](size_t p) { return uint32_t(data[p]) << 16 | uint32_t(data[p + 1]) << 8 | data[p + 2]; };
    auto hash = [](uint32_t k) { return (k * 2654435761u) >> 16; };
    size_t pos = 0;
    while (pos < n) {
        const size_t flag_at = out.size();
        out.push_back(0);
        uint8_t flags = 0;
        for (int bit = 0; bit < 8 && pos < n; bit++) {
            size_t best = 0, best_d = 0;
            if (pos + 2 < n) {
                const uint32_t k = key(pos);
                const size_t limit = std::min(kMax, n - pos);
                int tried = 0;
                for (int32_t cand = head[hash(k)]; cand >= 0; cand = prev[size_t(cand)]) {
                    if (pos - size_t(cand) > kWindow || tried >= kChain) break;
                    if (key(size_t(cand)) != k) continue;
                    tried++;
                    size_t m = 3;
                    while (m < limit && data[size_t(cand) + m] == data[pos + m]) m++;
                    if (m > best) {
                        best = m, best_d = pos - size_t(cand);
                        if (m == limit) break;
                    }
                }
            }
            size_t step = 1;
            if (best >= 3) {
                const size_t d = best_d - 1;
                if (best <= 0x11) out.push_back(uint8_t(((best - 2) << 4) | (d >> 8))), out.push_back(uint8_t(d));
                else out.push_back(uint8_t(d >> 8)), out.push_back(uint8_t(d)), out.push_back(uint8_t(best - 0x12));
                step = best;
            } else {
                flags |= uint8_t(0x80 >> bit);
                out.push_back(data[pos]);
            }
            for (size_t q = pos; q < pos + step; q++)
                if (q + 2 < n) {
                    const uint32_t h = hash(key(q));
                    prev[q] = head[h], head[h] = int32_t(q);
                }
            pos += step;
        }
        out[flag_at] = flags;
    }
    return out;
}

Bytes extend_pack(const Bytes& pack, const std::vector<Bytes>& icons) {
    std::map<std::string, Bytes> members = read_sarc(pack);
    auto it = members.find(kMember);
    if (it == members.end()) fail(std::string("no ") + kMember + " in the pack");
    it->second = yaz0_compress(archive(extend(read_sarc(yaz0_decode(it->second)), icons)));
    return sarc_with(members, [](const std::string&, const Bytes&) -> uint32_t { return 0x2000; });
}

// ---------------------------------------------------------------- the port
namespace {
std::mutex g_mu;
std::vector<Item> g_items;
unsigned g_spares = 0;
std::vector<uint16_t> g_spare_keys;            // the item bound to each spare frame (0: free)
std::map<int, std::string> g_pending;          // frame -> the picture to write there
std::map<int, uint64_t> g_written;             // frame -> the picture (hash) written there

std::filesystem::path g_cache;
std::map<std::string, std::string> g_built;  // input pack path -> extended pack path ("" failed)
bool g_served = false;                       // the game read an extended pack

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}
bool read_file(const std::filesystem::path& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
bool is_2d_pack(const std::string& guest) {
    std::string g = guest;
    for (char& c : g) c = char(std::tolower((unsigned char)c));
    const size_t slash = g.find_last_of('/');
    const std::string name = slash == std::string::npos ? g : g.substr(slash + 1);
    return name.rfind("permanent_2d_", 0) == 0 && name.size() > 5 && name.compare(name.size() - 5, 5, ".pack") == 0;
}
// an item's picture as 128 x 128 RGBA: scaled to fit, centred
Bytes icon_of(const std::string& png_path) {
    Bytes file;
    if (!read_file(png_path, file)) fail("can't read " + std::filesystem::path(png_path).filename().string());
    const guestmods::hud::Pixels px = guestmods::hud::decode_png({file.data(), file.size()});
    game_glyph::Bitmap in{int(px.width), int(px.height), px.rgba};
    const float k = float(kIconSize) / float(std::max(in.width, in.height));
    const int w = std::max(1, int(std::lround(in.width * k))), h = std::max(1, int(std::lround(in.height * k)));
    const game_glyph::Bitmap s = story_notebook::art::resized(in, w, h);
    Bytes out(size_t(kIconSize) * kIconSize * 4, 0);
    const int x0 = (kIconSize - w) / 2, y0 = (kIconSize - h) / 2;
    for (int y = 0; y < h; y++) memcpy(&out[(size_t(y0 + y) * kIconSize + x0) * 4], &s.rgba[size_t(y) * w * 4], size_t(w) * 4);
    return out;
}
}  // namespace

void set_items(const std::vector<Item>& items, const std::filesystem::path& cache_dir, unsigned spares) {
    std::lock_guard lock(g_mu);
    g_items = items, g_cache = cache_dir, g_built.clear(), g_served = false;
    g_spares = spares, g_spare_keys.assign(spares, 0), g_pending.clear(), g_written.clear();
}

std::string pack_for(const std::string& guest, const std::string& host_path) {
    if (!is_2d_pack(guest)) return "";
    std::lock_guard lock(g_mu);
    if (g_items.empty() || g_cache.empty()) return "";
    std::error_code exists_ec;
    if (!std::filesystem::is_regular_file(host_path, exists_ec)) return "";  // the game also asks for packs that aren't there
    if (auto it = g_built.find(host_path); it != g_built.end()) {
        g_served = g_served || !it->second.empty();
        return it->second;
    }
    std::string& result = g_built[host_path];
    try {
        Bytes pack;
        if (!read_file(host_path, pack)) fail("can't read the game's 2D pack");
        std::vector<Bytes> icons;
        uint64_t h = fnv(0xCBF29CE484222325ull, pack.data(), pack.size());
        h = fnv(h, &kVersion, sizeof kVersion);
        for (const Item& it : g_items) {
            icons.push_back(icon_of(it.icon_png));
            h = fnv(h, &it.key, sizeof it.key), h = fnv(h, icons.back().data(), icons.back().size());
        }
        for (unsigned i = 0; i < g_spares; i++) {  // the spare frames: empty until an item takes one
            icons.push_back(Bytes(size_t(kIconSize) * kIconSize * 4, 0));
            h = fnv(h, icons.back().data(), icons.back().size());
        }
        char name[64];
        snprintf(name, sizeof name, "%016llx", (unsigned long long)h);
        const std::filesystem::path out = g_cache / ("item-icons-" + std::string(name) + ".pack");
        std::error_code ec;
        if (!std::filesystem::exists(out, ec)) {
            const Bytes built = extend_pack(pack, icons);
            std::filesystem::create_directories(g_cache, ec);
            const std::filesystem::path tmp = out.string() + ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(built.data()), std::streamsize(built.size()));
                if (!f) fail("can't write the cache");
            }
            std::filesystem::rename(tmp, out, ec);
            if (ec) fail("can't write the cache (" + ec.message() + ")");
            LOG("[items] item icons: %zu mod icons and %u spare frames added to %s (%.1f MB)", g_items.size(), g_spares,
                std::filesystem::path(guest).filename().string().c_str(), built.size() / 1048576.0);
        }
        result = out.string();
        g_served = true;
    } catch (const std::exception& e) {
        LOG("[items] item icons: not built (%s); the game's own icons only", e.what());
        result.clear();
    }
    return result;
}

Bytes picture(const std::string& png_path) { return icon_of(png_path); }

namespace {
int frame_locked(uint16_t key) {
    if (!g_served) return -1;
    for (size_t i = 0; i < g_items.size(); i++)
        if (g_items[i].key == key) return kFirstFrame + int(i);
    for (size_t i = 0; i < g_spare_keys.size(); i++)
        if (key && g_spare_keys[i] == key) return kFirstFrame + int(g_items.size() + i);
    return -1;
}
}  // namespace

int frame(uint16_t key) {
    std::lock_guard lock(g_mu);
    return frame_locked(key);
}
bool has_frame(uint16_t key) { return frame(key) >= 0; }
unsigned free_spares() {
    std::lock_guard lock(g_mu);
    if (!g_served) return 0;
    return unsigned(std::count(g_spare_keys.begin(), g_spare_keys.end(), uint16_t(0)));
}
void served_for_tests() {
    std::lock_guard lock(g_mu);
    g_served = true;
}
bool live_icons() {
    std::lock_guard lock(g_mu);
    return g_served && g_spares > 0;
}
int live_icon(uint16_t key, const std::string& png) {
    std::lock_guard lock(g_mu);
    if (!g_served || !key) return -1;
    int f = frame_locked(key);
    if (f < 0) {
        auto it = std::find(g_spare_keys.begin(), g_spare_keys.end(), uint16_t(0));
        if (it == g_spare_keys.end()) return -1;
        *it = key;
        f = kFirstFrame + int(g_items.size() + size_t(it - g_spare_keys.begin()));
    }
    g_pending[f] = png;
    return f;
}

std::vector<std::pair<int, std::string>> pending_icons() {
    std::lock_guard lock(g_mu);
    return {g_pending.begin(), g_pending.end()};
}
bool icon_written(int f, uint64_t hash) {
    std::lock_guard lock(g_mu);
    auto it = g_written.find(f);
    return it != g_written.end() && it->second == hash;
}
void icon_done(int f, const std::string& png, uint64_t hash, bool ok) {
    std::lock_guard lock(g_mu);
    if (ok) g_written[f] = hash;
    if (g_pending.count(f) && g_pending[f] == png) g_pending.erase(f);  // done (or refused: logged once)
}
uint64_t picture_hash(const Bytes& px) { return fnv(0xCBF29CE484222325ull, px.data(), px.size()); }
std::string icon_file(int f) { return "timg/" + icon_name(f); }
std::string glow_file(int f) { return "timg/" + float_name(f); }


}  // namespace item_icons
