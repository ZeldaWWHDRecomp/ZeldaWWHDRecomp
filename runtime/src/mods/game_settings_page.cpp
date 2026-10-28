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
// Prototype limits (p9 report): the entry is ZL or the arrow (not yet "left past the first column",
// which needs the item cursor), the page does not know the items page from the second page, and its
// look code is a copy of the notebook's (to be shared).
#include "game_settings_page.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

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
const uint32_t kIngameUi = GD(0x101F8344);       // sIngameUi (hdIngameUi_c*)
const uint32_t kStateMenu = GD(0x1049ECB0);      // l_ingameMenuState (::StateID_Menu)
const uint32_t kPlay = GD(0x1046F0B0);           // g_dComIfG_gameInfo.play
constexpr uint32_t kUiCurrentState = 0x18, kUiPendingState = 0x14, kUiItemScreen = 0x17C, kMenuWindowMode = 0x54;
constexpr uint32_t kPlayEventRun = 0x5292;
const uint32_t kCursorMgr = GD(0x101F5A44);      // sCursorMgrInstance (hdCursorMgr_c*)
// the items page's left column: ITEMS (-462), Save and Options (-397), the bottom left place (about
// -380); the next column starts at about -154
constexpr float kLeftColumnX = -300;

bool trace() {
    static const bool on = getenv("WWHD_GAME_SETTINGS_TRACE") != nullptr;
    return on;
}
bool object(uint32_t p, uint32_t size) { return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size; }

struct Menu {
    uint32_t ui = 0, descriptor = 0, items = 0, window = ~0u;
    uint32_t selection_mode = ~0u, selected = ~0u;  // hdMenuMain_c::mSelectionMode (+0x50), mSelectedChild (+0x58)
    float offset = 0;                                // hdMenuMain_c::mWindowOffset (+0x5C) (research trace)
    // the items page, standing still: its content (N_All_00) at 0 and the game's right page arrow in its
    // place on the screen (the second page: -1280 and the arrow out at the right edge)
    bool still = false;
    float arrow_x = 0;    // the game's right arrow (L_MenuArrowR_00), layout units
    float cursor_x = 0;   // the game's menu cursor (hdCursorMgr_c's active cursor screen), layout units
    bool cursor = false;
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
        const uint32_t all = game_look::find_pane(root, "N_All_00"), arrow = game_look::find_pane(root, "L_MenuArrowR_00");
        if (all && arrow) {
            const float page_x = u32_as_f32(ld32(all + 0x1C));
            m.arrow_x = u32_as_f32(ld32(arrow + 0x1C));
            m.still = page_x == 0.0f && m.arrow_x > 0 && m.arrow_x < 640 - 40;  // (out: 686)
        }
        const uint32_t mgr = ld32(kCursorMgr);  // sCursorMgrInstance (hd_cursor_mgr.cpp)
        const uint32_t active = object(mgr, 0x20) ? ld32(mgr + 0x18) : ~0u;
        const uint32_t cursor = active < 2 ? ld32(mgr + 0x10 + 4 * active) : 0, croot = game_look::layout_root(cursor);
        if (croot) m.cursor = true, m.cursor_x = u32_as_f32(ld32(croot + 0x1C));
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

Page g_page;
std::atomic<bool> g_has_controller{false};  // the page (or its slide out) has the controller
std::atomic<uint32_t> g_swallow{0};

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
        r.value = it->kind == game_settings::Kind::Number || (!port && it->kind == game_settings::Kind::Choice) ? value : tr(value);
        r.number = it->kind == game_settings::Kind::Number, r.toggle = it->kind == game_settings::Kind::Toggle;
        r.choice = it->kind == game_settings::Kind::Choice;
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

}  // namespace

bool page_open() { return g_has_controller.load(std::memory_order_relaxed); }

void mute(input::PadState& p) {
    if (g_has_controller.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
}

void pad(input::PadState& p, Cpu* c) {
    static bool disabled = getenv("WWHD_GAME_SETTINGS") && *getenv("WWHD_GAME_SETTINGS") == '0';
    if (disabled) return;
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
    const bool pro = input::pro_controller();
    {
        const bool on_tv = pro && ((m.items_page && m.still && !other_page && !tabs.empty()) || g_page.shown());
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
        g_page.tick(dt);
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        }
        const bool eligible = m.items_page && m.still && !other_page && !tabs.empty();
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
        in.cursor_left = m.cursor && m.cursor_x < kLeftColumnX;
        Input watched = in;
        if (!eligible) watched.touch = false;  // (the buttons are followed: a ZL held into the menu does not open it)
        const bool on_arrow_touch = eligible && !pro && p.touch && g_page.on_arrow(in.x, in.y, true);
        if (g_page.entry(watched) && eligible) {
            g_page.show(int(tabs.size()));
            g_has_controller.store(true, std::memory_order_relaxed);
            LOG("[game-settings] page opened (%s)", (p.buttons & kPadZL) ? "ZL" : (in.buttons & kPadLeft) || in.lx < -0.5f ? "Left from the left column"
                                                                                                            : "the arrow");
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
        g_has_controller.store(false, std::memory_order_relaxed);
        hide_game_pieces(m, false);
        if (!eligible) {
            publish({}, c);
            return;
        }
        if (on_arrow_touch) p.touch = false;  // a touch on our arrow is ours (a tap or the start of a swipe)
        static bool shown_once = false;
        if (!shown_once) shown_once = true, shot_after(20);
        publish(g_page.draw_entry(), c);
        return;
    }
    // open: the page has the controller
    g_has_controller.store(true, std::memory_order_relaxed);
    if (!m.items_page && m.descriptor != kStateMenu) {  // the menu closed under the page (a state load, a cutscene)
        g_page.hide();
        g_hidden.clear();  // (its screens may be gone: nothing is written back)
        LOG("[game-settings] page closed: the menu is gone");
    } else {
        hide_game_pieces(m, true);
    }
    const std::string tab = tabs.empty() ? "" : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))];
    std::vector<Row> rows = rows_of(tab);
    const Action act = g_page.input(in, rows, int(tabs.size()));
    g_page.tick(dt);
    if (!act.id.empty())
        if (const game_settings::Item* it = game_settings::find(act.id)) {
            const float v = game_settings::apply(*it, act.direction);
            LOG("[game-settings] %s -> %g", act.id.c_str(), double(v));
            rows = rows_of(tab);
            shot_after(10);
        }
    play(c, act.sound);
    if (act.closed) {
        g_swallow.store(p.buttons, std::memory_order_relaxed);  // the B that closed it doesn't reach the game
        LOG("[game-settings] page closed");
    }
    publish(g_page.draw(rows, labels), c);
    p = input::PadState{};
}

}  // namespace game_settings_page
