// SPDX-License-Identifier: MPL-2.0
// Link's appearance in the port (link_appearance.h): the look shown, Link's recoloured textures, the save's
// records, the colour page with its close camera, and test aids.
//
// Textures. Link.bfres stays in the game's memory for the whole session (the 3D pack's) and its textures are
// drawn from it in place. Its first 64 bytes (from the masks, link_masks.h) find it (a scan on a worker thread,
// as player_models.cpp finds it); each texture with a mask is its FTEX object at the mask's offset. For a look,
// a copy of the texture's bytes is tinted (link_masks::tint per part) and the FTEX's surface pointers (image,
// mips) aimed at the copy; with no look they point at the game's own bytes again. The copies are host memory
// (not in save states): after a state load each pointer is checked and one that is not the game's own nor a
// copy of this session is put back from the FTEX's own data offset. Nothing shown and nothing changed: service()
// returns at once and neither the masks nor the game's memory are touched.
//
// Test aids (not settings): WWHD_LINK_APPEARANCE=<look text> (link_appearance::from_text) is the look of every
// save file at the start, as if saved; WWHD_LINK_APPEARANCE_PAGE=<n> asks for the colour page at TV frame n
// (tried until Link can stand still for it); WWHD_LINK_APPEARANCE_KEYS=<frame>:<buttons hex>[,...] presses buttons on
// the page (each for 4 reads), for scripted runs.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../input.h"
#include "../runtime.h"
#include "../savestate.h"
#include "console_language.h"
#include "game_look.h"
#include "game_progress_host.h"
#include "game_settings_text.h"
#include "guest_addr.h"
#include "guest_hud.h"
#include "link_appearance.h"
#include "link_appearance_page.h"
#include "link_masks.h"
#include "../screenshot.h"
#include "../gfx/renderer.h"
#include "../aspect.h"
#include "story_notebook_game.h"
#include "story_notebook_page.h"
#include "story_scenes.h"

namespace link_appearance {
namespace {

using game_settings_page::Command;
using game_settings_page::Input;
using game_settings_page::Sound;

bool trace() {
    static const bool on = getenv("WWHD_LINK_APPEARANCE_TRACE") != nullptr;
    return on;
}

uint32_t be32(uint32_t ea) { return ld32(ea); }

// ---------------------------------------------------------------- the save file played
constexpr uint32_t kDataNum = 0x1290;  // dSv_info_c::mDataNum (as player_models.cpp)
constexpr uint32_t kEvents = 0x624;    // the save's event bits
constexpr uint32_t kScanStep = 0x100;  // Link.bfres starts on a 256-byte boundary in the heap (as player_models_core.h scans)
// a new game in the save file played: no event of the story done yet (the game's two set from the start excepted),
// as player_models_core.h tells it
bool new_game(const uint8_t* events /* save + kEvents, 0x100 bytes */) {
    for (int i = 0; i < 0x100; i++)
        if (events[i] && i != 0x7E && i != 0xBE) return false;
    return true;
}

int read_file() {
    const uint32_t save = game_progress::save_address();
    if (!save) return -1;
    const int f = int(ld8(save + kDataNum));
    return f >= 0 && f < 3 ? f : -1;
}

// ---------------------------------------------------------------- textures
struct Tex {
    const link_masks::Mask* m = nullptr;
    uint32_t game_img = 0, game_mip = 0;    // the game's own bytes
    static constexpr int kRing = 3;
    uint32_t img[kRing] = {}, mip[kRing] = {};
    int next = 0;
    uint32_t worn_img = 0, worn_mip = 0;    // the copy pointed at now (0: the game's own)
    Look made;                               // the look of that copy
    int wait = 0;                            // frames until its bytes are looked at again (not Link's own: another model)
};
std::vector<Tex> g_tex;                      // game thread
std::atomic<uint32_t> g_base{0};
std::atomic<int> g_scan{0};  // 0 to do, 1 running, 2 found, 3 not found
uint64_t g_loads = ~0ull;

void scan(std::array<uint8_t, 64> head) {
    uint32_t found = 0;
    for (uint32_t a = mem::kMem2Start; a + head.size() <= mem::kMem2End; a += kScanStep) {
        const uint8_t* p = mem::ptr(a);
        if (p[0] != 'F' || memcmp(p, head.data(), head.size()) != 0) continue;
        found = a;
        break;
    }
    if (found) LOG("[appearance] Link.bfres at %08X", found);
    g_base = found;
    g_scan.store(found ? 2 : 3, std::memory_order_release);
}

// the texture's FTEX in memory, its GX2Surface at +4 (+4 width, +8 height, +0x14 format, +0x20 image size,
// +0x24 image, +0x28 mip size, +0x2C mips, +0x38 alignment), the data offsets as in the file at +0xB0 / +0xB4
bool ftex_ok(uint32_t ftex, const link_masks::Mask& m) {
    if (ftex < mem::kMem2Start || ftex + 0xC0 > mem::kMem2End) return false;
    const uint8_t* p = mem::ptr(ftex);
    if (memcmp(p, "FTEX", 4)) return false;
    const uint32_t gx = ftex + 4;
    return be32(gx + 4) == m.width && be32(gx + 8) == m.height && be32(gx + 0x14) == m.format && be32(gx + 0x20) == m.image_size &&
           be32(gx + 0x28) == m.mip_size;
}
bool in_mem2(uint32_t a, uint32_t n) { return a >= mem::kMem2Start && n <= mem::kMem2End - a; }
bool is_copy(const Tex& t, uint32_t img) {
    for (uint32_t c : t.img)
        if (c && c == img) return true;
    return false;
}
// the game's own data of a texture: its pointer when it is no copy of ours, else the FTEX's data offsets
bool game_bytes(uint32_t ftex, const link_masks::Mask& m, uint32_t& img, uint32_t& mip) {
    const uint32_t gx = ftex + 4;
    img = be32(gx + 0x24), mip = be32(gx + 0x2C);
    auto ok = [&](uint32_t i, uint32_t p) {
        return in_mem2(i, m.image_size) && (!m.mip_size || in_mem2(p, m.mip_size)) &&
               link_masks::find(mem::ptr(i), m.image_size, m.mip_size ? mem::ptr(p) : nullptr, m.mip_size) == &m;
    };
    if (ok(img, mip)) return true;
    // (a pointer of another session's copy, from a save state: the file's own offsets)
    const uint32_t ri = ftex + 0xB0 + be32(ftex + 0xB0), rm = m.mip_size ? ftex + 0xB4 + be32(ftex + 0xB4) : 0;
    if (ok(ri, rm)) return img = ri, mip = rm, true;
    return false;
}

bool relevant(const link_masks::Mask& m, const Look& l, Slot s) {
    if (!l.has(s)) return false;
    for (uint8_t p : parts_of(s))
        if (p < link_masks::kParts && m.base_rgb[p]) return true;
    return false;
}
// a look restricted to what this texture shows (so a change of eyes doesn't remake the clothes)
Look for_texture(const link_masks::Mask& m, const Look& l) {
    Look o;
    for (int i = 0; i < kSlots; i++)
        if (relevant(m, l, Slot(i))) o.put(Slot(i), l.colour(Slot(i)));
    return o;
}

void put_back(Tex& t, uint32_t ftex) {
    if (!t.worn_img) return;
    st32(ftex + 4 + 0x24, t.game_img);
    if (t.m->mip_size) st32(ftex + 4 + 0x2C, t.game_mip);
    t.worn_img = t.worn_mip = 0;
    t.made = Look{};
}

void wear(Tex& t, uint32_t ftex, const Look& look) {
    const link_masks::Mask& m = *t.m;
    const int k = t.next;
    t.next = (t.next + 1) % Tex::kRing;
    const uint32_t align = std::max<uint32_t>(be32(ftex + 4 + 0x38), 0x100);
    if (!t.img[k]) t.img[k] = mem::host_alloc(m.image_size, align), t.mip[k] = m.mip_size ? mem::host_alloc(m.mip_size, align) : 0;
    memcpy(mem::ptr(t.img[k]), mem::ptr(t.game_img), m.image_size);
    if (m.mip_size) memcpy(mem::ptr(t.mip[k]), mem::ptr(t.game_mip), m.mip_size);
    for (int i = 0; i < kSlots; i++)
        if (look.has(Slot(i)))
            for (uint8_t p : parts_of(Slot(i)))
                link_masks::tint(m, link_masks::Part(p), look.colour(Slot(i)), mem::ptr(t.img[k]), m.mip_size ? mem::ptr(t.mip[k]) : nullptr);
    st32(ftex + 4 + 0x24, t.img[k]);
    if (m.mip_size) st32(ftex + 4 + 0x2C, t.mip[k]);
    t.worn_img = t.img[k], t.worn_mip = t.mip[k], t.made = look;
    if (trace()) LOG("[appearance] %s: %s (copy %08X)", m.texture.c_str(), to_text(look).c_str(), t.img[k]);
}

// all textures back to the game's own (the look cleared, Link.bfres gone or another model in it)
bool g_worn_any = false;
int g_wanted = 0;  // frames left that another part wants Link's textures found (want_textures)
void put_all_back() {
    const uint32_t base = g_base.load();
    for (Tex& t : g_tex)
        if (t.worn_img && base && ftex_ok(base + t.m->ftex, *t.m)) put_back(t, base + t.m->ftex);
        else t.worn_img = t.worn_mip = 0, t.made = Look{};
    if (g_worn_any) LOG("[appearance] Link's own colours");
    g_worn_any = false;
}

void apply(const Look& look) {
    // nothing to show and nothing shown: nothing at all (the masks are not even made)
    if (look.empty() && !g_worn_any && !g_wanted) return;
    if (g_tex.empty()) {
        static const char* const kNames[] = {"linktexS3TC", "linktexbci4", "katsuraS3TC", "hitomi"};
        bool all = true;
        for (const char* n : kNames) all &= link_masks::get(n) != nullptr;  // (starts the masks' worker)
        if (!all) return;  // (not ready yet)
        for (const char* n : kNames) g_tex.push_back(Tex{link_masks::get(n)});
    }
    // Link.bfres in memory
    int s = g_scan.load(std::memory_order_acquire);
    if (s == 1) return;
    const uint32_t base = g_base.load();
    if (s == 2 && memcmp(mem::ptr(base), g_tex[0].m->file_head, sizeof g_tex[0].m->file_head) != 0) s = 0;
    static int frame = 0, last_scan = -100000;
    frame++;
    if (s != 2) {
        for (Tex& t : g_tex) t.worn_img = t.worn_mip = 0, t.made = Look{};
        if (frame - last_scan < 120) return;
        last_scan = frame;
        g_scan = 1;
        std::array<uint8_t, 64> head;
        memcpy(head.data(), g_tex[0].m->file_head, 64);
        std::thread(scan, head).detach();
        return;
    }
    // after a save state: each texture's pointer checked again
    const uint64_t lc = ss::last_load_frame() ^ (ss::last_arrival_frame() << 32);
    const bool loaded = lc != g_loads;
    g_loads = lc;
    bool any = false;
    for (Tex& t : g_tex) {
        const uint32_t ftex = base + t.m->ftex;
        if (!ftex_ok(ftex, *t.m)) {  // (another model in Link.bfres: a GameCube player model)
            t.worn_img = t.worn_mip = 0, t.made = Look{};
            continue;
        }
        const uint32_t now = be32(ftex + 4 + 0x24);
        if (t.wait > 0 && !loaded) { t.wait--; continue; }
        if (loaded || (now != t.worn_img && now != t.game_img)) {
            uint32_t img, mip;
            if (!game_bytes(ftex, *t.m, img, mip)) { t.wait = 60; continue; }  // (not Link's own bytes: left as they are)
            if (!is_copy(t, now) || img != now) {
                if (now != img) {  // a pointer of another session's copy: the game's own again
                    st32(ftex + 4 + 0x24, img);
                    if (t.m->mip_size) st32(ftex + 4 + 0x2C, mip);
                    LOG("[appearance] %s: its texture pointer put back (save state)", t.m->texture.c_str());
                }
                t.worn_img = t.worn_mip = 0, t.made = Look{};
            }
            t.game_img = img, t.game_mip = mip;
        } else if (!t.game_img) {
            uint32_t img, mip;
            if (!game_bytes(ftex, *t.m, img, mip)) { t.wait = 60; continue; }
            t.game_img = img, t.game_mip = mip;
        }
        const Look want = for_texture(*t.m, look);
        if (want.empty()) put_back(t, ftex);
        else if (!t.worn_img || t.made != want) wear(t, ftex, want);
        any |= t.worn_img != 0;
    }
    if (any && !g_worn_any) LOG("[appearance] Link's look: %s", to_text(look).c_str());
    g_worn_any = any;
}

// ---------------------------------------------------------------- the colour page
const uint32_t kPlayFn = GC(0x025200D4), kCameraFn = GC(0x024F8044), kStopCamera = GC(0x02514F2C), kStartCamera = GC(0x02514F38),
               kSetCamera = GC(0x02514F50), kResetCamera = GC(0x02515048), kOrder = GC(0x0253EC0C), kCheckStart = GC(0x0253FD1C),
               kEnd = GC(0x0253EFA8), kHideHud = GC(0x025916C4), kShowHud = GC(0x0259169C);
constexpr uint32_t kControl = 0x51D0, kMode = 0x5292, kOrderCount = 0x5290, kPlayer = 0x5B34, kHudShown = 0x5C22;
constexpr uint32_t kCenter = 0x10, kEye = 0x1C, kActive = 0x4;  // dCamera_c
constexpr uint32_t kPos = 0x314, kAngleY = 0x32A;                // fopAc_ac_c
const uint32_t kIngameUi = GD(0x101F8344);
constexpr uint32_t kUiItemScreen = 0x17C;

uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    Cpu k = *c;
    return guest_call(&k, fn, args);
}

const std::string kOwner = "port:link-appearance";
game_look::Look g_look(kOwner, "appearance");
uint32_t g_art_menu = 0;
float measure(const std::string& text, float size) { return g_look.measure(text, size); }

page::Page g_page;
std::atomic<int> g_want{0};          // frames left to open the asked-for page (0: none)
std::atomic<bool> g_has_pad{false};  // the page has the controller
std::atomic<int> g_close{0};         // the page closed at a controller read: 1 put back, 2 kept (the logic pass ends it)
std::atomic<uint32_t> g_reads_without_pad{0};
struct Stage {
    bool on = false;
    uint32_t play = 0, camera = 0, scratch = 0;
    uint8_t saved[24] = {};
    bool camera_was_active = false, hud = false;
    float link[3] = {};
    int16_t heading = 0;
    float orbit = 0;      // the camera's turn around Link (radians)
    float turn = 0;       // the page's turn input
} g_stage;
std::mutex g_page_mu;  // g_page between the controller reads and the logic passes

std::string tr(const std::string& english) { return game_settings_text::tr(english, console_language()); }

void texts() {
    auto& t = g_page.texts;
    t.title = tr("Link's look"), t.original = tr("Original"), t.custom = tr("Custom"), t.reset = tr("Original look"), t.done = tr("Done");
    t.back = tr("Back"), t.choose = tr("Change"), t.turn = tr("Right stick or L / R: turn");
    g_page.area_names.clear(), g_page.colour_names.clear();
    for (int i = 0; i < kSlots; i++) {
        g_page.area_names.push_back(tr(slot_label(Slot(i))));
        std::vector<std::string> names;
        for (const Preset& p : presets(Slot(i))) names.push_back(tr(p.name));
        g_page.colour_names.push_back(names);
    }
}

void sound(Cpu* c, Sound s) {
    const char* label = nullptr;
    switch (s) {
    case Sound::Cursor: label = "SE_MENU_CURSOR"; break;
    case Sound::Change: label = "SE_MENU_OPTION_SWITCH"; break;
    case Sound::Decide: label = "SE_MENU_DECIDE_S"; break;
    case Sound::Cancel: case Sound::Blocked: label = "SE_MENU_CANCEL"; break;
    case Sound::Open: label = "SE_MENU_OPTION_IN"; break;
    default: break;
    }
    const auto& ui = story_notebook::game_ui();
    if (label && c && ui.sound) ui.sound(c, label);
}

void publish(Cpu* c, std::vector<Command> cmds) {
    auto& store = guestmods::hud::store();
    if (cmds.empty()) {
        store.drop(kOwner);
        return;
    }
    const auto& ui = story_notebook::game_ui();
    g_look.ui = &ui;
    // the menu art from the item screen (built with the stage; plain boxes when it can't be read)
    const uint32_t ingame = ld32(kIngameUi);
    const uint32_t items = (ingame & 3) == 0 && ingame >= mem::kMem2Start && ingame < mem::kMem2End - 0x24C ? ld32(ingame + kUiItemScreen) : 0;
    if (ui.art && items && items != g_art_menu) {
        g_art_menu = items;
        std::string why;
        g_look.art_ok = ui.art(items, g_look.art, why);
        if (g_look.art_ok) story_notebook::style::trim_plates(g_look.art);
        g_look.forget_boxes();
        LOG("[appearance] menu art %s%s%s", g_look.art_ok ? "read" : "not read (plain boxes)", why.empty() ? "" : ": ", why.c_str());
    }
    if (ui.prompts) g_look.prompts = ui.prompts();
    g_look.load_glyphs(c, story_notebook::font::texts_of(cmds));
    const uint32_t screen = 0;  // the TV: the page goes with Link in play
    const guestmods::hud::Handle list = store.begin(kOwner, screen);
    if (!list) return;
    std::vector<guestmods::hud::Handle> images;
    cmds = g_look.render(std::move(cmds), images, screen);
    for (Command& k : cmds) k.anchor = guestmods::hud::Anchor::Right;  // (the page keeps to the picture's right edge: 4:3 to 32:9)
    g_look.append(list, cmds, images);
    store.commit(kOwner, list);
}

// the close camera: in front of Link (turned by the orbit), looking at his chest from a little above, the
// look point moved to the camera's right so Link stands left of the window
void place_camera(Cpu* c) {
    const double a = double(g_stage.heading) * 3.14159265358979 / 32768.0 + g_stage.orbit;
    const double fx = std::sin(a), fz = std::cos(a);
    const double dist = 235, eye_up = 72, look_up = 46, shift = 56;
    const double rx = fz, rz = -fx;  // the camera's right (it looks along -f)
    const double eye[3] = {g_stage.link[0] + fx * dist, g_stage.link[1] + eye_up, g_stage.link[2] + fz * dist};
    const double ctr[3] = {g_stage.link[0] + rx * shift, g_stage.link[1] + look_up, g_stage.link[2] + rz * shift};
    for (int i = 0; i < 3; i++) stf32(g_stage.scratch + 4 * i, float(ctr[i])), stf32(g_stage.scratch + 12 + 4 * i, float(eye[i]));
    call(c, kSetCamera, {g_stage.camera, g_stage.scratch, g_stage.scratch + 12});
}

bool stage_start(Cpu* c, std::string& why) {
    if (!(why = ss::control_problem()).empty()) return false;
    if (story_scenes::active()) return why = "a mod scene is running", false;
    const uint32_t play = call(c, kPlayFn), camera = call(c, kCameraFn);
    const uint32_t link = play ? ld32(play + kPlayer) : 0;
    if (!play || !camera || !link) return why = "Link is not in the scene", false;
    if (ld8(play + kMode) || ld8(play + kOrderCount)) return why = "an event is running", false;
    // a compulsory event without an event script (as the editor's fly camera and mod scenes start it): Link
    // stands still, no stage or save flags are involved
    uint8_t previous[0xF4];
    memcpy(previous, mem::ptr(play + kControl), sizeof previous);
    if (!call(c, kOrder, {play + kControl, 3, 1, 0, 0, 0, 0, 0xFFFFFFFFu, 0xFF})) return why = "the game refused the event", false;
    if (!call(c, kCheckStart, {play + kControl}) || ld8(play + kMode) != 3) {
        memcpy(mem::ptr(play + kControl), previous, sizeof previous);
        return why = "the game refused the event", false;
    }
    if (!g_stage.scratch) g_stage.scratch = mem::host_alloc(32, 16);
    g_stage.on = true, g_stage.play = play, g_stage.camera = camera;
    memcpy(g_stage.saved, mem::ptr(camera + kCenter), 24);
    g_stage.camera_was_active = ld8(camera + kActive) != 0;
    g_stage.hud = ld8(play + kHudShown) != 0;
    for (int i = 0; i < 3; i++) g_stage.link[i] = float(ldf32(link + kPos + 4 * uint32_t(i)));
    g_stage.heading = int16_t(ld16(link + kAngleY));
    g_stage.orbit = 0, g_stage.turn = 0;
    call(c, kStopCamera, {camera});
    call(c, kHideHud);
    place_camera(c);
    return true;
}

void stage_stop(Cpu* c, bool ours) {
    if (!g_stage.on) return;
    if (ours) {
        memcpy(mem::ptr(g_stage.camera + kCenter), g_stage.saved, 24);
        call(c, kResetCamera, {g_stage.camera});
        if (g_stage.camera_was_active) call(c, kStartCamera, {g_stage.camera});
        call(c, g_stage.hud ? kShowHud : kHideHud);
        call(c, kEnd, {g_stage.play + kControl});
    }
    g_stage.on = false;
}

void close_page(Cpu* c, bool keep, bool ours) {
    Look kept;
    {
        std::lock_guard<std::mutex> lk(g_page_mu);
        kept = g_page.look();
    }
    stage_stop(c, ours);
    publish(c, {});
    store::preview(false, {});
    if (keep) {
        std::string why;
        set_current(kept, why);
    }
    g_has_pad = false;
    LOG("[appearance] colour page closed (%s): %s", keep ? "kept" : "put back", keep ? (kept.empty() ? "Link's own colours" : to_text(kept).c_str()) : "-");
}

std::vector<int> number_list(const char* env) {
    std::vector<int> v;
    for (const char* p = env; p && *p;) {
        char* end;
        const long n = strtol(p, &end, 10);
        if (end == p) break;
        v.push_back(int(n));
        p = *end == ',' ? end + 1 : end;
    }
    return v;
}
// test aid: the page's HUD list as the TV overlay draws it, into a PNG of the TV picture's shape (screenshots hold the
// game's picture only; the guest HUD is drawn over it in the window). Pictures (text, menu art) sampled nearest,
// rectangles, circles and lines blended over a transparent picture.
void test_overlay(const std::string& path) {
    const float h = 720, w = std::round(h * aspect::game());
    std::vector<float> px(size_t(w) * size_t(h) * 4, 0.0f);
    auto blend = [&](int x, int y, float r, float g, float b, float a) {
        if (x < 0 || y < 0 || x >= int(w) || y >= int(h) || a <= 0) return;
        float* d = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        d[0] = r * a + d[0] * (1 - a), d[1] = g * a + d[1] * (1 - a), d[2] = b * a + d[2] * (1 - a), d[3] = a + d[3] * (1 - a);
    };
    for (const auto& list : guestmods::hud::store().snapshot(0)) {
        if (list->owner != kOwner) continue;
        for (const Command& c : list->commands) {
            const auto a = guestmods::hud::anchored(c.x, c.y, c.anchor, w, h, 1280, 720), b = guestmods::hud::anchored(c.x + c.w, c.y + c.h, c.anchor, w, h, 1280, 720);
            const float cr = float(c.rgba >> 24) / 255, cg = float(c.rgba >> 16 & 255) / 255, cb = float(c.rgba >> 8 & 255) / 255, ca = float(c.rgba & 255) / 255;
            switch (c.kind) {
            case Command::Rect:
                for (int y = int(a.y); y < int(b.y); y++)
                    for (int x = int(a.x); x < int(b.x); x++) blend(x, y, cr, cg, cb, ca);
                break;
            case Command::Circle: case Command::CircleOutline:
                for (int y = int(a.y - c.size - 2); y <= int(a.y + c.size + 2); y++)
                    for (int x = int(a.x - c.size - 2); x <= int(a.x + c.size + 2); x++) {
                        const float d = std::hypot(x + 0.5f - a.x, y + 0.5f - a.y);
                        const bool in = c.kind == Command::Circle ? d <= c.size : std::fabs(d - c.size) <= c.thickness / 2;
                        if (in) blend(x, y, cr, cg, cb, ca);
                    }
                break;
            case Command::Line: {
                const float dx = b.x - a.x, dy = b.y - a.y, len2 = std::max(1e-6f, dx * dx + dy * dy);
                for (int y = int(std::min(a.y, b.y) - c.thickness); y <= int(std::max(a.y, b.y) + c.thickness); y++)
                    for (int x = int(std::min(a.x, b.x) - c.thickness); x <= int(std::max(a.x, b.x) + c.thickness); x++) {
                        const float t = std::clamp(((x + 0.5f - a.x) * dx + (y + 0.5f - a.y) * dy) / len2, 0.0f, 1.0f);
                        if (std::hypot(x + 0.5f - (a.x + t * dx), y + 0.5f - (a.y + t * dy)) <= c.thickness / 2) blend(x, y, cr, cg, cb, ca);
                    }
                break;
            }
            case Command::Picture: {
                if (!c.image || !c.image->width) break;
                const auto& im = *c.image;
                for (int y = int(a.y); y < int(b.y); y++)
                    for (int x = int(a.x); x < int(b.x); x++) {
                        const float u = c.u0 + (c.u1 - c.u0) * ((x + 0.5f - a.x) / std::max(1.0f, b.x - a.x));
                        const float v = c.v0 + (c.v1 - c.v0) * ((y + 0.5f - a.y) / std::max(1.0f, b.y - a.y));
                        const uint32_t sx = std::min(im.width - 1, uint32_t(std::max(0.0f, u * float(im.width))));
                        const uint32_t sy = std::min(im.height - 1, uint32_t(std::max(0.0f, v * float(im.height))));
                        const uint8_t* t = &im.rgba[(size_t(sy) * im.width + sx) * 4];
                        blend(x, y, t[0] / 255.f * cr, t[1] / 255.f * cg, t[2] / 255.f * cb, t[3] / 255.f * ca);
                    }
                break;
            }
            default: break;
            }
        }
    }
    std::vector<uint8_t> out(px.size());
    for (size_t i = 0; i < px.size(); i += 4) {
        const float al = px[i + 3];
        for (int k = 0; k < 3; k++) out[i + size_t(k)] = uint8_t(std::lround(std::clamp(al > 0 ? px[i + size_t(k)] / al : 0.0f, 0.0f, 1.0f) * 255));
        out[i + 3] = uint8_t(std::lround(std::clamp(al, 0.0f, 1.0f) * 255));
    }
    std::vector<uint8_t> png;
    if (!screenshot::encode_png_rgba(uint32_t(w), uint32_t(h), out.data(), png)) return;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    LOG("[appearance] test: page overlay %s", path.c_str());
}

// test aid: scripted presses on the page, and screenshots at page reads (WWHD_LINK_APPEARANCE_SHOTS=<read>,...)
// with the page's overlay (page_<read>.png in the screenshot folder)
int g_test_reads = 0;
void test_shot() {
    static const std::vector<int> at = number_list(getenv("WWHD_LINK_APPEARANCE_SHOTS"));
    if (std::find(at.begin(), at.end(), g_test_reads) == at.end()) return;
    screenshot::request();
    test_overlay(screenshot::dir() + "/page_" + std::to_string(g_test_reads) + ".png");
    LOG("[appearance] test: screenshot at page read %d", g_test_reads);
}
uint32_t test_buttons() {
    static const char* env = getenv("WWHD_LINK_APPEARANCE_KEYS");
    static std::vector<std::pair<int, uint32_t>> keys = [] {
        std::vector<std::pair<int, uint32_t>> v;
        if (!env) return v;
        std::string s(env);
        size_t at = 0;
        while (at < s.size()) {
            size_t end = s.find(',', at);
            if (end == std::string::npos) end = s.size();
            const std::string it = s.substr(at, end - at);
            const size_t colon = it.find(':');
            if (colon != std::string::npos) v.push_back({atoi(it.c_str()), uint32_t(strtoul(it.c_str() + colon + 1, nullptr, 16))});
            at = end + 1;
        }
        return v;
    }();
    if (keys.empty() || !g_page.is_open()) return 0;
    for (const auto& [at, b] : keys)
        if (g_test_reads >= at && g_test_reads < at + 4) return b;
    return 0;
}

}  // namespace

bool request_page(std::string& why) {
    if (current_file() < 0) return why = "no save file is played", false;
    if (g_page.is_open()) return why = "the colour page is open", false;
    g_want = 150;  // (5 s at 30 Hz for the talk's event to end)
    return true;
}

bool page_open() { return g_has_pad.load(std::memory_order_relaxed); }

void service() {
    const int f = read_file();
    bool fresh = false;
    if (f >= 0)
        if (const uint32_t save = game_progress::save_address())
            fresh = new_game(static_cast<const uint8_t*>(mem::ptr(save + kEvents)));
    const Look look = store::frame(f, fresh);
    if (g_wanted) g_wanted--;
    if (look.empty() && !g_worn_any && !g_wanted) return;
    apply(look);
}

void want_textures() { g_wanted = 4; }

std::vector<GameTexture> game_textures() {
    std::vector<GameTexture> out;
    const uint32_t base = g_base.load();
    if (g_scan.load(std::memory_order_acquire) != 2 || !base) return out;
    for (const Tex& t : g_tex)
        if (t.game_img && ftex_ok(base + t.m->ftex, *t.m)) out.push_back({base + t.m->ftex, t.game_img, t.game_mip, t.m});
    return out;
}

void logic_service(Cpu* c, bool logic_pass) {
    // test aid: the page asked for at that TV frame (and tried until Link can stand still for it)
    if (static const uint64_t at = getenv("WWHD_LINK_APPEARANCE_PAGE") ? strtoull(getenv("WWHD_LINK_APPEARANCE_PAGE"), nullptr, 10) : 0; at && logic_pass) {
        static bool asked = false;
        if (!asked && render::frame_count() >= at) {
            asked = true;
            g_want = 1 << 20;
            LOG("[appearance] test: colour page asked for");
        }
    }
    if (!logic_pass) return;
    if (const int closed = g_close.exchange(0)) {
        const uint32_t play = g_stage.on ? call(c, kPlayFn) : 0;
        close_page(c, closed == 2, g_stage.on && play == g_stage.play && ld8(play + kMode) == 3);
        return;
    }
    if (!g_page.is_open() && !g_stage.on) {
        const int want = g_want.load();
        if (!want) return;
        std::string why;
        if (!stage_start(c, why)) {
            if (g_want.fetch_sub(1) == 1) LOG("[appearance] colour page not opened: %s", why.c_str());
            return;
        }
        g_want = 0;
        texts();
        {
            const Look now = current();
            store::preview(true, now);
            std::lock_guard<std::mutex> pk(g_page_mu);
            g_page.open(now);
        }
        g_reads_without_pad = 0;
        g_has_pad = true;
        sound(c, Sound::Open);
        LOG("[appearance] colour page open");
        return;
    }
    if (!g_stage.on) return;
    const uint32_t play = call(c, kPlayFn);
    const bool ours = play == g_stage.play && call(c, kCameraFn) == g_stage.camera && ld8(play + kMode) == 3;
    if (!ours) {  // a stage change, a state load: the page goes
        {
            std::lock_guard<std::mutex> pk(g_page_mu);
            g_page = page::Page{};
        }
        close_page(c, false, false);
        return;
    }
    g_stage.orbit += g_stage.turn * 2.6f / 30.0f;  // a turn in about 2.4 s
    place_camera(c);
}

void pad(input::PadState& p, Cpu* c) {
    if (!g_has_pad.load(std::memory_order_relaxed)) return;
    g_reads_without_pad = 0;
    g_test_reads++;
    test_shot();
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
    last = now;
    Input in;
    in.buttons = p.buttons | test_buttons(), in.lx = p.lx, in.ly = p.ly, in.touch = p.touch, in.dt = dt;
    in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;
    page::Action act;
    std::vector<Command> cmds;
    Look look;
    {
        std::lock_guard<std::mutex> pk(g_page_mu);
        g_page.measure = g_look.font_ready() ? measure : nullptr;
        g_page.set_right_stick(p.rx);
        g_page.tick(dt);
        act = g_page.input(in);
        look = g_page.look();
        cmds = g_page.draw();
    }
    g_stage.turn = act.turn;
    if (trace() && (act.sound != Sound::None || act.closed))
        LOG("[appearance] page read %d: buttons %04X, row %d, %s%s", g_test_reads, unsigned(in.buttons), g_page.selected(), to_text(look).c_str(),
            act.closed ? (act.keep ? " (closed: kept)" : " (closed: put back)") : "");
    if (act.changed) store::preview(true, look);
    if (act.sound != Sound::None) sound(c, act.sound);
    if (act.closed) g_close = act.keep ? 2 : 1, publish(c, {});
    else publish(c, std::move(cmds));
    p = input::PadState{};  // the game gets an idle controller while the page is open
}

void mute(input::PadState& p) {
    if (!g_has_pad.load(std::memory_order_relaxed)) return;
    if (g_reads_without_pad.fetch_add(1, std::memory_order_relaxed) < 90) p = input::PadState{};
}

}  // namespace link_appearance
