// SPDX-License-Identifier: MPL-2.0
// Link's appearance: each save file's look, as cking.sav's story block has it and as it is now
// (link_appearance.h). Kept apart from the port's textures and page so the save tests link it alone.
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>

#include "../runtime.h"
#include "link_appearance.h"
#include "story_block.h"

namespace link_appearance {
namespace {
std::mutex g_mu;
std::array<Look, 3> g_saved;         // each save file's look as cking.sav has it
std::array<Look, 3> g_live;          // ... and as it is now (changed since the last save)
std::array<bool, 3> g_fresh_seen{};  // a new game seen in that file (its saved look let go once)
std::atomic<int> g_file{-1};         // the save file played
bool g_preview_on = false;           // the colour page is open: its look is shown
Look g_preview;
}  // namespace

void loaded(const uint8_t* payload) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t f = 0; f < 3; f++) {
        g_saved[f] = payload ? read_area(payload + f * story_block::kAreaSize) : Look{};
        g_live[f] = g_saved[f];
        g_fresh_seen[f] = false;
        if (!g_saved[f].empty()) LOG("[appearance] save file %zu: %s", f + 1, to_text(g_saved[f]).c_str());
    }
    if (const char* env = getenv("WWHD_LINK_APPEARANCE")) {  // test aid: as if every file had it saved
        Look l;
        if (from_text(env, l)) {
            for (size_t f = 0; f < 3; f++) g_saved[f] = g_live[f] = l;
            LOG("[appearance] test look for every save file: %s", to_text(l).c_str());
        } else {
            LOG("[appearance] WWHD_LINK_APPEARANCE: not a look (\"hair=FFEF10,tunic=E52B36\")");
        }
    }
}

void written(uint8_t* areas) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t f = 0; f < 3; f++) {
        if (!write_area(areas + f * story_block::kAreaSize, g_live[f])) {
            if (!g_live[f].empty()) LOG("[appearance] save file %zu: no room in the story block for Link's look", f + 1);
            continue;  // (an area of data this port did not write: left alone)
        }
        if (g_live[f] != g_saved[f])
            LOG("[appearance] save file %zu saved with %s", f + 1, g_live[f].empty() ? "Link's own colours" : to_text(g_live[f]).c_str());
        g_saved[f] = g_live[f];
    }
}

int current_file() { return g_file.load(); }

Look current() {
    std::lock_guard<std::mutex> lk(g_mu);
    const int f = g_file.load();
    return f >= 0 ? g_live[size_t(f)] : Look{};
}

Look shown() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_preview_on) return g_preview;
    const int f = g_file.load();
    return f >= 0 ? g_live[size_t(f)] : Look{};
}

bool set_current(const Look& l, std::string& why) {
    std::lock_guard<std::mutex> lk(g_mu);
    const int f = g_file.load();
    if (f < 0) return why = "no save file is played", false;
    if (g_live[size_t(f)] != l) LOG("[appearance] save file %d: %s (kept when the game saves)", f + 1, l.empty() ? "Link's own colours" : to_text(l).c_str());
    g_live[size_t(f)] = l;
    return true;
}

namespace store {
Look frame(int file, bool fresh_game) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_file = file;
    if (file >= 0) {
        const size_t f = size_t(file);
        // a new game over a file whose look came from the save: Link's own colours (once; a look chosen in the
        // new game stays)
        if (fresh_game && !g_saved[f].empty() && g_live[f] == g_saved[f] && !g_fresh_seen[f]) {
            g_fresh_seen[f] = true;
            g_live[f] = Look{};
            LOG("[appearance] save file %d: a new game, Link's own colours", file + 1);
        } else if (!fresh_game) {
            g_fresh_seen[f] = false;
        }
    }
    return g_preview_on ? g_preview : file >= 0 ? g_live[size_t(file)] : Look{};
}

void preview(bool on, const Look& l) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_preview_on = on;
    g_preview = on ? l : Look{};
}
}  // namespace store

}  // namespace link_appearance
