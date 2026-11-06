// SPDX-License-Identifier: MPL-2.0
// Online co-op: the mods a player plays with that change the game (GAPS C5 party matching, server mod rules).
//
// Only mods that change how the game plays count ("gameplay" mods): the built-in ones manager::changes_gameplay
// names, packages whose manifest says "gameplay": true (Mod SDK docs), and a randomized game (one entry, id
// "randomizer", its RPX's SHA-256). Look-only mods (graphics packs, cameras, texts) are never sent or compared.
//
// Each entry is {id, version, hash}: the hash is SHA-256 of the id, the version and what the mod is (a package: its
// code's fingerprint, its files' hashes and its option values; a built-in mod: its option values). A party plays with
// its leader's set: a player whose set differs can't join it, and is told exactly which mods differ. Players outside
// the party (presence) are never checked. Through a server, the server's rules can also require, allow or ban mods.
//
// The pure part here (the wire form, the comparison, the words) is checked by coop_modset_test without the game.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mods::coop::modset {

constexpr size_t kMaxMods = 32, kMaxId = 48, kMaxVersion = 24;
struct ModRef {
    std::string id, version;
    std::array<uint8_t, 32> hash{};
    std::string name;  // for the words only (not sent)
    bool operator==(const ModRef& o) const { return id == o.id && version == o.version && hash == o.hash; }
};
using Set = std::vector<ModRef>;

// The problems (the server's codes, the same on the direct transport): 1 a required mod missing, 2 not on the allowed
// list, 3 banned, 4 wrong version or hash, 5 the server takes no gameplay mods; 6 the party has it and you don't,
// 7 you have it and the party doesn't, 8 the same mod in another version or build
struct Problem {
    uint8_t code = 0;
    std::string id, wanted, have;
};
enum : uint8_t { kMissing = 1, kNotAllowed = 2, kBanned = 3, kWrongVersion = 4, kVanillaOnly = 5, kPartyHas = 6, kOnlyYou = 7,
                 kPartyOther = 8 };

// a valid id: 1-48 of [a-z0-9._-]
inline bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > kMaxId) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

// the wire form: u8 n, n x {str8 id, str8 version, hash 32}
inline std::string encode(const Set& s) {
    std::string out(1, char(std::min(s.size(), kMaxMods)));
    for (size_t i = 0; i < s.size() && i < kMaxMods; i++) {
        const std::string id = s[i].id.substr(0, kMaxId), v = s[i].version.substr(0, kMaxVersion);
        out += char(id.size()), out += id, out += char(v.size()), out += v;
        out.append((const char*)s[i].hash.data(), 32);
    }
    return out;
}
// false: malformed (too long, a bad id, more than kMaxMods); `used` the bytes taken
inline bool decode(const uint8_t* d, size_t n, Set& out, size_t* used = nullptr) {
    out.clear();
    if (n < 1 || d[0] > kMaxMods) return false;
    size_t at = 1;
    for (int i = 0; i < d[0]; i++) {
        ModRef r;
        if (at >= n) return false;
        const size_t li = d[at++];
        if (li > kMaxId || at + li > n) return false;
        r.id.assign((const char*)d + at, li), at += li;
        if (at >= n) return false;
        const size_t lv = d[at++];
        if (lv > kMaxVersion || at + lv + 32 > n) return false;
        r.version.assign((const char*)d + at, lv), at += lv;
        std::copy(d + at, d + at + 32, r.hash.begin()), at += 32;
        if (!valid_id(r.id)) return false;
        out.push_back(std::move(r));
    }
    if (used) *used = at;
    return true;
}

// a set as the party compares it: the same entries in any order
inline bool same(const Set& a, const Set& b) {
    if (a.size() != b.size()) return false;
    for (const ModRef& m : a)
        if (std::find(b.begin(), b.end(), m) == b.end()) return false;
    return true;
}
// what differs between the party's set and this player's (codes 6, 7, 8), in the party's order, then this player's
inline std::vector<Problem> diff(const Set& party, const Set& mine) {
    std::vector<Problem> out;
    for (const ModRef& p : party) {
        const auto m = std::find_if(mine.begin(), mine.end(), [&](const ModRef& x) { return x.id == p.id; });
        if (m == mine.end()) out.push_back({kPartyHas, p.id, p.version, ""});
        else if (!(*m == p)) out.push_back({kPartyOther, p.id, p.version, m->version});
    }
    for (const ModRef& m : mine)
        if (std::none_of(party.begin(), party.end(), [&](const ModRef& x) { return x.id == m.id; })) out.push_back({kOnlyYou, m.id, "", m.version});
    return out;
}
// one problem in words: "Jump anywhere 1.2: the party plays with it, you don't"
inline std::string words(const Problem& p, const std::string& name = "") {
    const std::string who = (name.empty() ? p.id : name);
    const auto ver = [](const std::string& v) { return v.empty() || v == "builtin" ? std::string() : " " + v; };  // (the port's own mods: no version)
    switch (p.code) {
    case kMissing: return who + ver(p.wanted) + ": the server needs it, you don't have it on";
    case kNotAllowed: return who + ver(p.have) + ": not allowed on this server";
    case kBanned: return who + ver(p.have) + ": banned on this server";
    case kWrongVersion: return who + ": the server wants" + (p.wanted.empty() ? " another build" : ver(p.wanted)) + ", you have" + (p.have.empty() ? " another" : ver(p.have));
    case kVanillaOnly: return who + ver(p.have) + ": this server takes no mods that change the game";
    case kPartyHas: return who + ver(p.wanted) + ": the party plays with it, you don't";
    case kOnlyYou: return who + ver(p.have) + ": you play with it, the party doesn't";
    case kPartyOther: return who + ": the party has" + (p.wanted.empty() ? " another build" : ver(p.wanted)) + ", you have" + (p.have.empty() ? " another" : ver(p.have)) +
                             (p.wanted == p.have ? " (other files or settings)" : "");
    default: return who + ": not accepted";
    }
}
// the problem list as one text (at most `most` lines named, then "and n more")
inline std::string words(const std::vector<Problem>& ps, size_t most = 6) {
    std::string out;
    for (size_t i = 0; i < ps.size() && i < most; i++) out += (i ? "; " : "") + words(ps[i]);
    if (ps.size() > most) out += "; and " + std::to_string(ps.size() - most) + " more";
    return out;
}

// ---- the port (coop_modset.cpp)
// the problems in words with the mods' names where this game knows them (built-in mods, installed packages)
std::string describe(const std::vector<Problem>& ps);
std::string name_of(const std::string& id);
// this player's gameplay mods now (built-in, packages turned on, a randomized game)
Set current();
// can this game switch to `party`'s set: every mod of it installed in that version and build. False with the reason
// (the mod that is missing, in words)
bool can_use(const Set& party, std::string& why);
// switch to `party`'s set: its mods turned on, the other gameplay mods off. `restart` says that some change waits for a
// restart. False with the reason (nothing changed then)
bool use(const Set& party, bool& restart, std::string& why);

}  // namespace mods::coop::modset
