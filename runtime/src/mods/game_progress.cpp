// SPDX-License-Identifier: MPL-2.0
// Built-in mod "Game progress" (mods/manager.cpp, off by default): how far the save file in play is.
//   - The count: game_progress.h (ten parts, a percent) of the live save data, read once per full logic step on the
//     game thread while the mod is on (progress_service, interp.cpp); nothing is read while it is off.
//   - Its page: a row "Game progress" on the Game tab of the item menu's settings page opens game_progress_page.h
//     (checklist and map), with the game's own names and pictures read from the player's 2D pack at run time.
//   - Optionally the percent on the TV picture (bottom left), hidden while a menu, an event or a message is up.
//   - The Settings window: Mods > Built-in > Game progress shows the same count (overlay.cpp).
// Read only: nothing here writes the save data or any file. Off, no game state is read and nothing is drawn.
//
// Idea, the count and the places from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid
// db65d60, MPL-2.0): mods/progress.cpp, GameProgress.java, ProgressPlaces.java, ProgressMapView.java.
//
// Test aids: WWHD_MOD_GAME_PROGRESS=1 switches it on (manager.cpp); WWHD_MOD_PROGRESS_ON_SCREEN=0|1 the percent;
// WWHD_PROGRESS_LOG=1 logs the count when it changes ("[progress] ..."). Screenshots of the page: the settings
// page's test aids WWHD_TEST_SETTINGS_PAGE=game and WWHD_TEST_SETTINGS_DETAIL=game.progress (game_settings_page.cpp).
#include "game_progress_host.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "game_layouts.h"
#include "game_progress_names.h"
#include "game_progress_page.h"
#include "game_settings_page.h"
#include "guest_addr.h"
#include "guest_hud.h"
#include "hud_visibility.h"
#include "mods.h"
#include "msbt_text.h"
#include "runtime.h"
#include "../console_language.h"
#include "../overlay/hostui.h"

namespace game_progress {
namespace {

const std::string kOwner = "port:game-progress";
const uint32_t kSaveInfoPtr = GD(0x101F84DC);        // dComIfGs save area (dSv_info_c - 0x20)
const uint32_t kStageName = GD(0x1046F0B0) + 0x5134;  // the stage in play (as savestate.cpp)

bool env_on(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}
std::atomic<bool> g_on{env_on("WWHD_MOD_GAME_PROGRESS")}, g_on_screen{env_on("WWHD_MOD_PROGRESS_ON_SCREEN")};
std::mutex g_mu;
bool g_loaded = false;                 // a save file is in play (g_mu)
unsigned char g_save[GP_SAVE_LEN];     // its data at the last read (g_mu)
gp_result g_result;                    // the count of it (g_mu)
int g_percent = -1;                    // (g_mu; -1 none)
bool g_hud = false;                    // the percent is published (game thread)

bool log_on() {
    static const bool on = std::getenv("WWHD_PROGRESS_LOG") != nullptr;
    return on;
}

}  // namespace

// the save data in play, 0 on the title screen, in the file select or with no file loaded (cheats.cpp's checks)
uint32_t save_address() {
    const uint32_t sv = ld32(kSaveInfoPtr);
    if ((sv & 3) || sv < mem::kMem2Start || sv > mem::kMem2End - 0x2000) return 0;
    const uint32_t s = sv + 0x20;
    char stage[9] = {};
    std::memcpy(stage, mem::ptr(kStageName), 8);
    if (!stage[0] || !std::strcmp(stage, "sea_T") || !std::strcmp(stage, "Name")) return 0;
    const uint16_t max = ld16(s), life = ld16(s + 2);
    if (max < 12 || max > 80 || life > max || ld8(s + 0x12) > 2) return 0;
    return s;
}

namespace {

// ---- the game's words and pictures (read once per language from the player's 2D pack) ----
struct Words {
    uint32_t language = ~0u;
    game_progress_names::Names names;
} g_words;  // (written on the game thread under g_words_mu)
std::mutex g_words_mu;

void load_words() {
    const uint32_t lang = console_language();
    if (g_words.language == lang) return;
    game_progress_names::Texts t;
    std::string why;
    auto read = [&](const char* archive, const char* member, std::map<std::string, std::string>& out) {
        game_layouts::Bytes m;
        if (game_layouts::file(archive, member, m, why)) out = mods::msbt::texts(m, true);
        else if (log_on()) LOG("[progress] %s: %s", member, why.c_str());
    };
    read("message_msbt", "message.msbt", t.message);
    read("message4_msbt", "message4.msbt", t.message4);
    read("DungeonMap_00_msbt", "DungeonMap_00.msbt", t.dungeons);
    read("MenuMiiverseContent_00_msbt", "MenuMiiverseContent_00.msbt", t.places);
    LOG("[progress] game texts: %zu messages, %zu gallery texts, %zu dungeon and %zu place names", t.message.size(), t.message4.size(),
        t.dungeons.size(), t.places.size());
    // (a language whose texts aren't readable yet: the English names, and another try at the next opening)
    std::lock_guard lock(g_words_mu);
    if (!t.message.empty()) g_words.language = lang;
    g_words.names.t = std::move(t);
}

// a picture of the pack scaled into a cell of `cell` x `cell` pixels at (cx, cy) of the sheet (its aspect kept)
void put(guestmods::hud::Image& sheet, const std::string& spec, int cx, int cy, int cell) {
    game_layouts::Image img;
    std::string why;
    if (!game_layouts::texture(spec, img, why) || !img.width || !img.height) {
        if (log_on()) LOG("[progress] picture %s: %s", spec.c_str(), why.c_str());
        return;
    }
    const float s = float(cell) / float(std::max(img.width, img.height));
    const int w = std::max(1, int(std::lround(img.width * s))), h = std::max(1, int(std::lround(img.height * s)));
    const int ox = cx + (cell - w) / 2, oy = cy + (cell - h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {  // (bilinear)
            const float fx = (float(x) + 0.5f) / s - 0.5f, fy = (float(y) + 0.5f) / s - 0.5f;
            const int x0 = std::clamp(int(std::floor(fx)), 0, int(img.width) - 1), y0 = std::clamp(int(std::floor(fy)), 0, int(img.height) - 1);
            const int x1 = std::min(x0 + 1, int(img.width) - 1), y1 = std::min(y0 + 1, int(img.height) - 1);
            const float ax = std::clamp(fx - float(x0), 0.0f, 1.0f), ay = std::clamp(fy - float(y0), 0.0f, 1.0f);
            uint8_t* d = &sheet.rgba[(size_t(oy + y) * sheet.width + size_t(ox + x)) * 4];
            for (int k = 0; k < 4; k++) {
                auto at = [&](int px, int py) { return float(img.rgba[(size_t(py) * img.width + size_t(px)) * 4 + size_t(k)]); };
                const float v = (at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay;
                d[k] = uint8_t(std::clamp(std::lround(v), 0L, 255L));
            }
        }
}

struct Sheets {
    bool tried = false, icons = false, map = false;
} g_sheets;  // (game thread)

void make_sheets() {
    if (g_sheets.tried) return;
    g_sheets.tried = true;
    using game_progress_page::kIconCells;
    constexpr int kIcon = 96, kIsland = 96;
    static const char* const icons[game_progress_page::kIconCount] = {
        "DungeonMapBoss_00/MapBossIcon_00^q", "BtnMenuHeart_00/HeartPiece_02^l", "BtnItemIcon_00/Icon128_10^l",
        "BtnCollectIcon_00/CollectIcon118_03^l", "Rupy_00/Rupy_02^l", "BtnCollectIcon_00/CollectIcon118_15^l",
        "BtnTriforce_00/Triforce_00^l", "BtnMapIcon_01/MapBtn_00^l", "BtnFishMan_00/FishMan_00^l",
        "BestShotMark_00/BestShotMark_00^t", "DungeonMapBoss_00/MapBossIcon_00^q", "Shortcut_00/IconSalvage_00^q", ""};
    auto sheet = std::make_shared<guestmods::hud::Image>();
    sheet->width = kIconCells * kIcon, sheet->height = kIcon;
    sheet->rgba.assign(size_t(sheet->width) * sheet->height * 4, 0);
    uint32_t w, h;
    int found = 0;
    for (int i = 0; i < game_progress_page::kIconCount; i++)
        if (icons[i][0] && game_layouts::texture_size(icons[i], w, h)) put(*sheet, icons[i], i * kIcon, 0, kIcon), found++;
    if (found) game_settings_page::set_sheet(game_progress_page::kIconSheet, sheet), g_sheets.icons = true;
    // the sea chart's squares: each island's picture ("BtnMapBalloon_00/r2c_b^t#P_DummyMapBig_00", the sea room in hex)
    auto map = std::make_shared<guestmods::hud::Image>();
    map->width = map->height = 7 * kIsland;
    map->rgba.assign(size_t(map->width) * map->height * 4, 0);
    int islands = 0;
    for (int room = 1; room <= 49; room++) {
        char spec[64];
        std::snprintf(spec, sizeof spec, "BtnMapBalloon_00/r%02x_b^t#P_DummyMapBig_00", room);
        if (!game_layouts::texture_size(std::string(spec).substr(0, std::string(spec).find('#')), w, h)) continue;
        put(*map, spec, (room - 1) % 7 * kIsland, (room - 1) / 7 * kIsland, kIsland);
        islands++;
    }
    if (islands) game_settings_page::set_sheet(game_progress_page::kMapSheet, map), g_sheets.map = true;
    LOG("[progress] pictures: %d icons, %d islands from the game's 2D pack", found, islands);
}

// the page's snapshot of the save in play (game thread)
game_progress_page::Snapshot snapshot() {
    game_progress_page::Snapshot s;
    unsigned char d[GP_SAVE_LEN];
    {
        std::lock_guard lock(g_mu);
        if (!g_loaded) return s;
        std::memcpy(d, g_save, sizeof d);
        s.r = g_result;
        s.percent = g_percent;
    }
    load_words();
    make_sheets();
    std::lock_guard words(g_words_mu);
    const auto& n = g_words.names;
    s.valid = true;
    s.icons = g_sheets.icons, s.map_art = g_sheets.map;
    s.entry.reserve(s.r.count);
    for (unsigned i = 0; i < s.r.count; i++) s.entry.push_back(n.entry(s.r.e[i]));
    for (int p = 0; p < GP_PARTS; p++) s.part[size_t(p)] = game_progress_names::kPartEn[p];
    for (unsigned sq = 1; sq <= 49; sq++) s.square[sq] = n.square(sq), s.filled[sq] = d[GP_FMAP + sq - 1] & 1;
    gp_place_states(d, s.state.data());
    s.place.reserve(GP_PLACES);
    for (const gp_place& p : gp_places) s.place.push_back(n.place(p));
    for (int w = 0; w <= GP_S_UNKNOWN; w++) s.way[size_t(w)] = game_progress_names::kWayEn[w];
    return s;
}

void publish_percent(bool show, int percent) {
    auto& store = guestmods::hud::store();
    if (!show) {
        if (g_hud) store.drop(kOwner), g_hud = false;
        return;
    }
    const guestmods::hud::Handle list = store.begin(kOwner, 0);
    if (!list) return;
    using guestmods::hud::Command;
    const std::string text = std::to_string(percent) + " %";
    Command c;
    c.kind = Command::Text, c.anchor = guestmods::hud::Anchor::BottomLeft, c.size = 26;
    c.x = 42 + 2, c.y = 668 + 2, c.rgba = 0x00000090, c.text = text;  // a shadow, then the number
    store.append(kOwner, list, c);
    c.x = 42, c.y = 668, c.rgba = 0xFFF7D8FF;
    store.append(kOwner, list, c);
    store.commit(kOwner, list);
    g_hud = true;
}

}  // namespace

bool enabled() { return g_on.load(std::memory_order_relaxed); }
void set_enabled(bool on) {
    g_on.store(on, std::memory_order_relaxed);
    LOG("[progress] game progress %s", on ? "on" : "off");
}
bool on_screen() { return g_on_screen.load(std::memory_order_relaxed); }
void set_on_screen(bool on) { g_on_screen.store(on, std::memory_order_relaxed); }

bool loaded() {
    std::lock_guard lock(g_mu);
    return enabled() && g_loaded;
}
int percent() {
    std::lock_guard lock(g_mu);
    return enabled() && g_loaded ? g_percent : -1;
}
bool result(gp_result& out, std::vector<std::string>* names) {
    {
        std::lock_guard lock(g_mu);
        if (!enabled() || !g_loaded) return false;
        out = g_result;
    }
    if (names) {  // (the overlay: English names unless the game thread has read the game's words)
        std::lock_guard words(g_words_mu);
        names->clear();
        for (unsigned i = 0; i < out.count; i++) names->push_back(g_words.names.entry(out.e[i]));
    }
    return true;
}

void service(Cpu* c) {
    (void)c;
    if (!enabled()) {
        if (g_hud) publish_percent(false, 0);
        std::lock_guard lock(g_mu);
        g_loaded = false, g_percent = -1;
        return;
    }
    const uint32_t s = save_address();
    int pc = -1;
    {
        std::lock_guard lock(g_mu);
        g_loaded = s != 0;
        if (s) {
            unsigned char d[GP_SAVE_LEN];
            std::memcpy(d, mem::ptr(s), sizeof d);  // (guest memory is big-endian, as the count reads it)
            if (g_percent < 0 || std::memcmp(d, g_save, sizeof d)) {
                std::memcpy(g_save, d, sizeof d);
                gp_count(g_save, &g_result);
                const int before = g_percent;
                g_percent = gp_percent(&g_result);
                if (log_on() && before != g_percent) {
                    char parts[160];
                    int n = 0;
                    for (int p = 0; p < GP_PARTS && n < int(sizeof parts) - 12; p++)
                        n += std::snprintf(parts + n, sizeof parts - size_t(n), " %u/%u", g_result.done[p], g_result.total[p]);
                    LOG("[progress] %d %%:%s", g_percent, parts);
                }
            }
        } else {
            g_percent = -1;
        }
        pc = g_percent;
    }
    const bool show = on_screen() && pc >= 0 && !mods::game_ui_hides_hud(mods::game_ui_now());
    publish_percent(show, pc);
}

std::string fill_name(const std::string& text, int item, int place) {
    const size_t at = text.find("%s");
    if (at == std::string::npos) return text;
    std::string name;
    {
        std::lock_guard words(g_words_mu);
        const auto& n = g_words.names;
        if (item <= -1000) name = n.song(unsigned(-item - 1000));
        else if (item > 0) name = n.item(unsigned(item));
        else if (place > 0) name = n.square(unsigned(place));
        else if (place < 0) name = n.dungeon(unsigned(-place));
    }
    return text.substr(0, at) + name + text.substr(at + 2);
}

void prepare_words() { load_words(); }

void register_page() {
    game_settings_page::register_detail("game.progress", []() -> std::unique_ptr<game_settings_page::Detail> {
        game_progress_page::Snapshot s = snapshot();
        if (!s.valid) return nullptr;
        LOG("[progress] page opened: %d %%", s.percent);
        return std::make_unique<game_progress_page::View>(std::move(s));
    });
}

}  // namespace game_progress
