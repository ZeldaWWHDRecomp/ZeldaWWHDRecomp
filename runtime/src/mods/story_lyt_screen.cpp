// Story UI layouts: our layout with the game's item screen (story_lyt_screen.h).
#include "story_lyt_screen.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <map>

#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"
#include "story_lyt_clients.h"
#include "story_lyt_text.h"
#include "lyt_build.h"
#include "../input.h"

namespace story_lyt_screen {
namespace {
const uint32_t kLoadItemScreen = GC(0x02693650);   // hdIngameLoadItemScreen(screen, projection, matrix, flags)
const uint32_t kPartsDraw = GC(0x026F8B54);        // hdParts_c::draw(this, viewport)
const uint32_t kFinalizeScreens = GC(0x02715374);  // hdIngameUi_c::finalizeScreens(this)
const uint32_t kDrawPrimary = GC(0x02716AD0);      // hdIngameUi_c::drawPrimaryScreens(this, info): the item menu on the GamePad
const uint32_t kCursorDraw = GC(0x026538A0);       // hdCursorMgr_c::draw(this, info): the menus' selection cursor
const uint32_t kDrawHud = GC(0x02715CA0);          // hdIngameUi_c::drawHudScreens(this, info): the TV (the item menu there with the Pro Controller)
const uint32_t kResMgrCache = GC(0x026FB694);      // hdLayoutResMgr_c::cache(this, name, heap)
const uint32_t kResMgrFind = GC(0x026FBB24);       // hdLayoutResMgr_c::find(this, name): the archive's data
const uint32_t kNoControlConstruct = GC(0x02700ADC);  // NoControlPart_l::construct(0)
const uint32_t kAttachArchives = GC(0x026F90D8);   // hdParts_c::attachArchives(this, names, count)
const uint32_t kPartsNew = GC(0x0273B050);         // allocate(size, heap, alignment)
const uint32_t kLayoutConstruct = GC(0x026FADB0);  // hdPartsLayout_c::hdPartsLayout_c()
const uint32_t kBindGroup = GC(0x02004E04);        // animation set: bindGroup(entry, animation, group)
const uint32_t kAnimPlay = GC(0x020053E4);         // animation set: play(entry, slot, rate f1)
const uint32_t kAnimSetFrame = GC(0x0200552C);     // animation set: setFrame(entry, slot, frame f1)
const uint32_t kAnimIsEnd = GC(0x02005840);        // animation set: isEnd(entry)
const uint32_t kCalcLayout = GC(0x026F8CD0);       // hdParts_c::calcLayout(this)
const uint32_t kUiSystem = GD(0x1018C404);         // the UI system (its heap at +0x10)
const uint32_t kLayoutResMgr = GD(0x101F7274);     // the layout resource manager
const uint32_t kIngameUi = GD(0x101F8344);         // the in-game UI scene (item screen at +0x17C)
const uint32_t kSafeStringVtable = GD(0x1010A3FC); // a sead SafeString vtable of the parts framework
// where hdIngameUi_c's screen creation takes the item screen's projection and matrix from (HD decomp
// hd_scene_ingame_ui.cpp, the screens' load: the GamePad's render layer, 7, else the UI defaults):
// the render manager (layer count +0x1020, layers +0x1024) and hdLayerBase_c (view matrix pointer
// +0x48, projection pointer +0x4C, flags +0x50, own view matrix +0x84, internal projection +0x164)
const uint32_t kRenderManager = GD(0x101F95D0);
const uint32_t kBecomeCurrentHeap = GC(0x027EC220);  // JKRHeap::becomeCurrentHeap(this): the heap current before
const uint32_t kDefaultUiProjection = GD(0x104A20FC), kDefaultUiMatrix = GD(0x104A2098);
constexpr uint32_t kItemScreen = 0x17C;
// hdParts_c: vtable at +4 (delete +0x0C, finalize +0x54); its layout at +0x44, resource accessor +0x34;
// hdPartsLayout_c: vtable +0xE0 (vLoad +0x14), layout object +4, animation set +0xD4
constexpr uint32_t kPartsVtable = 4, kVtDelete = 0x0C, kVtFinalize = 0x54, kPartsLayout = 0x44, kPartsAccessor = 0x34;
constexpr uint32_t kLayoutVtable = 0xE0, kVtLoad = 0x14, kLayoutLyt = 4, kLayoutAnimSet = 0xD4;
constexpr uint32_t kPaneTranslate = 0x1C, kPaneSize = 0x3C, kPaneFlags = 0x44, kPaneAlpha = 0x45;


bool guest_pointer(uint32_t a) {
    return (a >= mem::kMem2Start && a < mem::kMem2End - 0x200) || (a >= mem::kMem1 && a < mem::kMem1 + mem::kMem1Size - 0x200);
}

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;

// one client and its layout as built with this item screen
struct Slot {
    Client client;
    uint32_t part = 0, layout = 0, set = 0;
    uint32_t part_vt = 0, layout_vt = 0;
    uint32_t archive = 0;   // the archive's data in guest memory (a SARC), found once built
    uint32_t names = 0;     // the archive names for attachArchives (made once)
    std::map<std::string, uint32_t> panes;
    bool rebuild = false;   // dropped by a state load: built again once the game's item screen draws
};
std::vector<Slot> g_slots;   // by id
std::vector<int> g_order;    // ids in draw (and build) order
bool g_installed = false;
bool g_check = false;  // a state was loaded: check still_there() before the next use
std::atomic<uint32_t> g_drawn_screen{0};  // the item screen drawn while a layout waits for its build (draw passes)
uint64_t g_passes = 0, g_covered = 0;  // screen passes drawn (drawPrimaryScreens / drawHudScreens), the last with ours
uint32_t g_viewport = 0;  // the item screen's viewport, while drawPrimaryScreens runs (0: it didn't draw)

Slot* slot(Id id) { return id >= 0 && size_t(id) < g_slots.size() ? &g_slots[size_t(id)] : nullptr; }
bool any_built() {
    for (const Slot& s : g_slots)
        if (s.part) return true;
    return false;
}
const char* name_of(const Slot& s) { return s.client.layout.c_str(); }

// Test aid (WWHD_STORY_LYT_HEAP=1): the free room of the heaps the layout manager and the UI use, logged
// around our build and when the item screen goes (JKRHeap: getFreeSize vtable +0x74, getTotalFreeSize).
const uint32_t kHeapTotalFree = GC(0x027EC1FC);  // JKRHeap::getTotalFreeSize(this)
void heap_report(Cpu* c, const char* where) {
    static const bool on = std::getenv("WWHD_STORY_LYT_HEAP") != nullptr;
    if (!on) return;
    const uint32_t mgr = ld32(kLayoutResMgr), ui = ld32(kUiSystem);
    // and the game's main heaps (mDoExt_getArchiveHeap / GameHeap / ZeldaHeap / CommandHeap)
    const std::pair<const char*, uint32_t> heaps[] = {{"mgr+10", mgr ? ld32(mgr + 0x10) : 0}, {"mgr+14", mgr ? ld32(mgr + 0x14) : 0},
                                                      {"ui+10", ui ? ld32(ui + 0x10) : 0}, {"archive", g_call(c, GC(0x025E3268), {})},
                                                      {"game", g_call(c, GC(0x025E2FBC), {})}, {"zelda", g_call(c, GC(0x025EE048), {})},
                                                      {"command", g_call(c, GC(0x025EE060), {})}};
    static bool dumped = false;
    for (const auto& [label, h] : heaps) {
        if (!h || !ld32(h + 0xC)) { LOG("[lyt-heap] %s %s: none", where, label); continue; }
        const uint32_t vt = ld32(h + 0xC);
        const uint32_t free_ = g_call(c, ld32(vt + 0x74), {h}), total = g_call(c, kHeapTotalFree, {h});
        const uint32_t max4 = g_call(c, ld32(vt + 0x7C), {h, 4});
        LOG("[lyt-heap] %s %s %08X: free %.2f MB, total free %.2f MB, vt+7C(4) %.2f MB", where, label, h, free_ / 1048576.0,
            total / 1048576.0, max4 / 1048576.0);
        if (!dumped) {
            std::string words;
            char b[16];
            for (uint32_t o = 0; o < 0x80; o += 4) snprintf(b, sizeof b, " %08X", ld32(h + o)), words += b;
            LOG("[lyt-heap] %s header:%s", label, words.c_str());
        }
    }
    dumped = true;
    // the item icons' count modes (sCountModes, one word per icon frame, 55 of them) and what follows: a
    // frame past them reads on (item_icons.h; 1 or 2 there would show a count)
    std::string words;
    for (uint32_t i = 55; i < 128; i++) {
        const uint32_t v = ld32(GD(0x100E7E48) + 4 * i);
        if (v == 1 || v == 2) words += " [" + std::to_string(i) + "]=" + std::to_string(v);
    }
    LOG("[lyt-heap] count modes 1/2 past frame 54 (to 127):%s", words.empty() ? " none" : words.c_str());
    // where the game's own archives sit (the heap ranges above), and their size in memory
    for (const char* n : {"BtnItemIcon_00", "BtnCollectIcon_00", "BtnMenu_00", "MenuItem_00", "Cursor_00", "BtnBack_00"}) {
        const uint32_t data = mgr ? g_call(c, kResMgrFind, {mgr, safe_string(n)}) : 0;
        LOG("[lyt-heap] %s %s: %08X, %.2f MB", where, n, data, data && ld32(data) == 0x53415243 ? ld32(data + 8) / 1048576.0 : 0.0);
    }
}

void drop(Slot& s) { s.part = s.layout = s.set = 0, s.archive = 0, s.panes.clear(); }

// After a state load: are the client's objects still in guest memory (a portable state, or a full state
// saved after the build)? Otherwise the handles are dropped without touching the memory.
bool still_there(Slot& s) {
    if (!s.part) return false;
    const bool there = ld32(s.part + kPartsVtable) == s.part_vt && ld32(s.part + kPartsLayout) == s.layout &&
                       ld32(s.layout + kLayoutVtable) == s.layout_vt && ld32(s.layout + kLayoutAnimSet) == s.set;
    if (after_load(true, there) == AfterLoad::Keep) return true;
    LOG("[lyt] %s: not in the loaded state: built again when the item screen is next shown", name_of(s));
    drop(s);
    s.rebuild = true;
    return false;
}
void check_all() {
    if (!g_check) return;
    g_check = false;
    for (Slot& s : g_slots) still_there(s);
}

int entry_of(const Slot& s, const std::string& anim) {
    for (size_t i = 0; i < s.client.anims.size(); i++)
        if (s.client.anims[i].name == anim) return int(i);
    return -1;
}

void destroy(Cpu* c, Slot& s) {
    if (!still_there(s)) return;
    const uint32_t part = s.part;
    drop(s);
    const uint32_t vt = ld32(part + kPartsVtable);
    g_call(c, ld32(vt + kVtFinalize), {part});
    g_call(c, ld32(vt + kVtDelete), {part, 3});
    LOG("[lyt] %s: freed with the game's screens", name_of(s));
}

// the archive's size in guest memory (the SARC's file size), 0 if it isn't there
uint32_t archive_bytes(uint32_t sarc) { return sarc && ld32(sarc) == 0x53415243 ? ld32(sarc + 8) : 0; }

void build(Cpu* c, Slot& s, uint32_t projection, uint32_t matrix) {
    if (s.part) destroy(c, s);  // a new item screen (should have been freed with the old one)
    s.rebuild = false;          // built now, or failed with its reason logged (not tried every frame)
    const Client& cl = s.client;
    if (FILE* f = fopen(cl.archive.c_str(), "rb")) fclose(f);
    else return LOG("[lyt] %s: no archive %s", name_of(s), cl.archive.c_str());
    const uint32_t name = safe_string(cl.layout.c_str());
    const uint32_t mgr = ld32(kLayoutResMgr), ui = ld32(kUiSystem), heap = ui ? ld32(ui + 0x10) : 0;
    if (!mgr || !heap) return LOG("[lyt] %s: no layout manager / UI heap", name_of(s));
    heap_report(c, "before cache");
    // Test aid (WWHD_TEST_ICON_FRAME=n): every item's icon frame in the game's item -> frame table (itemForSlot,
    // 57 entries of 16 bytes: item, frame), to see the extended item icons (item_icons.h) without a mod item
    static const char* icon_frame = std::getenv("WWHD_TEST_ICON_FRAME");
    if (icon_frame) {
        for (uint32_t e = GD(0x100E7AB8); e < GD(0x100E7AB8) + 57 * 16; e += 16) st32(e + 4, uint32_t(atoi(icon_frame)));
        LOG("[lyt] test: every item icon shows frame %d", atoi(icon_frame));
    }
    if (!g_call(c, kResMgrCache, {mgr, name, 0})) return LOG("[lyt] %s: the archive didn't load", name_of(s));
    // cache() reports success even when the archive couldn't be read into memory (too large for the
    // layout heap, a bad file): only go on with the archive actually there
    heap_report(c, "after cache");
    if (!g_call(c, kResMgrFind, {mgr, name})) return LOG("[lyt] %s: the archive is not in memory (too large?)", name_of(s));
    const uint32_t part = g_call(c, kNoControlConstruct, {0});
    if (!part) return LOG("[lyt] %s: no parts object", name_of(s));
    // our archive first, then the game's archives whose textures the layout names (an array of SafeStrings)
    // (made once: the client doesn't change after install)
    const uint32_t count = 1 + uint32_t(cl.game_archives.size());
    if (!s.names) {
        s.names = mem::host_alloc(8 * count, 4);
        memcpy(mem::ptr(s.names), mem::ptr(name), 8);
        for (uint32_t i = 1; i < count; i++) memcpy(mem::ptr(s.names + 8 * i), mem::ptr(safe_string(cl.game_archives[i - 1].c_str())), 8);
    }
    g_call(c, kAttachArchives, {part, s.names, count});
    uint32_t layout = g_call(c, kPartsNew, {0x110, heap, 4});
    if (layout) layout = g_call(c, kLayoutConstruct, {layout});
    if (!layout) {
        const uint32_t vt = ld32(part + kPartsVtable);
        g_call(c, ld32(vt + kVtDelete), {part, 3});
        return LOG("[lyt] %s: no layout object", name_of(s));
    }
    st32(part + kPartsLayout, layout);
    s.part = part, s.layout = layout;  // from here destroy() frees both
    s.part_vt = ld32(part + kPartsVtable), s.layout_vt = ld32(layout + kLayoutVtable);
    uint32_t slots = 1;
    for (const auto& a : cl.anims) slots = std::max(slots, a.slot + 1);
    const uint32_t load = ld32(ld32(layout + kLayoutVtable) + kVtLoad);
    const uint32_t entries = uint32_t(cl.anims.size());
    if (!g_call(c, load, {layout, name, ld32(part + kPartsAccessor), projection, matrix, entries, entries ? slots : 0, 0})) {
        LOG("[lyt] %s: the layout didn't load", name_of(s));
        return destroy(c, s);
    }
    s.set = ld32(layout + kLayoutAnimSet);
    if (entries && !s.set) {
        LOG("[lyt] %s: no animation set", name_of(s));
        return destroy(c, s);
    }
    for (uint32_t i = 0; i < entries; i++)
        g_call(c, kBindGroup, {s.set, i, safe_string(cl.anims[i].name.c_str()), safe_string(cl.anims[i].group.c_str())});
    s.archive = g_call(c, kResMgrFind, {mgr, name});
    s.layout_vt = ld32(layout + kLayoutVtable);
    LOG("[lyt] %s: built (parts %08X, layout %08X, %u animations, archive %.2f MB)", name_of(s), part, layout, entries,
        archive_bytes(s.archive) / 1048576.0);
    if (cl.built) cl.built(c, ld32(s.layout + kLayoutLyt));
}

void build_all(Cpu* c, uint32_t projection, uint32_t matrix) {
    uint32_t total = 0;
    int built = 0;
    for (int id : g_order) {
        Slot& s = g_slots[size_t(id)];
        build(c, s, projection, matrix);  // a client that fails leaves the others alone
        if (s.part) total += archive_bytes(s.archive), built++;
    }
    if (g_slots.size() > 1) LOG("[lyt] %d of %zu layouts built, their archives %.2f MB together", built, g_slots.size(), total / 1048576.0);
}

// The item screen's projection and matrix as the game's screen creation picks them (kRenderManager):
// for a build without the game's load call (after a state load). False if the renderer isn't there.
bool item_screen_view(uint32_t& projection, uint32_t& matrix) {
    const uint32_t mgr = ld32(kRenderManager);
    if (!guest_pointer(mgr)) return false;
    const uint32_t count = ld32(mgr + 0x1020), layers = ld32(mgr + 0x1024);
    const uint32_t layer = guest_pointer(layers) ? ld32(layers + 4 * (count > 7 ? 7 : 0)) : 0;
    if (!guest_pointer(layer)) return false;
    LayerView l;
    l.flags = ld32(layer + 0x50), l.projection = ld32(layer + 0x4C), l.internal_projection = ld32(layer + 0x164);
    l.view_matrix = ld32(layer + 0x48), l.own_matrix = layer + 0x84;
    const ScreenView v = story_lyt_screen::item_screen_view(l, kDefaultUiProjection, kDefaultUiMatrix);
    projection = v.projection, matrix = v.matrix;
    return true;
}

bool any_rebuild() {
    for (const Slot& s : g_slots)
        if (s.rebuild) return true;
    return false;
}

// After a state load whose memory doesn't hold our layouts: the loaded game's item screen was built
// before the state was saved, and the game builds it again only with the next stage or controller
// change (hdIngameLoadItemScreen). The layouts are built once that item screen has been drawn (it is
// there and in use), at the next frame boundary on the main thread (as the game's own build: a draw
// pass runs on the graphics thread, where loading the archive waits forever), with the projection
// and matrix the game gave it. The game builds its screens with the UI heap as the current heap (the
// parts object comes from it); at the frame boundary another one is current: the UI heap is made
// current for the build (JKRHeap::becomeCurrentHeap, which returns the one before), then the one before.
void rebuild_dropped(Cpu* c) {
    if (!any_rebuild()) return;
    uint32_t projection = 0, matrix = 0;
    const uint32_t ui = ld32(kUiSystem), heap = ui ? ld32(ui + 0x10) : 0;
    if (!item_screen_view(projection, matrix) || !guest_pointer(heap)) return;
    const uint32_t before = g_call(c, kBecomeCurrentHeap, {heap});
    for (int id : g_order) {
        Slot& s = g_slots[size_t(id)];
        if (!s.rebuild || s.part) continue;
        LOG("[lyt] %s: building again for the loaded state's item screen", name_of(s));
        build(c, s, projection, matrix);
    }
    if (guest_pointer(before)) g_call(c, kBecomeCurrentHeap, {before});
}

// Our calls into the game must not change what the hooked function's caller sees: the registers are
// put back as they were (the original's return values included).
struct Keep {
    Cpu* c;
    Cpu saved;
    explicit Keep(Cpu* cpu) : c(cpu), saved(*cpu) {}
    ~Keep() { *c = saved; }
};

namespace hooks {
void load_item_screen(Cpu* c) {
    const uint32_t projection = c->r[4], matrix = c->r[5];
    g_original(c, kLoadItemScreen);
    Keep keep(c);
    static const bool check_view = std::getenv("WWHD_STORY_LYT_VIEW_CHECK") != nullptr;  // test aid
    if (check_view) {
        uint32_t p = 0, m = 0;
        const bool ok = item_screen_view(p, m);
        LOG("[lyt] view check: the game's projection %08X matrix %08X, ours %08X %08X (%s)", projection, matrix, p, m,
            ok && p == projection && m == matrix ? "same" : "DIFFERENT");
    }
    build_all(c, projection, matrix);
}

// the item screen draws: remember its viewport; ours are drawn after all of the item menu's screens
void parts_draw(Cpu* c) {
    const uint32_t self = c->r[3], viewport = c->r[4];
    g_original(c, kPartsDraw);
    if (!any_built() && !any_rebuild()) return;
    const uint32_t scene = ld32(kIngameUi);
    if (!scene || self != ld32(scene + kItemScreen)) return;
    g_viewport = viewport;
    if (any_rebuild()) g_drawn_screen = self;
}

// after a screen group was drawn: ours on top of it (in draw order), if the item screen was among it
void draw_after(Cpu* c) {
    const uint32_t viewport = g_viewport;
    g_viewport = 0;
    if (!viewport || (!any_built() && !g_check && !any_rebuild())) return;
    Keep keep(c);
    check_all();
    for (int id : g_order) {
        Slot& s = g_slots[size_t(id)];
        if (!s.part) continue;
        if (s.client.frame && !s.client.frame(c, ld32(s.layout + kLayoutLyt))) continue;
        if (!s.part) continue;
        g_call(c, kCalcLayout, {s.part});
        g_call(c, kPartsDraw, {s.part, viewport});
        if (s.client.draw_hides_cursor) g_covered = g_passes;
    }
}

// The menus' selection cursor (four corner brackets) is drawn by a pass after ours on the TV: while one of
// our layouts covers the item menu (drawn in the last frame or this one) the cursor is not drawn; nothing
// of its state changes, it is drawn again as soon as our layouts are gone.
void cursor_draw(Cpu* c) {
    if (any_built()) {
        // covered: one of ours was drawn in one of the last passes (both passes run every frame, the cursor's
        // pass may come before ours in a frame), counted in passes, not time: a slow frame must not show it
        if (g_covered && g_passes <= g_covered + 3) return;
        for (const Slot& s : g_slots)
            if (s.part && s.client.covers && s.client.covers()) return;
    }
    g_original(c, kCursorDraw);
}

void before_all() {
    for (const Slot& s : g_slots)
        if (s.part && s.client.before) s.client.before();
}

void after_all() {
    for (auto it = g_slots.rbegin(); it != g_slots.rend(); ++it)
        if (it->part && it->client.after) it->client.after();
}

// The item menu on the GamePad: its screens (item screen, sub window, menu buttons, cursor, messages) are
// drawn by drawPrimaryScreens; ours right after them.
void draw_primary(Cpu* c) {
    ++g_passes;
    before_all();
    g_viewport = 0;
    g_original(c, kDrawPrimary);
    after_all();
    if (input::pro_controller()) g_viewport = 0;  // the menu is the TV's
    draw_after(c);
}

// With the Pro Controller the item menu is on the TV, drawn by drawHudScreens; ours right after it, in the
// render state that pass set up (a later pass, e.g. drawInputScreens with the menu cursor, draws the layout's
// pictures wrongly).
void draw_hud(Cpu* c) {
    ++g_passes;
    before_all();
    g_viewport = 0;
    g_original(c, kDrawHud);
    after_all();
    if (!input::pro_controller()) g_viewport = 0;  // the menu is the GamePad's
    draw_after(c);
}

void finalize_screens(Cpu* c) {
    {
        Keep keep(c);  // the original still needs its `this` (r3)
        for (auto it = g_order.rbegin(); it != g_order.rend(); ++it) destroy(c, g_slots[size_t(*it)]);
        heap_report(c, "screen gone");
    }
    for (Slot& s : g_slots) if (s.client.finalized) s.client.finalized();
    g_covered = 0;
    g_original(c, kFinalizeScreens);
}
}  // namespace hooks
}  // namespace

bool set_texture_pixels(Id id, const std::string& file, const uint8_t* rgba, uint32_t w, uint32_t h) {
    const Slot* s = slot(id);
    const uint32_t sarc = s ? s->archive : 0;
    if (!sarc || ld32(sarc) != 0x53415243) return false;  // "SARC"
    const std::string path = "timg/" + file;
    uint32_t hash = 0;
    for (unsigned char ch : path) hash = hash * 0x65 + ch;  // the SARC name hash (key 0x65)
    const uint32_t data = sarc + ld32(sarc + 0x0C), sfat = sarc + ld16(sarc + 0x04);  // SFAT after the 0x14-byte header
    const uint32_t count = ld16(sfat + 0x06);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t e = sfat + 0x0C + 16 * i;
        if (ld32(e) != hash) continue;
        const uint32_t start = data + ld32(e + 8), end = data + ld32(e + 12);
        if (end < start + 0x28) return false;
        const uint32_t footer = end - 0x28;  // "FLIM" ... "imag" w h align format tile size
        if (ld32(footer) != 0x464C494D || ld16(footer + 0x1C) != w || ld16(footer + 0x1E) != h || ld8(footer + 0x22) != 0x14)
            return false;
        const uint8_t tile = ld8(footer + 0x23);
        const lyt_build::Bytes texels(rgba, rgba + size_t(w) * h * 4);
        const lyt_build::Bytes image = lyt_build::tile_rgba8(texels, w, h, tile & 15, tile >> 5);
        if (image.empty() || image.size() != ld32(footer + 0x24) || start + image.size() > footer) return false;
        memcpy(mem::ptr(start), image.data(), image.size());
        return true;
    }
    return false;
}
bool set_texture_pixels(const std::string& file, const uint8_t* rgba, uint32_t w, uint32_t h) {
    return set_texture_pixels(0, file, rgba, w, h);
}

uint32_t safe_string(const char* text) {
    const uint32_t n = uint32_t(strlen(text)) + 1;
    const uint32_t buf = mem::host_alloc(n, 4);
    memcpy(mem::ptr(buf), text, n);
    const uint32_t s = mem::host_alloc(8, 4);
    st32(s, buf);
    st32(s + 4, kSafeStringVtable);
    return s;
}

Id add_client(Client client) {
    if (g_installed) {
        LOG("[lyt] %s: added after the hooks were installed: not shown", client.layout.c_str());
        return -1;
    }
    if (client.layout.empty() || client.archive.empty()) return -1;
    for (const Slot& s : g_slots)
        if (s.client.layout == client.layout) {
            LOG("[lyt] %s: a second client with that layout name: not shown", client.layout.c_str());
            return -1;
        }
    Slot s;
    s.client = std::move(client);
    g_slots.push_back(std::move(s));
    std::vector<int> orders;
    for (const Slot& x : g_slots) orders.push_back(x.client.order);
    g_order = draw_order(orders);
    return Id(g_slots.size() - 1);
}
void set_client(Client client) {
    if (!g_slots.empty()) {
        LOG("[lyt] %s: set_client after another client: added as a further one", client.layout.c_str());
    }
    add_client(std::move(client));
}
bool has_client() { return !g_slots.empty(); }

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_slots.empty() || !g_original || !g_call || g_installed) return;
    // all six or nothing: a layout that is built but never freed (or never drawn) must not happen
    for (uint32_t fn : {kLoadItemScreen, kPartsDraw, kFinalizeScreens, kDrawPrimary, kDrawHud, kCursorDraw})
        if (const std::string by = replaced_by(fn); !by.empty()) {
            LOG("[lyt] %08X is replaced by %s: no story layouts", fn, by.c_str());
            g_slots.clear(), g_order.clear();
            return;
        }
    replace(kLoadItemScreen, hooks::load_item_screen);
    replace(kPartsDraw, hooks::parts_draw);
    replace(kFinalizeScreens, hooks::finalize_screens);
    replace(kDrawPrimary, hooks::draw_primary);
    replace(kDrawHud, hooks::draw_hud);
    replace(kCursorDraw, hooks::cursor_draw);
    g_installed = true;
    for (int id : g_order)
        LOG("[lyt] %s (%s, client %d, order %d): from %s", name_of(g_slots[size_t(id)]), g_slots[size_t(id)].client.owner, id,
            g_slots[size_t(id)].client.order, g_slots[size_t(id)].client.archive.c_str());
}

void service(Cpu* c) {
    const uint32_t drawn = g_drawn_screen.exchange(0);
    if (!drawn) return;
    const uint32_t scene = ld32(kIngameUi);
    if (!scene || ld32(scene + kItemScreen) != drawn) return;  // gone since (a stage change builds them anyway)
    Keep keep(c);
    check_all();
    rebuild_dropped(c);
}

void state_loaded() {
    bool any = false;
    // our guest objects made by the host (archive names, text buffers) may be the loaded state's memory
    // now: made again. A client without a layout now (none built yet, e.g. a load from the title screen)
    // gets one with the loaded state's item screen as well.
    for (Slot& s : g_slots) {
        s.panes.clear(), s.names = 0, any |= s.part != 0;
        if (!s.part && after_load(false, false) == AfterLoad::BuildWithItemScreen) s.rebuild = true;
    }
    g_check = any;
    story_lyt_text::state_loaded();
}

std::string archive_for(const std::string& guest) {
    for (const Slot& s : g_slots)
        if (is_layout_file(guest, s.client.layout)) return s.client.archive;
    return "";
}

bool ready(Id id) {
    check_all();
    const Slot* s = slot(id);
    return s && s->part != 0;
}
uint32_t lyt(Id id) {
    const Slot* s = slot(id);
    return s && s->layout ? ld32(s->layout + kLayoutLyt) : 0;
}
uint32_t anim_set(Id id) {
    const Slot* s = slot(id);
    return s ? s->set : 0;
}

uint32_t pane(Cpu* c, Id id, const char* name) {
    Slot* s = slot(id);
    if (!s || !s->layout) return 0;
    auto it = s->panes.find(name);
    if (it != s->panes.end()) return it->second;
    const uint32_t p = story_lyt_text::find_pane(c, lyt(id), name);
    s->panes[name] = p;
    if (!p) LOG("[lyt] %s: no pane %s", name_of(*s), name);
    return p;
}

void play(Cpu* c, Id id, const std::string& anim, float rate) {
    const Slot* s = slot(id);
    const int e = s ? entry_of(*s, anim) : -1;
    if (!s || !s->set || e < 0) return;
    c->f[1].ps0 = rate, c->f[1].ps1 = rate;
    g_call(c, kAnimPlay, {s->set, uint32_t(e), s->client.anims[size_t(e)].slot});
}
void set_frame(Cpu* c, Id id, const std::string& anim, float frame) {
    const Slot* s = slot(id);
    const int e = s ? entry_of(*s, anim) : -1;
    if (!s || !s->set || e < 0) return;
    c->f[1].ps0 = frame, c->f[1].ps1 = frame;
    g_call(c, kAnimSetFrame, {s->set, uint32_t(e), s->client.anims[size_t(e)].slot});
}
bool ended(Cpu* c, Id id, const std::string& anim) {
    const Slot* s = slot(id);
    const int e = s ? entry_of(*s, anim) : -1;
    return !s || !s->set || e < 0 || g_call(c, kAnimIsEnd, {s->set, uint32_t(e)}) != 0;
}

// the first client's (the notebook's) calls
bool ready() { return ready(0); }
uint32_t lyt() { return lyt(0); }
uint32_t anim_set() { return anim_set(0); }
uint32_t pane(Cpu* c, const char* name) { return pane(c, 0, name); }
void play(Cpu* c, const std::string& anim, float rate) { play(c, 0, anim, rate); }
void set_frame(Cpu* c, const std::string& anim, float frame) { set_frame(c, 0, anim, frame); }
bool ended(Cpu* c, const std::string& anim) { return ended(c, 0, anim); }

void set_visible(uint32_t p, bool visible) {
    if (p) st8(p + kPaneFlags, uint8_t((ld8(p + kPaneFlags) & 0xFE) | (visible ? 1 : 0)));
}
void set_alpha(uint32_t p, uint8_t alpha) {
    if (p) st8(p + kPaneAlpha, alpha);
}
void set_translate(uint32_t p, float x, float y) {
    if (!p) return;
    st32(p + kPaneTranslate, f32_as_u32(x));
    st32(p + kPaneTranslate + 4, f32_as_u32(y));
    st8(p + kPaneFlags, uint8_t(ld8(p + kPaneFlags) | 0x10));
}
void set_size(uint32_t p, float w, float h) {
    if (!p) return;
    st32(p + kPaneSize, f32_as_u32(w));
    st32(p + kPaneSize + 4, f32_as_u32(h));
    st8(p + kPaneFlags, uint8_t(ld8(p + kPaneFlags) | 0x10));
}

}  // namespace story_lyt_screen
