// Mod charts: a story mod gives Link a chart, and the chart's markers appear on Link's sea chart in
// the game's own style (docs/mod-sdk-v2.md, "Charts and markers").
//
// - Whether Link owns a mod chart is kept in the save data, in the story-mod record shared with the
//   letters (story_mail_book.h): one record of kind kRecordMarker (3) per chart, key chart_key(),
//   value kChartOwned (1) or kChartDone (2). Marker places are not saved (the mod places them).
// - Markers are session state of the mods: a mod places them (wwhd_chart_mark). Like the game's
//   treasure charts, the sea chart shows nothing for them while the chart is only owned; once the
//   chart is finished, the game's red hand-drawn circle (the mark of a salvaged treasure) marks each
//   marker's spot, on the chart and in the zoomed square. See story_chart.cpp.
// - The notebook's tracked quest has the game's white story ring (the mark of the next story island)
//   on its step's place.
//
// Nothing of this runs unless a loaded guest mod imports a wwhd_chart_* service: only then are the
// sea chart hooks installed (install()).
#pragma once
#include <initializer_list>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <span>

struct Cpu;

namespace story_chart {

// Results of the guest services (wwhd_guest.h: WWHD_CHART_*)
inline constexpr int32_t kResultNone = 0, kResultOwned = 1, kResultDone = 2;
inline constexpr int32_t kResultInvalid = -1, kResultNoRoom = -2;
inline constexpr uint32_t kIconStory = 0;
// the chart's record in the story-mod book: kind story_mail::kRecordMarker, this key, these values
inline constexpr uint8_t kChartOwned = 1, kChartDone = 2, kChartOpened = 3;  // 3: owned, opened in the chart list
uint16_t chart_key(const std::string& mod, const std::string& chart);
inline constexpr size_t kMaxIdBytes = 32, kMaxLabelBytes = 64;
inline constexpr unsigned kMaxMarkersPerMod = 8, kMaxMarkers = 32;
inline constexpr size_t kMaxNameBytes = 40, kMaxDescriptionBytes = 200;
inline constexpr unsigned kMaxCharts = 32;
// The Great Sea: 7 x 7 squares of 100000 units, square D4's center at (0, 0), x east, z south.
inline constexpr float kSquare = 100000.0f, kSeaHalf = 3.5f * kSquare;
// A point's square (column and row -3..3, from square D4) and its place from the square's centre
struct SquarePlace {
    int column = 0, row = 0;
    float x = 0, z = 0;
};
SquarePlace square_place(float x, float z);
// The chart view of a treasure chart (the MAP screen's chart list) is not the whole square: it shows a
// window of kViewSize x kViewSize units, centred on an origin in the square. The game has four pictures
// of each square, one per salvage variant, each the window around that variant's origin (cmapdat.bin,
// the chart's entry: {origin x, origin z, salvage x, salvage z} per variant, from the square's centre);
// the game's chart shows the picture of the save's variant, and its salvage spot is always in it.
// A mod chart's spot can be anywhere: its view shows the picture whose window holds the spot (the most
// central one), centred on that picture's origin; -1 when none does: no picture, the window centred on
// the spot (plain sea).
inline constexpr float kViewSize = 10000.0f;
int view_variant(const int16_t origins[4][2], float x, float z);

// wwhd_chart_give / wwhd_chart_finish / wwhd_chart_state: the chart's state in the current save file
int32_t give(const std::string& mod, const std::string& chart);
int32_t finish(const std::string& mod, const std::string& chart);
int32_t state(const std::string& mod, const std::string& chart);
// wwhd_chart_mark: adds the marker or moves it (and changes its label); wwhd_chart_unmark removes
// one marker, or all markers of the chart when marker is empty. Results: the chart's state, or < 0.
int32_t mark(const std::string& mod, const std::string& chart, const std::string& marker, float x, float z,
             uint32_t icon, const std::string& label);
int32_t unmark(const std::string& mod, const std::string& chart, const std::string& marker);
// A declared chart (manifest "charts"): its name and description and the place its X marks. While Link
// owns it, the MAP screen's chart list shows it after the game's charts (its own chart number, 61 and
// up), with the square's art and the X at the place (story_chart.cpp).
// Declaring again replaces it. Results: kResultNone, kResultInvalid, kResultNoRoom.
// item_mod / item_game: what its treasure chest holds (manifest "item"; story_treasure.h)
int32_t define(const std::string& mod, const std::string& chart, const std::string& name, const std::string& description, float x, float z,
               const std::string& item_mod = {}, uint8_t item_game = 0);
struct ChartView {
    std::string mod, chart, name, description;
    float x, z;
    int32_t state;  // kResultNone / kResultOwned / kResultDone in the loaded save file
    bool opened;    // owned and opened in the chart list
    std::string item_mod;   // its treasure chest: one of the mod's items (manifest "item": "item:<id>"), or ...
    uint8_t item_game = 0;  // ... a game item ("game:<name>", story_manifest.h chart_game_item); both empty: an orange rupee
};
std::vector<ChartView> charts();  // the declared charts, in declaration order
size_t declared();                // how many charts are declared (no save data read: safe at startup)
// The in-game menu switches to the MAP screen with that chart selected in its chart list (game thread;
// false when Link doesn't own it or there is no in-game menu).
bool open_in_list(const std::string& mod, const std::string& chart);

// What the sea chart shows now: the tracked quest's step (done false), then the markers of finished
// charts (done true: the red circle), in the order they were placed. For other mods that draw a map
// of their own (a minimap, a quest notebook).
struct MarkerView {
    std::string mod, chart, marker, label;
    float x, z;
    uint32_t icon;
    bool done = false;
};
std::vector<MarkerView> shown_markers();

// Immutable world-island declarations, published independently of treasure ownership/completion.
// Atomically replaces this mod's set; IDs are unique, 8/mod and 32 total. Invalid input leaves it intact.
// Publish before chart hook installation (publication requests the hooks), then refresh eligibility
// on the game thread as owned mod conditions change. Full-state pane transients are reset separately.
struct WorldMarker {std::string id,label;float x=0,z=0;};
bool publish_world_markers(const std::string& mod,std::span<const WorldMarker> markers);
void clear_world_markers(const std::string& mod);
std::vector<MarkerView> world_markers();

// The story notebook's tracked quest (story_notebook.h): its next step has the white ring, whether
// or not a chart is owned (and in the zoomed square the red circle, before finished charts' markers). The source returns false when there is
// nothing to show (no tracked quest, its step has no place, it is done). Game thread.
using TrackedSource = bool (*)(float& x, float& z, std::string& label);
void set_tracked_source(TrackedSource source);

void note_import();  // a module imports a chart service: the sea chart hooks are needed
bool imported();
// After all guest modules are loaded. replaced_by(addr) names a mod that replaces that game function
// (empty: none); replace(addr, fn) installs a host replacement. Nothing is installed on a conflict.
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// guest memory for the mod charts' entries (kMaxCharts x 0x28 bytes), and the chart list's captions: fn(number,
// text) arms a message's replacement text, fn(0, nullptr) ends it (story_mail::caption)
void set_entry_memory(uint32_t guest, uint32_t bytes);
using CaptionFn = void (*)(uint32_t number, const std::u16string* text);
void set_caption_text(CaptionFn fn);

// Full save states: the game's own pane state is what a state holds. capture_begin() puts the panes
// the markers use back as the game left them (all guest threads are stopped), capture_end() shows the
// markers again. state_loaded(): guest memory was replaced, nothing of ours is in it.
void capture_begin();
void capture_end();
void state_loaded();

// The hooks, exposed for runtime/tools/story_chart_test.cpp
namespace hooks {
void progress_icons(Cpu* c);  // hdSeaChartMenu_c::updateProgressIcons
void icon_update(Cpu* c);     // hdBtnMapIcon_c::update (the zoomed square balloon)
void hide_panes(Cpu* c);      // hdSeaChartMenu_c::hideMenuPanes
void destroy(Cpu* c);         // hdSeaChartMenu_c::destroy
void mark_layout(Cpu* c);     // hdSeaChartGrid_c::updateChartMarkLayout
void mark_refresh(Cpu* c);    // hdSeaChartGrid_c::refreshChartMarks
void find_chart(Cpu* c);      // hdMapDisplayCoordinator_c::findChart
void chart_obtained(Cpu* c);  // ::isChartObtained
void chart_opened(Cpu* c);    // ::isChartOpened
void chart_complete(Cpu* c);  // ::isChartComplete
void obtain_chart(Cpu* c);    // ::obtainChart
void count_obtained(Cpu* c);  // ::countAllObtainedCharts
void menu_find_chart(Cpu* c); // hdMenuMap_c::findChart
void chart_caption(Cpu* c);   // hdMenuMap_c::setChartCaption
void chart_kind(Cpu* c);      // hdMenuMapChartKind
void chart_icon(Cpu* c);      // hdMenuMapUpdateChartIcon
void chart_frame(Cpu* c);     // hdMenuMapUpdateChartFrame
void view_destroy(Cpu* c);    // hdTreMapScreen_c::destroy (the chart view)
void chart_details(Cpu* c);   // hdMenuMapUpdateChartDetails
void reset_selection(Cpu* c); // hdMenuMap_c::resetChartSelection
}  // namespace hooks

// The game's sea chart menu (hdSeaChartMenu_c) once it has been shown this session (it stays alive
// with the pause menus), 0 before that or after it was destroyed. Read-only users: the story
// notebook's mini chart takes the chart's own icons from its layout.
uint32_t sea_chart_menu();

}  // namespace story_chart
