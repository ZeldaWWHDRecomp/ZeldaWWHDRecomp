// The game-style settings page in the item menu: the driver (game_settings_page.h is the page).
//
// Where it runs: VPADRead (hle/system_stubs.cpp) hands every GamePad read to pad() after the story
// notebook and the Delivery Bag pages; KPADReadEx hands the Pro Controller's reads to mute(). While
// the page is open the game gets an idle controller (as with the notebook), and buttons held when it
// closes reach the game only after their release.
//
// When it is offered: the in-game UI (hdIngameUi_c, sIngameUi) is in its Menu state (the + menu
// stands still: not opening, not in a sub window, not saving) and no bag window is open. The page is
// drawn on the screen with the item menu (the GamePad, or the TV with the Pro Controller) through the
// HUD layer; its text and boxes use the game's menu font and window art, read from the running game
// (story_notebook_game.cpp's services), never stored.
//
#include "game_settings_page.h"
#include "game_settings_entry.h"
#include "input_map.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#include "game_settings.h"
#include "game_settings_text.h"
#include "console_language.h"
#include "game_look.h"
#include "guest_hud.h"
#include "story_notebook_game.h"
#include "story_notebook.h"
#include "story_notebook_page.h"  // menu_screen, fit_to_screen
#include "guest_addr.h"
#include "input.h"
#include "runtime.h"
#include "screenshot.h"
#include "tv_pointer.h"
#include "fixed_seed.h"
#include "gfx/renderer.h"

namespace game_settings_page {
namespace {

namespace font = story_notebook::font;
namespace style = story_notebook::style;
using guestmods::hud::Handle;

const std::string kOwner = "port:game-settings";
Handle sheet_handle(const std::string& text);  // a page's sheet picture (set_sheet)
const uint32_t kIngameUi = GD(0x101F8344);       // sIngameUi (hdIngameUi_c*)
const uint32_t kStateMenu = GD(0x1049ECB0);      // l_ingameMenuState (::StateID_Menu)
const uint32_t kPlay = GD(0x1046F0B0);           // g_dComIfG_gameInfo.play
constexpr uint32_t kUiCurrentState = 0x18, kUiPendingState = 0x14, kUiItemScreen = 0x17C, kMenuWindowMode = 0x54;
constexpr uint32_t kPlayEventRun = 0x5292;
const uint32_t kCursorMgr = GD(0x101F5A44);      // sCursorMgrInstance (hdCursorMgr_c*)

bool trace() {
    static const bool on = getenv("WWHD_GAME_SETTINGS_TRACE") != nullptr;
    return on;
}
bool object(uint32_t p, uint32_t size) { return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size; }

struct Menu {
    uint32_t ui = 0, descriptor = 0, items = 0, window = ~0u;
    uint32_t selection_mode = ~0u, selected = ~0u;  // hdMenuMain_c::mSelectionMode (+0x50), mSelectedChild (+0x58)
    float offset = 0;                                // hdMenuMain_c::mWindowOffset (+0x5C) (research trace)
    // The game enables menu input once its transitions have finished.
    bool still = false;
    bool cursor = false;
    uint32_t receiver = 0, cursor_pane = 0;
    bool right_target = false, left_target = false, back_target = false;
    bool left_neighbour = true; // fail closed unless the game's focus links are readable
    bool items_page = false;  // the + menu stands still with no bag window
};
Menu read_menu() {
    Menu m;
    m.ui = ld32(kIngameUi);
    if (!object(m.ui, 0x24C)) return m;
    const uint32_t current = ld32(m.ui + kUiCurrentState);
    m.descriptor = object(current, 12) ? ld32(current + 8) : ld32(m.ui + kUiPendingState);
    m.items = ld32(m.ui + kUiItemScreen);
    m.window = object(m.items, 0x58) ? ld32(m.items + kMenuWindowMode) : ~0u;
    if (object(m.items, 0x64))
        m.selection_mode = ld32(m.items + 0x50), m.selected = ld32(m.items + 0x58), m.offset = u32_as_f32(ld32(m.items + 0x5C));
    m.items_page = m.descriptor == kStateMenu && m.window >= 3 && m.window != ~0u;
    if (m.items_page) {
        const uint32_t root = game_look::layout_root(m.items);
        // A pane's visible flag alone is insufficient: the game parks unused arrows offscreen
        // without clearing it. The event manager's registered pane targets are the logical
        // visible controls. Inspect their pane ancestry, never slot numbers or coordinates.
        const uint32_t events = ld32(GD(0x101F4FF0));
        if (object(events, 0xE4)) {
            m.receiver = ld32(events + 0x70);
            for (uint32_t r = ld32(events + 4), guard = 0;
                 r && r != events && guard < 256 && object(r, 0x60); r = ld32(r + 4), ++guard) {
                if (ld32(r + 0xC) != events) continue;
                bool visible = true;
                std::string kind;
                for (uint32_t pane = ld32(r + 0x18), depth = 0;
                     pane && game_look::guest_pointer(pane) && depth < 24; pane = ld32(pane + 0xC), ++depth) {
                    visible &= game_look::visible(pane) && ld8(pane + 0x45) != 0;
                    const std::string name = game_look::pane_name(pane);
                    if (name.starts_with("L_MenuArrowR_")) kind = "right";
                    if (name.starts_with("L_MenuArrowL_")) kind = "left";
                    if (name.starts_with("L_BtnBack_")) kind = "back";
                }
                if (visible) {
                    m.right_target |= kind == "right";
                    m.left_target |= kind == "left";
                    m.back_target |= kind == "back";
                }
            }
            if (object(m.receiver, 0x60)) {
                m.left_neighbour = ld32(m.receiver + 0x38 + 6 * 4) != 0;
                m.cursor_pane = game_look::find_pane(root, "L_MenuArrowR_00");
            } else m.receiver = 0;
        }
        m.still = object(m.items, 0x97) && ld8(m.items + 0x95) && !ld8(m.items + 0x96);
        const uint32_t mgr = ld32(kCursorMgr);  // sCursorMgrInstance (hd_cursor_mgr.cpp)
        const uint32_t active = object(mgr, 0x20) ? ld32(mgr + 0x18) : ~0u;
        const uint32_t cursor = active < 2 ? ld32(mgr + 0x10 + 4 * active) : 0, croot = game_look::layout_root(cursor);
        if (croot) m.cursor = true;

    }
    return m;
}

// ---- the look: the game's fonts and menu art (game_look.h, shared with the story notebook) ----
game_look::Look g_look(kOwner, "game-settings");
uint32_t g_art_menu = 0;  // the item screen the art was read from (the game builds a new one on a stage change)
float game_measure(const std::string& text, float size) { return g_look.measure(text, size); }

// The game's page arrow (the item screen's L_MenuArrowR_00: BtnMenuArrow_00, its shadow and its body, two
// BC5 pictures the layout tints yellow), composed once and mirrored for ours; where the game shows its own.
struct Arrow {
    std::shared_ptr<guestmods::hud::Image> right, left;  // composed at the pictures' size
    Handle right_handle = 0, left_handle = 0;
    uint64_t generation = 0;
    float x = 1280 - 50, y = 360, w = 80, h = 160;  // the game's right arrow in page units (centre, size)
} g_arrow;
constexpr uint32_t kArrowYellowDark = 0xE2C40CFF, kArrowYellowLight = 0xFFF7A0FF, kArrowShadow = 0x2B4A1EFF;

std::shared_ptr<guestmods::hud::Image> compose_arrow(const game_glyph::Bitmap& shadow, const game_glyph::Bitmap& body,
                                                     const game_glyph::Bitmap& gloss, bool mirror) {
    const int w = body.width, h = body.height, sw = shadow.width, sh = shadow.height;
    const int W = std::max(w, sw), H = std::max(h, sh);
    style::Canvas cv(W, H);
    auto layer = [&](const game_glyph::Bitmap& b, auto&& colour) {
        if (b.width <= 0) return;
        const int ox = (W - b.width) / 2, oy = (H - b.height) / 2;
        for (int y = 0; y < b.height; y++)
            for (int x = 0; x < b.width; x++) {
                const uint8_t* s = &b.rgba[(size_t(y) * b.width + x) * 4];
                float c[4];
                colour(s, c);
                cv.over(mirror ? W - 1 - (ox + x) : ox + x, oy + y, c);
            }
    };
    using style::ch;
    layer(shadow, [](const uint8_t* s, float c[4]) {
        for (int i = 0; i < 3; i++) c[i] = ch(kArrowShadow, i);
        c[3] = s[3] * 0.55f;
    });
    layer(body, [](const uint8_t* s, float c[4]) {
        const float l = s[0] / 255.0f;
        for (int i = 0; i < 3; i++) c[i] = ch(kArrowYellowDark, i) * (1 - l) + ch(kArrowYellowLight, i) * l;
        c[3] = s[3];
    });
    layer(gloss, [](const uint8_t* s, float c[4]) {
        c[0] = c[1] = c[2] = 255.0f;
        c[3] = s[3] * (s[0] / 255.0f) * 0.5f;
    });
    return cv.image();
}

void load_art(uint32_t menu) {
    const auto& ui = story_notebook::game_ui();
    g_look.ui = &ui;
    g_look.trace = trace;
    if (!ui.art || !menu || menu == g_art_menu) return;
    g_art_menu = menu;
    std::string why;
    g_look.art_ok = ui.art(menu, g_look.art, why);
    if (g_look.art_ok) style::trim_plates(g_look.art);
    g_look.forget_boxes();
    LOG("[game-settings] menu art %s%s%s", g_look.art_ok ? "read" : "not read (plain boxes)", why.empty() ? "" : ": ", why.c_str());
    // the page arrow's pictures
    const uint32_t arrow = game_look::find_pane(game_look::layout_root(menu), "L_MenuArrowR_00");
    const uint32_t body = game_look::find_pane(arrow, "P_Btn_00"), shadow = game_look::find_pane(arrow, "P_BtnShadow_00");
    game_glyph::Bitmap b, s, g;
    if (body && game_look::texture(body, {0xF4, 0x28}, b)) {
        game_look::texture(shadow, {0xF4, 0x28}, s);
        game_look::texture(body, {0xF4, 0x44}, g);
        g_arrow.right = compose_arrow(s, b, g, false), g_arrow.left = compose_arrow(s, b, g, true);
        g_arrow.right_handle = g_arrow.left_handle = 0;
        LOG("[game-settings] the page arrows use the game's arrow picture (%dx%d)", b.width, b.height);
    } else {
        g_arrow.right = g_arrow.left = nullptr;
        LOG("[game-settings] the game's page arrow is not readable: drawn arrows");
    }
}

// where the game's right page arrow is now (layout units: centre origin, y up -> page units)
void read_arrow_place(uint32_t menu) {
    const uint32_t root = game_look::layout_root(menu), arrow = game_look::find_pane(root, "L_MenuArrowR_00");
    if (!arrow) return;
    const float x = u32_as_f32(ld32(arrow + 0x1C)), y = u32_as_f32(ld32(arrow + 0x20));
    const float w = u32_as_f32(ld32(arrow + 0x3C)), h = u32_as_f32(ld32(arrow + 0x40));
    if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1280 || std::fabs(y) > 720 || !(w > 8 && w < 400) || !(h > 8 && h < 400)) return;
    g_arrow.x = 640 + x, g_arrow.y = 360 - y, g_arrow.w = w, g_arrow.h = h;
}

// The game's own pieces the page covers (its right page arrow, the GamePad's A / B guide at the top
// right), hidden while the page is up and given back as they were when it goes.
struct Hidden { uint32_t pane = 0; bool was = false; std::string name; };
std::vector<Hidden> g_hidden;
constexpr uint32_t kUiCommandGuides[] = {0x188, 0x18C};  // sIngameUi: the command guides (A / B prompts; GamePad, TV)
void hide_game_pieces(const Menu& m, bool hide) {
    if (!hide) {
        for (const Hidden& h : g_hidden)  // (only a pane that is still there: a stage change frees the screens)
            if (game_look::pane_name(h.pane) == h.name) game_look::set_visible(h.pane, h.was);
        if (!g_hidden.empty() && trace()) LOG("[game-settings] %zu game panes shown again", g_hidden.size());
        g_hidden.clear();
        return;
    }
    if (g_hidden.empty()) {
        std::vector<uint32_t> panes = {game_look::find_pane(game_look::layout_root(m.items), "L_MenuArrowR_00")};
        for (uint32_t o : kUiCommandGuides) panes.push_back(game_look::layout_root(object(m.ui, 0x24C) ? ld32(m.ui + o) : 0));
        for (uint32_t p : panes)
            if (p) g_hidden.push_back({p, game_look::visible(p), game_look::pane_name(p)});
        if (trace()) LOG("[game-settings] %zu game panes hidden under the page", g_hidden.size());
    }
    for (const Hidden& h : g_hidden)  // (every read: the game may show them again)
        if (game_look::pane_name(h.pane) == h.name) game_look::set_visible(h.pane, false);
}

// "gs:arrow-left" / "gs:arrow-right" pictures: the game's arrow, ours mirrored (page units)
void append_arrows(Handle list, const std::vector<Command>& arrows, uint32_t screen) {
    auto& store = guestmods::hud::store();
    const uint64_t generation = store.state_generation();
    if (g_arrow.generation != generation) g_arrow.right_handle = g_arrow.left_handle = 0, g_arrow.generation = generation;
    auto handle = [&](bool left) {
        Handle& h = left ? g_arrow.left_handle : g_arrow.right_handle;
        const auto& img = left ? g_arrow.left : g_arrow.right;
        if (!h && img) h = store.create_image(kOwner, img->width, img->height, img->width * 4, img->rgba.data(), img->rgba.size());
        return h;
    };
    for (Command c : arrows) {
        const bool left = c.text == "gs:arrow-left";
        c.text.clear();
        std::vector<Command> one{c};
        story_notebook::fit_to_screen(one, screen);
        if (const Handle h = handle(left)) store.picture(kOwner, list, h, one[0]);
    }
}

void publish(std::vector<Command> cmds, Cpu* c) {
    auto& store = guestmods::hud::store();
    if (cmds.empty()) {
        store.drop(kOwner);
        return;
    }
    if (!g_look.picture_handle) g_look.picture_handle = sheet_handle;
    g_look.load_glyphs(c, font::texts_of(cmds));
    const uint32_t screen = story_notebook::menu_screen(input::pro_controller());
    const Handle list = store.begin(kOwner, screen);
    if (!list) return;
    // the arrows: the game's picture when it was read (else the page's drawn arrow stays)
    std::vector<Command> arrows, rest;
    for (Command& k : cmds) {
        if (k.kind == Command::Picture && k.text.rfind("gs:arrow", 0) == 0) {
            if (g_arrow.right) arrows.push_back(k);
            continue;
        }
        if (g_arrow.right && k.text.rfind("gs:drawn", 0) == 0) continue;  // the drawn arrow's parts
        rest.push_back(std::move(k));
    }
    for (Command& k : rest)
        if (k.text.rfind("gs:drawn", 0) == 0) k.text.clear();
    std::vector<Handle> images;
    // in drawing order: the arrows belong to the page's layers (the closed page: only the entry arrow)
    rest = g_look.render(std::move(rest), images, screen);
    story_notebook::fit_to_screen(rest, screen);
    g_look.append(list, rest, images);
    append_arrows(list, arrows, screen);
    store.commit(kOwner, list);
}

const char* sound_label(Sound s) {
    switch (s) {
    case Sound::Cursor: return "SE_MENU_CURSOR";
    case Sound::Change: return "SE_MENU_OPTION_SWITCH";
    case Sound::Decide: return "SE_MENU_DECIDE_S";
    case Sound::Cancel: case Sound::Blocked: return "SE_MENU_CANCEL";
    case Sound::Tab: return "SE_MENU_TAB_SWITCH";
    case Sound::Open: return "SE_MENU_OPTION_IN";
    case Sound::Close: return "SE_MENU_OPTION_OUT";
    case Sound::None: break;
    }
    return nullptr;
}
void play(Cpu* c, Sound s) {
    const char* label = sound_label(s);
    const auto& ui = story_notebook::game_ui();
    if (!label || !c || !ui.sound || ld8(kPlay + kPlayEventRun)) return;
    if (trace()) LOG("[game-settings] sound %s", label);
    ui.sound(c, label);
}

EntryFocus g_entry;
// A detached pane is the extra cursor stop. Only the focused receiver's cursor-pane
// override is borrowed; its navigation links and the game's layouts remain intact.
// The pane and restoration receipt live in state-serialized runtime guest memory, so
// loading a state taken on the arrow can restore the original target even in a new process.
void entry_cursor(const Menu& m, bool focus) {
    constexpr uint32_t magic = 0x47534541, bytes = 0xB0;
    static uint32_t pane = 0, borrowed = 0, menu = 0, old_cursor = 0;
    static uint64_t generation = 0;
    const uint64_t now = guestmods::hud::store().state_generation();
    auto ours = [&](uint32_t p) {
        return p >= mem::kRuntimeStart && p <= mem::runtime_top() - bytes &&
               p < mem::kHostStart - bytes && ld32(p + 0xA0) == magic;
    };
    if (generation != now) borrowed = menu = 0, generation = now;
    if (pane && !ours(pane)) pane = 0;
    // A state may contain an override from this or a previous host session.
    if (m.receiver && !borrowed) {
        const uint32_t current = ld32(m.receiver + 0x5C);
        if (ours(current) && ld32(current + 0xA4) == m.items && ld32(current + 0xA8) == m.receiver)
            st32(m.receiver + 0x5C, ld32(current + 0xAC));
    }
    if (borrowed && (!focus || menu != m.items || borrowed != m.receiver)) {
        if (menu == m.items && object(borrowed, 0x60) && ld32(borrowed + 0x5C) == pane)
            st32(borrowed + 0x5C, old_cursor);
        borrowed = menu = 0;
    }
    if (!focus || !m.receiver || !m.cursor_pane) return;
    if (!pane) pane = mem::runtime_alloc(bytes, 16);
    if (!pane) return;
    if (!borrowed) {
        borrowed = m.receiver; menu = m.items; old_cursor = ld32(borrowed + 0x5C);
        std::memcpy(mem::ptr(pane), mem::ptr(m.cursor_pane), 0xA0);
        st32(pane + 0xC, 0); // detached: never traversed or drawn as a game layout
        st32(pane + 0x14, pane + 0x14); st32(pane + 0x18, pane + 0x14);
        std::memset(mem::ptr(pane + 0x48), 0, 0x30);
        st32(pane + 0x48, f32_as_u32(1)); st32(pane + 0x5C, f32_as_u32(1)); st32(pane + 0x70, f32_as_u32(1));
        st8(pane + 0x47, 0); // centre anchor
        st32(pane + 0xA0, magic); st32(pane + 0xA4, menu);
        st32(pane + 0xA8, borrowed); st32(pane + 0xAC, old_cursor);
    }
    st32(pane + 0x54, f32_as_u32(640.0f - g_arrow.x));
    st32(pane + 0x64, f32_as_u32(360.0f - g_arrow.y));
    st32(pane + 0x3C, f32_as_u32(g_arrow.w)); st32(pane + 0x40, f32_as_u32(g_arrow.h));
    st32(borrowed + 0x5C, pane);
}

Page g_page;
std::atomic<bool> g_has_controller{false};  // the entry stop, page or slide out has the controller
std::atomic<uint32_t> g_swallow{0};
std::atomic<bool> g_swallow_x{false}; // a stick used to leave the entry stop must return to centre

float frame_seconds() {
    using clock = std::chrono::steady_clock;
    static clock::time_point last{};
    const clock::time_point now = clock::now();
    const float dt = last == clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - last).count();
    last = now;
    return std::min(dt, 0.1f);
}

uint32_t language() { return console_language(); }
std::string tr(const std::string& english) { return game_settings_text::tr(english, language()); }

std::vector<Row> rows_of(const std::string& tab) {
    std::vector<Row> rows;
    for (const game_settings::Item* it : game_settings::items(tab)) {
        Row r;
        const std::string value = game_settings::value_text(*it);
        const bool port = it->owner == "port";  // (mods' texts are shown as their manifests write them)
        r.id = it->id, r.label = port ? tr(it->label) : it->label, r.help = port ? tr(it->help) : it->help;
        if (it->next_start) r.help += " " + tr("(applies at the next start)");
        r.number = it->kind == game_settings::Kind::Number, r.toggle = it->kind == game_settings::Kind::Toggle;
        r.choice = it->kind == game_settings::Kind::Choice, r.action = it->kind == game_settings::Kind::Action;
        r.value = r.number || r.action || (!port && r.choice) ? value : tr(value);
        r.on = r.toggle && value == "On";
        r.enabled = !it->enabled || it->enabled();
        rows.push_back(std::move(r));
    }
    return rows;
}

// Test aid WWHD_TEST_GAME_SETTINGS_SHOTS=1: a screenshot (screenshot.h; the GamePad picture too with
// WWHD_SCREENSHOT_GAMEPAD=1) a few reads after the entry arrow first shows, the page opens and each change
void shot_after(int reads) {
    static const bool on = getenv("WWHD_TEST_GAME_SETTINGS_SHOTS") && *getenv("WWHD_TEST_GAME_SETTINGS_SHOTS");
    static int countdown = -1;
    if (!on) return;
    if (reads > 0) { countdown = reads; return; }
    if (countdown > 0 && --countdown == 0) {
        screenshot::request();
        LOG("[game-settings] test screenshot requested");
    }
}

// Test aid WWHD_TEST_TV_POINTER=4000-4006:0.05:0.5,... holds the TV pointer down at (x, y) (0..1 of a 16:9
// TV picture) during those (scripted) TV frames, as a click there would while the page wants it
bool test_tv_pointer(tv_pointer::State& s) {
    struct Hold { uint64_t from, to; float x, y; };
    static const std::vector<Hold> holds = [] {
        std::vector<Hold> v;
        const char* e = getenv("WWHD_TEST_TV_POINTER");
        unsigned long long a, b;
        float x, y;
        int n;
        while (e && sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
            v.push_back({a, b, x, y});
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    if (holds.empty()) return false;
    const uint64_t f = fixed_seed::script_frame(render::frame_count());
    s = {};
    for (const Hold& h : holds)
        if (f >= h.from && f < h.to) s.down = true, s.x = h.x, s.y = h.y;
    return true;
}

// the pages opened from rows (register_detail / open_detail) and the one shown now (game thread)
std::mutex g_detail_mu;
std::map<std::string, std::function<std::unique_ptr<Detail>()>> g_detail_makers;
std::string g_detail_wanted;
std::unique_ptr<Detail> g_detail;
int g_test_detail = -1;  // reads until WWHD_TEST_SETTINGS_DETAIL's page opens (test aid)

// the sheets (set_sheet): their images, and the handles made of them in the store (game thread)
struct Sheet {
    std::shared_ptr<const guestmods::hud::Image> image, made;
    Handle handle = 0;
    uint64_t generation = 0;
};
std::map<std::string, Sheet> g_sheets;  // (g_detail_mu)
Handle sheet_handle(const std::string& text) {
    if (text.rfind("gs:sheet:", 0) != 0) return 0;
    auto& store = guestmods::hud::store();
    std::lock_guard lock(g_detail_mu);
    auto it = g_sheets.find(text.substr(9));
    if (it == g_sheets.end() || !it->second.image) return 0;
    Sheet& sh = it->second;
    const uint64_t generation = store.state_generation();
    if (sh.made != sh.image || sh.generation != generation || !sh.handle) {
        if (sh.handle && sh.generation == generation) store.release(kOwner, sh.handle);
        const auto& img = *sh.image;
        sh.handle = store.create_image(kOwner, img.width, img.height, img.width * 4, img.rgba.data(), img.rgba.size());
        sh.made = sh.image, sh.generation = generation;
    }
    return sh.handle;
}

}  // namespace

void set_sheet(const std::string& name, std::shared_ptr<const guestmods::hud::Image> image) {
    std::lock_guard lock(g_detail_mu);
    g_sheets[name].image = std::move(image);
}

void register_detail(const std::string& id, std::function<std::unique_ptr<Detail>()> make) {
    std::lock_guard lock(g_detail_mu);
    g_detail_makers[id] = std::move(make);
}
void open_detail(const std::string& id) {
    std::lock_guard lock(g_detail_mu);
    g_detail_wanted = id;
}

bool page_open() { return g_has_controller.load(std::memory_order_relaxed); }

void mute(input::PadState& p) {
    if (g_has_controller.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
    if (g_swallow_x.load(std::memory_order_relaxed)) p.lx = 0;
}

void pad(input::PadState& p, Cpu* c) {
    static bool disabled = getenv("WWHD_GAME_SETTINGS") && *getenv("WWHD_GAME_SETTINGS") == '0';
    if (disabled) return;
    if (g_swallow_x.load(std::memory_order_relaxed)) {
        if (std::fabs(p.lx) <= 0.5f) g_swallow_x.store(false, std::memory_order_relaxed);
        else p.lx = 0;
    }
    game_settings::register_builtin();
    g_page.measure = g_look.font_ready() ? game_measure : nullptr;
    const float dt = frame_seconds();
    shot_after(0);
    const Menu m = read_menu();
    if (trace()) {
        static uint32_t last_descriptor = ~0u, last_window = ~1u;
        if (m.descriptor != last_descriptor || m.window != last_window)
            LOG("[game-settings] in-game UI %08X: state %08X%s, bag window %d -> %s", m.ui, m.descriptor, m.descriptor == kStateMenu ? " (Menu)" : "",
                int(m.window), m.items_page ? "the page is offered" : "not offered");
        last_descriptor = m.descriptor, last_window = m.window;
        static uint32_t last_selected = ~1u, last_mode = ~1u;
        static float last_offset = -1e9f;
        if (m.items_page && (m.selected != last_selected || m.selection_mode != last_mode || m.offset != last_offset))
            LOG("[game-settings] item screen: selection mode %u, selected child %d, page offset %.1f", m.selection_mode, int(m.selected), m.offset);
        last_selected = m.selected, last_mode = m.selection_mode, last_offset = m.offset;
        if (m.items_page && getenv("WWHD_GAME_SETTINGS_CURSOR_TRACE")) {  // research: the game's cursor and the pages
            const uint32_t mgr = ld32(GD(0x101F5A44));
            const uint32_t active = object(mgr, 0x20) ? ld32(mgr + 0x18) : 0, cur = object(mgr, 0x20) && active < 2 ? ld32(mgr + 0x10 + 4 * active) : 0;
            const uint32_t wp = object(cur, 0x54) ? ld32(cur + 0x50) : 0, croot = game_look::layout_root(cur);
            auto f = [](uint32_t a) { return game_look::guest_pointer(a) ? u32_as_f32(ld32(a)) : 0.0f; };
            const uint32_t root = game_look::layout_root(m.items);
            const uint32_t al = game_look::find_pane(root, "L_MenuArrowL_00"), ar = game_look::find_pane(root, "L_MenuArrowR_00");
            const uint32_t n0 = game_look::find_pane(root, "N_All_00"), n1 = game_look::find_pane(root, "N_All_01");
            char line[400];
            snprintf(line, sizeof line, "cursor %d root (%.0f,%.0f) win (%.0f,%.0f %.0fx%.0f) | arrowL f%02X a%u/%u (%.0f) arrowR f%02X a%u/%u (%.0f) | N_All_00 %.0f N_All_01 %.0f",
                     int(active), f(croot + 0x1C), f(croot + 0x20), f(wp + 0x1C), f(wp + 0x20), f(wp + 0x3C), f(wp + 0x40), al ? ld8(al + 0x44) : 0,
                     al ? ld8(al + 0x45) : 0, al ? ld8(al + 0x46) : 0, f(al + 0x1C), ar ? ld8(ar + 0x44) : 0, ar ? ld8(ar + 0x45) : 0, ar ? ld8(ar + 0x46) : 0,
                     f(ar + 0x1C), f(n0 + 0x1C), f(n1 + 0x1C));
            static std::string last;
            if (last != line) LOG("[game-settings] %s", line), last = line;
        }
    }
    if (trace() && m.items_page) {
        std::string state = "targets R" + std::to_string(m.right_target) + " L" + std::to_string(m.left_target) +
            " Back" + std::to_string(m.back_target) + " still" + std::to_string(m.still) + " focus " + std::to_string(m.receiver);
        if (m.receiver) {
            std::string label;
            for (uint32_t pane = ld32(m.receiver + 0x18), depth = 0;
                 pane && game_look::guest_pointer(pane) && depth < 24; pane = ld32(pane + 0xC), ++depth) {
                const std::string name = game_look::pane_name(pane);
                if (!label.empty()) label += "/";
                label += name;
                if (name.starts_with("L_")) break;
            }
            state += " " + label + " left=" + std::to_string(ld32(m.receiver + 0x50));
        }
        static std::string last;
        if (last != state) LOG("[game-settings] %s", state.c_str()), last = state;
    }
    const bool other_page = story_notebook::page_open();
    const std::vector<std::string> tabs = game_settings::tabs();
    std::vector<std::string> labels;
    for (const std::string& t : tabs) labels.push_back(tr(game_settings::tab_label(t)));
    g_page.texts = {tr("Settings"), tr("Change"), tr("Back"), tr("L / R: tabs"), tr("More in Settings (Esc / hold Select)")};
    Input in;
    in.buttons = p.buttons, in.lx = p.lx, in.ly = p.ly, in.touch = p.touch, in.dt = dt;
    in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;  // the GamePad picture as the page's 1280 x 720
    // With the Pro Controller the menu (and the page) are on the TV: the mouse on the TV picture is the
    // page's touch (tv_pointer.h), wanted while the page or its entry arrow is there
    const bool enabled = input_map::settings_pause_page();
    const bool offered = entry_exists(enabled, m.right_target, m.left_target, m.back_target);
    const bool pro = input::pro_controller();
    {
        const bool on_tv = pro && ((offered && m.items_page && m.still && !other_page && !tabs.empty()) || g_page.shown());
        tv_pointer::set_wanted(on_tv);
        if (pro) {
            tv_pointer::State s = tv_pointer::read();
            if (!on_tv) s.down = false;
            if (!test_tv_pointer(s) && !on_tv) s.down = false;
            in.touch = s.down;
            tv_pointer::to_page(s, in.x, in.y);
        }
    }
    if (!g_page.open()) {
        if (!enabled) g_page.cancel_entry();
        g_page.tick(dt);
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        }
        const bool eligible = offered && m.items_page && m.still && !other_page && !tabs.empty();
        if (eligible) {
            load_art(m.items);
            read_arrow_place(m.items);
            g_page.game_arrow = {g_arrow.x, g_arrow.y, g_arrow.w, g_arrow.h};
            if (story_notebook::game_ui().prompts) g_look.prompts = story_notebook::game_ui().prompts();
            if (static bool dumped = false; !dumped && getenv("WWHD_GAME_SETTINGS_PANES") && story_notebook::game_ui().dump) {
                dumped = true;  // research aid: the item screen's panes and textures, in the log
                story_notebook::game_ui().dump(c, m.items, "");
                for (uint32_t o = 0x100; o < 0x248; o += 4) {  // the in-game UI's other screens with a layout
                    const uint32_t s = ld32(m.ui + o), lay = object(s, 0x48) ? ld32(s + 0x44) : 0, lyt = object(lay, 8) ? ld32(lay + 4) : 0;
                    const uint32_t root = object(lyt, 0x10) ? ld32(lyt + 0xC) : 0;
                    if (!object(root, 0x100) || s == m.items) continue;
                    LOG("[notebook-panes] ---- sIngameUi+%X: screen %08X", o, s);
                    story_notebook::game_ui().dump(c, s, "");
                }
            }
        }
        const bool left = (in.buttons & kPadLeft) || in.lx < -0.5f;
        const uint32_t source = m.receiver ? ld32(m.receiver + 0x18) : 0;
        EntryPosition position{m.items, m.receiver, m.selected, m.selection_mode, m.offset};
        if (game_look::guest_pointer(source)) {
            position.x = u32_as_f32(ld32(source + 0x1C));
            position.y = u32_as_f32(ld32(source + 0x20));
        }
        const bool focused_before = g_entry.focused();
        if (g_entry.update(eligible && m.cursor && m.cursor_pane && !in.touch, left,
                           m.left_neighbour, position, render::frame_count())) {
            LOG("[game-settings] entry arrow highlighted after dead Left");
            play(c, Sound::Cursor);
            shot_after(20);
        }
        if (g_entry.focused() && focused_before &&
            ((in.buttons & (kPadRight | kPadB | kPadUp | kPadDown | kPadPlus | kPadL | kPadR)) || in.lx > 0.5f || std::fabs(in.ly) > 0.5f)) {
            const bool back = (in.buttons & (kPadRight | kPadB)) || in.lx > 0.5f;
            g_entry.clear();
            if (back) {
                g_swallow.store(in.buttons, std::memory_order_relaxed);
                if (in.lx > 0.5f) g_swallow_x.store(true, std::memory_order_relaxed);
                p.buttons = 0; p.lx = p.ly = 0;
                play(c, Sound::Cursor);
                LOG("[game-settings] entry arrow returned to the game's focus");
            }
        }
        entry_cursor(m, g_entry.focused());
        // ZL is deliberately not an entry: the shared menu event dispatcher does not
        // route its normalized bit 6, but no shortcut needs to borrow GameCube L/ZL.
        in.arrow_focused = focused_before && g_entry.focused();
        Input watched = in;
        if (!eligible) watched.touch = false, watched.arrow_focused = false;
        const bool on_arrow_touch = eligible && !pro && p.touch && g_page.on_arrow(in.x, in.y, true);
        // test aid WWHD_TEST_SETTINGS_PAGE=<tab> (e.g. game): the page opens by itself on that tab, once, as soon as
        // it is offered (scripted runs: the item menu opened by WWHD_PRESS)
        static const char* test_tab = getenv("WWHD_TEST_SETTINGS_PAGE");
        static bool test_opened = false;
        const bool test_open = test_tab && !test_opened && eligible && m.cursor;
        if (test_open) {
            test_opened = true;
            g_test_detail = 30;
            for (size_t i = 0; i < tabs.size(); i++)
                if (tabs[i] == test_tab) g_page.set_tab(int(i));
        }
        if ((g_page.entry(watched) || test_open) && eligible) {
            const int tab = g_page.tab();
            g_page.show(int(tabs.size()));
            if (test_open) g_page.set_tab(tab);
            g_has_controller.store(true, std::memory_order_relaxed);
            LOG("[game-settings] page opened (%s)", in.arrow_focused ? "focused entry arrow" : "the arrow");
            g_entry.clear();
            entry_cursor(m, false);
            play(c, Sound::Open);
            shot_after(20);
            g_swallow.store(0, std::memory_order_relaxed);
            hide_game_pieces(m, true);
            p = input::PadState{};
            return;
        }
        if (g_page.shown() && (eligible || g_page.swiping() || m.items_page)) {  // sliding out, or following a swipe
            if (m.items_page) hide_game_pieces(m, true);
            const std::string tab = tabs.empty() ? "" : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))];
            publish(g_page.draw(rows_of(tab), labels), c);
            p = input::PadState{};
            return;
        }
        g_has_controller.store(g_entry.focused(), std::memory_order_relaxed);
        hide_game_pieces(m, false);
        if (!eligible) {
            publish({}, c);
            return;
        }
        if (g_entry.focused()) p = input::PadState{};
        if (on_arrow_touch) p.touch = false;  // a touch on our arrow is ours (a tap or the start of a swipe)
        static bool shown_once = false;
        if (!shown_once) shown_once = true, shot_after(20);
        publish(g_page.draw_entry(), c);
        return;
    }
    // open: the page has the controller
    if (!enabled) g_page.hide();
    g_has_controller.store(true, std::memory_order_relaxed);
    if (!m.items_page && m.descriptor != kStateMenu) {  // the menu closed under the page (a state load, a cutscene)
        g_page.hide();
        g_detail.reset();
        g_hidden.clear();  // (its screens may be gone: nothing is written back)
        LOG("[game-settings] page closed: the menu is gone");
    } else {
        hide_game_pieces(m, true);
    }
    if (g_test_detail > 0 && --g_test_detail == 0)
        if (const char* id = getenv("WWHD_TEST_SETTINGS_DETAIL")) LOG("[game-settings] test: opening %s", id), open_detail(id);
    {  // a row's own page asked for (its set() ran on the main thread)
        std::function<std::unique_ptr<Detail>()> make;
        {
            std::lock_guard lock(g_detail_mu);
            if (!g_detail_wanted.empty()) {
                if (auto it = g_detail_makers.find(g_detail_wanted); it != g_detail_makers.end()) make = it->second;
                g_detail_wanted.clear();
            }
        }
        if (make && g_page.open()) {
            g_detail = make();
            if (g_detail) {
                play(c, Sound::Open);
                LOG("[game-settings] a row's page opened");
                shot_after(10);
            }
        }
    }
    if (g_detail && g_page.open()) {  // that page has the controller until B closes it
        const Action act = g_detail->input(in);
        g_detail->tick(dt);
        g_page.tick(dt);
        play(c, act.sound);
        if (act.closed) {
            g_detail.reset();
            g_page.hold_buttons();  // (the B that closed it doesn't close the settings page too)
            LOG("[game-settings] a row's page closed");
            publish(g_page.draw(rows_of(tabs.empty() ? "" : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))]), labels), c);
        } else {
            if (!act.id.empty()) shot_after(10);
            publish(g_detail->draw(g_page.measure), c);
        }
        p = input::PadState{};
        return;
    }
    auto tab_name = [&] { return tabs.empty() ? std::string() : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))]; };
    std::string tab = tab_name();
    std::vector<Row> rows = rows_of(tab);
    const Action act = g_page.input(in, rows, int(tabs.size()));
    g_page.tick(dt);
    if (tab_name() != tab) rows = rows_of(tab = tab_name());  // L / R: the new tab's rows at once (not the old tab's for a read)
    if (!act.id.empty())
        if (const game_settings::Item* it = game_settings::find(act.id)) {
            const float v = game_settings::apply(*it, act.direction);
            LOG("[game-settings] %s -> %g", act.id.c_str(), double(v));
            rows = rows_of(tab);
            shot_after(10);
        }
    play(c, act.sound);
    if (act.closed) {
        g_detail.reset();
        g_swallow.store(p.buttons, std::memory_order_relaxed);  // the B that closed it doesn't reach the game
        LOG("[game-settings] page closed");
    }
    publish(g_page.draw(rows, labels), c);
    p = input::PadState{};
}

}  // namespace game_settings_page
