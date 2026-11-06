// SPDX-License-Identifier: MPL-2.0
// Online co-op's settings (docs/online-coop.md): the "Online" tab of the Settings window and the "Online" tab of the
// pause menu's settings page (game_settings). Both read and change mods::coop::sync's settings, so they always show
// the same values. Nothing goes online before the player has accepted the privacy notice and switched it on.
// Environment variables (WWHD_COOP...) still start a session for tests.
#include "coop_settings.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "coop_block.h"
#include "coop_emotes.h"
#include "coop_modset.h"
#include "coop_net.h"
#include "coop_servers.h"
#include "coop_sync.h"
#include "game_settings.h"
#include "imgui.h"
#include "../input_map.h"
#include "../overlay/extensions.h"
#include "../overlay/hostui.h"
#include "../overlay/text_entry.h"
#include "../input.h"
#include "../savestate.h"

namespace mods::coop::settings_ui {
namespace {

namespace sync = mods::coop::sync;
namespace net = mods::coop::net;
namespace servers = mods::coop::servers;


// a change from either page (main thread): kept, and the session follows at the game's next step
template <class F> void change(F f) {
    sync::Settings s = sync::settings();
    f(s);
    sync::save_settings(s);
}

// the overlay's text prompt for a setting, else the host's own
void ask_text(const std::string& now, size_t most, void (*apply)(const std::string&)) {
    text_entry::Request r;
    std::u16string init;
    for (const char ch : now) init += (char16_t)(unsigned char)ch;
    r.initial = init;
    r.max_len = (int)most;
    auto done = [apply](bool ok, std::u16string text) {
        if (!ok) return;
        std::string out;
        for (const char16_t ch : text) if (ch >= 0x20 && ch < 0x7F) out += (char)ch;  // (addresses and names: plain ASCII)
        while (!out.empty() && out.back() == ' ') out.pop_back();
        while (!out.empty() && out.front() == ' ') out.erase(out.begin());
        if (!out.empty()) hostui::post([apply, out] { apply(out); });
    };
    if (!text_entry::start(r, done)) input::prompt_text(r.initial, (int)most, done);
}
void set_addr(const std::string& a) { change([&](sync::Settings& s) { s.addr = a; }); }
void set_name(const std::string& n) { change([&](sync::Settings& s) { s.name = n; }); }

// ---- the Settings window's tab ----
bool party_leader = false;
// (C5) refused by a party for the mods: what differs, and switching to the party's set when every mod is installed
std::string g_mods_done;  // what the last switch said
void draw_party_mods() {
    const std::string got = net::refused_mods();
    modset::Set party;
    if (got.empty() || !modset::decode((const uint8_t*)got.data(), got.size(), party)) {
        // (a server's rules: each mod it doesn't take; no set to switch to)
        const auto issues = net::mod_issues();
        if (issues.empty()) return;
        ImGui::Spacing();
        ImGui::TextUnformatted("The server doesn't take these mods:");
        for (const net::ModIssue& i : issues) ImGui::BulletText("%s", modset::words(modset::Problem{i.code, i.id, i.wanted, i.have}, modset::name_of(i.id)).c_str());
        ImGui::TextDisabled("Change them in Mods, then connect again.");
        return;
    }
    const auto problems = modset::diff(party, modset::current());
    ImGui::Spacing();
    ImGui::TextUnformatted("The party plays with other mods that change the game:");
    for (const modset::Problem& p : problems) ImGui::BulletText("%s", modset::words(p, modset::name_of(p.id)).c_str());
    std::string why;
    if (problems.empty()) ImGui::TextDisabled("Your mods match the party's now: connect again.");
    else if (!modset::can_use(party, why)) ImGui::TextDisabled("Can't use the party's mods: %s.", why.c_str());
    else if (ImGui::Button("Use the party's mods")) hostui::post([party] {  // (the mods change on the main thread)
        bool restart = false;
        std::string no;
        g_mods_done = modset::use(party, restart, no) ? (restart ? "Done. Restart the game for the change, then connect again." : "Done: connect again.")
                                                      : "Can't use the party's mods: " + no + ".";
    });
    if (!g_mods_done.empty()) ImGui::TextWrapped("%s", g_mods_done.c_str());
}

// (C14) players outside the party near by: block (hidden, the server told) or mute (seen, not heard); and the list
void draw_blocks() {
    bool any = false;
    for (int id = 0; id < net::kMaxPlayers; id++) {
        if (id == net::my_id() || !net::connected(id) || net::member(id) || !net::player_key(id) || block::blocked_id(id)) continue;
        if (!any) ImGui::TextUnformatted("Players near you outside your party"), any = true;
        ImGui::BulletText("%s", net::name(id).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("Block##b" + std::to_string(id)).c_str())) hostui::post([id] { std::string why; if (!block::set(id, block::kBlocked, why)) ss::notice("Can't block: " + why); });
        if (!block::silent_id(id)) {
            ImGui::SameLine();
            if (ImGui::SmallButton(("Mute##m" + std::to_string(id)).c_str())) hostui::post([id] { std::string why; if (!block::set(id, block::kMuted, why)) ss::notice("Can't mute: " + why); });
        }
    }
    const block::List list = block::current();
    if (list.entries().empty()) return;
    ImGui::TextUnformatted("Blocked and muted players");
    for (const block::Entry& e : list.entries()) {
        ImGui::BulletText("%s (%s)", e.name.empty() ? "someone" : e.name.c_str(), e.kind == block::kBlocked ? "blocked" : "muted");
        ImGui::SameLine();
        const uint64_t key = e.key;
        if (ImGui::SmallButton(("Undo##u" + std::to_string(key)).c_str())) hostui::post([key] { block::undo(key); });
    }
    ImGui::TextDisabled("Blocked: you and they don't see each other. Muted: seen, but no sounds or emotes from them.");
}

void draw_tab() {
    sync::Settings s = sync::settings();
    ImGui::TextWrapped("Play together with friends: up to three directly (one of you hosts, the others join), more through a server.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Privacy");
    ImGui::TextWrapped("%s", sync::kNotice);
    bool accepted = s.notice_ok;
    if (ImGui::Checkbox("I understand: accept", &accepted)) {
        hostui::post([accepted] { change([&](sync::Settings& x) { x.notice_ok = accepted, x.on = accepted && x.on; }); });
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!s.notice_ok);
    bool on = s.on && s.notice_ok;
    if (ImGui::Checkbox("Online co-op", &on)) hostui::post([on] { change([&](sync::Settings& x) { x.on = on; }); });
    ImGui::EndDisabled();
    if (!s.notice_ok) ImGui::TextDisabled("Accept the privacy notice first.");
    ImGui::Spacing();
    static char addr[128], name[32];
    static int port = 0;
    static bool filled = false;
    if (!filled) snprintf(addr, sizeof addr, "%s", s.addr.c_str()), snprintf(name, sizeof name, "%s", s.name.c_str()), port = s.port, filled = true;
    if (net::server_built()) {
        if (ImGui::RadioButton("Directly", !s.server)) hostui::post([] { change([](sync::Settings& x) { x.server = false; }); });
        ImGui::SameLine();
        if (ImGui::RadioButton("Through a server", s.server)) hostui::post([] { change([](sync::Settings& x) { x.server = true; }); });
    }
    if (s.server && net::server_built()) {
        static char srv[128];
        static int sport = 0;
        static bool sfilled = false;
        static std::string shown_addr;
        static int shown_port = 0;
        // (filled again when the server changes elsewhere: a saved server chosen, the pause page)
        if (!sfilled || shown_addr != s.server_addr || shown_port != s.server_port)
            snprintf(srv, sizeof srv, "%s", s.server_addr.c_str()), sport = s.server_port, sfilled = true, shown_addr = s.server_addr, shown_port = s.server_port;
        ImGui::InputText("Server address", srv, sizeof srv);
        if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([a = std::string(srv)] { change([&](sync::Settings& x) { if (!a.empty()) x.server_addr = a; }); });
        ImGui::InputInt("Server port (UDP)", &sport, 0);
        if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([p = std::clamp(sport, 1, 65535)] { change([&](sync::Settings& x) { x.server_port = p; }); });
        ImGui::TextDisabled("A server you or a friend runs (wwhd-online-server). Everyone on it sees each other nearby;");
        ImGui::TextDisabled("your party shares enemies and progress.");
        const std::vector<servers::Server> saved = net::saved_servers();
        if (!saved.empty()) {
            ImGui::TextUnformatted("Saved servers");
            for (const servers::Server& v : saved) {
                const bool now = v.addr == s.server_addr && v.port == s.server_port;
                ImGui::PushID((v.addr + ":" + std::to_string(v.port)).c_str());
                ImGui::BulletText("%s (%s:%d)%s", v.name.c_str(), v.addr.c_str(), v.port, now ? ", chosen" : "");
                ImGui::SameLine();
                ImGui::BeginDisabled(now);
                if (ImGui::SmallButton("Use")) hostui::post([a = v.addr, p = v.port] { change([&](sync::Settings& x) { x.server_addr = a, x.server_port = p; }); });
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Forget")) hostui::post([a = v.addr, p = v.port] { net::forget_server(a, p); });
                ImGui::SameLine();  // (C5) the server's mod rules, asked for without joining
                if (ImGui::SmallButton("Mod rules")) hostui::post([a = v.addr, p = v.port] { net::query_rules(a, p); });
                if (const std::string r = net::server_rules(v.addr, v.port); !r.empty()) ImGui::TextDisabled("    Mod rules: %s", r.c_str());
                ImGui::PopID();
            }
            if (std::none_of(saved.begin(), saved.end(), [&](const servers::Server& v) { return v.addr == s.server_addr && v.port == s.server_port; }) &&
                !s.server_addr.empty()) {
                ImGui::BulletText("%s:%d (not visited yet)", s.server_addr.c_str(), s.server_port);
                ImGui::SameLine();
                if (ImGui::SmallButton("Mod rules##new")) hostui::post([a = s.server_addr, p = s.server_port] { net::query_rules(a, p); });
                if (const std::string r = net::server_rules(s.server_addr, s.server_port); !r.empty()) ImGui::TextDisabled("    Mod rules: %s", r.c_str());
            }
            ImGui::TextDisabled("Servers are saved when you connect. Each server's key is remembered from your first visit, so");
            ImGui::TextDisabled("another server using its address is noticed. Forget also forgets the key.");
        }
        ImGui::Spacing();
        const std::string code = net::friend_code();
        ImGui::Text("Your friend code: %s", code.empty() ? "-" : code.c_str());
        if (!code.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) hostui::post([code] { hostui::set_clipboard(code); ss::notice("Friend code copied"); });
        }
        ImGui::TextDisabled("Tell it to friends: with it they invite you into their party.");
        if (net::via_server()) ImGui::TextDisabled("Parties on this server: up to %d players.", net::party_max());
        static char invite[32];
        static std::string invite_msg;
        ImGui::InputText("Friend's code", invite, sizeof invite);
        ImGui::SameLine();
        ImGui::BeginDisabled(!net::via_server());
        if (ImGui::Button("Invite")) {
            std::string why;
            invite_msg = net::invite(invite, why) ? "Sending the invite..." : why;
        }
        ImGui::EndDisabled();
        if (!net::via_server()) ImGui::TextDisabled("Go online through a server to invite.");
        else if (!invite_msg.empty()) ImGui::TextWrapped("%s", invite_msg.c_str());
        const auto waiting = net::invites();
        if (!waiting.empty()) ImGui::TextUnformatted("Invitations");
        for (const auto& [party, from] : waiting) {
            ImGui::BulletText("%s invites you to their party", from.c_str());
            ImGui::SameLine();
            if (ImGui::Button(("Join##" + std::to_string(party)).c_str())) hostui::post([p = party] { net::answer(p, true); });
            ImGui::SameLine();
            if (ImGui::Button(("No##" + std::to_string(party)).c_str())) hostui::post([p = party] { net::answer(p, false); });
        }
        if (net::via_server() && ImGui::Button("Leave the party")) hostui::post([] { net::leave_party(); });
        if (uint16_t flags = 0; [&] { bool l = false; return net::party_flags(flags, l) && (party_leader = l, true); }()) {
            ImGui::TextUnformatted(party_leader ? "Party switches (you lead: yours to set)" : "Party switches (the leader sets them)");
            ImGui::BeginDisabled(!party_leader);
            const struct { net::PartyFlag f; const char* name; } sw[] = {
                {net::kShareProgress, "Shared progress"}, {net::kShareRupees, "Shared rupees"},
                {net::kFriendlyFire, "Friendly fire"}, {net::kStayTogether, "Stay together (the leader's world)"}};
            for (const auto& w : sw) {
                bool v = flags & w.f;
                if (ImGui::Checkbox(w.name, &v)) {
                    const uint16_t next = (uint16_t)(v ? flags | w.f : flags & ~w.f);
                    std::string why;
                    if (!net::set_party_flags(next, why)) ss::notice("Can't change it: " + why);
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
    } else {
    if (ImGui::RadioButton("Host on this computer", s.role == 0)) hostui::post([] { change([](sync::Settings& x) { x.role = 0; }); });
    ImGui::SameLine();
    if (ImGui::RadioButton("Join a friend", s.role == 1)) hostui::post([] { change([](sync::Settings& x) { x.role = 1; }); });
    if (s.role == 1) {
        ImGui::InputText("Host's address", addr, sizeof addr);
        if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([a = std::string(addr)] { set_addr(a); });
        ImGui::TextDisabled("An IP address or a name, e.g. the host's VPN name.");
    }
    ImGui::InputInt("Port (UDP)", &port, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([p = std::clamp(port, 1, 65535)] { change([&](sync::Settings& x) { x.port = p; }); });
    if (s.role == 0) ImGui::TextDisabled("Your friends need to reach this port (on a VPN nothing else is needed).");
    }
    ImGui::InputText("Your name", name, sizeof name);
    if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([n = std::string(name)] { if (!n.empty()) set_name(n); });
    ImGui::TextUnformatted("Tunic");
    for (int i = 0; i < 4; i++) {
        ImGui::SameLine();
        if (ImGui::RadioButton(sync::kTunics[i], s.tunic == i)) hostui::post([i] { change([&](sync::Settings& x) { x.tunic = i; }); });
    }
    bool together = s.stay_together;
    if (ImGui::Checkbox("Stay together (play in the host's world)", &together))
        hostui::post([together] { change([&](sync::Settings& x) { x.stay_together = together; }); });
    ImGui::TextDisabled("Off: everyone plays their own Quest Log; progress is shared and saved by each player.");
    ImGui::TextUnformatted("Other players outside your party");
    for (int i = 0; i < 2; i++) {
        ImGui::SameLine();
        if (ImGui::RadioButton(i ? "See-through" : "Like any Link", s.strangers == i)) hostui::post([i] { change([&](sync::Settings& x) { x.strangers = i; }); });
    }
    ImGui::TextDisabled("Party members always look solid. (Players outside your party come with online servers.)");
    {
        bool radar = s.radar;
        if (ImGui::Checkbox("Far players on a sea radar", &radar)) hostui::post([radar] { change([&](sync::Settings& x) { x.radar = radar; }); });
        ImGui::TextDisabled("At sea, through a server: players too far to be drawn show as dots, party members in green.");
    }
    {  // (C28) PvP at sea: the toggle (hidden when the server has everyone fight), the server's rule, this player's state
        const net::PvpRules r = net::pvp_rules();
        uint32_t left = 0;
        const uint8_t st = net::pvp_state(&left);
        if (r.mode != 2) {
            bool pvp = s.pvp_sea;
            if (ImGui::Checkbox("PvP at sea", &pvp)) hostui::post([pvp] { change([&](sync::Settings& x) { x.pvp_sea = pvp; }); });
            ImGui::TextDisabled("Through a server that allows it: you and other players who have it on can fight on the open sea,");
            ImGui::TextDisabled("outside the safe zones around the big islands. A knockout costs some rupees; party members only by");
            ImGui::TextDisabled("the party's friendly fire.");
        }
        if (r.mode == 0) ImGui::TextDisabled("This server has no PvP.");
        else if (r.mode > 0) {
            if (r.mode == 2) ImGui::TextUnformatted("PvP at sea: on for everyone on this server.");
            ImGui::TextDisabled("A knockout costs %d%% of your rupees (at most %d). %zu safe zones.", r.ko_rupee_percent, r.ko_rupee_max, r.zones.size());
            ImGui::Text("Now: %s", (st & 4) ? "in a safe zone" : (st & 2) ? ("protected, " + std::to_string((left + 999) / 1000) + " s").c_str()
                                    : (st & 1) ? "PvP zone: you can fight" : "not fighting");
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", sync::status());
    if (s.on && s.notice_ok && net::role() == net::Role::kOff && ImGui::Button("Connect again")) hostui::post([] { sync::retry(); });
    draw_party_mods();
    if (net::role() != net::Role::kOff) {
        // (C29) the party, one line each (up to 16 through a server): its name, its tunic, and "Go to"
        if (net::party_size() > 1) ImGui::Text("Your party: %d of up to %d", net::party_size(), net::party_max());
        for (int id = 0; id < net::kMaxPlayers; ++id)
            if (id != net::my_id() && net::member(id)) {  // (party members: strangers are only shown)
                ImGui::PushID(id);
                ImGui::BulletText("%s (%s)", net::name(id).c_str(), sync::kTunics[net::tunic(id) & 3]);
                ImGui::SameLine();
                if (ImGui::SmallButton("Go to")) hostui::post([id] { sync::go_to(id); });
                ImGui::PopID();
            }
        // emotes (C22): the others see this player's Link play it; here a bubble over him names it
        ImGui::TextUnformatted("Emotes (keys: Settings > Controls, Online)");
        const input_map::Mapping& keys = input_map::current();
        for (int e = 0; e < emotes::kCount; e++) {
            if (e) ImGui::SameLine();
            const input_map::ShortcutKey k = e < 5 ? keys.shortcuts[input_map::kEmote1 + e].keys[0] : input_map::ShortcutKey{};
            const std::string label = std::string(emotes::kEmotes[e].name) + (k.code == input_map::kNoKey ? "" : " (" + input_map::shortcut_key_label(k) + ")");
            if (ImGui::Button(label.c_str())) emotes::mine(emotes::kEmotes[e].id);
        }
    }
    draw_blocks();
    ImGui::Spacing();
    ImGui::TextDisabled("Before your first session your save is backed up. A new Quest Log just for co-op is recommended.");
}

// ---- the pause menu's page ----
int g_emote_choice = 0;  // (the pause page's emote row: the last one played)
void register_items() {
    using game_settings::Item;
    using game_settings::Kind;
    {
        Item it;
        it.id = "online.notice", it.tab = "online", it.label = "Accept the privacy notice", it.kind = Kind::Toggle;
        it.help = "Other players or the server see your IP address, which can show roughly where you are. A server keeps your party's player keys and progress for up to 72 hours; whoever runs it controls that data. Only play with people you trust.";
        it.get = [] { return sync::settings().notice_ok ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.notice_ok = v >= 0.5f, x.on = x.on && x.notice_ok; }); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.enabled", it.tab = "online", it.label = "Online co-op", it.kind = Kind::Toggle;
        it.help = "Play together with friends: up to three directly, more through a server. Nothing is sent while this is off.";
        it.get = [] { const sync::Settings s = sync::settings(); return s.on && s.notice_ok ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.on = v >= 0.5f && x.notice_ok; }); };
        it.enabled = [] { return sync::settings().notice_ok; };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.connection", it.tab = "online", it.label = "Connection", it.kind = Kind::Choice;
        it.choices = {"Directly", "Through a server"};
        it.help = "Directly: one of you hosts. Through a server: a server you or a friend runs.";
        it.get = [] { return sync::settings().server ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.server = v >= 0.5f; }); };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.server", it.tab = "online", it.label = "Server address", it.kind = Kind::Action;
        it.help = "The server's address, e.g. its IP address or name.";
        it.text = [] { return sync::settings().server_addr; };
        it.set = [](float) { ask_text(sync::settings().server_addr, 120, [](const std::string& a) { change([&](sync::Settings& x) { x.server_addr = a; }); }); };
        it.enabled = [] { return sync::settings().server; };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.invite", it.tab = "online", it.label = "Invite a friend", it.kind = Kind::Action;
        it.help = "Enter your friend's code. Your own code is shown here; tell it to friends so they can invite you.";
        it.text = [] { const std::string c = net::friend_code(); return c.empty() ? std::string("-") : c; };
        it.set = [](float) { ask_text("", 16, [](const std::string& c) { std::string why; if (!net::invite(c, why)) ss::notice("Can't invite: " + why); }); };
        it.enabled = [] { return net::via_server(); };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.invitation", it.tab = "online", it.label = "Join an invitation", it.kind = Kind::Action;
        it.help = "A friend invited you: A joins their party.";
        it.text = [] { const auto v = net::invites(); return v.empty() ? std::string("-") : v.front().second; };
        it.set = [](float) { const auto v = net::invites(); if (!v.empty()) net::answer(v.front().first, true); };
        it.enabled = [] { return !net::invites().empty(); };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.decline", it.tab = "online", it.label = "Decline the invitation", it.kind = Kind::Action;
        it.help = "A says no to the first invitation waiting.";
        it.text = [] { const auto v = net::invites(); return v.empty() ? std::string("-") : v.front().second; };
        it.set = [](float) { const auto v = net::invites(); if (!v.empty()) net::answer(v.front().first, false); };
        it.enabled = [] { return !net::invites().empty(); };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.leave", it.tab = "online", it.label = "Leave the party", it.kind = Kind::Action;
        it.help = "You stay online on the server; the others play on.";
        it.text = [] { return std::string(net::via_server() ? "Leave" : "-"); };
        it.set = [](float) { net::leave_party(); };
        it.enabled = [] { return net::via_server(); };
        game_settings::add(std::move(it));
    }
    if (net::server_built()) {
        Item it;
        it.id = "online.saved", it.tab = "online", it.label = "Saved servers", it.kind = Kind::Action;
        it.help = "A goes to the next server you have been on. Forget a server in Settings > Online.";
        it.text = [] {
            const sync::Settings s = sync::settings();
            for (const servers::Server& v : net::saved_servers()) if (v.addr == s.server_addr && v.port == s.server_port) return v.name;
            return net::saved_servers().empty() ? std::string("-") : std::string("(another)");
        };
        it.set = [](float) {
            const std::vector<servers::Server> v = net::saved_servers();
            if (v.empty()) return;
            const sync::Settings s = sync::settings();
            size_t at = v.size() - 1;
            for (size_t i = 0; i < v.size(); i++) if (v[i].addr == s.server_addr && v[i].port == s.server_port) at = i;
            const servers::Server& next = v[(at + 1) % v.size()];
            change([&](sync::Settings& x) { x.server_addr = next.addr, x.server_port = next.port; });
        };
        it.enabled = [] { return sync::settings().server && !net::saved_servers().empty(); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.role", it.tab = "online", it.label = "Play as", it.kind = Kind::Choice;
        it.choices = {"Host on this computer", "Join a friend"};
        it.help = "The host's friends join it. Joining needs the host's address.";
        it.get = [] { return (float)sync::settings().role; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.role = v >= 0.5f; }); };
        it.enabled = [] { return !sync::settings().server; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.address", it.tab = "online", it.label = "Host's address", it.kind = Kind::Action;
        it.help = "An IP address or a name, e.g. the host's VPN name.";
        it.text = [] { return sync::settings().addr; };
        it.set = [](float) { ask_text(sync::settings().addr, 120, set_addr); };
        it.enabled = [] { return sync::settings().role == 1 && !sync::settings().server; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.name", it.tab = "online", it.label = "Your name", it.kind = Kind::Action;
        it.help = "The name the other players see.";
        it.text = [] { return sync::settings().name; };
        it.set = [](float) { ask_text(sync::settings().name, 16, set_name); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.tunic", it.tab = "online", it.label = "Tunic", it.kind = Kind::Choice;
        it.choices = {"Green", "Red", "Blue", "Purple"};
        it.help = "Your tunic's colour, for you and as the others see you.";
        it.get = [] { return (float)sync::settings().tunic; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.tunic = std::clamp((int)(v + 0.5f), 0, 3); }); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.strangers", it.tab = "online", it.label = "Players outside your party", it.kind = Kind::Choice;
        it.choices = {"Like any Link", "See-through"};
        it.help = "See-through players are easy to tell from your party, who always look solid.";
        it.get = [] { return (float)sync::settings().strangers; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.strangers = v >= 0.5f; }); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.pvp_sea", it.tab = "online", it.label = "PvP at sea", it.kind = Kind::Toggle;
        it.help = "Through a server that allows it: you and other players who have it on can fight on the open sea, outside the safe zones around the big islands. A knockout costs some of your rupees. Party members fight only with the party's friendly fire.";
        it.get = [] { return sync::settings().pvp_sea ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.pvp_sea = v >= 0.5f; }); };
        it.enabled = [] { return net::pvp_rules().mode != 2; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.emote", it.tab = "online", it.label = "Emote", it.kind = Kind::Choice;
        for (const emotes::Emote& e : emotes::kEmotes) it.choices.push_back(e.name);
        it.help = "Left and right play an emote: the other players see your Link do it. On a keyboard: Alt+1 to Alt+5 (change them in Settings > Controls, Online).";
        it.get = [] { return (float)g_emote_choice; };
        it.set = [](float v) {
            g_emote_choice = std::clamp((int)(v + 0.5f), 0, emotes::kCount - 1);
            emotes::mine(emotes::kEmotes[g_emote_choice].id);
        };
        it.enabled = [] { return net::role() != net::Role::kOff; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.party_mods", it.tab = "online", it.label = "Use the party's mods", it.kind = Kind::Action;
        it.help = "The party plays with other mods that change the game. A turns on the party's and turns off your others, when all of them are installed (some need a restart). Settings > Online lists what differs.";
        it.text = [] {
            const std::string got = net::refused_mods();
            modset::Set party;
            if (got.empty() || !modset::decode((const uint8_t*)got.data(), got.size(), party)) return std::string("-");
            std::string why;
            return modset::can_use(party, why) ? std::string("Use") : "Can't: " + why;
        };
        it.set = [](float) {
            const std::string got = net::refused_mods();
            modset::Set party;
            std::string why;
            bool restart = false;
            if (got.empty() || !modset::decode((const uint8_t*)got.data(), got.size(), party)) return;
            ss::notice(modset::use(party, restart, why) ? (restart ? "Done. Restart the game for the change, then connect again." : "Done: connect again.")
                                                        : "Can't use the party's mods: " + why);
        };
        it.enabled = [] { return !net::refused_mods().empty(); };
        game_settings::add(std::move(it));
    }
    for (int k = 0; k < 2; k++) {  // (C14) the nearest player outside the party: block or mute, one press
        Item it;
        it.id = k ? "online.mute" : "online.block", it.tab = "online", it.label = k ? "Mute the nearest player" : "Block the nearest player";
        it.kind = Kind::Action;
        it.help = k ? "The nearest player outside your party stays visible, without sounds or emotes. Undo it in Settings > Online."
                    : "The nearest player outside your party is hidden, and you from them. Undo it in Settings > Online.";
        it.text = [] { const int id = sync::nearest_stranger(); return id < 0 ? std::string("-") : net::name(id); };
        it.set = [k](float) {
            const int id = sync::nearest_stranger();
            std::string why;
            if (id >= 0 && !block::set(id, k ? block::kMuted : block::kBlocked, why)) ss::notice("Can't: " + why);
            else if (id >= 0) ss::notice(net::name(id) + (k ? " muted" : " blocked"));
        };
        it.enabled = [] { return sync::nearest_stranger() >= 0; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.status", it.tab = "online", it.label = "Status", it.kind = Kind::Action;
        it.help = "A connects again after the session stopped. More online options are in Settings > Online.";
        it.text = [] { return std::string(sync::status()); };
        it.set = [](float) { const sync::Settings s = sync::settings(); if (s.on && s.notice_ok && net::role() == net::Role::kOff) sync::retry(); };
        game_settings::add(std::move(it));
    }
}

}  // namespace

void register_tab() {
    overlay::ext::Tab tab{"online", "Online", [] { draw_tab(); }, nullptr};
    overlay::ext::add_tab(std::move(tab), "about");
}

void register_page_items() { register_items(); }

}  // namespace mods::coop::settings_ui
