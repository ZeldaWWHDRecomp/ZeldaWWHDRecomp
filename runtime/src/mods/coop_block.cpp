// SPDX-License-Identifier: MPL-2.0
// Online co-op: blocked and muted players, the port's part (coop_block.h): the list in the user folder.
#include "coop_block.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "coop_net.h"
#include "mods.h"
#include "../platform/host.h"

namespace mods::coop::block {
namespace {
namespace fs = std::filesystem;
std::mutex g_mu;
List g_list;
bool g_loaded = false;

fs::path file() { return fs::u8path(host::config_dir()) / "online-blocks.txt"; }
// (test aid: WWHD_COOP_BLOCKS=<file>: that file instead, so a test never touches the player's own list)
fs::path path() { const char* e = getenv("WWHD_COOP_BLOCKS"); return e && *e ? fs::u8path(e) : file(); }
void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream f(path());
    if (!f) return;
    std::stringstream s;
    s << f.rdbuf();
    g_list = List::parse(s.str());
    mods::trace("coop: %zu blocked or muted players", g_list.entries().size());
}
void save_locked() {
    const fs::path p = path(), tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << g_list.text();
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
}
}  // namespace

List current() {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_list;
}

bool set(int id, Kind kind, std::string& why) {
    if (net::member(id)) return why = "a party member (leave the party instead)", false;
    const uint64_t key = net::player_key(id);
    if (!key) return why = "only players met through a server can be blocked", false;
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    const bool was_blocked = g_list.blocked(key);
    if (!g_list.set(key, kind, net::name(id))) return why = "the list is full", false;
    save_locked();
    if (kind == kBlocked) net::tell_block(key, true);
    else if (was_blocked) net::tell_block(key, false);  // (a block turned into a mute: they are seen again)
    net::tell_mute(key, kind == kMuted);  // (the server stops their emotes, effects and sounds too)
    mods::trace("coop: player %d %s", id, kind == kBlocked ? "blocked" : "muted");
    return true;
}

bool undo(uint64_t key) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    const bool was_blocked = g_list.blocked(key);
    if (!g_list.clear(key)) return false;
    save_locked();
    if (was_blocked) net::tell_block(key, false);
    else net::tell_mute(key, false);
    mods::trace("coop: a player unblocked or unmuted");
    return true;
}

bool blocked_id(int id) {
    const uint64_t key = net::player_key(id);
    if (!key || net::member(id)) return false;
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_list.blocked(key);
}
bool silent_id(int id) {
    const uint64_t key = net::player_key(id);
    if (!key || net::member(id)) return false;
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_list.silent(key);
}

void resend() {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    size_t n = 0;
    for (const Entry& e : g_list.entries())
        e.kind == kBlocked ? net::tell_block(e.key, true) : net::tell_mute(e.key, true), n++;
    if (n) mods::trace("coop: %zu blocks and mutes sent to the server again", n);
}

}  // namespace mods::coop::block
