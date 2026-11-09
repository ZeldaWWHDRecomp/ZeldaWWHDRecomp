// SPDX-License-Identifier: MPL-2.0
// Online co-op: text chat, the port's part (coop_chat.h): the box over the game picture with its tabs, the line to
// type, the chat menu with the quick phrases, local mutes in the user folder, and the test aids.
//
// The box sits at the picture's left edge, below the hearts and magic and above the sea radar (16:9 and 4:3 alike:
// placed on the TV picture as drawn, in the HUD's layout units, 720 to the picture's height). Closed, it shows the
// active tab's newest lines, each fading out after a few seconds, with the tab bar (and the other tab's unread count)
// while anything shows. Open (T on a keyboard; a bound controller button or the pause menu's Online page: the chat
// menu), it shows the active tab's history, opaque, and takes clicks on the tabs.
#include "coop_chat.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include "coop_block.h"
#include "coop_net.h"
#include "coop_sync.h"
#include "imgui.h"
#include "mods.h"
#include "../aspect.h"
#include "../fixed_seed.h"
#include "../gfx/renderer.h"
#include "../input.h"
#include "../input_map.h"
#include "../overlay/extensions.h"
#include "../overlay/overlay.h"
#include "../overlay/text_entry.h"
#include "../platform/host.h"
#include "../platform/keycodes.h"
#if WWHD_ONLINE
#include "proto/chat.h"
#endif

namespace gfx { bool main_picture(float* x, float* y, float* w, float* h); }  // display_modes.h

namespace mods::coop::chat {
namespace {
namespace fs = std::filesystem;

std::mutex g_mu;
Box g_box;              // (g_mu)
bool g_loaded = false;  // (g_mu) the mutes read from the file
enum Open { kClosed, kTyping, kMenu };
std::atomic<int> g_open{kClosed};
std::atomic<uint32_t> g_now{0};  // the game's clock as the last step saw it (the render thread draws with it)
int g_menu_sel = 0;              // (render thread) the chat menu's row
std::atomic<int> g_test_pick{-1};  // (test aid) a row of the chat menu chosen as if by A
bool g_trace_text = getenv("WWHD_COOP_CHAT_TRACE") != nullptr;  // (tests: the lines' text in the trace)

fs::path mutes_path() {
    const char* e = getenv("WWHD_COOP_CHAT_MUTES");  // (tests: never the player's own list)
    return e && *e ? fs::u8path(e) : fs::u8path(host::config_dir()) / "online-chat-mutes.txt";
}
void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream f(mutes_path());
    if (!f) return;
    std::stringstream s;
    s << f.rdbuf();
    g_box.parse_mutes(s.str());
    if (!g_box.mutes().empty()) mods::trace("coop: chat: %zu players muted", g_box.mutes().size());
}
void save_locked() {
    const fs::path p = mutes_path(), tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << g_box.mutes_text();
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
}
uint32_t now() { return net::now_ms(); }

// UTF-16 (the text prompt's) to UTF-8
std::string utf8(const std::u16string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c < 0x80) o += char(c);
        else if (c < 0x800) o += char(0xC0 | (c >> 6)), o += char(0x80 | (c & 0x3F));
        else if (c < 0x10000) o += char(0xE0 | (c >> 12)), o += char(0x80 | ((c >> 6) & 0x3F)), o += char(0x80 | (c & 0x3F));
        else o += char(0xF0 | (c >> 18)), o += char(0x80 | ((c >> 12) & 0x3F)), o += char(0x80 | ((c >> 6) & 0x3F)), o += char(0x80 | (c & 0x3F));
    }
    return o;
}

// a line to the active tab's channel (cleaned here as the server will; the server checks it again)
void send_line(std::string text) {
    int channel;
    Rules r;
    {
        std::lock_guard<std::mutex> l(g_mu);
        channel = g_box.active(), r = g_box.rules();
    }
    if (channel < 0 || !r.enabled) return;
#if WWHD_ONLINE
    std::string clean;
    const size_t n = online::proto::chat_clean(text, clean);
    if (!n) return;
    if (n > size_t(r.max_length)) {
        std::lock_guard<std::mutex> l(g_mu);
        g_box.note(channel, "Not sent: longer than " + std::to_string(r.max_length) + " characters", now());
        return;
    }
    text = std::move(clean);
#endif
    if (text.empty()) return;
    net::chat_send(channel, text);
    mods::trace("coop: chat: a %s line sent (%zu bytes)%s%s", channel_name(channel), text.size(), g_trace_text ? ": " : "",
                g_trace_text ? text.c_str() : "");
}

// ---- the layout: on the TV picture as drawn, in the HUD's units (720 to the picture's height)
struct Frame {
    float px = 0, py = 0, pw = 0, ph = 0, s = 1;
};
Frame frame(float w, float h) {
    Frame f;
    f.pw = w, f.ph = h;
    if (float x, y, pw, ph; gfx::main_picture(&x, &y, &pw, &ph) && pw > 0 && ph > 0) f.px = x * w, f.py = y * h, f.pw = pw * w, f.ph = ph * h;
    f.s = f.ph / 720.f;
    return f;
}
// the box: its left edge, its bottom (the newest line's foot) and width, in layout units from the picture's top left
// (between the hearts and item pictures at the top left, which end at about 145, and the sea radar from about 440;
// lines past kMaxHeight are left out, the oldest first)
constexpr float kLeft = 30, kBottom = 428, kWidth = 430, kFont = 19, kTabFont = 17, kMaxHeight = 236;
// the box's width in layout units: kWidth, at most 36% of a narrower picture (4:3: 345 of its 960)
float box_width(float aspect) { return std::min(kWidth, 0.36f * 720.f * (aspect > 0 ? aspect : 16.f / 9.f)); }

const ImU32 kPartyName = IM_COL32(120, 235, 130, 255), kOtherName = IM_COL32(150, 205, 255, 255), kYouName = IM_COL32(255, 222, 120, 255),
            kNote = IM_COL32(255, 170, 120, 255), kText = IM_COL32(245, 245, 240, 255);

// the tab bar at (x, y): Party and Global (with the other tab's unread count); clickable while `inputs`
float tab_bar(ImDrawList* d, ImFont* font, float x, float y, float s, float alpha, bool inputs, int active, const std::vector<int>& tabs,
              const int unread[2]) {
    const float fs = kTabFont * s, padx = 9 * s, pady = 3 * s;
    float at = x;
    for (int c : tabs) {
        std::string label = channel_name(c);
        if (unread[c] && c != active) label += " (" + std::to_string(unread[c]) + ")";
        const ImVec2 t = font->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str());
        const ImVec2 a(at, y), b(at + t.x + 2 * padx, y + t.y + 2 * pady);
        const bool on = c == active;
        d->AddRectFilled(a, b, on ? IM_COL32(40, 110, 150, int(235 * alpha)) : IM_COL32(20, 30, 40, int(170 * alpha)), 4 * s);
        if (on) d->AddRect(a, b, IM_COL32(150, 230, 255, int(230 * alpha)), 4 * s, 1.5f * s);
        if (unread[c] && c != active) d->AddCircleFilled(ImVec2(b.x - 3 * s, a.y + 3 * s), 3.5f * s, IM_COL32(255, 200, 60, int(255 * alpha)));
        d->AddText(font, fs, ImVec2(a.x + padx, a.y + pady), on ? IM_COL32(255, 255, 255, int(255 * alpha)) : IM_COL32(200, 210, 220, int(230 * alpha)),
                   label.c_str());
        if (inputs) {
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(c);
            if (ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, b.y - a.y))) {
                std::lock_guard<std::mutex> l(g_mu);
                g_box.select(c);
            }
            ImGui::PopID();
        }
        at = b.x + 6 * s;
    }
    return (fs + 2 * pady);
}

// one chat line's height, wrapped to `width`
struct Shown {
    std::string name, text;
    ImU32 name_col;
    bool note;
    float alpha;
};
float line_height(ImFont* font, float fs, float width, const Shown& l) {
    const std::string all = l.note ? l.text : l.name + ": " + l.text;
    return font->CalcTextSizeA(fs, FLT_MAX, width, all.c_str()).y;
}
void draw_line(ImDrawList* d, ImFont* font, float fs, float x, float y, float width, const Shown& l, float s) {
    const int a = int(255 * l.alpha);
    const ImU32 shadow = IM_COL32(0, 0, 0, int(200 * l.alpha));
    auto col = [&](ImU32 c) { return (c & 0x00FFFFFF) | (ImU32(a) << 24); };
    if (l.note) {
        d->AddText(font, fs, ImVec2(x + s, y + s), shadow, l.text.c_str(), nullptr, width);
        d->AddText(font, fs, ImVec2(x, y), col(kNote), l.text.c_str(), nullptr, width);
        return;
    }
    // the name in its colour, then the text, wrapped as one paragraph (the name's width taken off the first line)
    const std::string name = l.name + ": ";
    const std::string all = name + l.text;
    d->AddText(font, fs, ImVec2(x + s, y + s), shadow, all.c_str(), nullptr, width);
    d->AddText(font, fs, ImVec2(x, y), col(kText), all.c_str(), nullptr, width);
    d->AddText(font, fs, ImVec2(x, y), col(l.name_col), name.c_str());
}

// the box (render thread)
void draw_box(float w, float h) {
    const Frame f = frame(w, h);
    const float s = f.s;
    const int open = g_open.load();
    const uint32_t t = g_now.load();
    std::vector<Shown> lines;
    std::vector<int> tabs;
    int unread[2], active;
    const block::List blocks = block::current();
    {
        std::lock_guard<std::mutex> l(g_mu);
        tabs = g_box.tabs(), active = g_box.active(), unread[0] = g_box.unread(0), unread[1] = g_box.unread(1);
        auto shown = [&](const Line& x, float alpha) {
            const bool silent = !x.yours && x.key && blocks.silent(x.key);  // (muted in the block list too)
            if (silent) return;
            lines.push_back({x.yours ? std::string("You") : x.name, x.text, x.yours ? kYouName : x.party ? kPartyName : kOtherName, x.key == 0 && !x.yours, alpha});
        };
        if (open != kClosed) {
            const auto& hist = g_box.history(active);
            for (size_t i = hist.size() > 12 ? hist.size() - 12 : 0; i < hist.size(); i++) shown(hist[i], 1.0f);
        } else {
            for (const auto& [x, alpha] : g_box.fading(t)) shown(*x, alpha);
        }
    }
    if (tabs.empty()) return;
    ImFont* font = ImGui::GetFont();
    const float fs = kFont * s, width = box_width(f.pw / f.ph) * s, x = f.px + kLeft * s, bottom = f.py + kBottom * s;
    float hsum = 0;
    for (const Shown& l : lines) hsum += line_height(font, fs, width - 16 * s, l) + 2 * s;
    while (!lines.empty() && hsum > kMaxHeight * s) {  // (the oldest go first)
        hsum -= line_height(font, fs, width - 16 * s, lines.front()) + 2 * s;
        lines.erase(lines.begin());
    }
    const float tabs_h = (kTabFont + 6) * s + 6 * s;
    float top = bottom - hsum - (open != kClosed ? 10 * s : 0);
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float bar_alpha = 1.0f;
    if (open == kClosed) {
        bar_alpha = 0;
        for (const Shown& l : lines) bar_alpha = std::max(bar_alpha, l.alpha);
        if (unread[0] + unread[1] && bar_alpha < 0.75f) {
            std::lock_guard<std::mutex> l(g_mu);
            if (g_box.recent(t, kShowMs + kFadeMs)) bar_alpha = std::max(bar_alpha, 0.75f);
        }
        if (bar_alpha <= 0.01f) return;
    } else {
        const float min_h = 5 * (fs + 2 * s);  // (an open box keeps a height, also empty)
        if (bottom - top < min_h) top = bottom - min_h;
        d->AddRectFilled(ImVec2(x - 8 * s, top - tabs_h - 4 * s), ImVec2(x + width, bottom + 6 * s), IM_COL32(10, 18, 28, 200), 6 * s);
        if (lines.empty()) d->AddText(font, fs, ImVec2(x, bottom - fs - 4 * s), IM_COL32(170, 180, 190, 220), "No messages yet");
    }
    // the tab bar, over the lines (clickable while the chat is open: an ImGui window under it takes the clicks)
    const bool inputs = open != kClosed;
    if (inputs) {
        ImGui::SetNextWindowPos(ImVec2(x - 8 * s, top - tabs_h - 4 * s));
        ImGui::SetNextWindowSize(ImVec2(width + 8 * s, tabs_h + 4 * s));
        ImGui::SetNextWindowBgAlpha(0);
        ImGui::Begin("##coop_chat_tabs", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    }
    tab_bar(d, font, x, top - tabs_h, s, bar_alpha, inputs, active, tabs, unread);
    if (inputs) {
        if (tabs.size() > 1) {
            const char* hint = open == kTyping ? "Tab: switch" : "L / R: switch";
            const ImVec2 hs = font->CalcTextSizeA(kTabFont * 0.85f * s, FLT_MAX, 0, hint);
            d->AddText(font, kTabFont * 0.85f * s, ImVec2(x + width - hs.x - 8 * s, top - tabs_h + 4 * s), IM_COL32(170, 185, 200, 230), hint);
        }
        ImGui::End();
    }
    float y = top;
    for (const Shown& l : lines) {
        if (open == kClosed) {  // (a soft backing under each line, fading with it)
            const float lh = line_height(font, fs, width - 16 * s, l);
            d->AddRectFilled(ImVec2(x - 6 * s, y - 1 * s), ImVec2(x + std::min(width, font->CalcTextSizeA(fs, FLT_MAX, width - 16 * s, (l.name + ": " + l.text).c_str()).x + 12 * s), y + lh + 1 * s),
                             IM_COL32(0, 0, 0, int(110 * l.alpha)), 4 * s);
        }
        draw_line(d, font, fs, x, y, width - 16 * s, l, s);
        y += line_height(font, fs, width - 16 * s, l) + 2 * s;
    }
}

// ---- the chat menu (a controller, touch or the mouse): the quick phrases, typing with the on-screen keyboard
void start_typing(bool compact);
bool menu_draw(const float* pad) {
    const input::PadState st = input_map::controller_state(input_map::current(), pad);
    const uint32_t b = st.buttons;
    static uint32_t prev = ~0u;
    static bool keys_prev[256];
    bool keys[256];
    input::held_keys(keys);
    if (prev == ~0u) prev = b, std::copy(keys, keys + 256, keys_prev);
    auto hit = [&](uint32_t bit, std::initializer_list<int> ks) {
        bool k = false;
        for (int c : ks) k |= keys[c] && !keys_prev[c];
        return (b & bit && !(prev & bit)) || k;
    };
    const int rows = kPhraseCount + 2;  // "Type a message", the phrases, "Close"
    if (hit(input::kUp, {kVK_UpArrow})) g_menu_sel = (g_menu_sel + rows - 1) % rows;
    if (hit(input::kDown, {kVK_DownArrow})) g_menu_sel = (g_menu_sel + 1) % rows;
    if (hit(input::kL, {}) || hit(input::kR, {kVK_Tab})) {
        std::lock_guard<std::mutex> l(g_mu);
        g_box.next_tab();
    }
    int chosen = hit(input::kA, {kVK_Return, kVK_Space}) ? g_menu_sel : -1;
    if (const int pick = g_test_pick.exchange(-1); pick >= 0) chosen = g_menu_sel = pick;
    const bool back = hit(input::kB, {kVK_Escape});
    prev = b, std::copy(keys, keys + 256, keys_prev);
    bool shown;
    int active;
    {
        std::lock_guard<std::mutex> l(g_mu);
        shown = g_box.shown(), active = g_box.active();
    }
    const ImGuiIO& io = ImGui::GetIO();
    const Frame f = frame(io.DisplaySize.x, io.DisplaySize.y);
    const float s = f.s;
    if (shown) {
        ImGui::SetNextWindowPos(ImVec2(f.px + (kLeft - 8) * s, f.py + (kBottom + 14) * s));
        ImGui::SetNextWindowSize(ImVec2(std::min(kWidth * 0.62f, box_width(f.pw / f.ph)) * s, 0));
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * std::max(0.7f, s * 1.05f));
        if (ImGui::Begin("##coop_chat_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                                          ImGuiWindowFlags_NoNav)) {
            ImGui::TextDisabled("To %s", channel_name(active));
            for (int i = 0; i < rows; i++) {
                const char* label = i == 0 ? "Type a message..." : i <= kPhraseCount ? kPhrases[i - 1] : "Close";
                if (ImGui::Selectable(label, g_menu_sel == i)) chosen = i, g_menu_sel = i;
            }
            ImGui::TextDisabled("A choose  B close  L / R tab");
        }
        ImGui::End();
        ImGui::PopFont();
    }
    if (!shown || back || chosen == rows - 1) return prev = ~0u, g_open = kClosed, true;
    if (chosen == 0) {
        prev = ~0u;
        start_typing(false);  // (the on-screen keyboard: the prompt shows once this panel is gone)
        return true;
    }
    if (chosen > 0) {
        send_line(kPhrases[chosen - 1]);
        return prev = ~0u, g_open = kClosed, true;
    }
    return false;
}

// typing: the one-line form under the box (a keyboard), or the full prompt with the on-screen keyboard
void start_typing(bool compact) {
    int max_len;
    size_t tabs;
    int active;
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_box.shown()) return;
        max_len = g_box.rules().max_length, tabs = g_box.tabs().size(), active = g_box.active();
    }
    text_entry::Request r;
    r.max_len = std::clamp(max_len, 1, 200);
    r.compact = compact;
    const std::string hint = compact ? std::string(tabs > 1 ? "Enter: send   Esc: cancel   Tab: switch tab" : "Enter: send   Esc: cancel")
                                     : std::string("Message to ") + channel_name(active);
    for (char ch : hint) r.hint += char16_t((unsigned char)ch);
    // the field under the box, as fractions of the picture (the layout's 720 to its height, its width by the aspect)
    const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
    r.y = (kBottom + 14) / 720.f;
    r.x = (kLeft - 8) / (720.f * a);
    r.width = (box_width(a) - 20) / (720.f * a);
    r.key = [](int code) {
        if (code != kVK_Tab) return false;
        std::lock_guard<std::mutex> l(g_mu);
        g_box.next_tab();
        return true;
    };
    g_open = kTyping;
    auto done = [](bool ok, std::u16string text) {
        g_open = kClosed;
        if (ok) send_line(utf8(text));
    };
    if (!text_entry::start(r, done)) g_open = kClosed;
}

// test aids, timed in TV frames as the other scripted aids (fixed_seed::script_frame): WWHD_COOP_TEST_CHAT=
// "<frame>:<p|g|P|G>:<text>|..." sends those lines to the party (p) or global (g) channel; P and G send without this
// game's own length check (to test the server's). WWHD_COOP_TEST_CHAT_UI="<frame>:typing|menu|close|tab|party|global|
// mute:<name>|unmute:<name>|select:<row>|pick:<row>|..." opens, closes and switches the chat, and mutes a sender by name
struct TestStep {
    uint64_t at;
    std::string what, arg;
};
std::vector<TestStep> parse_steps(const char* env, bool lines) {
    std::vector<TestStep> v;
    if (!env) return v;
    std::stringstream in(env);
    std::string item;
    while (std::getline(in, item, '|')) {
        const size_t a = item.find(':');
        if (a == std::string::npos) continue;
        TestStep t;
        t.at = strtoull(item.substr(0, a).c_str(), nullptr, 10);
        const std::string rest = item.substr(a + 1);
        const size_t b = rest.find(':');
        t.what = b == std::string::npos ? rest : rest.substr(0, b);
        t.arg = b == std::string::npos ? "" : rest.substr(b + 1);
        if (lines && t.what != "p" && t.what != "g" && t.what != "P" && t.what != "G") continue;
        v.push_back(t);
    }
    return v;
}
}  // namespace

void server_rules(const Rules& r) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    g_box.set_rules(r);
}

void server_line(const Line& line) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    const bool kept = g_box.receive(line, now());
    mods::trace("coop: chat: %s line from %s%s%s (%zu bytes)%s%s%s", channel_name(line.channel), line.yours ? "you" : line.name.c_str(),
                line.party && !line.yours ? " (party)" : "", kept ? "" : ", not shown", line.text.size(), g_trace_text ? ": " : "",
                g_trace_text ? line.text.c_str() : "", kept && line.channel != g_box.active() && !line.yours ? " [unread]" : "");
}

void server_result(int channel, const std::string& why) {
    std::lock_guard<std::mutex> l(g_mu);
    g_box.note(channel, "Not sent: " + why, now());
}

void session_ended() {
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_box.set_rules(Rules{});
        g_box.clear_lines();
    }
    if (g_open.exchange(kClosed) == kTyping) text_entry::dismiss();
}

bool available() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_box.shown();
}

void open_typing() {
    if (!available() || g_open != kClosed || text_entry::active() || overlay::captures()) return;
    start_typing(true);
}

void open_menu() {
    if (!available() || g_open != kClosed || overlay::captures()) return;
    g_menu_sel = 0;
    g_open = kMenu;
    overlay::show_panel(menu_draw);
}

bool mute(uint64_t key, const std::string& name, bool on) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    if (!g_box.mute(key, name, on)) return false;
    save_locked();
    mods::trace("coop: chat: %s %s", name.c_str(), on ? "muted" : "unmuted");
    return true;
}
bool muted(uint64_t key) {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_box.muted(key);
}
std::vector<Box::Mute> mutes() {
    std::lock_guard<std::mutex> l(g_mu);
    load_locked();
    return g_box.mutes();
}
std::vector<Box::Mute> senders() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_box.senders();
}

void step() {
    const uint32_t t = now();
    g_now = t;
    const sync::Settings set = sync::settings();
    bool shown;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_box.set_prefs({set.chat_party, set.chat_global});
        shown = g_box.shown();
    }
    net::chat_prefs(uint8_t((set.chat_party ? 0 : 1) | (set.chat_global ? 0 : 2)));
    if (!shown && g_open == kTyping) g_open = kClosed, text_entry::dismiss();
    // the test aids
    if (!shown) return;
    static std::vector<TestStep> lines = parse_steps(getenv("WWHD_COOP_TEST_CHAT"), true), ui = parse_steps(getenv("WWHD_COOP_TEST_CHAT_UI"), false);
    if (lines.empty() && ui.empty()) return;
    const uint64_t f = fixed_seed::script_frame(render::frame_count());
    static size_t li = 0, ui_i = 0;
    while (li < lines.size() && f >= lines[li].at) {
        const TestStep& s = lines[li++];
        const bool global = s.what == "g" || s.what == "G", raw = s.what == "P" || s.what == "G";
        {
            std::lock_guard<std::mutex> l(g_mu);
            g_box.select(global ? kGlobal : kParty);
        }
        mods::trace("coop: test: chat line to %s%s (frame %llu)", global ? "Global" : "Party", raw ? ", unchecked" : "", (unsigned long long)f);
        if (raw) net::chat_send(global ? kGlobal : kParty, s.arg);
        else send_line(s.arg);
    }
    while (ui_i < ui.size() && f >= ui[ui_i].at) {
        const TestStep& s = ui[ui_i++];
        mods::trace("coop: test: chat %s%s%s (frame %llu)", s.what.c_str(), s.arg.empty() ? "" : " ", s.arg.c_str(), (unsigned long long)f);
        if (s.what == "typing") start_typing(true);
        else if (s.what == "menu") {
            g_menu_sel = 0;
            g_open = kMenu;
            overlay::show_panel(menu_draw);
        } else if (s.what == "close") {
            const int was = g_open.exchange(kClosed);
            if (was == kTyping) text_entry::dismiss();
            if (was == kMenu) overlay::show_panel(nullptr);
        } else if (s.what == "select") {  // (the chat menu's selection moved there, as by the D-pad)
            g_menu_sel = atoi(s.arg.c_str());
        } else if (s.what == "pick") {    // (that row chosen, as by A)
            g_test_pick = atoi(s.arg.c_str());
        } else if (s.what == "tab") {
            std::lock_guard<std::mutex> l(g_mu);
            g_box.next_tab();
        } else if (s.what == "party" || s.what == "global") {
            std::lock_guard<std::mutex> l(g_mu);
            g_box.select(s.what == "global" ? kGlobal : kParty);
        } else if (s.what == "mute" || s.what == "unmute") {
            for (const Box::Mute& m : senders())
                if (m.name == s.arg) mute(m.key, m.name, s.what == "mute");
            if (s.what == "unmute")
                for (const Box::Mute& m : mutes())
                    if (m.name == s.arg) mute(m.key, m.name, false);
        }
    }
}

void register_overlay() {
    overlay::ext::add_frame_layer({[](std::string&) {
                                       std::lock_guard<std::mutex> l(g_mu);
                                       if (!g_box.shown()) return false;
                                       if (g_open != kClosed) return true;
                                       const uint32_t t = g_now.load();
                                       return !g_box.fading(t).empty() || g_box.recent(t, kShowMs + kFadeMs);
                                   },
                                   [](float w, float h) {
                                       draw_box(w, h);
                                       return false;
                                   }});
}

}  // namespace mods::coop::chat
