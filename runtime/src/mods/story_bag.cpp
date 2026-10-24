// The Delivery Bag's extra pages for mod items (story_bag.h): input, the save's items and the layout.
#include "story_bag.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "guest_addr.h"
#include "guest_png.h"
#include "lyt_build.h"
#include "ppc.h"
#include "runtime.h"
#include "story_bag_page.h"
#include "story_items.h"
#include "story_lyt_screen.h"
#include "story_lyt_text.h"
#include "story_notebook.h"
#include "story_notebook_art.h"
#include "story_notebook_bag.h"
#include "story_notebook_game.h"

// the layout description (runtime/assets/story_bag/StoryBag_00.json, embedded by cmake/EmbedBytes.cmake)
extern const unsigned char wwhd_story_bag_layout[];
extern const unsigned long wwhd_story_bag_layout_size;

namespace story_bag {
namespace {

constexpr const char* kLayout = "StoryBag_00";
// the item menu (story_notebook.cpp): its children 0x14..0x1B are the open bag window's eight places,
// hdMenuItem_c::mSlot (+0xEC) the item number a place shows (0xFF: empty)
constexpr uint32_t kMenuChildren = 0x48, kFirstWindowChild = 0x14, kItemNumber = 0xEC, kEmpty = 0xFF;
constexpr uint32_t kNotebookItem = 0xA0, kLetterItem = 0xA1;
// which window child shows which save place: the menu's transition table for the Delivery Bag window
// (hdMenuMain_c::executeSaveChildren: sMenuMainWindowItems[mode] = {key, child index} x
// sMenuMainWindowCounts[mode]; keys 0x30..0x37 = the save's delivery places 0..7)
const uint32_t kWindowItems = GD(0x101F63BC), kWindowCounts = GD(0x100FA2C0);
constexpr uint32_t kDeliveryMode = 2, kFirstDeliveryKey = 0x30;
constexpr uint32_t kItemLayout = 0x44, kLayoutLyt = 0x4;  // hdParts_c's layout, hdPartsLayout_c's layout object
constexpr uint32_t kPaneParent = 0xC, kPaneTranslate = 0x1C, kPaneScale = 0x34, kPaneFlags = 0x44, kPaneGlobalAlpha = 0x46;
const uint32_t kExecuteSaveChildren = GC(0x026924EC);  // hdMenuMain_c::executeSaveChildren(): the open window's places read again
const uint32_t kPlay = GC(0x025200D4);                 // the play object
constexpr uint32_t kPlayEventRun = 0x5292;             // dComIfGp_event_runCheck()
constexpr uint32_t kIconSize = 128;
constexpr int kFloatSlot = 8;  // BagIcon8: the floating icon while a game place is chosen
constexpr float kMessageSeconds = 3.0f;

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
bool g_configured = false;
void (*g_after_assign)(Cpu*) = nullptr;  // story_item_buttons::refresh_strip (set_after_assign)
story_lyt_screen::Id g_id = -1;

// game thread
Page g_page;
Model g_model;
uint32_t g_menu = 0;               // the item menu while its Delivery Bag window runs (this frame), else 0
std::atomic<bool> g_open{false};   // the page has the controller
std::atomic<bool> g_covering{false};  // the page lies over the game's window (not while a game place is chosen)
std::atomic<bool> g_takes_lr{false};  // the game's Delivery Bag window with extra pages: L and R are the pages'
std::atomic<uint32_t> g_swallow{0};  // buttons held when the page closed: the game gets them once released
std::string g_message;             // a refusal or a result, shown for a while
float g_message_time = 0;
std::string g_last_text[64];       // the text panes' current texts (only changes are written)
uint16_t g_slot_key[kSlots + 1] = {};  // the item each icon slot texture holds (0: none written)
bool g_built_seen = false;
int g_turn = 0;                    // a page turn to animate in the next frame (-1 / +1)
bool g_open_anim = false;          // the page's opening animation to play in the next frame
std::map<uint16_t, story_notebook::art::Bitmap> g_icons;  // the items' icons at kIconSize (decoded once)

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_BAG_TRACE"); return e && *e == '1'; }();
    return on;
}

// ---- the save's items ----
Model read_model(uint32_t menu) {
    Model m;
    std::vector<unsigned> used;
    std::vector<std::pair<uint8_t, Entry>> placed;
    for (uint16_t key : story_items::owned()) {
        const std::optional<story_items::Item> it = story_items::find(key);
        if (!it) continue;
        const uint8_t place = story_items::place(key);
        if (!story_items::page_place(place)) {
            if (!place) m.unplaced++;
            continue;
        }
        const unsigned index = place - story_items::kFirstPagePlace;
        Entry e;
        e.key = key, e.count = story_items::count(key), e.usable = it->usable();
        placed.push_back({place, e});
        used.push_back(index / kSlots);
    }
    if (placed.empty() && !m.unplaced) return m;  // no pages
    m.pages.resize(pages_shown(used));
    for (const auto& [place, e] : placed) {
        const unsigned index = place - story_items::kFirstPagePlace;
        m.pages[index / kSlots][index % kSlots] = e;
    }
    // the game's places as its open window shows them
    if (menu) {
        const uint32_t count = ld32(menu + kMenuChildren), data = ld32(menu + kMenuChildren + 4);
        for (uint32_t i = 0; i < kSlots; i++) {
            const uint32_t child = data && kFirstWindowChild + i < count ? ld32(data + 4 * (kFirstWindowChild + i)) : 0;
            const uint32_t n = child ? ld32(child + kItemNumber) & 0xFF : kEmpty;
            if (n == kEmpty) m.game[i] = 0;
            else if (story_items::pool_number(n)) {
                const std::optional<story_items::Item> it = story_items::by_number(n);
                m.game[i] = it ? it->key : kGameItem;
            } else m.game[i] = kGameItem;  // a game item, the notebook (0xA0) or a carried letter (0xA1)
        }
    }
    return m;
}

// ---- sounds: the game's menu sounds ----
void play(Cpu* c, Sound s) {
    const char* label = nullptr;
    switch (s) {
    case Sound::Cursor: label = "SE_MENU_CURSOR"; break;
    case Sound::Decide: label = "SE_MENU_DECIDE"; break;
    case Sound::Cancel: label = "SE_MENU_CANCEL"; break;
    case Sound::Open: label = "SE_MENU_MINI_WINDOW_OPEN"; break;
    case Sound::Close: label = "SE_MENU_TAB_SWITCH"; break;
    case Sound::Turn: label = "SE_MENU_TAB_SWITCH"; break;
    case Sound::Error: label = "SE_MENU_CANCEL"; break;
    case Sound::None: break;
    }
    const story_notebook::GameUi& ui = story_notebook::game_ui();
    if (!label || !c || !ui.sound || !g_call) return;
    Cpu probe = *c;
    const uint32_t play_object = g_call(&probe, kPlay, {});
    if (!play_object || ld8(play_object + kPlayEventRun)) return;
    if (trace()) LOG("[bag] sound %s", label);
    Cpu call = *c;
    ui.sound(&call, label);
}

void say(const std::string& text) {
    g_message = text, g_message_time = kMessageSeconds;
    if (trace() && !text.empty()) LOG("[bag] message: %s", text.c_str());
}

// the open window shows the places again (after an item moved between a page and the game's places): the
// game reads every place's item again (inventoryItem: story_notebook.cpp answers mod items)
void refresh_window(Cpu* c) {
    if (!g_menu || !g_call) return;
    Cpu call = *c;
    g_call(&call, kExecuteSaveChildren, {g_menu});
    if (trace()) LOG("[bag] the Delivery Bag window's places read again");
}

std::string item_name(uint16_t key);
// the save's delivery place (0..7) a window place (cell 0..7: child 0x14 + cell) shows, -1 if none
int save_place_of_cell(int cell) {
    const uint32_t items = ld32(kWindowItems + 4 * kDeliveryMode), count = ld32(kWindowCounts + 4 * kDeliveryMode);
    for (uint32_t i = 0; items && i < count && i < 32; i++) {
        const uint32_t key = ld32(items + 8 * i), child = ld32(items + 8 * i + 4);
        if (child == kFirstWindowChild + uint32_t(cell) && key >= kFirstDeliveryKey && key < kFirstDeliveryKey + kSlots)
            return int(key - kFirstDeliveryKey);
    }
    return -1;
}

void carry_out(Cpu* c, const Action& a) {
    using story_items::Action;
    switch (a.kind) {
    case story_bag::Action::Info:
        if (!story_items::act(a.key, Action::Info)) say(story_items::reason());
        break;
    case story_bag::Action::Use:
        if (!story_items::act(a.key, Action::Use)) say(story_items::reason());
        break;
    case story_bag::Action::Put: {
        const int place = save_place_of_cell(a.place);
        if (place < 0) {
            say("That place can't take it.");
        } else if (story_items::act(a.key, Action::MoveToGamePlace, place)) {
            LOG("[bag] item %04X to the game's place %d (the window's place %d)", a.key, place, a.place);
            refresh_window(c);
            // its page place is free now: an item that waits for one (the pages were full) takes it
            for (uint16_t waiting : story_items::unplaced())
                if (story_items::act(waiting, Action::MoveToPage, 0)) {
                    LOG("[bag] item %04X (it waited) gets the freed place", waiting);
                    break;
                }
        } else {
            say(story_items::reason());
        }
        break;
    }
    case story_bag::Action::Take: {
        const int to = int(story_items::kFirstPagePlace + a.page * kSlots + unsigned(a.slot));
        if (story_items::act(a.key, Action::MoveToPage, to)) {
            LOG("[bag] item %04X from the window's place %d to page %u place %d", a.key, a.place, a.page + 1, a.slot);
            refresh_window(c);
        } else {
            say(story_items::reason());
        }
        break;
    }
    case story_bag::Action::Assign:
        // one button at a time, as the game moves its own items between X, Y and R
        if (story_items::act(a.key, Action::Assign, -1) && story_items::act(a.key, Action::Assign, a.slot)) {
            LOG("[bag] item %04X on %s", a.key, a.slot == 0 ? "X" : "Y");
            if (g_after_assign) g_after_assign(c);  // the menu's X / Y / R strip shows it at once
            say(item_name(a.key) + (a.slot == 0 ? " is on X." : " is on Y."));
        } else {
            say(story_items::reason());
        }
        break;
    case story_bag::Action::Close:
    case story_bag::Action::None: break;
    }
}

bool menu_up() { return story_notebook::item_menu_shown(); }

// ---- the layout ----
std::string item_name(uint16_t key) {
    const std::optional<story_items::Item> it = story_items::find(key);
    return it ? it->name : std::string();
}

const story_notebook::art::Bitmap* icon_of(uint16_t key) {
    auto found = g_icons.find(key);
    if (found != g_icons.end()) return found->second.width ? &found->second : nullptr;
    story_notebook::art::Bitmap& out = g_icons[key];
    const std::optional<story_items::Item> it = story_items::find(key);
    if (!it) return nullptr;
    try {
        const std::filesystem::path path(it->icon_path);
        const guestmods::hud::Pixels px = guestmods::hud::load_png(path.parent_path(), path.filename().string());
        story_notebook::art::Bitmap b;
        b.width = int(px.width), b.height = int(px.height), b.rgba = px.rgba;
        // fitted into the square, its aspect kept
        const float k = std::min(float(kIconSize) / float(b.width), float(kIconSize) / float(b.height));
        const int w = std::max(1, int(std::lround(b.width * k))), h = std::max(1, int(std::lround(b.height * k)));
        const story_notebook::art::Bitmap fit = story_notebook::art::resized(b, w, h);
        out.width = out.height = int(kIconSize);
        out.rgba.assign(size_t(kIconSize) * kIconSize * 4, 0);
        const int ox = (int(kIconSize) - w) / 2, oy = (int(kIconSize) - h) / 2;
        for (int y = 0; y < h; y++)
            std::copy_n(&fit.rgba[size_t(y) * w * 4], size_t(w) * 4, &out.rgba[(size_t(y + oy) * kIconSize + size_t(ox)) * 4]);
    } catch (const std::exception& e) {
        LOG("[bag] item %04X: its icon can't be read: %s", key, e.what());
        out = {};
        return nullptr;
    }
    return &out;
}

// the icon of `key` in a slot texture (once per item and slot); false if it can't be shown
bool icon_into(int slot, uint16_t key) {
    if (g_slot_key[slot] == key) return true;
    const story_notebook::art::Bitmap* b = icon_of(key);
    if (!b) return false;
    const std::string file = "BagIcon" + std::to_string(slot) + "_00^l.bflim";
    if (!story_lyt_screen::set_texture_pixels(g_id, file, b->rgba.data(), kIconSize, kIconSize)) {
        LOG("[bag] item %04X: its icon not written into %s", key, file.c_str());
        return false;
    }
    g_slot_key[slot] = key;
    return true;
}

uint32_t pane(Cpu* c, const char* name) { return story_lyt_screen::pane(c, g_id, name); }
void show(Cpu* c, const char* name, bool on) { story_lyt_screen::set_visible(pane(c, name), on); }
void put_text(Cpu* c, int index, const char* name, const std::string& text) {
    if (g_last_text[index] == text) return;
    if (const uint32_t p = pane(c, name)) story_lyt_text::set_text(c, p, text), g_last_text[index] = text;
}

// The game's bag window's place on the screen (MenuItem_00's W_BagItemBase_00 composed up to its root, as the
// notebook's icon follows its place: story_notebook_bag.h): our N_Bag goes there, at its scale and fade.
struct WindowPlace {
    bool shown = false;
    float x = 313, y = -92, scale = 1, alpha = 1;
};
WindowPlace window_place(Cpu* c) {
    WindowPlace out;
    if (!g_menu) return out;
    const uint32_t parts_layout = ld32(g_menu + kItemLayout);
    const uint32_t lyt = parts_layout ? ld32(parts_layout + kLayoutLyt) : 0;
    static uint32_t cached_lyt = 0, cached_pane = 0;
    if (lyt != cached_lyt) cached_lyt = lyt, cached_pane = lyt ? story_lyt_text::find_pane(c, lyt, "W_BagItemBase_00") : 0;
    const uint32_t window = cached_pane;
    if (!window) return out;
    std::vector<story_notebook::PaneNode> chain;
    for (uint32_t p = window; p && chain.size() < 32; p = ld32(p + kPaneParent)) {
        story_notebook::PaneNode n;
        n.tx = u32_as_f32(ld32(p + kPaneTranslate)), n.ty = u32_as_f32(ld32(p + kPaneTranslate + 4));
        n.sx = u32_as_f32(ld32(p + kPaneScale)), n.sy = u32_as_f32(ld32(p + kPaneScale + 4));
        n.visible = (ld8(p + kPaneFlags) & 1) != 0;
        chain.push_back(n);
    }
    // the window pane itself counts (the game hides it while the window is closed)
    if (chain.empty() || !chain[0].visible) return out;
    const story_notebook::IconPlace at = story_notebook::place_icon(chain, 1.0f, ld8(window + kPaneGlobalAlpha), 1280.0f, 720.0f);
    if (!at.shown) return out;
    out.shown = true;
    out.x = at.x - 640.0f, out.y = 360.0f - at.y, out.scale = at.size, out.alpha = at.alpha;
    return out;
}

const char* option_label(Page::Option o) {
    switch (o) {
    case Page::Option::Info: return "Info";
    case Page::Option::Use: return "Use";
    case Page::Option::Move: return "Move to the first page";
    case Page::Option::Cancel: return "Cancel";
    }
    return "";
}

void built(Cpu* c, uint32_t) {
    for (auto& t : g_last_text) t.clear();
    for (uint16_t& k : g_slot_key) k = 0;  // a new archive in memory: its slots are empty
    g_built_seen = false;
    for (const char* name : {"T_Name", "T_Msg", "T_Hint"})
        if (const uint32_t p = pane(c, name)) story_lyt_text::set_capacity(c, p, 96);
}

// every item screen draw: the page's state onto the panes
bool frame(Cpu* c, uint32_t) {
    const bool open = g_page.open();
    if (!open && (!g_menu || g_model.pages.empty())) return false;
    const WindowPlace at = window_place(c);
    if (!at.shown && !open) return false;
    if (!g_built_seen) {
        g_built_seen = true;
        story_lyt_screen::play(c, g_id, "Breathe");
    }
    if (const uint32_t bag = pane(c, "N_Bag")) {
        story_lyt_screen::set_translate(bag, at.x, at.y);
        st32(bag + kPaneScale, f32_as_u32(at.scale)), st32(bag + kPaneScale + 4, f32_as_u32(at.scale));
        story_lyt_screen::set_alpha(bag, uint8_t(std::lround(255 * std::clamp(at.alpha, 0.0f, 1.0f))));
    }
    const Page::Mode mode = g_page.mode();
    const bool browse = mode == Page::Mode::Browse || mode == Page::Mode::Menu;
    show(c, "N_Page", browse);
    if (g_open_anim) story_lyt_screen::play(c, g_id, "Open"), g_open_anim = false;
    if (g_turn) story_lyt_screen::play(c, g_id, g_turn < 0 ? "TurnLeft" : "TurnRight"), g_turn = 0;

    // the page dots: the game's page, then the extra pages
    const unsigned pages = unsigned(g_model.pages.size()), dots = pages + 1;
    const int current = browse ? int(g_page.page()) + 1 : 0;
    for (unsigned i = 0; i < 17; i++) {
        char name[16];
        snprintf(name, sizeof name, "P_Dot%u", i);
        const uint32_t p = pane(c, name);
        story_lyt_screen::set_visible(p, i < dots && pages > 0);
        if (i < dots) {
            story_lyt_screen::set_translate(p, Page::dot_x(i, dots), 0);
            story_lyt_screen::set_alpha(p, int(i) == current ? 255 : 120);
            const float s = int(i) == current ? 1.25f : 1.0f;
            st32(p + kPaneScale, f32_as_u32(s)), st32(p + kPaneScale + 4, f32_as_u32(s));
        }
    }

    // the places of the current page
    if (browse && g_page.page() < pages) {
        const auto& entries = g_model.pages[g_page.page()];
        for (int i = 0; i < int(kSlots); i++) {
            const Entry& e = entries[size_t(i)];
            char name[16];
            snprintf(name, sizeof name, "P_Icon%d", i);
            show(c, name, e.key && icon_into(i, e.key));
            snprintf(name, sizeof name, "T_Num%d", i);
            show(c, name, e.key && e.count > 1);
            if (e.key && e.count > 1) put_text(c, 10 + i, name, std::to_string(e.count));
            snprintf(name, sizeof name, "P_Disc%d", i);
            show(c, name, e.key && g_page.cursor() == i);
        }
        const Entry sel = g_page.selected(g_model);
        put_text(c, 0, "T_Name", sel.key ? item_name(sel.key) : std::string());
        std::string msg = g_message_time > 0 ? g_message : std::string();
        if (msg.empty() && g_model.unplaced)
            msg = "The bag is full: " + std::to_string(g_model.unplaced) + (g_model.unplaced == 1 ? " item waits" : " items wait") +
                  " for a free place.";
        if (msg.empty() && !sel.key && g_page.cursor() < int(kSlots) && g_model.game_has_mod_item())
            msg = "A: bring an item here from the first page";
        put_text(c, 1, "T_Msg", msg);
    }
    // the menu of the selected item
    show(c, "N_Menu", mode == Page::Mode::Menu);
    if (mode == Page::Mode::Menu) {
        const auto& opts = g_page.options();
        for (int i = 0; i < 4; i++) {
            char name[16];
            snprintf(name, sizeof name, "T_Opt%d", i);
            show(c, name, i < int(opts.size()));
            if (i < int(opts.size())) {
                put_text(c, 20 + i, name, option_label(opts[size_t(i)]));
                story_lyt_screen::set_alpha(pane(c, name), g_page.option_enabled(size_t(i), g_model) ? 255 : 110);
            }
        }
        story_lyt_screen::set_translate(pane(c, "P_MenuSel"), 0, Page::option_y(g_page.menu_cursor()));
    }
    // choosing one of the game's places: the game's window shows, our cursor and the item over it
    const bool pick = mode == Page::Mode::Pick;
    show(c, "T_Hint", pick);
    if (pick) {
        const bool put = g_page.pick_kind() == Page::PickKind::Put;
        put_text(c, 2, "T_Hint", g_message_time > 0 ? g_message : put ? "Choose an empty place" : "Choose an item to bring back");
    }
    const bool float_icon = pick && g_page.pick_key() && icon_into(kFloatSlot, g_page.pick_key());
    show(c, "P_Float", float_icon);
    const int cell = g_page.cursor();
    const float cx = kPlaceX[cell], cy = kPlaceY[cell];
    if (float_icon) story_lyt_screen::set_translate(pane(c, "P_Float"), cx + 26, cy + 26);
    show(c, "N_Cursor", open && mode != Page::Mode::Menu);
    story_lyt_screen::set_translate(pane(c, "N_Cursor"), cx, cy);
    return true;
}

}  // namespace

bool configure(const std::filesystem::path& cache_dir) {
    if (g_configured) return true;
    if (!story_items::imported()) return false;
    if (const char* e = getenv("WWHD_BAG_PAGES"); e && *e) {
        const int n = std::atoi(e);
        if (n >= 1) story_items::set_page_limit(unsigned(n));
    }
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    const std::filesystem::path desc = cache_dir / (std::string(kLayout) + ".json");
    {
        const std::string want(reinterpret_cast<const char*>(wwhd_story_bag_layout), wwhd_story_bag_layout_size);
        std::string have;
        if (std::ifstream f(desc, std::ios::binary); f) have.assign(std::istreambuf_iterator<char>(f), {});
        if (have != want) {
            std::ofstream out(desc, std::ios::binary | std::ios::trunc);
            out.write(want.data(), std::streamsize(want.size()));
            if (!out) return LOG("[bag] can't write %s: the bag has no extra pages", desc.string().c_str()), false;
        }
    }
    const std::string archive = lyt_build::build_cached(desc, cache_dir);
    if (archive.empty()) return LOG("[bag] the layout %s can't be built: the bag has no extra pages", kLayout), false;
    story_lyt_screen::Client client;
    client.layout = kLayout;
    client.archive = archive;
    client.owner = "the Delivery Bag's extra pages";
    client.order = -1;  // below the notebook's page (it opens over the bag)
    client.draw_hides_cursor = false;  // the dots on the game's page leave its cursor alone
    client.game_archives = {"MenuItem_00", "Cursor_00", "BtnItemIcon_00", "BtnBack_00"};
    client.anims = {{"Open", "G_Page", 0}, {"TurnLeft", "G_Slots", 1}, {"TurnRight", "G_Slots", 1}, {"Breathe", "G_Cursor", 2}};
    client.built = built;
    client.frame = frame;
    client.covers = [] { return g_open.load(std::memory_order_relaxed); };
    story_notebook::set_bag_cover([] { return g_covering.load(std::memory_order_relaxed); });
    g_id = story_lyt_screen::add_client(std::move(client));
    if (g_id < 0) return false;
    g_configured = true;
    LOG("[bag] the Delivery Bag's extra pages: layout %s (%s), up to %u pages for new items", kLayout, archive.c_str(),
        story_items::page_limit());
    return true;
}

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original, g_call = call;
}
void set_after_assign(void (*after)(Cpu*)) { g_after_assign = after; }

void pad(input::PadState& p, Cpu* c) {
    if (!g_configured) return;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
    last = now;
    if (g_message_time > 0) g_message_time -= dt;
    g_menu = story_notebook::delivery_window_menu();
    const bool window = g_menu && menu_up() && !story_notebook::page_open();
    g_model = window || g_page.open() ? read_model(g_menu) : Model{};
    if (trace()) {
        static int last = -1;
        const int now = (g_menu ? 1 : 0) | (window ? 2 : 0) | (g_model.pages.empty() ? 0 : 4) | (g_page.open() ? 8 : 0);
        if (now != last) {
            const auto own = story_items::owned();
            std::string places;
            for (uint32_t i = 0; i < kSlots; i++) {
                const uint32_t count = g_menu ? ld32(g_menu + kMenuChildren) : 0, data = g_menu ? ld32(g_menu + kMenuChildren + 4) : 0;
                const uint32_t child = data && kFirstWindowChild + i < count ? ld32(data + 4 * (kFirstWindowChild + i)) : 0;
                char b[8];
                snprintf(b, sizeof b, " %02X", child ? ld32(child + kItemNumber) & 0xFF : 0xFFu);
                places += b;
            }
            places += "; save places by window place:";
            for (int i = 0; i < int(kSlots); i++) places += " " + std::to_string(save_place_of_cell(i));
            LOG("[bag] owned %zu, first place %u; the game's places:%s", own.size(), own.empty() ? 0u : unsigned(story_items::place(own[0])),
                places.c_str());
        }
        if (now != last)
            LOG("[bag] window %s, menu shown %d, %zu extra page(s), %u unplaced, page %s", g_menu ? "open" : "closed", menu_up() ? 1 : 0,
                g_model.pages.size(), g_model.unplaced, g_page.open() ? "open" : "closed");
        last = now;
    }
    if (!g_page.open()) {
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        }
        g_takes_lr = window && !g_model.pages.empty();
        if (!g_takes_lr.load(std::memory_order_relaxed)) return;
        // the game's Delivery Bag window with extra pages: L and R are the pages' (R opens the first), a tap on
        // a page dot opens that page
        static uint32_t prev = 0;
        static bool touch_prev = false;
        static int touch_dot = -1;  // the page dot a touch began on
        const uint32_t pressed = p.buttons & ~prev;
        prev = p.buttons;
        if (p.touch && !touch_prev) {
            const WindowPlace at = window_place(c);
            const float k = at.scale > 0.01f ? at.scale : 1.0f;
            touch_dot = Page::hit_dot((p.tx * 1280.0f - 640.0f - at.x) / k, (360.0f - p.ty * 720.0f - at.y) / k, unsigned(g_model.pages.size()));
        }
        const bool tap = !p.touch && touch_prev && touch_dot >= 1;  // released: the page of the dot it began on
        touch_prev = p.touch;
        int open_at = (pressed & kPadR) ? 0 : -1;
        if (tap && open_at < 0) open_at = touch_dot - 1;
        if (open_at >= 0) {
            g_page.open(p.buttons, unsigned(open_at));
            g_open = true, g_covering = true, g_open_anim = true, g_turn = 1;
            g_swallow.store(p.buttons | kPadR, std::memory_order_relaxed);  // the R (and what is held) never reaches the game
            say("");
            play(c, Sound::Turn);
            LOG("[bag] extra page %d of %zu opened (%s in the Delivery Bag)", open_at + 1, g_model.pages.size(), tap ? "a tap" : "R");
            p = input::PadState{};
            return;
        }
        p.buttons &= ~(kPadL | kPadR);
        return;
    }
    if (story_notebook::page_open()) return;  // an item's Info box (the notebook's caption) has the controller
    if (!g_menu) {  // the window went (the menu closed, a state load): the page goes with it
        g_page.close(), g_open = false, g_covering = false;
        LOG("[bag] the Delivery Bag window closed: the extra page with it");
        return;
    }
    Input in;
    in.buttons = p.buttons, in.touch = p.touch;
    const WindowPlace at = window_place(c);
    // the touch in the window's own units (the GamePad picture as the 1280 x 720 layout)
    const float k = at.scale > 0.01f ? at.scale : 1.0f;
    in.x = (p.tx * 1280.0f - 640.0f - at.x) / k, in.y = (360.0f - p.ty * 720.0f - at.y) / k;
    const Page::Mode before = g_page.mode();
    const unsigned page_before = g_page.page();
    const Action act = g_page.input(in, g_model);
    if (g_page.turned()) g_turn = g_page.turned();
    play(c, act.kind == Action::Put ? Sound::None : g_page.sound());
    if (act.kind == Action::Put) {
        Cpu call = *c;
        if (const auto& ui = story_notebook::game_ui(); ui.sound) ui.sound(&call, "SE_MENU_ITEM_SET");
    }
    carry_out(c, act);
    if (trace() && (g_page.mode() != before || g_page.page() != page_before || act.kind != Action::None))
        LOG("[bag] mode %d page %u cursor %d action %d", int(g_page.mode()), g_page.page(), g_page.cursor(), int(act.kind));
    if (!g_page.open()) {
        g_open = false, g_covering = false;
        g_swallow.store(p.buttons, std::memory_order_relaxed);
        LOG("[bag] back to the game's Delivery Bag window");
    } else if (before == Page::Mode::Pick && g_page.mode() == Page::Mode::Browse) {
        g_open_anim = true;
    }
    g_covering = g_page.open() && g_page.mode() != Page::Mode::Pick;
    p = input::PadState{};  // the game sees an idle controller while the page is open
}

void mute(input::PadState& p) {
    if (g_open.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
    // the Pro Controller (its buttons in VPAD bits, as the GamePad's): in the Delivery Bag window with extra
    // pages L and R turn the pages (VPADRead, pad()); the game never sees them there (no lift, no X/Y/R
    // assignment, no tab switch)
    if (g_takes_lr.load(std::memory_order_relaxed)) p.buttons &= ~(kPadL | kPadR);
}

bool page_open() { return g_open.load(std::memory_order_relaxed); }

void forget_icon(uint16_t key) {
    g_icons.erase(key);
    for (uint16_t& k : g_slot_key)
        if (k == key) k = 0;
}
void state_loaded() {
    g_page.close();
    g_open = false, g_covering = false, g_takes_lr = false;
    g_menu = 0;
    g_model = {};
    for (uint16_t& k : g_slot_key) k = 0;
    for (auto& t : g_last_text) t.clear();
}

}  // namespace story_bag
