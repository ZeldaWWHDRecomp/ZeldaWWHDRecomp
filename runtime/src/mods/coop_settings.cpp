// SPDX-License-Identifier: MPL-2.0
// Online co-op's settings (docs/online-coop.md): the "Online" tab of the Settings window and the "Online" tab of the
// pause menu's settings page (game_settings). Both read and change mods::coop::sync's settings, so they always show
// the same values. Nothing goes online before the player has accepted the privacy notice and switched it on.
// Environment variables (WWHD_COOP...) still start a session for tests.
#include "coop_settings.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "coop_net.h"
#include "coop_sync.h"
#include "game_settings.h"
#include "imgui.h"
#include "../overlay/extensions.h"
#include "../overlay/hostui.h"
#include "../overlay/text_entry.h"
#include "../input.h"

namespace mods::coop::settings_ui {
namespace {

namespace sync = mods::coop::sync;
namespace net = mods::coop::net;


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
void draw_tab() {
    sync::Settings s = sync::settings();
    ImGui::TextWrapped("Play together with up to three friends. One of you hosts, the others join.");
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
    if (ImGui::RadioButton("Host on this computer", s.role == 0)) hostui::post([] { change([](sync::Settings& x) { x.role = 0; }); });
    ImGui::SameLine();
    if (ImGui::RadioButton("Join a friend", s.role == 1)) hostui::post([] { change([](sync::Settings& x) { x.role = 1; }); });
    static char addr[128], name[32];
    static int port = 0;
    static bool filled = false;
    if (!filled) snprintf(addr, sizeof addr, "%s", s.addr.c_str()), snprintf(name, sizeof name, "%s", s.name.c_str()), port = s.port, filled = true;
    if (s.role == 1) {
        ImGui::InputText("Host's address", addr, sizeof addr);
        if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([a = std::string(addr)] { set_addr(a); });
        ImGui::TextDisabled("An IP address or a name, e.g. the host's VPN name.");
    }
    ImGui::InputInt("Port (UDP)", &port, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) hostui::post([p = std::clamp(port, 1, 65535)] { change([&](sync::Settings& x) { x.port = p; }); });
    if (s.role == 0) ImGui::TextDisabled("Your friends need to reach this port (on a VPN nothing else is needed).");
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
    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", sync::status());
    if (net::role() != net::Role::kOff) {
        bool first = true;
        for (int id = 0; id < net::kMaxPlayers; ++id)
            if (id != net::my_id() && net::connected(id)) {
                if (!first) ImGui::SameLine();
                first = false;
                if (ImGui::Button(("Go to " + net::name(id)).c_str())) hostui::post([id] { sync::go_to(id); });
            }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Before your first session your save is backed up. A new Quest Log just for co-op is recommended.");
}

// ---- the pause menu's page ----
void register_items() {
    using game_settings::Item;
    using game_settings::Kind;
    {
        Item it;
        it.id = "online.notice", it.tab = "online", it.label = "Accept the privacy notice", it.kind = Kind::Toggle;
        it.help = "Other players see your IP address, which can show roughly where you are. Only play with people you trust.";
        it.get = [] { return sync::settings().notice_ok ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.notice_ok = v >= 0.5f, x.on = x.on && x.notice_ok; }); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.enabled", it.tab = "online", it.label = "Online co-op", it.kind = Kind::Toggle;
        it.help = "Play together with up to three friends. Nothing is sent while this is off.";
        it.get = [] { const sync::Settings s = sync::settings(); return s.on && s.notice_ok ? 1.0f : 0.0f; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.on = v >= 0.5f && x.notice_ok; }); };
        it.enabled = [] { return sync::settings().notice_ok; };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.role", it.tab = "online", it.label = "Play as", it.kind = Kind::Choice;
        it.choices = {"Host on this computer", "Join a friend"};
        it.help = "The host's friends join it. Joining needs the host's address.";
        it.get = [] { return (float)sync::settings().role; };
        it.set = [](float v) { change([&](sync::Settings& x) { x.role = v >= 0.5f; }); };
        game_settings::add(std::move(it));
    }
    {
        Item it;
        it.id = "online.address", it.tab = "online", it.label = "Host's address", it.kind = Kind::Action;
        it.help = "An IP address or a name, e.g. the host's VPN name.";
        it.text = [] { return sync::settings().addr; };
        it.set = [](float) { ask_text(sync::settings().addr, 120, set_addr); };
        it.enabled = [] { return sync::settings().role == 1; };
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
        it.id = "online.status", it.tab = "online", it.label = "Status", it.kind = Kind::Action;
        it.help = "More online options are in Settings > Online.";
        it.text = [] { return std::string(sync::status()); };
        it.set = [](float) {};
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
