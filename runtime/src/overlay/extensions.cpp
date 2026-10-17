// Platform extensions of the settings overlay (see extensions.h): the registry. Filled on the main
// thread at start-up, sealed by the overlay's first frame or the first controller shortcut poll, read
// only afterwards.
#include "extensions.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>

namespace overlay::ext {
namespace {

std::atomic<bool> g_sealed{false};
std::vector<ShownTab> g_tabs;  // the built-in tabs and the added ones, in order (hidden ones left out at seal())
bool g_tabs_made = false;
std::set<std::string> g_hidden;
Features g_features;
FontLoader g_font_loader;
std::vector<Shortcut> g_shortcuts;
ReleaseGate g_taken;  // the inputs a shortcut took, until each one is released
TextInput g_text_input;
std::vector<FrameLayer> g_frame_layers;

bool too_late(const char* what) {
    if (!g_sealed.load(std::memory_order_acquire)) return false;
    fprintf(stderr, "[overlay] extension registered too late (%s): register at start-up, before the overlay runs\n", what);
    return true;
}

void make_tabs() {
    if (g_tabs_made) return;
    g_tabs_made = true;
    for (int i = 0; i < kBuiltinCount; i++) {
        ShownTab t;
        t.id = kBuiltinTabs[i].id;
        t.name = kBuiltinTabs[i].name;
        t.builtin = i;
        g_tabs.push_back(std::move(t));
    }
}

int find(const std::string& id) {
    for (size_t i = 0; i < g_tabs.size(); i++)
        if (g_tabs[i].id == id) return int(i);
    return -1;
}

}  // namespace

void add_tab(Tab tab, const char* before_id) {
    if (too_late("add_tab")) return;
    make_tabs();
    if (tab.id.empty() || find(tab.id) >= 0) {
        fprintf(stderr, "[overlay] add_tab: the tab id \"%s\" is empty or taken; not added\n", tab.id.c_str());
        return;
    }
    int at = before_id ? find(before_id) : -1;
    if (at < 0) at = find("about");
    if (at < 0) at = int(g_tabs.size());
    ShownTab t;
    t.id = std::move(tab.id);
    t.name = std::move(tab.name);
    t.draw = std::move(tab.draw);
    t.shoulder = std::move(tab.shoulder);
    t.shown = std::move(tab.shown);
    g_tabs.insert(g_tabs.begin() + at, std::move(t));
}

void hide_tab(const char* id) {
    if (too_late("hide_tab") || !id) return;
    g_hidden.insert(id);
}

void set_features(const Features& features) {
    if (too_late("set_features")) return;
    g_features = features;
}
const Features& features() { return g_features; }

void set_font_loader(FontLoader load) {
    if (too_late("set_font_loader")) return;
    g_font_loader = std::move(load);
}

void add_shortcut(Shortcut poll) {
    if (too_late("add_shortcut") || !poll) return;
    g_shortcuts.push_back(std::move(poll));
}

void set_text_input(TextInput begin) {
    if (too_late("set_text_input")) return;
    g_text_input = std::move(begin);
}

void add_frame_layer(FrameLayer layer) {
    if (too_late("add_frame_layer") || !layer.wanted || !layer.draw) return;
    g_frame_layers.push_back(std::move(layer));
}

void seal() {
    if (g_sealed.load(std::memory_order_acquire)) return;
    // The render thread's first frame and the input thread's first shortcut poll may both get here
    // first: one of them finishes the registry under the lock, the other then sees it sealed.
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    if (g_sealed.load(std::memory_order_relaxed)) return;
    make_tabs();
    g_tabs.erase(std::remove_if(g_tabs.begin(), g_tabs.end(), [](const ShownTab& t) { return g_hidden.count(t.id) != 0; }),
                 g_tabs.end());
    g_sealed.store(true, std::memory_order_release);
}
bool sealed() { return g_sealed.load(std::memory_order_acquire); }

const std::vector<ShownTab>& tabs() {
    seal();
    return g_tabs;
}
int tab_index(const std::string& id) {
    seal();
    return find(id);
}
int builtin_tab(int index) {
    seal();
    return index >= 0 && index < int(g_tabs.size()) ? g_tabs[index].builtin : -1;
}
int next_tab(int index, int direction) {
    seal();
    const int n = int(g_tabs.size());
    if (n == 0) return -1;
    int next = index;
    for (int k = 0; k < n; k++) {
        next = ((next + direction) % n + n) % n;
        if (g_tabs[next].visible()) return next;
    }
    return index;
}
int shoulder(int index, int direction, const std::function<bool(int builtin, int direction)>& builtin_shoulder) {
    seal();
    if (index < 0 || index >= int(g_tabs.size())) return -1;
    const ShownTab& t = g_tabs[index];
    const bool inside = t.builtin >= 0 ? builtin_shoulder && builtin_shoulder(t.builtin, direction)
                                       : t.shoulder && t.shoulder(direction);
    return inside ? -1 : next_tab(index, direction);
}
const FontLoader& font_loader() { seal(); return g_font_loader; }
const std::vector<Shortcut>& shortcuts() { seal(); return g_shortcuts; }
bool poll_shortcuts(PadValues& pad, bool menu_open) {
    bool took = false;
    for (const Shortcut& poll : shortcuts())
        if (poll(pad, menu_open)) {
            took = true;
            break;
        }
    g_taken.filter(pad.data(), took);
    return took;
}
const TextInput& text_input() { seal(); return g_text_input; }
const std::vector<FrameLayer>& frame_layers() { seal(); return g_frame_layers; }

void reset_for_test() {
    g_sealed = false;
    g_tabs.clear();
    g_tabs_made = false;
    g_hidden.clear();
    g_features = Features{};
    g_font_loader = nullptr;
    g_shortcuts.clear();
    g_taken = ReleaseGate{};
    g_text_input = nullptr;
    g_frame_layers.clear();
}

}  // namespace overlay::ext
