// Portable save state file format (portable_state.h).
#include "portable_state.h"

#include <cstdio>
#include <cstring>
#include <locale>
#include <map>
#include <sstream>

namespace pstate {
namespace {

// every key a file may have; anything else is refused (no room for extra data)
struct Blob { const char* key; size_t size; std::vector<uint8_t> State::*field; };
const Blob kBlobs[] = {
    {"savedata", kSaveDataSize, &State::savedata},
    {"hd_player", kHdPlayerSize, &State::hd_player},
    {"hd_status", kHdStatusSize, &State::hd_status},
    {"hd_event", kHdEventSize, &State::hd_event},
    {"hd_map", kHdMapSize, &State::hd_map},
};
const char* const kTextKeys[] = {
    "format", "title_id", "title_version", "game_hash", "runtime", "created", "file_slot", "player_name",
    "stage", "start_point", "start_room", "layer", "room", "link_pos", "link_angle_y", "link_proc", "on_ship",
    "has_ship", "ship_pos", "ship_angle_y",
    "time_of_day", "date",
};
constexpr size_t kMaxText = 128;  // longest value of a text key

const Blob* find_blob(const std::string& k) {
    for (auto& b : kBlobs)
        if (k == b.key) return &b;
    return nullptr;
}
bool is_text_key(const std::string& k) {
    for (auto* t : kTextKeys)
        if (k == t) return true;
    return false;
}

std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(v.size() * 2);
    for (uint8_t b : v) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}
int nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool unhex(const std::string& s, std::vector<uint8_t>& out) {
    if (s.size() % 2) return false;
    out.resize(s.size() / 2);
    for (size_t i = 0; i < out.size(); i++) {
        int a = nib(s[2 * i]), b = nib(s[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return true;
}

// values are single-line printable text
std::string clean(const std::string& v) {
    std::string s;
    for (unsigned char c : v)
        if (c >= 0x20 && c != 0x7F) s += (char)c;
    if (s.size() > kMaxText) s.resize(kMaxText);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// numbers in the C locale whatever the host's locale is
std::string fnum(float v) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(9);
    o << v;
    return o.str();
}
bool parse_floats(const std::string& s, float* out, int n) {
    std::istringstream i(s);
    i.imbue(std::locale::classic());
    for (int k = 0; k < n; k++)
        if (!(i >> out[k])) return false;
    i >> std::ws;
    return i.eof();
}
bool parse_int(const std::string& s, long long& out) {
    std::istringstream i(s);
    i.imbue(std::locale::classic());
    if (!(i >> out)) return false;
    i >> std::ws;
    return i.eof();
}

void be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// splits into lines (the checksum line must be the last); `body` = everything before the checksum line
bool lines_of(const std::string& text, std::vector<std::pair<std::string, std::string>>& kv, std::string& body,
              std::string& checksum, std::string& why) {
    if (text.size() > kMaxFileSize) {
        why = "file is larger than " + std::to_string(kMaxFileSize / 1024) + " KB";
        return false;
    }
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        size_t start = at;
        at = e + 1;
        if (line.size() > kMaxLine) {
            why = "line too long";
            return false;
        }
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            why = "not a portable save state (line without '=')";
            return false;
        }
        std::string k = trim(t.substr(0, eq)), v = trim(t.substr(eq + 1));
        if (k == "checksum") {
            body = text.substr(0, start);
            checksum = v;
            if (!trim(text.substr(std::min(at, text.size()))).empty()) {
                why = "data after the checksum";
                return false;
            }
            return true;
        }
        kv.push_back({k, v});
    }
    why = "no checksum (file is truncated?)";
    return false;
}

}  // namespace

uint32_t crc32(const void* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = [] {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)init;
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void save_checksum(const uint8_t* block, uint32_t& sum, uint32_t& complement) {
    sum = complement = 0;
    for (size_t i = 0; i < kSaveChecksumAt; i++) {
        sum += block[i];
        complement += (uint32_t)~(uint32_t)block[i];
    }
}

void seal_savedata(std::vector<uint8_t>& block) {
    if (block.size() != kSaveDataSize) return;
    uint32_t s, c;
    save_checksum(block.data(), s, c);
    be32(&block[kSaveChecksumAt], s);
    be32(&block[kSaveChecksumAt + 4], c);
}

bool savedata_checksum_ok(const std::vector<uint8_t>& block) {
    if (block.size() != kSaveDataSize) return false;
    uint32_t s, c;
    save_checksum(block.data(), s, c);
    return rd32(&block[kSaveChecksumAt]) == s && rd32(&block[kSaveChecksumAt + 4]) == c;
}

std::string write(const State& s, std::string& why) {
    for (auto& b : kBlobs)
        if ((s.*b.field).size() != b.size) {
            why = std::string("field ") + b.key + " has " + std::to_string((s.*b.field).size()) + " bytes, expected " +
                  std::to_string(b.size);
            return "";
        }
    std::string o;
    auto kv = [&](const char* k, const std::string& v) { o += std::string(k) + " = " + v + "\n"; };
    o += "# Wind Waker HD portable save state: progress and position only (no game code or game data).\n";
    o += "# Attach it to a bug report together with your cking.sav. Load: copy it into the states folder\n";
    o += "# as slotN.wwstate and load slot N, or start with WWHD_PORTABLE_LOAD=<this file>.\n";
    kv("format", std::to_string(s.format));
    kv("title_id", clean(s.title_id));
    kv("title_version", std::to_string(s.title_version));
    kv("game_hash", clean(s.game_hash));
    kv("runtime", clean(s.runtime));
    kv("created", clean(s.created));
    kv("file_slot", std::to_string(s.file_slot));
    kv("player_name", clean(s.player_name));
    kv("stage", clean(s.stage));
    kv("start_point", std::to_string(s.start_point));
    kv("start_room", std::to_string(s.start_room));
    kv("layer", std::to_string(s.layer));
    kv("room", std::to_string(s.room));
    kv("link_pos", fnum(s.pos[0]) + " " + fnum(s.pos[1]) + " " + fnum(s.pos[2]));
    kv("link_angle_y", std::to_string(s.angle_y));
    kv("link_proc", std::to_string(s.link_proc));
    kv("on_ship", s.on_ship ? "1" : "0");
    kv("has_ship", s.has_ship ? "1" : "0");
    kv("ship_pos", fnum(s.ship_pos[0]) + " " + fnum(s.ship_pos[1]) + " " + fnum(s.ship_pos[2]));
    kv("ship_angle_y", std::to_string(s.ship_angle_y));
    kv("time_of_day", fnum(s.time_of_day));
    kv("date", std::to_string(s.date));
    for (auto& b : kBlobs) kv(b.key, hex(s.*b.field));
    char ck[32];
    snprintf(ck, sizeof ck, "crc32:%08X", crc32(o.data(), o.size()));
    kv("checksum", ck);
    std::string w;
    if (!blob_check(o, w)) {  // the guard, on what is about to be written
        why = "refused to write: " + w;
        return "";
    }
    return o;
}

bool blob_check(const std::string& text, std::string& why) {
    std::vector<std::pair<std::string, std::string>> kv;
    std::string body, ck;
    if (!lines_of(text, kv, body, ck, why)) return false;
    size_t binary = 0;
    for (auto& [k, v] : kv) {
        if (const Blob* b = find_blob(k)) {
            if (v.size() != 2 * b->size) {
                why = "field " + k + " has the wrong size";
                return false;
            }
            binary += b->size;
        } else if (is_text_key(k)) {
            if (v.size() > kMaxText) {
                why = "field " + k + " is too long";
                return false;
            }
        } else {
            why = "unknown field " + k;
            return false;
        }
    }
    size_t expect = 0;
    for (auto& b : kBlobs) expect += b.size;
    if (binary > expect) {
        why = "too much binary data";
        return false;
    }
    return true;
}

bool read(const std::string& text, State& out, std::string& why) {
    std::vector<std::pair<std::string, std::string>> kv;
    std::string body, ck;
    if (!lines_of(text, kv, body, ck, why)) return false;
    char want[32];
    snprintf(want, sizeof want, "crc32:%08X", crc32(body.data(), body.size()));
    if (ck != want) {
        why = "checksum mismatch (file was changed or damaged)";
        return false;
    }
    if (!blob_check(text, why)) return false;
    std::map<std::string, std::string> m;
    for (auto& [k, v] : kv) {
        if (m.count(k)) {
            why = "field " + k + " appears twice";
            return false;
        }
        m[k] = v;
    }
    State s;
    long long n;
    if (!m.count("format") || !parse_int(m["format"], n)) {
        why = "not a portable save state";
        return false;
    }
    if (n != kFormatVersion) {
        why = "format version " + std::to_string(n) + " (this build reads version " + std::to_string(kFormatVersion) + ")";
        return false;
    }
    s.format = (int)n;
    auto get_int = [&](const char* k, long long lo, long long hi, int& dst) {
        if (!m.count(k) || !parse_int(m[k], n) || n < lo || n > hi) {
            why = std::string("missing or invalid ") + k;
            return false;
        }
        dst = (int)n;
        return true;
    };
    int tv = 0, ship = 0, has_ship = 0;
    if (!get_int("title_version", 0, 0xFFFF, tv) || !get_int("file_slot", 0, 2, s.file_slot) ||
        !get_int("start_point", -32768, 32767, s.start_point) || !get_int("start_room", -128, 127, s.start_room) ||
        !get_int("layer", -128, 127, s.layer) || !get_int("room", -128, 127, s.room) ||
        !get_int("link_angle_y", -32768, 65535, s.angle_y) || !get_int("link_proc", -1, 0xFFFF, s.link_proc) ||
        !get_int("on_ship", 0, 1, ship) || !get_int("date", 0, 0xFFFF, s.date) || !get_int("has_ship", 0, 1, has_ship) ||
        !get_int("ship_angle_y", -32768, 65535, s.ship_angle_y))
        return false;
    s.title_version = (uint32_t)tv;
    s.on_ship = ship != 0;
    s.has_ship = has_ship != 0;
    if (!m.count("ship_pos") || !parse_floats(m["ship_pos"], s.ship_pos, 3)) {
        why = "missing or invalid ship_pos";
        return false;
    }
    if (s.on_ship && !s.has_ship) {
        why = "on_ship without has_ship";
        return false;
    }
    if (!m.count("link_pos") || !parse_floats(m["link_pos"], s.pos, 3)) {
        why = "missing or invalid link_pos";
        return false;
    }
    if (!m.count("time_of_day") || !parse_floats(m["time_of_day"], &s.time_of_day, 1)) {
        why = "missing or invalid time_of_day";
        return false;
    }
    s.title_id = m["title_id"];
    s.game_hash = m["game_hash"];
    s.runtime = m["runtime"];
    s.created = m["created"];
    s.player_name = m["player_name"];
    s.stage = m["stage"];
    if (s.stage.empty() || s.stage.size() > 7) {
        why = "missing or invalid stage";
        return false;
    }
    for (auto& b : kBlobs) {
        if (!m.count(b.key) || !unhex(m[b.key], s.*b.field) || (s.*b.field).size() != b.size) {
            why = std::string("missing or invalid ") + b.key;
            return false;
        }
    }
    if (!savedata_checksum_ok(s.savedata)) {
        why = "save data checksum mismatch";
        return false;
    }
    out = std::move(s);
    return true;
}

}  // namespace pstate
