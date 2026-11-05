// SPDX-License-Identifier: MPL-2.0
// Built-in mod "Achievements" (mods/manager.cpp, off by default): goals read from the save data (achievements_goals.h)
// and earned for good, for all save games and for each one (achievements_store.h), kept in the port's own data folder
// (achievements.json next to its settings; never in the game's save file).
//   - The check: once a second of game time (every 30 full logic steps), on the game thread, while a save file is in
//     play. Off, nothing is read, drawn or written.
//   - The pop-up: when one is earned, a plate at the top of the TV picture for a few seconds, with Link holding it up
//     as he holds up a treasure (his item get "itemget", drawn once on the CPU from the player's own Link.bfres and
//     LkAnm.bfres by link_poses) and the game's item fanfare (NW_BGM_ITEM_GET decoded from the player's own sound
//     archive and mixed into the port's output, past the game's sound system), each part an option. More than three at once (a file loaded that had reached them
//     already) are one plate with their count.
//   - Pausing: a cheat that keeps running, or a one-shot cheat or story-item grant used in this run (cheats.cpp
//     cheats_pausing), pauses them; the page names what does; "Earn with cheats" lets them count anyway. A save
//     state load doesn't pause them and never takes an earned one back.
//   - The page: a row "Achievements" on the Game tab of the item menu's settings page (achievements_page.h), and the
//     Settings window's Mods > Built-in > Achievements.
// Idea, goals and texts from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60, MPL-2.0):
// Achievements.java, AchievementPopup.java.
//
// Test aids: WWHD_MOD_ACHIEVEMENTS=1 switches it on; WWHD_ACHIEVEMENTS_FILE=<path> keeps the record there (tests);
// WWHD_ACHIEVEMENTS_LOG=1 logs each check's result.
#include "achievements_host.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "achievements_store.h"
#include "game_layouts.h"
#include "guest_audio.h"
#include "nw_sound_archive.h"
#include "game_progress_host.h"
#include "guest_addr.h"
#include "guest_hud.h"
#include "hud_visibility.h"
#include "link_poses.h"
#include "mods.h"
#include "player_name.h"
#include "runtime.h"
#include "../platform/host.h"

namespace achievements {
namespace {

const std::string kOwner = "port:achievements";
const uint32_t kSaveInfoPtr = GD(0x101F84DC);        // dComIfGs save area (dSv_info_c - 0x20)
const uint32_t kStageName = GD(0x1046F0B0) + 0x5134;  // the stage in play
constexpr uint32_t kDataNum = 0x1290;                 // dSv_info_c::mDataNum (0..2: the file select's slot)
constexpr uint32_t kHeroFlags = 0x12C0 + 3 * 0x10 + 6;  // SaveMgr's slot flags of the file in play (hdSaveSlotFlags_c +6)

bool env_on(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}
bool log_on() {
    static const bool on = env_on("WWHD_ACHIEVEMENTS_LOG");
    return on;
}

std::atomic<bool> g_on{env_on("WWHD_MOD_ACHIEVEMENTS")};
std::atomic<bool> g_popup{true}, g_link{true}, g_jingle{true}, g_each_file{true}, g_with_cheats{false};
std::atomic<float> g_volume{0.8f};

std::mutex g_mu;  // the store, the file in play, what pauses them
Store g_store;
bool g_store_read = false;
std::string g_file;                   // the save game in play ("2:Link"), empty none
std::vector<std::string> g_pausing;   // at the last check
Checker g_checker;                    // (game thread)

std::string store_path() {
    if (const char* p = std::getenv("WWHD_ACHIEVEMENTS_FILE")) return p;
    const std::string dir = host::config_dir();
    return dir.empty() ? std::string() : dir + "/achievements.json";
}
void read_store() {
    if (g_store_read) return;
    g_store_read = true;
    const std::string path = store_path();
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!g_store.parse(text)) LOG("[achievements] %s is not a record of ours: it is left as it is and not written", path.c_str()), g_store_read = false;
    else LOG("[achievements] %u earned (for all save games) read from %s", g_store.count(nullptr), path.c_str());
}
void write_store() {
    if (!g_store.take_changed() || !g_store_read) return;
    const std::string path = store_path();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path).parent_path(), ec);
    const std::string tmp = path + ".new";
    {
        std::ofstream f(std::filesystem::u8path(tmp), std::ios::binary | std::ios::trunc);
        f << g_store.text();
        if (!f) return;
    }
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    if (ec) LOG("[achievements] %s not written: %s", path.c_str(), ec.message().c_str());
}

// ---- the pop-up ----
struct Shown {
    std::string title;
    Group group;
    bool many = false;
};
std::vector<Shown> g_queue;  // (game thread)
double g_since = -1;         // seconds the current one has shown, -1 none
bool g_jingled = false, g_hud = false;
constexpr double kShow = 4.0, kShowLink = 5.0, kFade = 0.25, kGetFrom = 0.15, kGetFrames = 1.2, kRise = 0.3;
constexpr int kLinkFrames = 6, kLinkSize = 192;

// Link's item get as kLinkFrames pictures in a row, drawn on a thread of its own once (link_poses)
struct LinkArt {
    std::mutex mu;
    std::shared_ptr<guestmods::hud::Image> sheet;
    bool started = false, done = false;
    guestmods::hud::Handle handle = 0;
    uint64_t generation = 0;
} g_link_art;

void start_link_art() {
    std::lock_guard lock(g_link_art.mu);
    if (g_link_art.started) return;
    g_link_art.started = true;
    std::thread([] {
        auto sheet = std::make_shared<guestmods::hud::Image>();
        sheet->width = kLinkFrames * kLinkSize, sheet->height = kLinkSize;
        sheet->rgba.assign(size_t(sheet->width) * sheet->height * 4, 0);
        int drawn = 0;
        for (int i = 0; i < kLinkFrames; i++) {
            // the hop round to face the camera, then the arm up (frames 0..35 of "itemget"; the animation turns him, the
            // camera stays behind him, as the fork's link_icons "link_get")
            link_poses::PoseSpec p{0, "itemget", float(i) * 35.0f / float(kLinkFrames - 1), 165.0f, 8.0f, link_poses::Gear::kNone};
            link_poses::Bytes rgba;
            std::string why;
            if (!link_poses::render_now(p, kLinkSize, rgba, why)) {
                LOG("[achievements] Link's picture %d not drawn: %s", i, why.c_str());
                break;
            }
            for (int y = 0; y < kLinkSize; y++)
                std::memcpy(&sheet->rgba[(size_t(y) * sheet->width + size_t(i) * kLinkSize) * 4], &rgba[size_t(y) * kLinkSize * 4], size_t(kLinkSize) * 4);
            drawn++;
        }
        std::lock_guard lock(g_link_art.mu);
        g_link_art.done = true;
        if (drawn == kLinkFrames) g_link_art.sheet = sheet;
        LOG("[achievements] Link's item get: %d of %d pictures drawn", drawn, kLinkFrames);
    }).detach();
}

// the groups' icons (the game's layout art), one row of 96 x 96
std::shared_ptr<guestmods::hud::Image> g_icons;
guestmods::hud::Handle g_icons_handle = 0;
uint64_t g_icons_generation = 0;
bool g_icons_tried = false;
void make_icons() {
    if (g_icons_tried) return;
    g_icons_tried = true;
    static const char* const spec[kGroups] = {"BtnTriforce_00/Triforce_00^l", "BtnItemIcon_00/Icon128_10^l", "BtnCollectIcon_00/CollectIcon118_15^l",
                                              "BtnMenuHeart_00/HeartPiece_02^l", "BtnMapIcon_01/MapBtn_00^l", "BestShotMark_00/BestShotMark_00^t"};
    auto img = std::make_shared<guestmods::hud::Image>();
    img->width = 96 * kGroups, img->height = 96;
    img->rgba.assign(size_t(img->width) * img->height * 4, 0);
    int found = 0;
    for (int g = 0; g < kGroups; g++) {
        game_layouts::Image t;
        std::string why;
        if (!game_layouts::texture(spec[g], t, why) || !t.width) continue;
        // nearest scaling into the cell (small art, drawn at about its size)
        for (int y = 0; y < 96; y++)
            for (int x = 0; x < 96; x++) {
                const uint32_t sx = std::min(t.width - 1, uint32_t(x) * t.width / 96), sy = std::min(t.height - 1, uint32_t(y) * t.height / 96);
                std::memcpy(&img->rgba[(size_t(y) * img->width + size_t(g * 96 + x)) * 4], &t.rgba[(size_t(sy) * t.width + sx) * 4], 4);
            }
        found++;
    }
    if (found) g_icons = img;
}
guestmods::hud::Handle handle_of(const std::shared_ptr<guestmods::hud::Image>& img, guestmods::hud::Handle& h, uint64_t& gen) {
    auto& store = guestmods::hud::store();
    const uint64_t now = store.state_generation();
    if (!img) return 0;
    if (!h || gen != now) h = store.create_image(kOwner, img->width, img->height, img->width * 4, img->rgba.data(), img->rgba.size()), gen = now;
    return h;
}

// The item fanfare: NW_BGM_ITEM_GET decoded once from the player's own sound archive (nw_sound_archive.h; the path
// the game opened, note_open) and mixed into the port's output through the PCM streams (guest_audio.h), at the sound's
// own archive volume times the option's. It goes past the game's sound system: the game's music and sounds and their
// state stay as they are.
struct Fanfare {
    std::mutex mu;
    std::string path;                        // c_king.bfsar on the host
    nw_sound::Pcm pcm;                       // decoded
    bool started = false, done = false;
    std::vector<int16_t> playing;            // 48 kHz stereo at the volume of the pop-up now
    size_t at = 0;
    int32_t stream = 0;
} g_fanfare;

void start_fanfare_decode() {
    std::lock_guard lock(g_fanfare.mu);
    if (g_fanfare.started || g_fanfare.path.empty()) return;
    g_fanfare.started = true;
    std::thread([path = g_fanfare.path] {
        std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        nw_sound::Archive a(bytes);
        nw_sound::Pcm p;
        std::string why;
        const bool ok = a.read("NW_BGM_ITEM_GET", p, why);
        std::lock_guard lock(g_fanfare.mu);
        g_fanfare.done = true;
        if (ok) g_fanfare.pcm = std::move(p);
        if (ok) LOG("[achievements] item fanfare read from the game's sound archive (%.2f s)", double(g_fanfare.pcm.samples.size()) / g_fanfare.pcm.channels / g_fanfare.pcm.rate);
        else LOG("[achievements] item fanfare not read: %s (the pop-up stays quiet)", why.c_str());
    }).detach();
}

void play_jingle() {
    if (!g_jingle.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(g_fanfare.mu);
    if (g_fanfare.pcm.samples.empty()) return;
    const float gain = float(g_fanfare.pcm.volume) / 127.0f * std::clamp(g_volume.load(std::memory_order_relaxed), 0.0f, 1.0f);
    g_fanfare.playing = nw_sound::to_48k_stereo(g_fanfare.pcm, gain);
    g_fanfare.at = 0;
    if (!g_fanfare.stream) g_fanfare.stream = guestmods::pcm::store().open(kOwner, 48000, 2);
    LOG("[achievements] item fanfare at volume %.2f (stream %d)", double(g_volume.load()), g_fanfare.stream);
}

// once per logic step: what the stream has room for (its queue holds about 0.17 s)
void feed_fanfare() {
    std::lock_guard lock(g_fanfare.mu);
    auto& pcm = guestmods::pcm::store();
    if (g_fanfare.stream <= 0) return;
    if (g_fanfare.at >= g_fanfare.playing.size() / 2) {
        pcm.close(kOwner, uint32_t(g_fanfare.stream)), g_fanfare.stream = 0, g_fanfare.playing.clear();
        return;
    }
    for (int i = 0; i < 4; i++) {
        const int32_t room = pcm.available(kOwner, uint32_t(g_fanfare.stream));
        if (room <= 0) break;
        const size_t n = std::min({size_t(room), size_t(guestmods::pcm::kMaxSubmit), g_fanfare.playing.size() / 2 - g_fanfare.at});
        if (!n) break;
        const int32_t took = pcm.submit(kOwner, uint32_t(g_fanfare.stream), &g_fanfare.playing[g_fanfare.at * 2], uint32_t(n), 2);
        if (took <= 0) break;
        g_fanfare.at += size_t(took);
    }
}

void draw_popup(Cpu* c, double dt, bool hidden) {
    auto& store = guestmods::hud::store();
    if (g_since < 0 && !g_queue.empty() && !hidden) {
        g_since = 0, g_jingled = false;
        LOG("[achievements] pop-up: %s (logic step %llu)", g_queue.front().title.c_str(), (unsigned long long)mods::step());
    }
    if (g_since < 0) {
        if (g_hud) store.drop(kOwner), g_hud = false;
        return;
    }
    const bool link = g_link.load(std::memory_order_relaxed) && g_link_art.sheet;
    const double total = link ? kShowLink : kShow;
    g_since += dt;
    const Shown cur = g_queue.front();
    if (!g_jingled && g_since >= (link ? kGetFrom + kGetFrames * 11.0 / 18.0 : 0.0)) play_jingle(), g_jingled = true;
    if (g_since >= total) {
        g_queue.erase(g_queue.begin());
        g_since = -1;
        if (g_hud) store.drop(kOwner), g_hud = false;
        return;
    }
    const float a = float(std::clamp(std::min(g_since / kFade, (total - g_since) / kFade), 0.0, 1.0));
    const guestmods::hud::Handle list = store.begin(kOwner, 0);
    if (!list) return;
    using guestmods::hud::Command;
    auto alpha = [&](uint32_t rgba) { return (rgba & 0xFFFFFF00u) | uint32_t(std::lround((rgba & 0xFF) * a)); };
    // (with Link: lower and to the right, so he stands clear of the hearts at the top left)
    const float w = 560, h = 100, x = 640 - w / 2 + (link ? 90 : 0), y = link ? 176.0f : 54.0f;
    Command r;
    r.kind = Command::Rect, r.anchor = guestmods::hud::Anchor::Top, r.x = x, r.y = y, r.w = w, r.h = h, r.rgba = alpha(0x1B2A4AE0);
    store.append(kOwner, list, r);
    r.kind = Command::RectOutline, r.rgba = alpha(0xB8C4D8FF), r.thickness = 2;
    store.append(kOwner, list, r);
    // the icon: at the plate's left, or rising over Link's raised hand with a golden shine
    make_icons();
    const float lx = x - 150, ly = y - 86;  // Link's picture (kLinkSize at 1.0): his feet at the plate's bottom
    float ix = x + 16, iy = y + 12, is = 76;
    if (link) {
        const double t = g_since - kGetFrom;
        const int frame = std::clamp(int(t / kGetFrames * kLinkFrames), 0, kLinkFrames - 1);
        Command p;
        p.kind = Command::Picture, p.anchor = guestmods::hud::Anchor::Top, p.x = lx, p.y = ly, p.w = p.h = kLinkSize, p.rgba = alpha(0xFFFFFFFF);
        p.u0 = float(frame) / kLinkFrames, p.u1 = float(frame + 1) / kLinkFrames, p.v0 = 0, p.v1 = 1;
        if (const auto hl = handle_of(g_link_art.sheet, g_link_art.handle, g_link_art.generation)) store.picture(kOwner, list, hl, p);
        const float rise = float(std::clamp((t - kGetFrames) / kRise, 0.0, 1.0));
        is = 64, ix = lx + kLinkSize * 0.555f - is / 2, iy = ly + kLinkSize * 0.24f - is - 6 * rise;
        if (rise > 0) {
            Command g;
            g.kind = Command::Circle, g.anchor = guestmods::hud::Anchor::Top, g.x = ix + is / 2, g.y = iy + is / 2, g.size = is * 0.75f;
            g.rgba = alpha(0xFFE89A00u | uint32_t(0x70 * rise)), g.blend = guestmods::hud::Blend::Additive;
            store.append(kOwner, list, g);
        }
        if (t < kGetFrames) is = 0;  // (his arm comes up first)
    }
    if (is > 0) {
        Command p;
        p.kind = Command::Picture, p.anchor = guestmods::hud::Anchor::Top, p.x = ix, p.y = iy, p.w = p.h = is, p.rgba = alpha(0xFFFFFFFF);
        p.u0 = float(cur.group) / kGroups, p.u1 = float(cur.group + 1) / kGroups, p.v0 = 0, p.v1 = 1;
        if (const auto hi = handle_of(g_icons, g_icons_handle, g_icons_generation)) store.picture(kOwner, list, hi, p);
    }
    const float tx = x + (link ? 24 : 108);
    Command t;
    t.kind = Command::Text, t.anchor = guestmods::hud::Anchor::Top, t.x = tx, t.y = y + 16, t.size = 22, t.rgba = alpha(0xB8C4D8FF);
    t.text = cur.many ? "Achievements earned" : "Achievement earned";
    store.append(kOwner, list, t);
    t.y = y + 46, t.size = 34, t.rgba = alpha(0xFFFFFFFF), t.text = cur.title;
    store.append(kOwner, list, t);
    store.commit(kOwner, list);
    g_hud = true;
}

std::string file_id(uint32_t sv) {
    const unsigned no = ld8(sv + 0x20 + kDataNum + 0);  // (the u8 file number)
    return std::to_string(no < 3 ? no + 1 : 0) + ":" + player_name::current();
}

}  // namespace

void note_open(const std::string& guest_path, const std::string& host_path) {
    const size_t slash = guest_path.find_last_of('/');
    std::string name = slash == std::string::npos ? guest_path : guest_path.substr(slash + 1);
    for (char& ch : name) ch = char(std::tolower((unsigned char)ch));
    if (name != "c_king.bfsar") return;
    std::lock_guard lock(g_fanfare.mu);
    g_fanfare.path = host_path;
}

bool enabled() { return g_on.load(std::memory_order_relaxed); }
void set_enabled(bool on) {
    g_on.store(on, std::memory_order_relaxed);
    LOG("[achievements] achievements %s", on ? "on" : "off");
}
Settings settings() {
    return {g_popup.load(), g_link.load(), g_jingle.load(), g_volume.load(), g_each_file.load(), g_with_cheats.load()};
}
void set_settings(const Settings& s) {
    g_popup = s.popup, g_link = s.link, g_jingle = s.jingle, g_volume = std::clamp(s.volume, 0.0f, 1.0f);
    g_each_file = s.each_file, g_with_cheats = s.with_cheats;
}

View view() {
    View v;
    std::lock_guard lock(g_mu);
    read_store();
    v.file = g_file;
    v.pausing = g_with_cheats.load() ? std::vector<std::string>{} : g_pausing;
    for (const Goal& g : kAll) {
        View::Row r;
        r.key = g.key, r.title = g.title, r.group = g.group, r.points = g.points;
        r.all = g_store.earned_all(g.key), r.file = g_file.empty() ? 0 : g_store.earned_file(g_file, g.key);
        r.desc = game_progress::fill_name(g.desc, g.item, g.place);
        v.rows.push_back(std::move(r));
    }
    return v;
}

void reset() {
    std::lock_guard lock(g_mu);
    read_store();
    g_store.reset();
    g_checker.restart();
    write_store();
    LOG("[achievements] reset");
}

void service(Cpu* c) {
    static bool was_on = false;
    if (!enabled()) {
        if (was_on) {
            g_queue.clear(), g_since = -1;
            if (g_hud) guestmods::hud::store().drop(kOwner), g_hud = false;
            std::lock_guard lock(g_mu);
            g_file.clear();
            g_checker.restart();
        }
        was_on = false;
        return;
    }
    was_on = true;
    const bool hidden = mods::game_ui_hides_hud(mods::game_ui_now()) || !g_popup.load(std::memory_order_relaxed);
    draw_popup(c, 1.0 / 30.0, hidden && g_since < 0);
    feed_fanfare();
    if (mods::step() % 30) return;  // (a second of game time)
    const uint32_t s = game_progress::save_address();
    std::lock_guard lock(g_mu);
    read_store();
    if (!s) {
        g_file.clear();
        g_checker.restart();
        return;
    }
    if (g_link.load(std::memory_order_relaxed)) start_link_art();
    if (g_jingle.load(std::memory_order_relaxed)) start_fanfare_decode();
    const uint32_t sv = ld32(kSaveInfoPtr);
    unsigned char d[GP_SAVE_LEN];
    std::memcpy(d, mem::ptr(s), sizeof d);
    char stage[9] = {};
    std::memcpy(stage, mem::ptr(kStageName), 8);
    const bool hero = ld8(sv + kHeroFlags) == 1;
    g_file = file_id(sv);
    g_pausing = mods::cheats_pausing();
    Options opt;
    opt.with_cheats = g_with_cheats.load(std::memory_order_relaxed);
    const int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const Earned e = g_checker.check(d, stage, hero, g_file, int64_t(mods::game_time() * 1000), when, g_store, opt, g_pausing);
    if (log_on()) LOG("[achievements] check %s in %s%s: %zu new (%zu new for all), %u earned here", g_file.c_str(), stage, e.paused ? " (paused)" : "",
                      e.keys.size(), e.new_for_all.size(), g_store.count(&g_file));
    const std::vector<std::string>& shown = g_each_file.load(std::memory_order_relaxed) ? e.keys : e.new_for_all;
    for (const std::string& k : e.keys) LOG("[achievements] earned %s (%s)", k.c_str(), g_file.c_str());
    if (shown.size() > 3) g_queue.push_back({std::to_string(shown.size()) + " achievements", kStory, true});
    else
        for (const std::string& k : shown)
            for (const Goal& g : kAll)
                if (k == g.key) g_queue.push_back({g.title, g.group, false});
    write_store();
}

}  // namespace achievements
