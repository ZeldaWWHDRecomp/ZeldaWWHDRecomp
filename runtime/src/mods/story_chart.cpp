// Mod charts and their markers on the sea chart (story_chart.h, docs/mod-sdk-v2.md "Charts and markers").
//
// How the game marks story destinations (HD decomp MAP-sea_chart#1, hd_screen_sea_chart_menu.cpp;
// GameCube: dMenu_Fmap_c::checkMarkCheck1/2/3):
//   - The sea chart menu (hdSeaChartMenu_c, the MAP screen of the GamePad, also shown on the TV) has
//     eight story-mark panes (mpMenuPanes, +0x50): null panes holding the white ring the game puts on
//     the island to go to next (Dragon Roost, Forest Haven, Greatfish, the three triangle islands, the
//     Earth and Wind Temples). Their places are fixed in the layout; the game never moves them.
//   - updateProgressIcons() (02678B74) shows the ones the story asks for, from event flags, every
//     update. While the screen is busy (opening, zooming, closing), it calls hideMenuPanes()
//     (02678B00) instead, which hides all eight.
//   - The chart area is the main pane (+0x70): the 7 x 7 squares, square D4 at its position, one
//     square (100000 world units) = its size / 7, y up (the game places Link's marker the same way,
//     updateMarkerPanePosition).
//   - Treasure and other charts are save bits (dSv_player_map_c: owned, opened, complete). The sea
//     chart shows nothing for an owned treasure chart; once its treasure is salvaged (complete), the
//     game's red hand-drawn circle (P_TreasureGet_NN, one per square) marks the salvage spot
//     (hdSeaChartGrid_c::updateChartMarkLayout 026815A8 places it, refreshChartMarks 026834A4 shows
//     it through the cell's "TreasureGet" animation). A mod chart has no game item: its ownership is
//     a record of the story-mod book (story_mail_book.h).
//
// The tracked quest's ring: after the game's updateProgressIcons, the first story-mark pane the game
// is not showing carries the notebook's tracked step: it is moved there and shown. Before the game's
// next update the pane goes back to its place and is hidden again, so the game always finds its panes
// as it left them and the game's own marks win.
//
// Mod chart markers behave like the game's treasure charts: nothing on the sea chart while the chart
// is only owned; once the mod marks the chart done (wwhd_chart_finish), the game's red circle at each
// marker's spot. The circle is the TreasureGet pane of a square whose own circle the game does not
// show (the marker's own square first): after the game's refreshChartMarks it is moved to the spot
// and shown with its own animation; it goes back when the marker goes or the game wants that square's
// circle. The zoomed square (the balloon) shows the circle of a finished chart's marker in its square
// the same way. Full save states never contain a moved pane (capture_begin/capture_end); a circle
// shown at a state's capture is hidden again after the state is loaded (heal).
#include "story_chart.h"
#include "story_mail_book.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <cmath>
#include <mutex>

#include "guest_addr.h"
#include "story_store.h"
#include "runtime.h"

namespace story_chart {
namespace {

// canonical USA addresses, translated for the running build (guest_addr.h)
const uint32_t kSavePtr = GD(0x101F84DC);     // save base pointer (d_com_inf_game)
const uint32_t kProgress = GC(0x02678B74);    // hdSeaChartMenu_c::updateProgressIcons
const uint32_t kHidePanes = GC(0x02678B00);   // hdSeaChartMenu_c::hideMenuPanes
const uint32_t kDestroy = GC(0x02677B90);     // hdSeaChartMenu_c::destroy(flags)
const uint32_t kMarkLayout = GC(0x026815A8);  // hdSeaChartGrid_c::updateChartMarkLayout
const uint32_t kMarkRefresh = GC(0x026834A4); // hdSeaChartGrid_c::refreshChartMarks (every update)
// The MAP screen's chart list (hdMenuMap_c, hd_screen_menu_map_states.cpp) and its chart view (TreMap,
// hd_screen_tre_map.cpp) ask the map display coordinator (hd_screen_map_display_coordinator.cpp)
const uint32_t kFindChart = GC(0x02687A64);      // hdMapDisplayCoordinator_c::findChart(chart): the chart's entry
const uint32_t kIsObtained = GC(0x0268A570);  // ::isChartObtained(chart)
const uint32_t kIsOpened = GC(0x0268A5DC);    // ::isChartOpened(chart)
const uint32_t kIsComplete = GC(0x0268A648);  // ::isChartComplete(chart)
const uint32_t kObtainChart = GC(0x0268A52C);    // ::obtainChart(chart): the player opened it (onOpenMap)
const uint32_t kCountObtained = GC(0x0268A3A0);  // ::countAllObtainedCharts(): the list's length
const uint32_t kMenuFindChart = GC(0x02698998);  // hdMenuMap_c::findChart(chart, step): the list's order
const uint32_t kChartCaption = GC(0x02699094);   // hdMenuMap_c::setChartCaption(chart): the chart's name
// the coordinator's chart table (a heap copy of cmapdat.bin) and its entries (hdChartEntry_c, 0x26 bytes)
constexpr uint32_t kCoordChartCount = 0x174, kCoordCharts = 0x17C;
constexpr uint32_t kEntryBytes = 0x26, kEntrySea = 0, kEntrySave = 1, kEntryNumber = 2, kEntryType = 3, kEntryColumn = 4,
                   kEntryRow = 5, kEntryPositions = 6;
constexpr uint32_t kSaveMapGot = 0xF4;  // the save's owned-chart bits (dSv_player_map_c, 4 x u32)
constexpr int kChartNumbers = 61;  // the game's charts 0..60
const uint32_t kChartKind = GC(0x02698A94);      // hdMenuMapChartKind(map, chart): 1 treasure
const uint32_t kChartIcon = GC(0x02698B44);      // hdMenuMapUpdateChartIcon(map, slot, chart)
const uint32_t kChartFrame = GC(0x02698D74);     // hdMenuMapUpdateChartFrame(map, slot, chart): the chart view
const uint32_t kChartDetails = GC(0x0269B414);   // hdMenuMapUpdateChartDetails(map, slot, chart)
const uint32_t kResetSelection = GC(0x02699118); // hdMenuMap_c::resetChartSelection()
constexpr uint32_t kMapChartIcons = 0x84, kTreMapChartIndex = 0x8C;
// The chart view (hdTreMapScreen_c, hd_screen_tre_map.cpp, layout TreMap_00) shows a window of the
// chart's square, kViewSize units wide (sMapWorldSize; the "100x100" under it), centred on mOrigin:
// selectChart sets mOrigin = the entry's square * 100000 + the origin of variant mChartKind, and puts
// the X at (salvage - origin) / scale, z down (updatePosition places Link's icon the same way). It has
// four pictures of the square, one per salvage variant (the panes of group G_MapPattern_0k, its
// texture frame set by selectChart's animation kind + 3), each the window around that variant's origin;
// mGroups (+0x50: group, its pane list at +0xC: next +0, pane +8). mChartKind (+0x90) is the save's
// variant (initializePanes, once), and only the picture of that variant is shown.
const uint32_t kTreMapDestroy = GC(0x026EBEF4);  // hdTreMapScreen_c::destroy(flags)
constexpr uint32_t kTreMapGroups = 0x50, kTreMapOrigin = 0x74, kTreMapKind = 0x90, kGroupPanes = 0xC, kNodePane = 8;
// the in-game menu (hdIngameUi_c, hd_scene_ingame_ui.h): its tab buttons and the map coordinator; the
// MAP screen's list (hdMenuMap_c): the selected chart. The menu switches tabs itself
// (hdIngameUi_c::changeActiveScreen): with no tab button selected this frame (hdMenuButtons::mSelected
// is reset every frame) it goes to the tab of mTransitionFrames; the MAP screen keeps a selected chart
// Link owns when it opens (resetChartSelection).
const uint32_t kIngameUi = GD(0x101F8344);
constexpr uint32_t kUiMenuButtons = 0x184, kUiCoordinator = 0x218, kUiTransitionTab = 0x228, kCoordSeaMap = 0x28, kMapSelectedChart = 0xAC;
constexpr uint32_t kTabMap = 1;
// hdSeaChartGrid_c (hd_sea_chart_components.h): its 49 cells, the chart area, the owner of the panes
constexpr uint32_t kGridCells = 0, kGridTouchPane = 0xC8, kGridMapOwner = 0x10C, kOwnerMarkPanes = 0x84;
constexpr unsigned kCells = 49;
// hdSeaChartCell_c: its square (s8 x, y), whether its circle shows, its animation set, entry and slot
constexpr uint32_t kCellCoord = 4, kCellResource = 0xC, kCellMarkActive = 0x10, kCellAnims = 0x54, kCellMarkEntry = 0x5C,
                   kCellMarkSlot = 0x6C;
// The zoomed square balloon (hdBtnMapIcon_c, layout BtnMapBalloon_00): its P_TreasureGet_00 pane is the
// game's red hand-drawn circle, which marks the salvage spot of that square's treasure chart once the
// treasure is salvaged (isSeaChartComplete 0268A7A0, position from findSeaChart 0268A720 and the save's
// random salvage point; the decomp names the functions refreshFish / updateFish / positionFishOnMap /
// positionFishInDetail;
// shown by stopping animation 13 slot 7 at its end, hidden by resetting it). While the game doesn't show
// it, the tracked quest's step borrows it for its exact spot in that square.
const uint32_t kIconUpdate = GC(0x026396FC);  // hdBtnMapIcon_c::update()
const uint32_t kAnimStop = GC(0x02005708);    // mapIconStop(animSet, anim, slot)
const uint32_t kAnimReset = GC(0x02005674);   // mapIconReset(animSet, anim, slot)
constexpr uint32_t kIconParent = 0x94, kIconTreasure = 0xA8, kIconPosition = 0xE4, kIconMapMode = 0x144,
                   kIconTranslateScale = 0x148, kIconPointScale = 0x150, kIconFishVisible = 0x158, kIconOwner = 0x164;
constexpr uint32_t kOwnerInfo = 0x118;              // HdMapIconOwner::mpInfo (HdMapInfo: +4 cell s8 x,y; +6 offset s16 x,y)
constexpr uint32_t kPartsLayout = 0x44, kLayoutAnimSet = 0xD4;
constexpr uint32_t kFishAnim = 13, kFishSlot = 7;
constexpr uint32_t kPaneScale = 0x34;
constexpr uint32_t kIconPrimary = 0xB8;               // L_LinkPos_00: Link's icon in the balloon (trace: checks the mapping)
const uint32_t kLinkPtr = GD(0x1046F0B0) + 0x5B34;  // g_dComIfG_gameInfo.play mpPlayerPtr[0]

// hdSeaChartMenu_c (hd_screen_sea_chart_menu.h) and library pane fields (hdPartsPaneSRT_c)
constexpr uint32_t kMenuPanes = 0x50, kMainPane = 0x70;
constexpr unsigned kStoryPanes = 8;
constexpr uint32_t kPaneTranslate = 0x1C, kPaneSize = 0x3C, kPaneFlags = 0x44;
constexpr uint8_t kVisible = 0x01, kMatrixDirty = 0x10;
constexpr float kSquaresPerSide = 7.0f;

struct Marker {
    std::string mod, chart, id, label;
    uint16_t key;  // the chart's record key
    float x, z;
    uint32_t icon;
};
// a story-mark pane carrying a marker
struct Applied {
    uint32_t pane;
    unsigned index;        // in the menu's story-mark panes
    uint32_t tx, ty;       // the game's place (raw bits)
    uint32_t mx, my;       // the marker's place (raw bits)
};

std::mutex g_mu;
std::vector<MarkerView> g_world_markers;  // immutable declarations, g_mu
std::vector<Marker> g_markers;  // placement order
std::map<uint32_t, std::vector<Applied>> g_applied;  // per menu (game thread)
bool g_imported = false, g_installed = false;
bool g_hidden = false;  // hideMenuPanes ran during the game's update
TrackedSource g_tracked = nullptr;
void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
std::map<uint32_t, uint32_t> g_borrowed;  // balloon -> its animation set, while a mod spot uses its circle
// a square's TreasureGet circle carrying a finished chart's marker
struct Circle {
    unsigned cell;   // whose pane and animation
    uint32_t pane;
    uint32_t tx, ty; // the game's place (raw bits)
    uint32_t mx, my; // the marker's place (raw bits)
};
std::map<uint32_t, std::vector<Circle>> g_circles;  // per sea chart grid (TV, GamePad, warp map)
bool g_heal_grids = false, g_heal_balloons = false;  // a state was loaded: circles of its capture may still show
std::vector<uint32_t> g_healed;                      // grids and balloons healed since

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_CHART_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(kSavePtr); }
bool have_save() { return save_base() != 0; }
story_mail::Book book() { return story_store::book(); }
// the chart's record (kind kRecordMarker): 1 owned, 3 owned and opened, 2 finished, 0 none (story_chart.h)
uint8_t chart_value(const story_mail::Book& b, uint16_t key) { return b.get(story_mail::kRecordMarker, key); }
// the places of the markers whose chart is finished: the game's red circle marks them
struct Spot {
    float x, z;
};
// a declared chart (manifest "charts", define()): what the MAP screen's chart list shows for it
struct Def {
    std::string mod, id, name, description;
    uint16_t key;
    float x, z;
    std::u16string caption;  // the name as the caption shows it
    std::string item_mod;    // its treasure chest: one of the mod's items, or ...
    uint8_t item_game = 0;   // ... a game item (0: none declared)
};
std::vector<Def> g_defs;  // declaration order (g_mu)
std::vector<Spot> done_spots() {
    std::vector<Spot> out;
    if (!have_save()) return out;
    story_mail::Book b = book();
    if (!b.usable()) return out;
    std::lock_guard lock(g_mu);
    for (const Marker& m : g_markers)
        if (chart_value(b, m.key) == kChartDone) out.push_back({m.x, m.z});
    // a declared chart without markers: its place
    for (const Def& d : g_defs)
        if (chart_value(b, d.key) == kChartDone &&
            std::none_of(g_markers.begin(), g_markers.end(), [&](const Marker& m) { return m.key == d.key; }))
            out.push_back({d.x, d.z});
    return out;
}
int32_t result_of(uint8_t v) {
    return v == kChartOwned || v == kChartOpened ? kResultOwned : v == kChartDone ? kResultDone : kResultNone;
}
bool valid_label(const std::string& label) {
    std::u16string ignored;
    return label.size() <= kMaxLabelBytes && story_mail::append_utf16(label, false, ignored);
}

// the panes of one menu back as the game left them: in place and hidden
void revert(uint32_t menu, const std::vector<Applied>& applied) {
    for (const Applied& a : applied) {
        if (ld32(menu + kMenuPanes + 4 * a.index) != a.pane) continue;  // not that menu's pane any more
        st32(a.pane + kPaneTranslate, a.tx);
        st32(a.pane + kPaneTranslate + 4, a.ty);
        st8(a.pane + kPaneFlags, uint8_t((ld8(a.pane + kPaneFlags) & ~kVisible) | kMatrixDirty));
    }
}
void show(const Applied& a) {
    st32(a.pane + kPaneTranslate, a.mx);
    st32(a.pane + kPaneTranslate + 4, a.my);
    st8(a.pane + kPaneFlags, uint8_t(ld8(a.pane + kPaneFlags) | kVisible | kMatrixDirty));
}

// the markers on the story-mark panes the game left hidden
void apply(uint32_t menu) {
    std::vector<Applied>& applied = g_applied[menu];
    applied.clear();
    const uint32_t main = ld32(menu + kMainPane);
    if (!main || !have_save()) return;
    struct Place {
        float x, z;
    };
    std::vector<Place> shown;
    bool tracked = false;
    if (Place t{}; g_tracked) {  // the notebook's tracked quest comes first
        std::string label;
        if (g_tracked(t.x, t.z, label)) shown.push_back(t), tracked = true;
    }
    for(const auto& m:world_markers())shown.push_back({m.x,m.z});
    if (shown.empty()) return;
    const float cx = float(ldf32(main + kPaneTranslate)), cy = float(ldf32(main + kPaneTranslate + 4));
    const float sx = float(ldf32(main + kPaneSize)) / kSquaresPerSide, sy = float(ldf32(main + kPaneSize + 4)) / kSquaresPerSide;
    size_t next = 0;
    for (unsigned i = 0; i < kStoryPanes && next < shown.size(); i++) {
        const uint32_t pane = ld32(menu + kMenuPanes + 4 * i);
        if (!pane || (ld8(pane + kPaneFlags) & kVisible)) continue;  // the game's own mark
        const Place& m = shown[next++];
        Applied a{pane, i, ld32(pane + kPaneTranslate), ld32(pane + kPaneTranslate + 4),
                  f32_as_u32(cx + m.x / kSquare * sx), f32_as_u32(cy - m.z / kSquare * sy)};
        show(a);
        applied.push_back(a);
    }
    // the tracked quest's ring, logged when it changes (not only with the trace): what the player sees
    {
        static std::string last_tracked;
        char now[128] = "";
        if (tracked && !applied.empty() && applied[0].index < kStoryPanes)
            snprintf(now, sizeof now, "the tracked quest's ring is on story pane %u at %.1f,%.1f", applied[0].index, u32_as_f32(applied[0].mx),
                     u32_as_f32(applied[0].my));
        else if (tracked)
            snprintf(now, sizeof now, "the tracked quest's ring waits: every story pane shows a mark of the game");
        if (last_tracked != now) {
            if (*now) LOG("[chart] %s", now);
            last_tracked = now;
        }
    }
    if (trace()) {
        static std::map<uint32_t, std::string> last;
        std::string now;
        for (const Applied& a : applied) {
            char line[96];
            snprintf(line, sizeof line, " pane %u at %.1f,%.1f;", a.index, u32_as_f32(a.mx), u32_as_f32(a.my));
            now += line;
        }
        if (next < shown.size()) now += " " + std::to_string(shown.size() - next) + " waiting for a free pane";
        if (last[menu] != now) LOG("[chart] menu %08X:%s", menu, now.empty() ? " no markers" : now.c_str());
        last[menu] = now;
    }
}

// a cell's circle: shown (the animation stopped at its end) or hidden (reset), as the game does it
// (hdSeaChartCell_c::resetMarkAnimation / activateMarkAnimation)
void cell_circle(const Cpu& keep, uint32_t cell, bool shown) {
    const uint32_t anims = ld32(cell + kCellAnims), entry = ld32(cell + kCellMarkEntry), slot = ld32(cell + kCellMarkSlot);
    if (!anims || entry == 0xFFFFFFFFu || slot == 0xFFFFFFFFu) return;
    Cpu call = keep;
    g_call(&call, shown ? kAnimStop : kAnimReset, {anims, entry, slot});
}
uint32_t grid_pane(uint32_t grid, unsigned i) {
    const uint32_t owner = ld32(grid + kGridMapOwner);
    return owner ? ld32(owner + kOwnerMarkPanes + 4 * i) : 0;
}
// a circle back as the game left it: at the game's place, hidden (unless the game shows it by now)
void give_back_circle(const Cpu& keep, uint32_t grid, const Circle& c) {
    const uint32_t cell = ld32(grid + kGridCells + 4 * c.cell);
    if (grid_pane(grid, c.cell) != c.pane) return;  // not that grid's pane any more
    st32(c.pane + kPaneTranslate, c.tx);
    st32(c.pane + kPaneTranslate + 4, c.ty);
    st8(c.pane + kPaneFlags, uint8_t(ld8(c.pane + kPaneFlags) | kMatrixDirty));
    if (cell && !ld8(cell + kCellMarkActive)) cell_circle(keep, cell, false);
}
// the finished charts' markers on the circles of squares whose own circle the game does not show
void place_circles(const Cpu& keep, uint32_t grid) {
    std::vector<Circle>& circles = g_circles[grid];
    const uint32_t touch = ld32(grid + kGridTouchPane);
    auto cell_at = [&](unsigned i) { return ld32(grid + kGridCells + 4 * i); };
    // the game shows a square's circle by now (its treasure was salvaged): the game's again, in place
    std::erase_if(circles, [&](const Circle& c) {
        const uint32_t cell = cell_at(c.cell);
        if (cell && !ld8(cell + kCellMarkActive) && grid_pane(grid, c.cell) == c.pane) return false;
        if (grid_pane(grid, c.cell) == c.pane) {
            st32(c.pane + kPaneTranslate, c.tx);
            st32(c.pane + kPaneTranslate + 4, c.ty);
            st8(c.pane + kPaneFlags, uint8_t(ld8(c.pane + kPaneFlags) | kMatrixDirty));
        }
        return true;
    });
    std::vector<Spot> spots = touch ? done_spots() : std::vector<Spot>{};
    // the chart area: 7 x 7 squares, the centre square at the area's position, y up
    // (updateChartMarkLayout: position = area + (square * 100000 + offset) * square size / 100000)
    const float ox = touch ? u32_as_f32(ld32(touch + kPaneTranslate)) : 0, oy = touch ? u32_as_f32(ld32(touch + kPaneTranslate + 4)) : 0;
    const float sx = touch ? u32_as_f32(ld32(touch + kPaneSize)) / kSquaresPerSide : 0;
    const float sy = touch ? u32_as_f32(ld32(touch + kPaneSize + 4)) / kSquaresPerSide : 0;
    std::vector<bool> used(kCells, false);
    std::vector<Circle> next;
    for (const Spot& m : spots) {
        const uint32_t mx = f32_as_u32(ox + m.x / kSquare * sx), my = f32_as_u32(oy - m.z / kSquare * sy);
        const int cx = int(std::lround(m.x / kSquare)), cz = int(std::lround(m.z / kSquare));
        auto free_cell = [&](unsigned i) {
            const uint32_t cell = cell_at(i);
            return cell && !used[i] && !ld8(cell + kCellMarkActive) && grid_pane(grid, i) &&
                   ld32(cell + kCellAnims) && ld32(cell + kCellMarkEntry) != 0xFFFFFFFFu;
        };
        int pick = -1;
        for (unsigned i = 0; i < kCells && pick < 0; i++)  // the marker's own square first
            if (free_cell(i) && int8_t(ld8(cell_at(i) + kCellCoord)) == cx && int8_t(ld8(cell_at(i) + kCellCoord + 1)) == cz) pick = int(i);
        for (const Circle& c : circles)  // else the circle it already has
            if (pick < 0 && free_cell(c.cell) && c.mx == mx && c.my == my) pick = int(c.cell);
        for (unsigned i = 0; i < kCells && pick < 0; i++)
            if (free_cell(i)) pick = int(i);
        if (pick < 0) continue;  // every circle is the game's: the marker waits
        used[pick] = true;
        const uint32_t pane = grid_pane(grid, unsigned(pick));
        auto had = std::find_if(circles.begin(), circles.end(), [&](const Circle& c) { return c.cell == unsigned(pick); });
        Circle c = had != circles.end() ? *had : Circle{unsigned(pick), pane, ld32(pane + kPaneTranslate), ld32(pane + kPaneTranslate + 4), 0, 0};
        c.mx = mx, c.my = my;
        if (ld32(pane + kPaneTranslate) != mx || ld32(pane + kPaneTranslate + 4) != my) {
            st32(pane + kPaneTranslate, mx);
            st32(pane + kPaneTranslate + 4, my);
            st8(pane + kPaneFlags, uint8_t(ld8(pane + kPaneFlags) | kMatrixDirty));
        }
        if (had == circles.end()) {
            cell_circle(keep, cell_at(unsigned(pick)), true);
            LOG("[chart] a finished chart's circle is on square %d,%d's circle at %.1f,%.1f (world %.0f,%.0f)",
                int8_t(ld8(cell_at(unsigned(pick)) + kCellCoord)), int8_t(ld8(cell_at(unsigned(pick)) + kCellCoord + 1)),
                u32_as_f32(mx), u32_as_f32(my), m.x, m.z);
        }
        next.push_back(c);
    }
    for (const Circle& c : circles)
        if (std::none_of(next.begin(), next.end(), [&](const Circle& n) { return n.cell == c.cell; })) give_back_circle(keep, grid, c);
    circles = std::move(next);
}

// ---- the MAP screen's chart list ----
// A mod chart has its own chart number after the game's last one: 61 + its declaration's index. The
// coordinator's questions about it (its entry, owned, opened, finished, opening it, the list's length
// and order) are answered from the mod's record; the game's chart bits and tables are never read or
// written for it. The list's functions that stop at the game's last chart (the icon, the chart view,
// its details, the caption, the kind) run for a mod chart with a game chart number in its place for
// the length of that one call only, the coordinator answering for the mod chart meanwhile; the chart
// view then keeps the mod chart's own number. Nothing of a mod chart is in the game's part of the save.
uint32_t g_entries = 0;  // guest memory: kMaxCharts entries of 0x28 bytes (built on each question)
uint32_t g_coordinator = 0;
CaptionFn g_caption = nullptr;
constexpr uint32_t kFirstModChart = 61;  // after the game's 61 charts (0..60)
constexpr uint32_t kProxyChart = 3;      // a treasure chart number, in place of a mod chart for one call
struct Proxy {
    bool on = false;
    uint32_t chart = 0;  // the mod chart it stands for during the call
} g_proxy;
// a chart number as the coordinator is asked: the mod chart during a proxied call
uint32_t asked(uint32_t chart) { return g_proxy.on && chart == kProxyChart ? g_proxy.chart : chart; }
// a mod chart Link owns: its declaration's index and its record's value; false for anything else
bool mod_chart(uint32_t chart, size_t& def, uint8_t& value) {
    if (chart < kFirstModChart || chart >= kFirstModChart + kMaxCharts || !have_save()) return false;
    story_mail::Book b = book();
    if (!b.usable()) return false;
    std::lock_guard lock(g_mu);
    def = chart - kFirstModChart;
    if (def >= g_defs.size()) return false;
    value = chart_value(b, g_defs[def].key);
    return value == kChartOwned || value == kChartOpened || value == kChartDone;
}
bool mod_chart(uint32_t chart) {
    size_t d;
    uint8_t v;
    return mod_chart(chart, d, v);
}
// the mod charts Link owns, in declaration order (their chart numbers)
std::vector<uint32_t> owned_mod_charts() {
    std::vector<uint32_t> out;
    size_t n;
    {
        std::lock_guard lock(g_mu);
        n = g_defs.size();
    }
    for (size_t i = 0; i < n; i++)
        if (mod_chart(kFirstModChart + uint32_t(i))) out.push_back(kFirstModChart + uint32_t(i));
    return out;
}
bool got_bit(int save_no) {  // the save's owned bit of a game chart (dSv_player_map_c::isGetMap(save_no - 1))
    const int n = save_no - 1;
    if (n < 0 || n >= 128 || !have_save()) return false;
    return (ld32(save_base() + kSaveMapGot + 4 * uint32_t(n >> 5)) >> (n & 31)) & 1;
}
uint32_t chart_entry(uint32_t coord, int number) {  // the game's entry of a chart number (0-based)
    const int32_t count = int32_t(ld32(coord + kCoordChartCount));
    const uint32_t table = ld32(coord + kCoordCharts);
    if (!table || count <= 0 || count > 256) return 0;
    for (int32_t i = 0; i < count; i++)
        if (const uint32_t e = ld32(table + 4 * uint32_t(i)); e && int8_t(ld8(e + kEntryNumber)) == number + 1) return e;
    return 0;
}
uint32_t square_entry(uint32_t coord, int column, int row) {  // the game's chart of a square: its art and origin
    const int32_t count = int32_t(ld32(coord + kCoordChartCount));
    const uint32_t table = ld32(coord + kCoordCharts);
    if (!table || count <= 0 || count > 256) return 0;
    for (int32_t i = 0; i < count; i++)
        if (const uint32_t e = ld32(table + 4 * uint32_t(i));
            e && int8_t(ld8(e + kEntryColumn)) == column && int8_t(ld8(e + kEntryRow)) == row)
            return e;
    for (int32_t i = 0; i < count; i++)  // (by the square's number: row by row from the north-west)
        if (const uint32_t e = ld32(table + 4 * uint32_t(i)); e && int8_t(ld8(e + kEntrySea)) == (row + 3) * 7 + column + 3 + 1) return e;
    return 0;
}
// How a mod chart's view is made (story_chart.h, ChartView): its square, the picture of that square
// that shows its spot (a salvage variant, or none) and the view's centre in the square
struct ViewPlan {
    SquarePlace place;
    uint32_t square = 0;  // the game's entry of the square
    int variant = -1;     // the picture; -1: none of the square's pictures shows the spot
    float origin_x = 0, origin_z = 0;
};
ViewPlan view_plan(uint32_t coord, size_t def) {
    ViewPlan p;
    float x, z;
    {
        std::lock_guard lock(g_mu);
        if (def >= g_defs.size()) return p;
        x = g_defs[def].x, z = g_defs[def].z;
    }
    p.place = square_place(x, z);
    p.square = coord ? square_entry(coord, p.place.column, p.place.row) : 0;
    if (!p.square) return p;
    int16_t origins[4][2];
    for (uint32_t k = 0; k < 4; k++)
        for (uint32_t a = 0; a < 2; a++) origins[k][a] = int16_t(ld16(p.square + kEntryPositions + 2 * (4 * k + a)));
    p.variant = view_variant(origins, p.place.x, p.place.z);
    p.origin_x = p.variant >= 0 ? float(origins[p.variant][0]) : p.place.x;
    p.origin_z = p.variant >= 0 ? float(origins[p.variant][1]) : p.place.z;
    return p;
}
// the mod chart's entry, in its own guest memory: the game's entry of its square (its picture's number),
// its own number, a treasure chart's type, no save number, and for every salvage variant the view's
// centre and the spot (the X, and where its treasure is salvaged)
uint32_t build_entry(uint32_t coord, uint32_t chart, size_t def) {
    const ViewPlan p = view_plan(coord, def);
    const uint32_t treasure = chart_entry(coord, int(kProxyChart));
    if (!p.square || !treasure || !g_entries) return 0;
    const uint32_t e = g_entries + uint32_t(def) * 0x28;
    for (uint32_t i = 0; i < kEntryBytes; i++) st8(e + i, ld8(p.square + i));
    st8(e + kEntrySave, 0);  // none: the game's chart bits are never this chart's
    st8(e + kEntryNumber, uint8_t(chart + 1));
    st8(e + kEntryType, ld8(treasure + kEntryType));
    auto offset = [](float v) { return uint16_t(int16_t(std::clamp(std::lround(v), -32767L, 32767L))); };
    for (uint32_t k = 0; k < 4; k++) {  // {origin x, origin y, salvage x, salvage y} per salvage variant
        st16(e + kEntryPositions + 2 * (4 * k + 0), offset(p.origin_x));
        st16(e + kEntryPositions + 2 * (4 * k + 1), offset(p.origin_z));
        st16(e + kEntryPositions + 2 * (4 * k + 2), offset(p.place.x));
        st16(e + kEntryPositions + 2 * (4 * k + 3), offset(p.place.z));
    }
    if (trace()) {
        static std::map<uint32_t, std::string> last;
        char line[256];
        snprintf(line, sizeof line, "mod chart %u: square %d,%d (sea %d), spot %.0f,%.0f in it; the view: %s %d, centre %.0f,%.0f", chart,
                 p.place.column, p.place.row, int8_t(ld8(p.square + kEntrySea)), p.place.x, p.place.z,
                 p.variant >= 0 ? "the square's picture of salvage variant" : "no picture shows the spot, plain sea;", p.variant, p.origin_x,
                 p.origin_z);
        if (last[chart] != line) LOG("[chart] %s", line), last[chart] = line;
    }
    return e;
}
// the chart view's pictures: only `variant`'s shown (-1: none); what the game set up when the view was made
void show_picture(uint32_t view, int variant) {
    for (uint32_t k = 0; k < 4; k++) {
        const uint32_t group = ld32(view + kTreMapGroups + 4 * k);
        if (!group) continue;
        const uint32_t sentinel = group + kGroupPanes;
        unsigned guard = 0;
        for (uint32_t node = ld32(sentinel); node && node != sentinel && guard < 16; node = ld32(node), guard++)
            if (const uint32_t pane = ld32(node + kNodePane))
                st8(pane + kPaneFlags, uint8_t((ld8(pane + kPaneFlags) & ~kVisible) | (int(k) == variant ? kVisible : 0)));
    }
}
// chart views showing another picture than the game's (a mod chart's): the game's picture, ours
struct ViewPictures {
    int game = 0, shown = 0;
};
std::map<uint32_t, ViewPictures> g_views;
// the caption's message of a chart number (hdMenuMap_c::setChartCaption)
uint32_t caption_message(uint32_t chart) {
    if (chart < 3) return chart + 0x31D8;
    if (chart < 36) return chart + 0x31DD;
    if (chart < 44) return chart + 0x36F1;
    if (chart < 49) return chart + 0x36FD;
    return chart + 0x36EC;
}

Marker* find(const std::string& mod, const std::string& chart, const std::string& id) {
    for (Marker& m : g_markers)
        if (m.mod == mod && m.chart == chart && m.id == id) return &m;
    return nullptr;
}

}  // namespace

// the same 16-bit hash as the letters' keys; kinds keep the two apart
uint16_t chart_key(const std::string& mod, const std::string& chart) { return story_mail::letter_key(mod, chart); }

SquarePlace square_place(float x, float z) {
    // (the sea's very edge, +-3.5 squares, belongs to the outer squares: as the salvage, story_treasure_salvage.cpp)
    const long column = std::clamp(std::lround(x / kSquare), -3L, 3L), row = std::clamp(std::lround(z / kSquare), -3L, 3L);
    return {int(column), int(row), x - float(column) * kSquare, z - float(row) * kSquare};
}
int view_variant(const int16_t origins[4][2], float x, float z) {
    int best = -1;
    float best_off = 0;
    for (int k = 0; k < 4; k++) {
        // the view's own test for Link's icon (hdTreMapScreen_c::updatePosition): inside while within half the view
        const float off = std::max(std::fabs(x - float(origins[k][0])), std::fabs(z - float(origins[k][1])));
        if (off <= 0.5f * kViewSize && (best < 0 || off < best_off)) best = k, best_off = off;
    }
    return best;
}

int32_t give(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint16_t key = chart_key(mod, chart);
    const uint8_t v = chart_value(b, key);
    if (v) return result_of(v);
    if (!b.set(story_mail::kRecordMarker, key, kChartOwned)) {
        LOG("[chart] %s: no room for chart \"%s\" in this save file", mod.c_str(), chart.c_str());
        return kResultNoRoom;
    }
    LOG("[chart] %s: Link got chart \"%s\"", mod.c_str(), chart.c_str());
    return kResultOwned;
}

int32_t finish(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint16_t key = chart_key(mod, chart);
    const uint8_t v = chart_value(b, key);
    if (v == kChartDone) return kResultDone;
    if (v && v != kChartOwned && v != kChartOpened) return kResultInvalid;  // a value this version doesn't know
    if (!b.set(story_mail::kRecordMarker, key, kChartDone)) return kResultNoRoom;
    LOG("[chart] %s: chart \"%s\" finished", mod.c_str(), chart.c_str());
    return kResultDone;
}

int32_t state(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNone;
    return result_of(chart_value(book(), chart_key(mod, chart)));
}

int32_t define(const std::string& mod, const std::string& chart, const std::string& name, const std::string& description, float x, float z,
               const std::string& item_mod, uint8_t item_game) {
    std::u16string caption, ignored;
    if (!story_mail::valid_id(chart) || name.empty() || name.size() > kMaxNameBytes || !story_mail::append_utf16(name, false, caption) ||
        description.size() > kMaxDescriptionBytes || !story_mail::append_utf16(description, true, ignored))
        return kResultInvalid;
    if (!(x >= -kSeaHalf && x <= kSeaHalf && z >= -kSeaHalf && z <= kSeaHalf)) return kResultInvalid;
    std::lock_guard lock(g_mu);
    Def d{mod, chart, name, description, chart_key(mod, chart), x, z, caption, item_mod, item_game};
    for (Def& old : g_defs)
        if (old.mod == mod && old.id == chart) return old = d, kResultNone;
    if (g_defs.size() >= kMaxCharts) return kResultNoRoom;
    g_defs.push_back(d);
    return kResultNone;
}

size_t declared() {
    std::lock_guard lock(g_mu);
    return g_defs.size();
}

std::vector<ChartView> charts() {
    std::vector<ChartView> out;
    story_mail::Book b = have_save() ? book() : story_mail::Book(nullptr);
    std::lock_guard lock(g_mu);
    for (const Def& d : g_defs)
        out.push_back({d.mod, d.id, d.name, d.description, d.x, d.z, b.usable() ? result_of(chart_value(b, d.key)) : kResultNone,
                       b.usable() && chart_value(b, d.key) == kChartOpened, d.item_mod, d.item_game});
    return out;
}

int32_t mark(const std::string& mod, const std::string& chart, const std::string& marker, float x, float z,
             uint32_t icon, const std::string& label) {
    if (!story_mail::valid_id(chart) || !story_mail::valid_id(marker) || icon != kIconStory || !valid_label(label))
        return kResultInvalid;
    if (!(x >= -kSeaHalf && x <= kSeaHalf && z >= -kSeaHalf && z <= kSeaHalf)) return kResultInvalid;  // NaN too
    {
        std::lock_guard lock(g_mu);
        if (Marker* m = find(mod, chart, marker)) {
            m->x = x, m->z = z, m->label = label;
        } else {
            unsigned mine = 0;
            for (const Marker& m : g_markers) mine += m.mod == mod;
            if (mine >= kMaxMarkersPerMod || g_markers.size() >= kMaxMarkers) return kResultNoRoom;
            g_markers.push_back({mod, chart, marker, label, chart_key(mod, chart), x, z, icon});
            if (trace()) LOG("[chart] %s: marker \"%s\" of chart \"%s\" at %.0f, %.0f", mod.c_str(), marker.c_str(), chart.c_str(), x, z);
        }
    }
    return state(mod, chart);
}

int32_t unmark(const std::string& mod, const std::string& chart, const std::string& marker) {
    if (!story_mail::valid_id(chart) || (!marker.empty() && !story_mail::valid_id(marker))) return kResultInvalid;
    {
        std::lock_guard lock(g_mu);
        std::erase_if(g_markers, [&](const Marker& m) { return m.mod == mod && m.chart == chart && (marker.empty() || m.id == marker); });
    }
    return state(mod, chart);
}

void set_tracked_source(TrackedSource source) { g_tracked = source; }

bool publish_world_markers(const std::string& mod,std::span<const WorldMarker> markers){
    if(mod.empty()||mod.size()>128||markers.size()>kMaxMarkersPerMod)return false;
    std::u16string checked;if(!story_mail::append_utf16(mod,false,checked))return false;
    for(size_t i=0;i<markers.size();++i){const auto&m=markers[i];if(!story_mail::valid_id(m.id)||m.label.empty()||!valid_label(m.label)||
        !std::isfinite(m.x)||!std::isfinite(m.z)||m.x<=-kSeaHalf||m.x>=kSeaHalf||m.z<=-kSeaHalf||m.z>=kSeaHalf)return false;
        for(size_t j=0;j<i;++j)if(markers[j].id==m.id)return false;
    }
    std::lock_guard lock(g_mu);
    const size_t others=std::count_if(g_world_markers.begin(),g_world_markers.end(),[&](const auto&m){return m.mod!=mod;});
    if(others+markers.size()>kMaxMarkers)return false;
    std::vector<MarkerView> next;next.reserve(others+markers.size());
    for(const auto&m:g_world_markers)if(m.mod!=mod)next.push_back(m);
    for(const auto&m:markers)next.push_back({mod,"islands",m.id,m.label,m.x,m.z,kIconStory,false});
    g_world_markers.swap(next);if(!markers.empty())g_imported=true;return true;
}
void clear_world_markers(const std::string& mod){publish_world_markers(mod,{});}
std::vector<MarkerView> world_markers(){std::lock_guard lock(g_mu);return g_world_markers;}

std::vector<MarkerView> shown_markers() {
    std::vector<MarkerView> out;
    if (!have_save()) return out;
    if (float x, z; g_tracked) {
        std::string label;
        if (g_tracked(x, z, label)) out.push_back({"", "notebook", "tracked", label, x, z, kIconStory});
    }
    story_mail::Book b = book();
    std::lock_guard lock(g_mu);
    out.insert(out.end(),g_world_markers.begin(),g_world_markers.end());
    for (const Marker& m : g_markers)
        if (chart_value(b, m.key) == kChartDone) out.push_back({m.mod, m.chart, m.id, m.label, m.x, m.z, m.icon, true});
    return out;
}

bool open_in_list(const std::string& mod, const std::string& chart) {
    const uint32_t ui = ld32(kIngameUi);
    const uint32_t coord = ui ? ld32(ui + kUiCoordinator) : 0;
    const uint32_t map = coord ? ld32(coord + kCoordSeaMap) : 0;
    if (!coord || !map || !g_installed) return false;
    size_t def = SIZE_MAX;
    {
        std::lock_guard lock(g_mu);
        for (size_t i = 0; i < g_defs.size(); i++)
            if (g_defs[i].mod == mod && g_defs[i].id == chart) def = i;
    }
    const uint32_t number = kFirstModChart + uint32_t(def);
    if (def == SIZE_MAX || !mod_chart(number)) return false;
    st32(map + kMapSelectedChart, number);
    st32(ui + kUiTransitionTab, kTabMap);
    LOG("[chart] %s: chart \"%s\" selected in the MAP screen's chart list (chart %u)", mod.c_str(), chart.c_str(), number);
    return true;
}

void note_import() { g_imported = true; }
void set_entry_memory(uint32_t guest, uint32_t bytes) { g_entries = bytes >= kMaxCharts * 0x28 ? guest : 0; }
void set_caption_text(CaptionFn fn) { g_caption = fn; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original) return;
    const std::pair<uint32_t, void (*)(Cpu*)> hooks[] = {
        {kProgress, hooks::progress_icons}, {kHidePanes, hooks::hide_panes}, {kDestroy, hooks::destroy},
        {kIconUpdate, hooks::icon_update}, {kMarkLayout, hooks::mark_layout}, {kMarkRefresh, hooks::mark_refresh},
        {kFindChart, hooks::find_chart}, {kIsObtained, hooks::chart_obtained}, {kIsOpened, hooks::chart_opened},
        {kIsComplete, hooks::chart_complete}, {kObtainChart, hooks::obtain_chart}, {kCountObtained, hooks::count_obtained},
        {kMenuFindChart, hooks::menu_find_chart}, {kChartCaption, hooks::chart_caption}, {kChartKind, hooks::chart_kind},
        {kChartIcon, hooks::chart_icon}, {kChartFrame, hooks::chart_frame}, {kChartDetails, hooks::chart_details},
        {kResetSelection, hooks::reset_selection}, {kTreMapDestroy, hooks::view_destroy}};
    // all or nothing: a pane moved without the hook that puts it back would stay moved
    for (const auto& [addr, fn] : hooks) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[chart] %s replaces %08X: mod chart markers are not shown this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : hooks) replace(addr, fn);
    g_installed = true;
    LOG("[chart] the sea chart shows markers of mod charts");
}

// (the circles' animations can't be run here: a circle stays shown at the game's place in the state,
// and is hidden again after the state is loaded, see heal_grid)
void capture_begin() {
    if (!g_installed) return;
    for (const auto& [menu, applied] : g_applied) revert(menu, applied);
    for (const auto& [view, pictures] : g_views) show_picture(view, pictures.game);
    for (const auto& [grid, circles] : g_circles)
        for (const Circle& c : circles)
            if (grid_pane(grid, c.cell) == c.pane) st32(c.pane + kPaneTranslate, c.tx), st32(c.pane + kPaneTranslate + 4, c.ty);
}
void capture_end() {
    if (!g_installed) return;
    for (const auto& [menu, applied] : g_applied)
        for (const Applied& a : applied)
            if (ld32(menu + kMenuPanes + 4 * a.index) == a.pane) show(a);
    for (const auto& [grid, circles] : g_circles)
        for (const Circle& c : circles)
            if (grid_pane(grid, c.cell) == c.pane) st32(c.pane + kPaneTranslate, c.mx), st32(c.pane + kPaneTranslate + 4, c.my);
    for (const auto& [view, pictures] : g_views) show_picture(view, pictures.shown);
}
uint32_t g_chart_menu = 0;  // the sea chart menu (hdSeaChartMenu_c) since it last ran, until destroyed

void state_loaded() {
    g_chart_menu = 0;  // the loaded memory has its own (seen again when it runs)
    g_views.clear();
    g_coordinator = 0;
    g_applied.clear();
    g_borrowed.clear();
    g_circles.clear();
    g_healed.clear();
    g_heal_grids = g_heal_balloons = true;
}

uint32_t sea_chart_menu() { return g_chart_menu; }

namespace hooks {

// hdBtnMapIcon_c::update(): the game places its circle (salvaged treasure) first; while the game doesn't show
// it and the tracked step's spot is in this balloon's square, the circle goes to the spot instead, placed
// with the same mapping the game uses for its own circle
void icon_update(Cpu* c) {
    const uint32_t icon = c->r[3];
    Cpu keep = *c;
    g_original(c, kIconUpdate);
    const uint32_t owner = ld32(icon + kIconOwner), pane = ld32(icon + kIconTreasure), layout = ld32(icon + kPartsLayout);
    const uint32_t info = owner ? ld32(owner + kOwnerInfo) : 0;
    const uint32_t anims = layout ? ld32(layout + kLayoutAnimSet) : 0;
    auto give_back = [&] {
        auto it = g_borrowed.find(icon);
        if (it == g_borrowed.end()) return;
        if (!ld8(icon + kIconFishVisible)) {
            Cpu call = keep;
            g_call(&call, kAnimReset, {it->second, kFishAnim, kFishSlot});
        }
        g_borrowed.erase(it);
        if (trace()) LOG("[chart] balloon %08X: the circle is the game's again", icon);
    };
    // a world position in this balloon: relative to the island's point in its square, then the game's
    // own scales (positionFishInDetail / positionFishOnMap). The square of a position, as the game counts
    // squares: position / 100000, rounded (the sea's centre square is 0,0)
    const uint32_t parent = ld32(icon + kIconParent);
    const uint32_t mode = ld32(icon + kIconMapMode);
    auto place = [&](float x, float z, float& px, float& py) {
        const int cell_x = int(std::lround(x / kSquare)), cell_y = int(std::lround(z / kSquare));
        if (!info || cell_x != int8_t(ld8(info + 4)) || cell_y != int8_t(ld8(info + 5))) return false;
        const float rx = x - float(cell_x) * kSquare - float(int16_t(ld16(info + 6)));
        const float ry = z - float(cell_y) * kSquare - float(int16_t(ld16(info + 8)));
        const float psx = parent ? u32_as_f32(ld32(parent + kPaneScale)) : 1.0f, psy = parent ? u32_as_f32(ld32(parent + kPaneScale + 4)) : 1.0f;
        px = rx * u32_as_f32(ld32(icon + kIconPointScale)) * psx;
        py = -(ry * u32_as_f32(ld32(icon + kIconPointScale + 4))) * psy;
        if (mode == 0 || mode == 4) {  // on the map: plus the balloon's own offset
            px += u32_as_f32(ld32(icon + kIconPosition)) * u32_as_f32(ld32(icon + kIconTranslateScale));
            py -= u32_as_f32(ld32(icon + kIconPosition + 4)) * u32_as_f32(ld32(icon + kIconTranslateScale + 4));
        }
        return true;
    };
    if (trace() && info) {  // the mapping checked against the game's own placement of Link's icon
        static std::map<uint32_t, std::string> last;
        const uint32_t link = ld32(kLinkPtr), primary = ld32(icon + kIconPrimary);
        float lx = 0, ly = 0;
        char line[200] = "";
        if (link && primary && (ld8(primary + kPaneFlags) & kVisible) &&
            place(u32_as_f32(ld32(link + 0x314)), u32_as_f32(ld32(link + 0x31C)), lx, ly))
            snprintf(line, sizeof line, "balloon %08X cell %d,%d mode %u: Link's icon at %.1f,%.1f, the mapping says %.1f,%.1f", icon,
                     int8_t(ld8(info + 4)), int8_t(ld8(info + 5)), mode, u32_as_f32(ld32(primary + kPaneTranslate)),
                     u32_as_f32(ld32(primary + kPaneTranslate + 4)), lx, ly);
        if (*line && last[icon] != line) LOG("[chart] %s", line), last[icon] = line;
    }
    // after a state load: a circle the state was captured with (the game would show it hidden)
    if (g_heal_balloons && anims && std::find(g_healed.begin(), g_healed.end(), icon) == g_healed.end()) {
        g_healed.push_back(icon);
        if (!ld8(icon + kIconFishVisible)) {
            Cpu call = keep;
            g_call(&call, kAnimReset, {anims, kFishAnim, kFishSlot});
        }
    }
    float x = 0, z = 0, px = 0, py = 0;
    std::string label;
    if (!pane || !info || !anims || ld8(icon + kIconFishVisible)) return give_back();
    // the tracked step's spot in this square first, else a finished chart's marker in it
    bool found = g_tracked && g_tracked(x, z, label) && place(x, z, px, py);
    const bool tracked = found;
    bool world=false;
    if(!found)for(const auto&m:world_markers())if(place(m.x,m.z,px,py)){x=m.x;z=m.z;found=world=true;break;}
    if (!found)
        for (const Spot& m : done_spots())
            if (place(m.x, m.z, px, py)) {
                x = m.x, z = m.z, found = true;
                break;
            }
    if (!found) return give_back();
    const int cell_x = int(std::lround(x / kSquare)), cell_y = int(std::lround(z / kSquare));
    st32(pane + kPaneTranslate, f32_as_u32(px));
    st32(pane + kPaneTranslate + 4, f32_as_u32(py));
    st32(pane + kPaneTranslate + 8, 0);
    st8(pane + kPaneFlags, uint8_t(ld8(pane + kPaneFlags) | kMatrixDirty));
    if (trace()) {  // the circle following a moving resident
        static float last_x = 1e9f, last_y = 1e9f;
        if (std::fabs(px - last_x) > 0.5f || std::fabs(py - last_y) > 0.5f)
            LOG("[chart] balloon %08X: the %s circle at %.1f,%.1f (world %.0f,%.0f)", icon, tracked ? "tracked quest's" : world ? "world island's" : "finished chart's", px, py, x,
                z),
                last_x = px, last_y = py;
    }
    if (!g_borrowed.count(icon)) {
        Cpu call = keep;
        g_call(&call, kAnimStop, {anims, kFishAnim, kFishSlot});
        g_borrowed[icon] = anims;
        LOG("[chart] the %s circle is in the zoomed square %d,%d at %.1f,%.1f (mode %u)", tracked ? "tracked quest's" : world ? "world island's" : "finished chart's", cell_x,
            cell_y, px, py, mode);
    }
}

// updateProgressIcons(): the game's story marks, then the mod markers on the panes left free
void progress_icons(Cpu* c) {
    const uint32_t menu = c->r[3];
    g_chart_menu = menu;
    auto it = g_applied.find(menu);
    if (it != g_applied.end()) {
        revert(menu, it->second);
        it->second.clear();
    }
    g_hidden = false;
    g_original(c, kProgress);
    if (!g_hidden) apply(menu);  // busy: the game hid every story mark, the markers stay hidden too
}

// hideMenuPanes(): the screen is busy
void hide_panes(Cpu* c) {
    g_hidden = true;
    g_original(c, kHidePanes);
}

// destroy(flags): forget the menu's panes (and its balloons and grids)
void destroy(Cpu* c) {
    if (c->r[3] == g_chart_menu) g_chart_menu = 0;
    g_applied.erase(c->r[3]);
    g_borrowed.clear();
    g_circles.clear();
    g_original(c, kDestroy);
}

// ---- the MAP screen's chart list: mod charts after the game's (chart numbers 61 and up) ----
// findChart(chart): the mod chart's entry
void find_chart(Cpu* c) {
    g_coordinator = c->r[3];
    size_t def;
    uint8_t value;
    if (mod_chart(asked(c->r[4]), def, value)) {
        if (const uint32_t e = build_entry(c->r[3], asked(c->r[4]), def)) return void(c->r[3] = e);
        return void(c->r[3] = 0);
    }
    if (c->r[4] >= uint32_t(kChartNumbers)) return void(c->r[3] = 0);
    g_original(c, kFindChart);
}
// isChartObtained / isChartOpened / isChartComplete: the mod chart's record
void chart_obtained(Cpu* c) {
    g_coordinator = c->r[3];
    if (mod_chart(asked(c->r[4]))) return void(c->r[3] = 1);
    g_original(c, kIsObtained);
}
void chart_opened(Cpu* c) {
    g_coordinator = c->r[3];
    size_t def;
    uint8_t value;
    if (mod_chart(asked(c->r[4]), def, value)) return void(c->r[3] = value == kChartOpened || value == kChartDone);
    g_original(c, kIsOpened);
}
void chart_complete(Cpu* c) {
    g_coordinator = c->r[3];
    size_t def;
    uint8_t value;
    if (mod_chart(asked(c->r[4]), def, value)) return void(c->r[3] = value == kChartDone);
    g_original(c, kIsComplete);
}
// obtainChart(chart): the player opens the chart in the list (the game's onOpenMap): the record's value
void obtain_chart(Cpu* c) {
    g_coordinator = c->r[3];
    size_t def;
    uint8_t value;
    if (mod_chart(asked(c->r[4]), def, value)) {
        if (value == kChartOwned) {
            std::string mod, id;
            {
                std::lock_guard lock(g_mu);
                mod = g_defs[def].mod, id = g_defs[def].id;
            }
            story_mail::Book b = book();
            b.set(story_mail::kRecordMarker, chart_key(mod, id), kChartOpened);
            LOG("[chart] %s: chart \"%s\" opened in the chart list", mod.c_str(), id.c_str());
        }
        return;
    }
    g_original(c, kObtainChart);
}
// countAllObtainedCharts(): the list's length, with the mod charts
void count_obtained(Cpu* c) {
    g_coordinator = c->r[3];
    g_original(c, kCountObtained);
    c->r[3] += uint32_t(owned_mod_charts().size());
}
// hdMenuMap_c::findChart(chart, step): the game's charts as the game orders them, then the mod charts
void menu_find_chart(Cpu* c) {
    const uint32_t chart = c->r[4];
    const int32_t step = int32_t(c->r[5]);
    const auto mods = owned_mod_charts();
    if (mods.empty() || !g_coordinator || (step != 0 && step != 1 && step != -1)) return g_original(c, kMenuFindChart);
    if (step == 0) {
        if (chart >= kFirstModChart) return void(c->r[3] = std::find(mods.begin(), mods.end(), chart) != mods.end() ? chart : uint32_t(-1));
        return g_original(c, kMenuFindChart);
    }
    std::vector<uint32_t> order;
    for (int n = 0; n < kChartNumbers; n++)
        if (const uint32_t e = chart_entry(g_coordinator, n); e && got_bit(int8_t(ld8(e + kEntrySave)))) order.push_back(uint32_t(n));
    order.insert(order.end(), mods.begin(), mods.end());
    const auto at = std::find(order.begin(), order.end(), chart);
    if (at == order.end()) return void(c->r[3] = step > 0 ? order.front() : order.back());
    const size_t i = size_t(at - order.begin()), n = order.size();
    c->r[3] = order[(i + n + size_t(step > 0 ? 1 : n - 1)) % n];
}
// a list function that stops at the game's last chart, run for a mod chart: with kProxyChart in its
// place for this call only
void proxied(Cpu* c, uint32_t fn, uint32_t chart_reg) {
    const uint32_t chart = c->r[chart_reg];
    if (!mod_chart(chart) || g_proxy.on) return g_original(c, fn);
    g_proxy = {true, chart};
    c->r[chart_reg] = kProxyChart;
    g_original(c, fn);
    g_proxy = {};
}
// setChartCaption(chart): the mod chart's name
void chart_caption(Cpu* c) {
    size_t def;
    uint8_t value;
    if (!mod_chart(c->r[4], def, value) || !g_caption) return g_original(c, kChartCaption);
    std::u16string text;
    {
        std::lock_guard lock(g_mu);
        text = g_defs[def].caption;
    }
    g_caption(caption_message(kProxyChart), &text);
    proxied(c, kChartCaption, 4);
    g_caption(0, nullptr);
}
// hdMenuMapChartKind(map, chart): a mod chart is a treasure chart
void chart_kind(Cpu* c) {
    if (mod_chart(asked(c->r[4]))) return void(c->r[3] = 1);
    g_original(c, kChartKind);
}
void chart_icon(Cpu* c) { proxied(c, kChartIcon, 5); }
void chart_details(Cpu* c) { proxied(c, kChartDetails, 5); }
// the chart view: a mod chart's view shows the picture of its square whose window holds its spot
// (ViewPlan; selectChart takes the picture and the window from the view's variant, so the view has
// that variant for this call), then the view keeps the mod chart's own number (its "got" circle
// follows the record). A game chart's view: the game's picture again.
void chart_frame(Cpu* c) {
    const uint32_t map = c->r[3], slot = c->r[4], chart = c->r[5];
    const uint32_t view = ld32(map + kMapChartIcons + 4 * (slot < 3 ? slot : 0));
    size_t def;
    uint8_t value;
    const bool mod = mod_chart(chart, def, value);
    // the game's picture: the view's variant as the game set it (0xFF before the view is made)
    const auto known = view ? g_views.find(view) : g_views.end();
    const int game = known != g_views.end() ? known->second.game : view ? ld8(view + kTreMapKind) : 0xFF;
    const bool ready = game < 4;
    int picture = game;
    ViewPlan plan;
    if (mod && ready) {
        uint32_t coord = g_coordinator;
        if (const uint32_t ui = coord ? 0 : ld32(kIngameUi)) coord = ld32(ui + kUiCoordinator);
        plan = view_plan(coord, def);
        if (plan.square) picture = plan.variant;
    }
    if (ready && picture != (known != g_views.end() ? known->second.shown : game)) show_picture(view, picture);
    if (ready && picture != game) g_views[view] = {game, picture};
    else if (known != g_views.end()) g_views.erase(known);
    if (ready && picture >= 0) st8(view + kTreMapKind, uint8_t(picture));
    proxied(c, kChartFrame, 5);
    if (ready) st8(view + kTreMapKind, uint8_t(game));
    if (ready && plan.square) {
        // the window's centre exactly: the entry's s16 origin can't hold a spot farther than 32767 from
        // its square's centre (no picture's window reaches there: the plain sea's window, centred on it)
        st32(view + kTreMapOrigin, f32_as_u32(float(plan.place.column) * kSquare + plan.origin_x));
        st32(view + kTreMapOrigin + 4, f32_as_u32(float(plan.place.row) * kSquare + plan.origin_z));
    }
    if (mod && view) st32(view + kTreMapChartIndex, chart);
}
// hdTreMapScreen_c::destroy(flags): the view is gone
void view_destroy(Cpu* c) {
    g_views.erase(c->r[3]);
    g_original(c, kTreMapDestroy);
}
// resetChartSelection(): the game shows its selected chart only below its last one: a mod chart too
void reset_selection(Cpu* c) {
    const uint32_t map = c->r[3];
    Cpu keep = *c;
    g_original(c, kResetSelection);
    const uint32_t chart = ld32(map + kMapSelectedChart);
    if (!mod_chart(chart)) return;
    for (const uint32_t fn : {kChartIcon, kChartFrame}) {
        Cpu call = keep;
        g_call(&call, fn, {map, 0, chart});
    }
    Cpu call = keep;
    g_call(&call, kChartCaption, {map, chart});
}

// updateChartMarkLayout(): the game puts every circle in its place and shows or hides it (complete or
// not); the finished charts' circles are placed again by the next refreshChartMarks
void mark_layout(Cpu* c) {
    const uint32_t grid = c->r[3];
    g_circles.erase(grid);
    g_original(c, kMarkLayout);
    if (trace()) {  // the mapping checked against the game's own placement of a square's circle
        const uint32_t touch = ld32(grid + kGridTouchPane);
        for (unsigned i = 0; touch && i < kCells; i++) {
            const uint32_t cell = ld32(grid + kGridCells + 4 * i), pane = grid_pane(grid, i);
            if (!cell || !pane || (!int8_t(ld8(cell + kCellCoord)) && !int8_t(ld8(cell + kCellCoord + 1)))) continue;
            const float x = float(int8_t(ld8(cell + kCellCoord))) * kSquare + float(int16_t(ld16(cell + kCellResource)));
            const float z = float(int8_t(ld8(cell + kCellCoord + 1))) * kSquare + float(int16_t(ld16(cell + kCellResource + 2)));
            LOG("[chart] grid %08X square %d,%d: the game's circle at %.1f,%.1f, the mapping says %.1f,%.1f (entry %u slot %u)", grid,
                int8_t(ld8(cell + kCellCoord)), int8_t(ld8(cell + kCellCoord + 1)), u32_as_f32(ld32(pane + kPaneTranslate)),
                u32_as_f32(ld32(pane + kPaneTranslate + 4)),
                u32_as_f32(ld32(touch + kPaneTranslate)) + x / kSquare * u32_as_f32(ld32(touch + kPaneSize)) / kSquaresPerSide,
                u32_as_f32(ld32(touch + kPaneTranslate + 4)) - z / kSquare * u32_as_f32(ld32(touch + kPaneSize + 4)) / kSquaresPerSide,
                ld32(cell + kCellMarkEntry), ld32(cell + kCellMarkSlot));
            break;
        }
    }
}

// refreshChartMarks() (every update): the game's circles, then the finished charts' circles
void mark_refresh(Cpu* c) {
    const uint32_t grid = c->r[3];
    Cpu keep = *c;
    g_original(c, kMarkRefresh);
    if (g_heal_grids && std::find(g_healed.begin(), g_healed.end(), grid) == g_healed.end()) {
        g_healed.push_back(grid);  // after a state load: hide the circles the game would not show
        for (unsigned i = 0; i < kCells; i++)
            if (const uint32_t cell = ld32(grid + kGridCells + 4 * i); cell && !ld8(cell + kCellMarkActive)) cell_circle(keep, cell, false);
    }
    place_circles(keep, grid);
}

}  // namespace hooks
}  // namespace story_chart
