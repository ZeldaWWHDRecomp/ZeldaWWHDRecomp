// SPDX-License-Identifier: MPL-2.0
#include "mods.h"

#include <algorithm>
#include <cstdio>

#include "sha256.h"

namespace online::proto {

bool valid_mod_id(std::string_view id) {
    if (id.empty() || id.size() > kMaxModId) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

bool valid_mod_version(std::string_view v) {
    if (v.size() > kMaxModVersion) return false;
    for (char c : v)
        if (c < 0x21 || c > 0x7E) return false;
    return true;
}

bool ModRules::has_whitelist() const {
    return std::any_of(rules.begin(), rules.end(), [](const ModRule& r) { return r.kind == ModRuleKind::kAllowed; });
}

uint8_t ModRules::flags() const {
    return uint8_t((vanilla_only ? 1 : 0) | (has_whitelist() ? 2 : 0) | (rules.empty() ? 0 : 4));
}

uint32_t ModRules::digest() const {
    Bytes b;
    Writer w(b);
    w.u8(vanilla_only ? 1 : 0);
    for (auto& r : rules) put_mod_rule(w, r);
    uint8_t h[32];
    sha256(b.data(), b.size(), h);
    return uint32_t(h[0]) | (uint32_t(h[1]) << 8) | (uint32_t(h[2]) << 16) | (uint32_t(h[3]) << 24);
}

namespace {
bool matches(const ModRule& r, const ModRef& m) {
    if (r.id != m.id) return false;
    if (!r.version.empty() && r.version != m.version) return false;
    if (r.has_hash && r.hash != m.hash) return false;
    return true;
}
std::string wanted_of(const ModRule& r) { return r.version.empty() ? std::string() : r.version; }
}  // namespace

std::vector<ModProblem> check_mods(const ModRules& rules, const std::vector<ModRef>& mods) {
    std::vector<ModProblem> out;
    auto add = [&](ModProblemKind k, const std::string& id, const std::string& wanted, const std::string& have) {
        if (out.size() < kMaxModProblems) out.push_back({k, id, wanted, have});
    };
    if (rules.vanilla_only) {
        for (auto& m : mods) add(ModProblemKind::kVanillaOnly, m.id, "", m.version);
        return out;
    }
    // required: present, in the right version and content
    for (auto& r : rules.rules) {
        if (r.kind != ModRuleKind::kRequired) continue;
        auto it = std::find_if(mods.begin(), mods.end(), [&](const ModRef& m) { return m.id == r.id; });
        if (it == mods.end())
            add(ModProblemKind::kMissing, r.id, wanted_of(r), "");
        else if (!matches(r, *it))
            add(ModProblemKind::kWrongVersion, r.id, r.version.empty() ? "same version, other content" : r.version, it->version);
    }
    bool whitelist = rules.has_whitelist();
    for (auto& m : mods) {
        // banned
        bool banned = false;
        for (auto& r : rules.rules)
            if (r.kind == ModRuleKind::kBanned && matches(r, m)) banned = true;
        if (banned) {
            add(ModProblemKind::kBanned, m.id, "", m.version);
            continue;
        }
        if (!whitelist) continue;
        // on the allowed list (or required): the id, then version and content
        const ModRule* by_id = nullptr;
        bool ok = false;
        for (auto& r : rules.rules) {
            if (r.kind == ModRuleKind::kBanned || r.id != m.id) continue;
            by_id = &r;
            if (matches(r, m)) ok = true;
        }
        if (ok) continue;
        if (!by_id)
            add(ModProblemKind::kNotAllowed, m.id, "", m.version);
        else if (by_id->kind == ModRuleKind::kAllowed)
            add(ModProblemKind::kWrongVersion, m.id, by_id->version.empty() ? "same version, other content" : by_id->version,
                m.version);
        // (a required mod in the wrong version is reported once, above)
    }
    return out;
}

std::vector<ModProblem> party_mod_problems(const std::vector<ModRef>& party, const std::vector<ModRef>& mine) {
    std::vector<ModProblem> out;
    auto add = [&](ModProblemKind k, const std::string& id, const std::string& wanted, const std::string& have) {
        if (out.size() < kMaxModProblems) out.push_back({k, id, wanted, have});
    };
    for (auto& p : party) {
        auto it = std::find_if(mine.begin(), mine.end(), [&](const ModRef& m) { return m.id == p.id; });
        if (it == mine.end()) add(ModProblemKind::kPartyHas, p.id, p.version, "");
        else if (!(*it == p)) add(ModProblemKind::kPartyOther, p.id, p.version, it->version);
    }
    for (auto& m : mine)
        if (std::none_of(party.begin(), party.end(), [&](const ModRef& p) { return p.id == m.id; }))
            add(ModProblemKind::kYouHave, m.id, "", m.version);
    return out;
}

bool parse_mod_rule(std::string_view text, ModRuleKind kind, ModRule& out) {
    out = ModRule();
    out.kind = kind;
    std::string_view id = text, ver, hash;
    if (auto h = id.find('#'); h != std::string_view::npos) {
        hash = id.substr(h + 1);
        id = id.substr(0, h);
    }
    if (auto a = id.find('@'); a != std::string_view::npos) {
        ver = id.substr(a + 1);
        id = id.substr(0, a);
    }
    if (!valid_mod_id(id)) return false;
    out.id = std::string(id);
    if (ver == "*") ver = {};
    if (!valid_mod_version(ver)) return false;
    out.version = std::string(ver);
    if (!hash.empty()) {
        if (hash.size() != 64) return false;
        for (size_t i = 0; i < 32; ++i) {
            unsigned v = 0;
            std::string two(hash.substr(2 * i, 2));
            if (std::sscanf(two.c_str(), "%2x", &v) != 1) return false;
            out.hash[i] = uint8_t(v);
        }
        out.has_hash = true;
    }
    return true;
}

std::string mod_rule_text(const ModRule& r) {
    std::string s = r.id;
    if (!r.version.empty()) s += "@" + r.version;
    if (r.has_hash) {
        if (r.version.empty()) s += "@*";
        s += "#";
        char b[3];
        for (uint8_t c : r.hash) {
            std::snprintf(b, 3, "%02x", c);
            s += b;
        }
    }
    return s;
}

void put_mod_refs(Writer& w, const std::vector<ModRef>& mods) {
    size_t n = std::min(mods.size(), kMaxMods);
    w.u8(uint8_t(n));
    for (size_t i = 0; i < n; ++i) {
        w.str8(mods[i].id);
        w.str8(mods[i].version);
        w.bytes(mods[i].hash.data(), 32);
    }
}

bool get_mod_refs(Reader& r, std::vector<ModRef>& out) {
    size_t n = r.u8();
    if (n > kMaxMods) return false;
    for (size_t i = 0; i < n && r.ok(); ++i) {
        ModRef m;
        m.id = r.str8(kMaxModId);
        m.version = r.str8(kMaxModVersion);
        r.bytes(m.hash.data(), 32);
        if (!r.ok() || !valid_mod_id(m.id) || !valid_mod_version(m.version)) return false;
        out.push_back(std::move(m));
    }
    return r.ok();
}

void put_mod_rule(Writer& w, const ModRule& r) {
    w.u8(uint8_t(r.kind));
    w.str8(r.id);
    w.str8(r.version);
    w.u8(r.has_hash ? 1 : 0);
    if (r.has_hash) w.bytes(r.hash.data(), 32);
}

bool get_mod_rule(Reader& r, ModRule& out) {
    out = ModRule();
    uint8_t k = r.u8();
    if (k < 1 || k > 3) return false;
    out.kind = ModRuleKind(k);
    out.id = r.str8(kMaxModId);
    out.version = r.str8(kMaxModVersion);
    out.has_hash = r.u8() != 0;
    if (out.has_hash) r.bytes(out.hash.data(), 32);
    return r.ok() && valid_mod_id(out.id) && valid_mod_version(out.version);
}

void put_mod_problems(Writer& w, const std::vector<ModProblem>& p) {
    size_t n = std::min(p.size(), kMaxModProblems);
    w.u8(uint8_t(n));
    for (size_t i = 0; i < n; ++i) {
        w.u8(uint8_t(p[i].problem));
        w.str8(p[i].id.substr(0, kMaxModId));
        w.str8(p[i].wanted.substr(0, 32));
        w.str8(p[i].have.substr(0, kMaxModVersion));
    }
}

bool get_mod_problems(Reader& r, std::vector<ModProblem>& out) {
    size_t n = r.u8();
    if (n > kMaxModProblems) return false;
    for (size_t i = 0; i < n && r.ok(); ++i) {
        ModProblem p;
        uint8_t k = r.u8();
        if (k < 1 || k > 8) return false;
        p.problem = ModProblemKind(k);
        p.id = r.str8(kMaxModId);
        p.wanted = r.str8(32);
        p.have = r.str8(kMaxModVersion);
        out.push_back(std::move(p));
    }
    return r.ok();
}

const char* mod_problem_text(ModProblemKind k) {
    switch (k) {
        case ModProblemKind::kMissing: return "required by this server";
        case ModProblemKind::kNotAllowed: return "not allowed on this server";
        case ModProblemKind::kBanned: return "banned on this server";
        case ModProblemKind::kWrongVersion: return "another version is required";
        case ModProblemKind::kVanillaOnly: return "this server allows no gameplay mods";
        case ModProblemKind::kPartyHas: return "the party plays with it";
        case ModProblemKind::kYouHave: return "the party plays without it";
        case ModProblemKind::kPartyOther: return "the party plays another version";
    }
    return "?";
}

}  // namespace online::proto
