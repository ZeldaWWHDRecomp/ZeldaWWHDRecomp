// SPDX-License-Identifier: MPL-2.0
// Server mod rules (docs/online-server.md, "Mod rules"). Only gameplay mods count (a mod whose manifest says it
// changes gameplay); look-only mods (textures, models, UI, graphics packs) are never sent or checked. The server
// never sends mods or any code or content: it only says which gameplay mods it requires, allows or bans.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wire.h"

namespace online::proto {

constexpr size_t kMaxMods = 32;        // gameplay mods a client may list
constexpr size_t kMaxModId = 48, kMaxModVersion = 24;
constexpr size_t kMaxModRules = 128;
constexpr size_t kMaxModProblems = 20;

// one enabled gameplay mod of a client
struct ModRef {
    std::string id;       // [a-z0-9._-], <= 48
    std::string version;  // <= 24
    std::array<uint8_t, 32> hash{};  // the client's hash of id + version + content (compared as bytes)
    bool operator==(const ModRef&) const = default;
};

enum class ModRuleKind : uint8_t { kRequired = 1, kAllowed = 2, kBanned = 3 };

struct ModRule {
    ModRuleKind kind = ModRuleKind::kAllowed;
    std::string id;
    std::string version;   // "": any version
    bool has_hash = false;
    std::array<uint8_t, 32> hash{};
};

struct ModRules {
    bool vanilla_only = false;  // no gameplay mods at all
    std::vector<ModRule> rules;
    bool empty() const { return !vanilla_only && rules.empty(); }
    bool has_whitelist() const;
    uint8_t flags() const;      // bit 0 vanilla only, bit 1 has a whitelist, bit 2 has rules
    uint32_t digest() const;    // changes when the rules change (clients can cache them)
};

enum class ModProblemKind : uint8_t {
    kMissing = 1,       // a required mod is not enabled
    kNotAllowed = 2,    // not on the server's allowed list
    kBanned = 3,
    kWrongVersion = 4,  // the right mod, another version or content (hash)
    kVanillaOnly = 5,   // the server allows no gameplay mods
    // party matching (the party's mod set is its leader's list)
    kPartyHas = 6,      // the party has it, you don't (wanted: the party's version)
    kYouHave = 7,       // you have it, the party doesn't (have: yours)
    kPartyOther = 8,    // same mod, another version or content
};
struct ModProblem {
    ModProblemKind problem = ModProblemKind::kNotAllowed;
    std::string id;
    std::string wanted;  // the version the server wants ("" any; for a hash mismatch: "same version, other content")
    std::string have;    // the client's version ("" when missing)
};

// what keeps this client out (empty: it may join)
std::vector<ModProblem> check_mods(const ModRules& rules, const std::vector<ModRef>& mods);
// how a member's mods differ from its party's set (empty: identical as a set)
std::vector<ModProblem> party_mod_problems(const std::vector<ModRef>& party, const std::vector<ModRef>& mine);

// a well-formed mod id / version
bool valid_mod_id(std::string_view id);
bool valid_mod_version(std::string_view v);

// "id", "id@version", "id@version#<64 hex>" ("*" or "" as the version: any); false on a bad entry
bool parse_mod_rule(std::string_view text, ModRuleKind kind, ModRule& out);
std::string mod_rule_text(const ModRule& r);

// codecs
void put_mod_refs(Writer& w, const std::vector<ModRef>& mods);
bool get_mod_refs(Reader& r, std::vector<ModRef>& out);
void put_mod_rule(Writer& w, const ModRule& r);
bool get_mod_rule(Reader& r, ModRule& out);
void put_mod_problems(Writer& w, const std::vector<ModProblem>& p);
bool get_mod_problems(Reader& r, std::vector<ModProblem>& out);
const char* mod_problem_text(ModProblemKind k);

}  // namespace online::proto
