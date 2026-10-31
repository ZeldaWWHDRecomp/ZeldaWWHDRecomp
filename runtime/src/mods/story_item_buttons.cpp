// Mod items and the story notebook on X, Y and R (story_item_buttons.h).
//
// The game's reads (HD decomp, decomp-readable 58254150):
//   - daPy_lk_c::checkNewItemChange(btn) 023EF7CC (d_a_player_main_03.cpp): X/Y/R/the fourth button
//     pressed while Link can change items; it reads dComIfGp_getSelectItem(btn) and starts the item's
//     action or equips it (setAnimeEquipItem). For our numbers the port runs the use action and
//     returns FALSE: nothing is equipped.
//   - the play object's mSelectItem[5] (dComIfG_play_c +0x491B; g_dComIfG_gameInfo +0x5BBB): every
//     actor's read of the button's item (inlined dComIfGp_getSelectItem). The game rewrites it from the
//     save key (inlined dComIfGp_setSelectItem: the menu, the meter's create, item gets), so the port
//     writes it again every frame while one of ours is on the button, and gives the game's item back
//     once ours is taken off.
//   - hdCommandGuide_c::getButtonCommand(button) 0264A774 (hd_screen_command_guide_extra.cpp): the HUD's
//     X/Y/R icon (buttons 2..4), read from the save key directly; the icon frame comes from
//     hdMenuItem itemForSlot (story_notebook.cpp: the extended layout's frame or a stand-in).
#include "story_item_buttons.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"
#include "story_items.h"
#include "story_notebook.h"
#include "story_store.h"

namespace story_item_buttons {
namespace {

const uint32_t kSavePtr = GD(0x101F84DC);              // save base pointer (d_com_inf_game)
const uint32_t kPlaySelect = GD(0x1046F0B0) + 0x5BBB;  // g_dComIfG_gameInfo.play.mSelectItem[5]
const uint32_t kLinkActor = GD(0x1046F0B0) + 0x5B34;   // g_dComIfG_gameInfo.play mpPlayerPtr[0]: Link (test_assign)
constexpr uint32_t kSaveSelect = 0x29;                 // dSv_player_status_a_c::mSelectItem[5] (save keys)
const uint32_t kCheckNewItemChange = GC(0x023EF7CC);   // daPy_lk_c::checkNewItemChange(btn)
const uint32_t kButtonCommand = GC(0x0264A774);        // hdCommandGuide_c::getButtonCommand(button)
const uint32_t kMeterItemTexture = GC(0x02591F68);    // dMeter_loadItemTextureHD(meter, btn): the legacy meter's X/Y/Z icon load
const uint32_t kSeStartSystem = GC(0x025E1988);        // mDoAud_seStartSystem(id)
const uint32_t kStripRefresh = GC(0x0266FDC8);         // hdIconSlideArea_c::refreshItems(): the item menu's X/Y/R strip
const uint32_t kStoreSelection = GC(0x02632724);       // hdMenuItem_c::storeSelection(this, key, item)
constexpr uint32_t kChildren = 0x48, kMenuStrip = 0x2D;  // hdParts_c::mChildren {count, data}; hdMenuMain_c's strip child
constexpr uint32_t kRefuseSound = 0x883;               // JA_SE_ITEM_TARGET_OUT (the game's "can't use it")
constexpr uint32_t kFirstItemButton = 2, kLastItemButton = 4;  // the command guide's X, Y, R
constexpr uint32_t kNoCommand = 0xFFFFFFFFu;

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
bool g_installed = false;
uint32_t g_strip = 0;  // the strip whose refreshItems runs now (game thread)
bool g_active = false;  // one of ours was on a button this session (tick() keeps the bytes right from then on)

bool trace() {
    static const bool on = [] {
        const char* a = getenv("WWHD_ITEM_TRACE");
        const char* b = getenv("WWHD_NOTEBOOK_TRACE");
        return (a && *a == '1') || (b && *b == '1');
    }();
    return on;
}
uint32_t save_base() { return ld32(kSavePtr); }
bool have_save() { return save_base() != 0 && story_store::book().usable(); }
bool is_pool(uint32_t n) { return story_items::pool_number(n); }
const char* name(unsigned button) {
    static const char* names[kButtons] = {"X", "Y", "R", "button 4", "button 5"};
    return button < kButtons ? names[button] : "?";
}
// the game's item for a button's save key (0xFF: none)
uint32_t game_item(unsigned button) {
    const uint32_t sv = save_base();
    const int off = save_item_offset(ld8(sv + kSaveSelect + button));
    return off < 0 ? kNone : ld8(sv + uint32_t(off));
}
void refuse_sound(Cpu* c) {
    if (!c || !g_call) return;
    Cpu call = *c;
    g_call(&call, kSeStartSystem, {kRefuseSound});
}
uint32_t child(uint32_t parts, uint32_t index) {
    const uint32_t count = ld32(parts + kChildren), data = ld32(parts + kChildren + 4);
    return data && index < count ? ld32(data + 4 * index) : 0;
}
void say(const std::string& why) {
    LOG("[items] %s", why.c_str());
    story_notebook::show_info("Item", why, "a refused assignment");
}

}  // namespace

Ours ours(unsigned button) {
    Ours o;
    if (button >= kButtons || !have_save()) return o;
    if (story_items::notebook_on_button(button) && story_notebook::notebook_owned()) {
        o.notebook = true;
        return o;
    }
    if (const uint16_t key = story_items::on_button(button)) {
        o.key = key;
        o.number = uint8_t(story_items::number(key));
    }
    return o;
}
uint32_t button_item(unsigned button) { return answer(ours(button)); }

bool assign_notebook(unsigned button) {
    if (button >= kAssignable || !have_save() || !story_notebook::notebook_owned()) return false;
    story_items::assign_notebook(-1);  // on one button at a time, as a game item
    if (!story_items::assign_notebook(int(button))) {
        say("The save file has no room left for this.");
        return false;
    }
    LOG("[items] the notebook is on %s", name(button));
    return true;
}

bool assign_item(uint16_t key, unsigned button) {
    if (button >= kAssignable) return false;
    const std::optional<story_items::Item> it = story_items::find(key);
    if (!it) return false;
    story_items::act(key, story_items::Action::Assign, -1);  // on one button at a time, as a game item
    if (!story_items::act(key, story_items::Action::Assign, int(button))) {
        say(story_items::reason());
        return false;
    }
    LOG("[items] %s: item \"%s\" is on %s (number %02X)", it->mod.c_str(), it->id.c_str(), name(button), story_items::number(key));
    return true;
}

void save_keys(uint8_t out[kAssignable]) {
    const uint32_t sv = have_save() ? save_base() : 0;
    for (unsigned b = 0; b < kAssignable; b++) out[b] = sv ? ld8(sv + kSaveSelect + b) : uint8_t(kNone);
}

void refresh_strip(Cpu* c) {
    const uint32_t menu = story_notebook::delivery_window_menu();
    const uint32_t strip = menu ? child(menu, kMenuStrip) : 0;
    if (!c || !g_call || !strip) return;
    Cpu call = *c;
    g_strip = strip;
    g_call(&call, kStripRefresh, {strip});
    g_strip = 0;
}

void game_assigned(const uint8_t before[kAssignable], unsigned slot) {
    if (!have_save()) return;
    uint8_t after[kAssignable];
    save_keys(after);
    const unsigned wins = game_wins(before, after, slot);
    for (unsigned b = 0; b < kAssignable; b++) {
        if (!(wins >> b & 1)) continue;
        const Ours o = ours(b);
        if (o.notebook) {
            story_items::assign_notebook(-1);
            LOG("[items] the game's item on %s: the notebook is off the button", name(b));
        } else if (o.key) {
            story_items::act(o.key, story_items::Action::Assign, -1);
            LOG("[items] the game's item on %s: the mod item is off the button", name(b));
        }
    }
}

void tick() {
    if (!have_save()) return;
    Ours now[kButtons];
    for (unsigned b = 0; b < kButtons; b++) g_active |= answer(now[b] = ours(b)) != 0;
    if (!g_active) return;  // nothing of ours on a button this session: the game's bytes are never read
    for (unsigned b = 0; b < kButtons; b++) {
        const Ours& o = now[b];
        const uint32_t play_byte = ld8(kPlaySelect + b);
        bool write = false;
        const uint32_t v = play_value(o, play_byte, answer(o) ? 0 : game_item(b), is_pool, write);
        if (!write) continue;
        st8(kPlaySelect + b, uint8_t(v));
        if (trace()) LOG("[items] %s: the game reads item %02X (was %02X)", name(b), v, play_byte);
    }
}

namespace {
// research aid: WWHD_ITEMS_TEST_PLACE=mod:item:place[:step] gives the item (if needed) and puts it in one
// of the game's Delivery Bag places (0..7) once the logic step reaches `step` in play
void test_place() {
    static bool done = false;
    const char* e = getenv("WWHD_ITEMS_TEST_PLACE");
    if (done || !e || !*e || !have_save() || !ld32(kLinkActor)) return;
    char mod[64] = {}, id[64] = {};
    unsigned place = 0;
    unsigned long long step = 0;
    if (sscanf(e, "%63[^:]:%63[^:]:%u:%llu", mod, id, &place, &step) < 3 || mods::step() < step) return;
    done = true;
    if (story_items::count(mod, id) <= 0) story_items::give(mod, id, 1, "WWHD_ITEMS_TEST_PLACE");
    const std::optional<story_items::Item> it = story_items::find(mod, id);
    const bool ok = it && story_items::act(it->key, story_items::Action::MoveToGamePlace, int(place));
    LOG("[items] WWHD_ITEMS_TEST_PLACE at logic step %llu: \"%s\" in place %u: %s", (unsigned long long)mods::step(), id, place,
        ok ? "ok" : story_items::reason().c_str());
}
}  // namespace

void test_assign() {
    test_place();
    // research aid: WWHD_ITEMS_TEST_BUTTON=mod:item:button[:step] gives the item (if needed) and puts it
    // on the button once the game's logic step reaches `step` in play; mod "notebook" puts the notebook there
    static bool done = false;
    const char* e = getenv("WWHD_ITEMS_TEST_BUTTON");
    if (done || !e || !*e || !have_save() || !ld32(kLinkActor)) return;
    char mod[64] = {}, id[64] = {};
    unsigned button = 0;
    unsigned long long step = 0;
    if (sscanf(e, "%63[^:]:%63[^:]:%u:%llu", mod, id, &button, &step) < 3 || mods::step() < step) return;
    done = true;
    LOG("[items] WWHD_ITEMS_TEST_BUTTON at logic step %llu", (unsigned long long)mods::step());
    if (!strcmp(mod, "notebook")) {
        assign_notebook(button);
        return;
    }
    if (story_items::count(mod, id) <= 0) story_items::give(mod, id, 1, "WWHD_ITEMS_TEST_BUTTON");
    if (const std::optional<story_items::Item> it = story_items::find(mod, id)) assign_item(it->key, button);
}

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original, g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed || !g_original || !story_notebook::installed()) return;
    const std::pair<uint32_t, void (*)(Cpu*)> list[] = {{kCheckNewItemChange, hooks::check_new_item_change},
                                                        {kButtonCommand, hooks::button_command},
                                                        {kStripRefresh, hooks::strip_refresh},
                                                        {kStoreSelection, hooks::store_selection}};
    for (const auto& [addr, fn] : list) {
        const std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[items] %s replaces %08X: mod items and the notebook can't go on X, Y or R this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : list) replace(addr, fn);
    // research aid (WWHD_ITEMS_METER_PROBE=1): log the legacy meter's X/Y/Z icon loads, to see whether
    // the HD game still runs that path for a button with one of ours
    if (const char* e = getenv("WWHD_ITEMS_METER_PROBE"); e && *e == '1' && replaced_by(kMeterItemTexture).empty())
        replace(kMeterItemTexture, hooks::meter_probe);
    g_installed = true;
    LOG("[items] mod items and the notebook can go on X, Y and R");
}
bool installed() { return g_installed; }

namespace hooks {

// checkNewItemChange(this, btn): one of ours on the pressed button runs its use action; Link equips nothing
void check_new_item_change(Cpu* c) {
    const unsigned button = c->r[4] & 0xFF;
    const Ours o = ours(button);
    if (!answer(o)) return g_original(c, kCheckNewItemChange);
    bool ok;
    if (o.notebook) {
        ok = story_notebook::open_page_from_button(button);
    } else {
        ok = story_items::act(o.key, story_items::Action::Use);
        const std::optional<story_items::Item> it = story_items::find(o.key);
        LOG("[items] %s in play: item \"%s\" %s", name(button), it ? it->id.c_str() : "?",
            ok ? "used" : ("not used: " + story_items::reason()).c_str());
    }
    if (!ok) refuse_sound(c);
    c->r[3] = 0;  // FALSE: no item proc, nothing equipped
}

// refreshItems(this): the item menu's X/Y/R strip reads the save's keys; ours answer in store_selection
void strip_refresh(Cpu* c) {
    const uint32_t outer = g_strip;
    g_strip = c->r[3];
    g_original(c, kStripRefresh);
    g_strip = outer;
}

// storeSelection(this, key, item): within the strip's refresh, the item of a button of ours
void store_selection(Cpu* c) {
    if (g_strip)
        for (unsigned b = 0; b < kAssignable; b++)
            if (child(g_strip, b) == c->r[3]) {
                if (const uint32_t a = button_item(b)) c->r[5] = a;
                break;
            }
    g_original(c, kStoreSelection);
}

// dMeter_loadItemTextureHD(meter, btn) (WWHD_ITEMS_METER_PROBE only): logged, then the game's
void meter_probe(Cpu* c) {
    const uint32_t button = c->r[4];
    LOG("[items] meter probe (step %llu): dMeter_loadItemTextureHD(button %u), the game reads item %02X, save key %02X", (unsigned long long)mods::step(), button,
        button < kButtons ? ld8(kPlaySelect + button) : 0xFFu, button < kButtons && save_base() ? ld8(save_base() + kSaveSelect + button) : 0xFFu);
    g_original(c, kMeterItemTexture);
}

// getButtonCommand(this, button): the HUD's X/Y/R icon for ours
void button_command(Cpu* c) {
    const uint32_t button = c->r[4];
    g_original(c, kButtonCommand);
    if (button < kFirstItemButton || button > kLastItemButton) return;
    const unsigned b = button - kFirstItemButton;
    const uint32_t a = button_item(b);
    if (!a) return;
    // the game's item answer only (not a forced command of an event or the HD context's icon)
    const uint32_t mine = game_item(b);
    if (c->r[3] != kNoCommand && c->r[3] != mine) return;
    static uint32_t last[kAssignable] = {0, 0, 0};
    if (trace() && last[b] != a) LOG("[items] the HUD's %s shows item %02X (the game's: %02X)", name(b), a, c->r[3]);
    last[b] = a;
    c->r[3] = a;
}

}  // namespace hooks
}  // namespace story_item_buttons
