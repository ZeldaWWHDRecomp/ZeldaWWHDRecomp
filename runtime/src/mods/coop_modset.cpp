// SPDX-License-Identifier: MPL-2.0
// Online co-op: this player's gameplay mods, and switching to a party's set (coop_modset.h).
#include "coop_modset.h"

#include <cstdio>
#include <string>

#include "manager.h"
#include "mod_hash.h"
#include "mod_json.h"
#include "mods.h"
#include "packages.h"
#include "../randomizer_identity.h"

namespace mods::coop::modset {
namespace {

std::array<uint8_t, 32> sha(const std::string& text) {
    const std::string hex = mods::hash::sha256_text(text);
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 32 && 2 * i + 1 < hex.size(); i++) out[i] = (uint8_t)std::stoi(hex.substr(2 * i, 2), nullptr, 16);
    return out;
}
// a built-in mod's settings that change what it does (its factors; Jump anywhere's double jump)
std::string builtin_settings(std::string_view id) {
    std::string s;
    for (const auto& o : manager::traversal_options())
        if (std::string_view(o.profile_key).starts_with(std::string(id) + ".")) {
            char b[64];
            snprintf(b, sizeof b, "%s=%.3f;", o.profile_key, o.value());
            s += b;
        }
    if (id == "jump-anywhere") s += mods::double_jump() ? "double=1;" : "double=0;";
    return s;
}
constexpr const char* kBuiltinVersion = "builtin";  // (the port's own mods: their behaviour comes with the protocol)

}  // namespace

std::string name_of(const std::string& id) {
    if (id == "randomizer") return "Randomizer seed";
    if (const manager::Entry* e = manager::find(id)) return e->name;
    for (const packages::View& v : packages::list()) if (v.id == id) return v.name;
    return id;
}
std::string describe(const std::vector<Problem>& ps) {
    std::string out;
    for (size_t i = 0; i < ps.size() && i < 6; i++) out += (i ? "; " : "") + words(ps[i], name_of(ps[i].id));
    if (ps.size() > 6) out += "; and " + std::to_string(ps.size() - 6) + " more";
    return out;
}

Set current() {
    Set out;
    for (const manager::Entry& e : manager::entries()) {
        if (!manager::changes_gameplay(e.id) || !e.enabled || !e.enabled()) continue;
        ModRef r;
        r.id = e.id, r.version = kBuiltinVersion, r.name = e.name;
        r.hash = sha(std::string(e.id) + "\n" + kBuiltinVersion + "\n" + builtin_settings(e.id));
        out.push_back(std::move(r));
    }
    for (const packages::View& v : packages::list()) {
        if (!v.gameplay || !v.enabled || !valid_id(v.id)) continue;
        ModRef r;
        r.id = v.id, r.version = v.version.substr(0, kMaxVersion), r.name = v.name;
        std::string content = v.id + "\n" + v.version + "\n" + v.fingerprint + "\n";
        auto files = v.content_hashes;
        std::sort(files.begin(), files.end());
        for (const auto& [path, h] : files) content += path + "=" + h + "\n";
        for (const packages::Option& o : v.options) content += o.id + "=" + json::dump(o.value) + "\n";
        r.hash = sha(content);
        out.push_back(std::move(r));
    }
    if (randomizer::compiled_seed()) {  // (its RPX's SHA-256: the seed and its settings)
        ModRef r;
        r.id = "randomizer", r.name = "Randomizer seed";
        r.hash = sha(std::string("randomizer\n") + g_guest_build_rpx_sha256);
        out.push_back(std::move(r));
    }
    if (out.size() > kMaxMods) out.resize(kMaxMods);
    return out;
}

bool can_use(const Set& party, std::string& why) {
    const Set mine = current();
    const std::vector<packages::View> installed = packages::list();
    for (const ModRef& p : party) {
        if (std::find(mine.begin(), mine.end(), p) != mine.end()) continue;
        if (p.id == "randomizer") return why = "the party plays another randomized game (start that seed to join)", false;
        if (const manager::Entry* e = manager::find(p.id)) {
            // a built-in mod in other settings: they can't be set from here
            if (e->enabled && e->enabled() && manager::changes_gameplay(p.id))
                return why = std::string(e->name) + ": set it like the party (Mods), its settings differ", false;
            continue;  // (turned on below; its settings checked once it is on)
        }
        const auto v = std::find_if(installed.begin(), installed.end(), [&](const packages::View& x) { return x.id == p.id; });
        if (v == installed.end()) return why = p.id + " " + p.version + " is not installed", false;
        if (v->version != p.version) return why = v->name + ": the party has " + p.version + ", you have " + v->version + " installed", false;
        if (!v->compatible) return why = v->name + " can't run here (" + v->reason + ")", false;
    }
    return true;
}

bool use(const Set& party, bool& restart, std::string& why) {
    restart = false;
    if (!can_use(party, why)) return false;
    const Set mine = current();
    const auto in = [](const Set& s, const std::string& id) { return std::any_of(s.begin(), s.end(), [&](const ModRef& x) { return x.id == id; }); };
    const std::vector<packages::View> installed = packages::list();
    // the other gameplay mods off, the party's on
    for (const ModRef& m : mine) {
        if (in(party, m.id) || m.id == "randomizer") continue;
        if (manager::find(m.id)) { if (!manager::set_enabled(m.id, false)) return why = "can't turn off " + m.name, false; continue; }
        std::string err;
        if (!packages::enable(m.id, false, err)) return why = "can't turn off " + m.name + ": " + err, false;
    }
    for (const ModRef& p : party) {
        if (in(mine, p.id) || p.id == "randomizer") continue;
        if (const manager::Entry* e = manager::find(p.id)) {
            if (!manager::set_enabled(p.id, true)) return why = "can't turn on " + p.id, false;
            restart |= e->restart_required;
            continue;
        }
        std::string err;
        if (!packages::enable(p.id, true, err)) return why = "can't turn on " + p.id + ": " + err, false;
    }
    for (const packages::View& v : packages::list())
        if (v.gameplay && v.pending_restart) restart = true;
    mods::trace("coop: switched to the party's mods (%zu)%s", party.size(), restart ? ", a restart needed" : "");
    return true;
}

}  // namespace mods::coop::modset
