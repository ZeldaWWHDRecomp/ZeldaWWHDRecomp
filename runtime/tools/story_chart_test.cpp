#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
// Mod charts and their sea chart markers (runtime/src/mods/story_chart.*): the shared save record, the
// services and the sea chart menu hooks on a synthetic menu with synthetic panes and save. The game's
// own functions are stubbed; no game files are needed.
#include "mods/story_chart.h"
#include "mods/story_mail_book.h"
#include "mods/story_store.h"
#include "runtime.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_chart;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

// synthetic guest memory: the save pointer, the save, a sea chart menu and its panes
constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000;
constexpr uint32_t kSave = kArena, kMenu = kArena + 0x2000, kMain = kArena + 0x3000, kPanes = kArena + 0x4000;
constexpr uint32_t kPaneStride = 0x100;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

uint32_t pane(unsigned i) { return kPanes + i * kPaneStride; }
float tx(unsigned i) { return u32_as_f32(ld32(pane(i) + 0x1C)); }
float ty(unsigned i) { return u32_as_f32(ld32(pane(i) + 0x20)); }
bool visible(unsigned i) { return ld8(pane(i) + 0x44) & 1; }
bool dirty(unsigned i) { return ld8(pane(i) + 0x44) & 0x10; }
void clear_dirty() {
    for (unsigned i = 0; i < 8; i++) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~0x10);
}
// the layout's places of the story marks (made up)
float home_x(unsigned i) { return 10.0f * float(i + 1); }
float home_y(unsigned i) { return -5.0f * float(i + 1); }
void reset_pane(unsigned i) {  // the pane as the layout made it
    st32(pane(i) + 0x1C, f32_as_u32(home_x(i)));
    st32(pane(i) + 0x20, f32_as_u32(home_y(i)));
    st8(pane(i) + 0x44, 0x02);
}
void build_menu() {
    for (unsigned i = 0; i < 8; i++) {
        st32(kMenu + 0x50 + 4 * i, pane(i));
        st32(pane(i) + 0x1C, f32_as_u32(home_x(i)));
        st32(pane(i) + 0x20, f32_as_u32(home_y(i)));
        st8(pane(i) + 0x44, 0x02);  // hidden, "influenced alpha" as the game's
    }
    st32(kMenu + 0x70, kMain);
    st32(kMain + 0x1C, f32_as_u32(71.0f));    // the chart's center (square D4)
    st32(kMain + 0x20, f32_as_u32(-44.0f));
    st32(kMain + 0x3C, f32_as_u32(511.0f));   // 7 squares of 73
    st32(kMain + 0x40, f32_as_u32(511.0f));
}

// the game's code
std::vector<unsigned> game_shows;  // story marks the game's update shows
bool game_busy = false;
std::vector<std::string> calls;
std::map<uint32_t, void (*)(Cpu*)> hooked;
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x02678B74:  // updateProgressIcons: busy -> hideMenuPanes, else the story's marks
        assert(c->r[3] == kMenu);
        // the game always finds its panes in place: a pane is either the game's (shown) or hidden
        for (unsigned i = 0; i < 8; i++) assert(tx(i) == home_x(i) && ty(i) == home_y(i));
        calls.push_back("progress");
        if (game_busy) {
            hooked.at(0x02678B00)(c);
            break;
        }
        for (unsigned i : game_shows) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) | 1);
        break;
    case 0x02678B00:
        calls.push_back("hide");
        for (unsigned i = 0; i < 8; i++) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~1);
        break;
    case 0x02677B90: calls.push_back("destroy"); break;
    case 0x026396FC: calls.push_back("balloon"); break;  // hdBtnMapIcon_c::update: the game placed its circle
    case 0x026815A8: case 0x026834A4: break;  // the sea chart grid (test_circles has its own stand-in)
    default: assert(!"unexpected original");
    }
}
uint32_t game_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    assert(a.size() == 3 && a[1] == 13 && a[2] == 7);
    if (fn == 0x02005708) calls.push_back("circle shown");
    else if (fn == 0x02005674) calls.push_back("circle hidden");
    else assert(!"unexpected game call");
    return 0;
}
void update() {
    Cpu c{};
    c.r[3] = kMenu;
    hooked.at(0x02678B74)(&c);
}
// a story mark of the game hides when its step is done (the game's own visibility writes)
void game_hides(unsigned i) {
    std::erase(game_shows, i);
    st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~1);
}
bool close_to(float a, float b) { return std::fabs(a - b) < 0.01f; }

void test_record() {
    uint8_t raw[story_mail::kBookSize] = {};
    story_mail::Book b(raw);
    const uint16_t chart = chart_key("mod", "tale"), letter = story_mail::letter_key("mod", "tale");
    assert(chart && chart_key("mod", "tale") == chart && chart_key("other", "tale") != chart);
    // letters and charts share the book; the record kind keeps them apart (same key, different kinds)
    assert(letter == chart);
    assert(b.set_state(letter, story_mail::kWaiting) && b.set(story_mail::kRecordMarker, chart, kChartOwned));
    assert(b.state(letter) == story_mail::kWaiting && b.get(story_mail::kRecordMarker, chart) == kChartOwned);
    assert(raw[0] == story_mail::kMagic && raw[1] == 2);
    // second record: kind 3, the key big-endian, value 1
    assert(raw[8] == 3 && raw[9] == uint8_t(chart >> 8) && raw[10] == uint8_t(chart) && raw[11] == 1);
    assert(b.set(story_mail::kRecordMarker, chart, kChartDone) && raw[11] == 2 && b.state(letter) == story_mail::kWaiting);
}

void test_services() {
    assert(state("mod", "tale") == kResultNone && give("mod", "tale") == kResultNoRoom);  // no save loaded
    st32(0x101F84DC, kSave);
    assert(give("mod", "Tale") == kResultInvalid && give("mod", "") == kResultInvalid);
    assert(give("mod", "tale") == kResultOwned && give("mod", "tale") == kResultOwned);  // idempotent
    assert(state("mod", "tale") == kResultOwned && state("other", "tale") == kResultNone);
    story_mail::Book b = story_store::book();
    assert(b.get(story_mail::kRecordMarker, chart_key("mod", "tale")) == kChartOwned);
    // markers: checked arguments, add or move, per-mod limit
    assert(mark("mod", "tale", "start", 0, -200000, kIconStory, "Windfall") == kResultOwned);
    assert(mark("mod", "tale", "Start", 0, 0, kIconStory, "") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, 7, "") == kResultInvalid);                    // unknown icon
    assert(mark("mod", "tale", "x", 400000, 0, kIconStory, "") == kResultInvalid);      // off the sea
    assert(mark("mod", "tale", "x", NAN, 0, kIconStory, "") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, kIconStory, "two\nlines") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, kIconStory, std::string(kMaxLabelBytes + 1, 'a')) == kResultInvalid);
    assert(mark("mod", "later", "x", 100000, 0, kIconStory, "") == kResultNone);  // a chart not given yet
    for (unsigned i = 2; i < kMaxMarkersPerMod; i++)
        assert(mark("mod", "later", "m" + std::to_string(i), 0, 0, kIconStory, "") == kResultNone);
    assert(mark("mod", "later", "one-more", 0, 0, kIconStory, "") == kResultNoRoom);
    assert(mark("mod", "tale", "start", 0, -200000, kIconStory, "Windfall Island") == kResultOwned);  // a move is no new marker
    // like the game's treasure charts: nothing shows while the chart is only owned
    assert(shown_markers().empty());
    assert(unmark("mod", "later", "") == kResultNone);  // all of a chart
    assert(unmark("mod", "tale", "nothing") == kResultOwned);
}

// the notebook's tracked quest (its step's place), the source of the white ring and the balloon's circle
bool tracked_on = false;
float tracked_x = 0.0f, tracked_z = -200000.0f;
bool tracked_source(float& x, float& z, std::string& label) {
    if (!tracked_on) return false;
    x = tracked_x, z = tracked_z, label = "Quest";
    return true;
}

void test_hooks() {
    // install: all or nothing
    set_game_calls(original, game_call);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x02678B00 ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 20);

    build_menu();
    set_tracked_source(tracked_source);
    // an owned chart's marker: no ring (the game shows nothing for an owned treasure chart)
    game_shows = {6};
    update();
    for (unsigned i = 0; i < 8; i++) assert(i == 6 || !visible(i));
    // the tracked quest's ring: the first pane the game left hidden
    tracked_on = true;
    update();
    assert(visible(6) && close_to(tx(6), home_x(6)));
    assert(visible(0) && dirty(0) && close_to(tx(0), 71.0f) && close_to(ty(0), -44.0f + 2 * 73.0f));  // square D2
    for (unsigned i = 1; i < 8; i++) assert(i == 6 || !visible(i));
    // every update puts the pane back first (checked in the stub), then shows the ring again
    clear_dirty();
    update();
    assert(visible(0) && dirty(0) && close_to(ty(0), 102.0f));
    // the game's own mark wins: the story now asks for pane 0
    game_shows = {0, 6};
    update();
    assert(visible(0) && close_to(tx(0), home_x(0)) && visible(1) && close_to(tx(1), 71.0f) && close_to(ty(1), 102.0f));
    game_hides(0);
    game_hides(6);
    // the ring moves with the step
    tracked_x = 100000, tracked_z = 0;
    update();
    assert(close_to(tx(0), 71.0f + 73.0f) && close_to(ty(0), -44.0f) && visible(0) && !visible(1));
    // busy screen: the game hides its marks and so does the ring
    game_busy = true;
    update();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i) && close_to(tx(i), home_x(i)));
    game_busy = false;
    update();
    assert(visible(0));
    // every pane the game's: the ring waits
    game_shows = {0, 1, 2, 3, 4, 5, 6, 7};
    update();
    for (unsigned i = 0; i < 8; i++) assert(close_to(tx(i), home_x(i)));
    for (unsigned i = 0; i < 8; i++) game_hides(i);
    // a full state is captured with the panes as the game left them
    update();
    capture_begin();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i) && close_to(tx(i), home_x(i)));
    capture_end();
    assert(visible(0) && close_to(tx(0), 71.0f + 73.0f));
    // a state load replaces guest memory: nothing is put back into it
    const uint32_t moved = ld32(pane(0) + 0x1C);
    state_loaded();
    capture_begin();
    assert(ld32(pane(0) + 0x1C) == moved);  // untouched: not ours any more
    reset_pane(0);  // (the loaded state's own pane)
    update();
    assert(visible(0) && close_to(tx(0), 71.0f + 73.0f));
    // a destroyed menu: its panes are forgotten
    Cpu c{};
    c.r[3] = kMenu;
    calls.clear();
    hooked.at(0x02677B90)(&c);
    assert(calls == std::vector<std::string>{"destroy"});
    capture_begin();
    assert(visible(0));  // nothing written into the freed menu
    // untracked: no ring
    reset_pane(0);
    tracked_on = false;
    update();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i));
    set_tracked_source(nullptr);
}

void test_world_markers(){
    const std::vector<uint8_t> save(mem::ptr(kSave),mem::ptr(kSave)+0x1000);
    const WorldMarker island{"island","Mod Island",100000,0};
    assert(publish_world_markers("world",{&island,1})&&world_markers().size()==1);
    auto shown=shown_markers();assert(std::any_of(shown.begin(),shown.end(),[](const auto&m){return m.mod=="world"&&!m.done&&m.label=="Mod Island";}));
    auto bad=island;bad.x=NAN;assert(!publish_world_markers("world",{&bad,1})&&world_markers().size()==1);bad=island;bad.label="bad\nlabel";assert(!publish_world_markers("world",{&bad,1}));
    const WorldMarker duplicates[]={island,island};assert(!publish_world_markers("world",duplicates));
    build_menu();game_shows={6};update();assert(visible(0)&&visible(6)&&close_to(tx(0),144)&&close_to(ty(0),-44));
    capture_begin();assert(!visible(0)&&close_to(tx(0),home_x(0)));capture_end();assert(visible(0)&&close_to(tx(0),144));
    // Pane transients disappear on load; immutable declarations remain available to republish.
    state_loaded();build_menu();update();assert(visible(0)&&world_markers().size()==1);
    clear_world_markers("world");update();assert(!visible(0));game_hides(6);
    std::vector<WorldMarker> many;for(unsigned i=0;i<8;++i)many.push_back({"i"+std::to_string(i),"Island",float(i),0});
    for(unsigned i=0;i<4;++i)assert(publish_world_markers("mod"+std::to_string(i),many));assert(world_markers().size()==32);assert(!publish_world_markers("overflow",{&island,1}));many.push_back(island);assert(!publish_world_markers("mod0",many)&&world_markers().size()==32);
    for(unsigned i=0;i<4;++i)clear_world_markers("mod"+std::to_string(i));assert(world_markers().empty());
    assert(std::memcmp(mem::ptr(kSave),save.data(),save.size())==0); // no treasure or retail progression writes
}

// ---- finished charts: the game's red circle (a salvaged treasure's mark) on the sea chart grid ----
constexpr uint32_t kGrid = kArena + 0x8000, kOwner = kArena + 0x8400, kTouch = kArena + 0x8600, kCells0 = kArena + 0x9000,
                   kCirclePanes = kArena + 0xA000, kCellAnims = kArena + 0x8800;
constexpr uint32_t kCellStride = 0x40, kCirclePaneStride = 0x50;
uint32_t cell(unsigned i) { return kCells0 + i * kCellStride; }
uint32_t circle(unsigned i) { return kCirclePanes + i * kCirclePaneStride; }
float cx(unsigned i) { return u32_as_f32(ld32(circle(i) + 0x1C)); }
float cy(unsigned i) { return u32_as_f32(ld32(circle(i) + 0x20)); }
bool complete[49];
std::vector<std::string> circle_calls;  // "show 12" / "hide 12": the cells' animations run
// the game's placement of square i's circle (made up salvage points)
float game_x(unsigned i) { return 1000.0f + float(i); }
float game_y(unsigned i) { return 2000.0f + float(i); }
void game_layout() {  // updateChartMarkLayout: every circle in place, shown if complete, else hidden
    for (unsigned i = 0; i < 49; i++) {
        st32(circle(i) + 0x1C, f32_as_u32(game_x(i)));
        st32(circle(i) + 0x20, f32_as_u32(game_y(i)));
        st8(cell(i) + 0x10, complete[i] ? 1 : 0);
        st8(circle(i) + 0x44, complete[i] ? 1 : 0);  // (the stand-in for the animation's visibility)
    }
}
void build_grid() {
    for (unsigned i = 0; i < 49; i++) {
        st32(kGrid + 4 * i, cell(i));
        st8(cell(i) + 4, uint8_t(int8_t(int(i % 7) - 3)));  // the square: x -3..3, y -3..3
        st8(cell(i) + 5, uint8_t(int8_t(int(i / 7) - 3)));
        st32(cell(i) + 0x54, kCellAnims);
        st32(cell(i) + 0x5C, 49 + i);  // its TreasureGet animation entry and slot
        st32(cell(i) + 0x6C, 50 + i);
        st32(kOwner + 0x84 + 4 * i, circle(i));
    }
    st32(kGrid + 0x10C, kOwner);
    st32(kGrid + 0xC8, kTouch);
    st32(kTouch + 0x1C, f32_as_u32(10.0f)), st32(kTouch + 0x20, f32_as_u32(20.0f));
    st32(kTouch + 0x3C, f32_as_u32(700.0f)), st32(kTouch + 0x40, f32_as_u32(350.0f));  // squares of 100 x 50
}
uint32_t grid_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    assert(a.size() == 3);
    if (a[0] == kCellAnims) {  // a cell's circle
        const unsigned i = a[1] - 49;
        assert(i < 49 && a[2] == 50 + i);
        if (fn == 0x02005708) circle_calls.push_back("show " + std::to_string(i)), st8(circle(i) + 0x44, 1);
        else if (fn == 0x02005674) circle_calls.push_back("hide " + std::to_string(i)), st8(circle(i) + 0x44, 0);
        else assert(!"unexpected game call");
        return 0;
    }
    return game_call(nullptr, fn, args);
}
void grid_original(Cpu* c, uint32_t fn) {
    if (fn == 0x026815A8) {
        assert(c->r[3] == kGrid);
        game_layout();
    } else if (fn == 0x026834A4) {  // refreshChartMarks: a square salvaged since shows its circle
        assert(c->r[3] == kGrid);
        for (unsigned i = 0; i < 49; i++)
            if (complete[i] && !ld8(cell(i) + 0x10)) {
                st8(cell(i) + 0x10, 1), st8(circle(i) + 0x44, 1);
                circle_calls.push_back("game shows " + std::to_string(i));
            }
    } else original(c, fn);
}
void grid_hook(uint32_t fn) {
    Cpu c{};
    c.r[3] = kGrid;
    hooked.at(fn)(&c);
}
unsigned shown_circles() {
    unsigned n = 0;
    for (unsigned i = 0; i < 49; i++) n += ld8(circle(i) + 0x44) & 1;
    return n;
}
void test_circles() {
    set_game_calls(grid_original, grid_call);
    build_grid();
    complete[3 * 7 + 4] = true;  // the game's own: square 1,0 salvaged
    grid_hook(0x026815A8);
    grid_hook(0x026834A4);
    assert(shown_circles() == 1);
    // an owned chart's marker in square 0,-2: nothing
    assert(give("map", "old") == kResultOwned && mark("map", "old", "spot", 1547, -202539, kIconStory, "Spot") == kResultOwned);
    circle_calls.clear();
    grid_hook(0x026834A4);
    assert(circle_calls.empty() && shown_circles() == 1);
    // finished: the red circle of its own square (cell 0,-2 = 1 * 7 + 3), at the spot
    assert(finish("map", "old") == kResultDone);
    auto shown = shown_markers();
    assert(shown.size() == 1 && shown[0].done && shown[0].marker == "spot");
    grid_hook(0x026834A4);
    const unsigned own = 1 * 7 + 3;
    assert((circle_calls == std::vector<std::string>{"show " + std::to_string(own)}));
    // x: 10 + 1547 / 100000 * 100 = 11.547; y: 20 - (-202539 / 100000) * 50 = 121.2695
    assert(close_to(cx(own), 11.547f) && close_to(cy(own), 121.2695f) && shown_circles() == 2);
    // every update: placed, shown once
    circle_calls.clear();
    grid_hook(0x026834A4);
    assert(circle_calls.empty() && close_to(cx(own), 11.547f));
    // the game's layout pass resets every circle; the next refresh shows the marker's again
    grid_hook(0x026815A8);
    assert(close_to(cx(own), game_x(own)) && shown_circles() == 1);
    grid_hook(0x026834A4);
    assert(close_to(cx(own), 11.547f) && shown_circles() == 2);
    // the game wants that square's circle (its treasure salvaged): the game's, at its place; the marker
    // takes another square's circle
    complete[own] = true;
    circle_calls.clear();
    grid_hook(0x026834A4);
    assert(close_to(cx(own), game_x(own)) && close_to(cy(own), game_y(own)) && ld8(circle(own) + 0x44));
    assert((circle_calls == std::vector<std::string>{"game shows " + std::to_string(own), "show 0"}));
    assert(close_to(cx(0), 11.547f) && shown_circles() == 3);
    complete[own] = false;
    grid_hook(0x026815A8);
    grid_hook(0x026834A4);
    assert(close_to(cx(own), 11.547f) && close_to(cx(0), game_x(0)));
    // a full state: the circles at the game's places (their visibility is healed after a load)
    capture_begin();
    assert(close_to(cx(own), game_x(own)) && close_to(cy(own), game_y(own)));
    capture_end();
    assert(close_to(cx(own), 11.547f));
    state_loaded();
    st32(circle(own) + 0x1C, f32_as_u32(game_x(own)));  // the state as captured: the circle shown at the game's place
    circle_calls.clear();
    unmark("map", "old", "spot");
    grid_hook(0x026834A4);
    unsigned hides = 0;
    for (const std::string& s : circle_calls) hides += s.rfind("hide ", 0) == 0;
    assert(hides == 49 - 1 && shown_circles() == 1);  // only the game's salvaged square
    // a removed marker gives its circle back
    assert(mark("map", "old", "spot", 1547, -202539, kIconStory, "Spot") == kResultDone);
    grid_hook(0x026834A4);
    assert(shown_circles() == 2);
    circle_calls.clear();
    unmark("map", "old", "");
    grid_hook(0x026834A4);
    assert((circle_calls == std::vector<std::string>{"hide " + std::to_string(own)}) && close_to(cx(own), game_x(own)) && shown_circles() == 1);
    set_game_calls(original, game_call);
}

}  // namespace

// the zoomed square balloon: the tracked step, else a finished chart's marker in that square, borrows the
// game's red circle while the game doesn't show it, at the spot, with the game's mapping; it gives it back
void test_balloon() {
    constexpr uint32_t kIcon = kArena + 0x6000, kOwner = kArena + 0x6400, kInfo = kArena + 0x6800, kCircle = kArena + 0x6C00,
                       kParent = kArena + 0x7000, kLayout = kArena + 0x7400, kAnims = kArena + 0x7800;
    st32(kIcon + 0x164, kOwner), st32(kOwner + 0x118, kInfo);
    st8(kInfo + 4, 0), st8(kInfo + 5, uint8_t(-2));         // Windfall's square: cell 0,-2
    st16(kInfo + 6, uint16_t(int16_t(-500))), st16(kInfo + 8, 1000);  // the island's point in it
    st32(kIcon + 0xA8, kCircle), st32(kIcon + 0x94, kParent), st32(kIcon + 0x44, kLayout), st32(kLayout + 0xD4, kAnims);
    st32(kParent + 0x34, f32_as_u32(2.0f)), st32(kParent + 0x38, f32_as_u32(2.0f));
    st32(kIcon + 0x150, f32_as_u32(0.01f)), st32(kIcon + 0x154, f32_as_u32(0.01f));
    st32(kIcon + 0x144, 1);  // zoomed in (detail)
    tracked_on = true, tracked_x = 1547.0f, tracked_z = -202539.0f;
    set_tracked_source(tracked_source);
    auto run = [&] {
        Cpu c{};
        c.r[3] = kIcon;
        hooked.at(0x026396FC)(&c);
    };
    calls.clear();
    run();
    // (1547 - 0 + 500) * 0.01 * 2 = 40.94; -((-202539 + 200000 - 1000) * 0.01 * 2) = 70.78
    // (the first update of a balloon after a state load (test_hooks) hides the circle the state may hold)
    assert((calls == std::vector<std::string>{"balloon", "circle hidden", "circle shown"}));
    assert(close_to(u32_as_f32(ld32(kCircle + 0x1C)), 40.94f) && close_to(u32_as_f32(ld32(kCircle + 0x20)), 70.78f));
    calls.clear();
    run();
    assert((calls == std::vector<std::string>{"balloon"}));  // shown once, placed every update
    // another square: the circle is the game's again
    st8(kInfo + 4, 1);
    calls.clear();
    run();
    assert((calls == std::vector<std::string>{"balloon", "circle hidden"}));
    st8(kInfo + 4, 0);
    run();
    // the game shows its own circle (that square's treasure is salvaged): never touched
    st8(kIcon + 0x158, 1);
    st32(kCircle + 0x1C, f32_as_u32(5.0f));
    calls.clear();
    run();
    assert((calls == std::vector<std::string>{"balloon"}) && close_to(u32_as_f32(ld32(kCircle + 0x1C)), 5.0f));
    st8(kIcon + 0x158, 0);
    // untracked: a finished chart's marker in this square has the circle; an owned one has not
    tracked_on = false;
    assert(give("bal", "c") == kResultOwned && mark("bal", "c", "m", 2047, -201539, kIconStory, "") == kResultOwned);
    calls.clear();
    run();
    assert((calls == std::vector<std::string>{"balloon"}));
    assert(finish("bal", "c") == kResultDone);
    calls.clear();
    run();
    // (2047 + 500) * 0.02 = 50.94; -((-201539 + 200000 - 1000) * 0.02) = 50.78
    assert((calls == std::vector<std::string>{"balloon", "circle shown"}));
    assert(close_to(u32_as_f32(ld32(kCircle + 0x1C)), 50.94f) && close_to(u32_as_f32(ld32(kCircle + 0x20)), 50.78f));
    // the tracked step comes first
    tracked_on = true;
    run();
    assert(close_to(u32_as_f32(ld32(kCircle + 0x1C)), 40.94f));
    tracked_on = false;
    unmark("bal", "c", "");
    calls.clear();
    run();
    assert((calls == std::vector<std::string>{"balloon", "circle hidden"}));
    // An island needs no owned/finished treasure chart and uses the free native ring.
    const WorldMarker island{"island","Island",1047,-201039};assert(publish_world_markers("world",{&island,1}));calls.clear();run();assert((calls==std::vector<std::string>{"balloon","circle shown"}));assert(close_to(ldf32(kCircle+0x1C),30.94f)&&close_to(ldf32(kCircle+0x20),40.78f));
    st8(kIcon+0x158,1);stf32(kCircle+0x1C,7);run();assert(close_to(ldf32(kCircle+0x1C),7));st8(kIcon+0x158,0);clear_world_markers("world");
    // a state load: a circle the state holds is hidden once, if the game doesn't show it
    state_loaded();
    calls.clear();
    run();
    run();
    assert((calls == std::vector<std::string>{"balloon", "circle hidden", "balloon"}));
    set_tracked_source(nullptr);
}

// ---- the MAP screen's chart list: mod charts after the game's (chart numbers 61 and up) ----
constexpr uint32_t kList = kArena + 0x10000;
constexpr uint32_t kCoord = kList, kTable = kList + 0x400, kEntries0 = kList + 0x800, kUi = kList + 0x2000, kMapScreen = kList + 0x2800,
                   kView = kList + 0x2C00, kEntryMem = kList + 0x3000;
uint32_t game_entry(unsigned n) { return kEntries0 + n * 0x28; }
bool owned_bit(int save_no) { const int b = save_no - 1; return (ld32(kSave + 0xF4 + 4 * uint32_t(b >> 5)) >> (b & 31)) & 1; }
void own(int save_no) { const int b = save_no - 1; st32(kSave + 0xF4 + 4 * uint32_t(b >> 5), ld32(kSave + 0xF4 + 4 * uint32_t(b >> 5)) | (1u << (b & 31))); }
std::vector<std::string> list_calls;
uint32_t armed_number = 0;
uint32_t list_hook(uint32_t fn, std::initializer_list<uint32_t> args);
// hdTreMapScreen_c::selectChart (hd_screen_tre_map.cpp), its geometry in world units: the view's window
// is centred on the entry's square * 100000 + the origin of the view's variant (mChartKind, +0x90),
// the X sits at the salvage point of that variant, relative to the window's centre (y up on screen)
struct Selected {
    int kind = -1;
    float origin_x = 0, origin_z = 0;  // the window's centre in the world
    float x = 0, y = 0;                // the X from the window's centre (world units; y up)
} selected;
void select_chart(uint32_t chart) {
    const uint32_t e = list_hook(0x02687A64, {kCoord, chart});
    if (!e) return void(selected = {});
    const int kind = ld8(kView + 0x90);
    auto at = [&](int i) { return float(int16_t(ld16(e + 6 + 2 * (4 * kind + i)))); };
    selected.kind = kind;
    selected.origin_x = float(int8_t(ld8(e + 4))) * 100000.0f + at(0);
    selected.origin_z = float(int8_t(ld8(e + 5))) * 100000.0f + at(1);
    selected.x = at(2) - at(0);
    selected.y = -(at(3) - at(1));
    st32(kView + 0x74, f32_as_u32(selected.origin_x)), st32(kView + 0x78, f32_as_u32(selected.origin_z));  // mOrigin
}
// the view shows the X: within half the window (hdTreMapScreen_c::updatePosition's test for Link's icon)
bool x_in_window() { return std::fabs(selected.x) <= 0.5f * kViewSize && std::fabs(selected.y) <= 0.5f * kViewSize; }
void list_original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x02687A64: c->r[3] = c->r[4] < 61 ? game_entry(c->r[4]) : 0; break;  // findChart: the game stops at 61
    case 0x0268A570: c->r[3] = c->r[4] < 61 && owned_bit(int8_t(ld8(game_entry(c->r[4]) + 1))); break;
    case 0x0268A5DC: case 0x0268A648: c->r[3] = 0; break;
    case 0x0268A52C: list_calls.push_back("game obtainChart " + std::to_string(c->r[4])); break;
    case 0x0268A3A0: {
        unsigned n = 0;
        for (unsigned i = 0; i < 61; i++) n += owned_bit(int8_t(ld8(game_entry(i) + 1)));
        c->r[3] = n;
        break;
    }
    case 0x02698998: c->r[3] = 0xDEAD; break;
    case 0x02698A94: c->r[3] = c->r[4] >= 61 ? 5 : 1; break;  // the game's kind: 5 (none) past its last chart
    case 0x02699094:  // setChartCaption: the game's message of its chart number (stops at 61)
        if (c->r[4] < 61) list_calls.push_back("caption " + std::to_string(c->r[4]) + (armed_number ? " armed " + std::to_string(armed_number) : ""));
        break;
    case 0x02698B44: case 0x0269B414:  // the icon and the details: the coordinator's answers for that number
        if (c->r[5] < 61) {
            const uint32_t entry = list_hook(0x02687A64, {kCoord, c->r[5]});
            list_calls.push_back(std::string(fn == 0x02698B44 ? "icon " : "details ") + std::to_string(c->r[5]) + " sea " +
                                 std::to_string(entry ? ld8(entry) : 0) + " owned " + std::to_string(list_hook(0x0268A570, {kCoord, c->r[5]})) +
                                 " kind " + std::to_string(list_hook(0x02698A94, {c->r[3], c->r[5]})));
        }
        break;
    case 0x02698D74:  // the chart view: selectChart keeps the number it was given
        if (c->r[5] < 61) st32(kView + 0x8C, c->r[5]), list_calls.push_back("view " + std::to_string(c->r[5])), select_chart(c->r[5]);
        break;
    case 0x026EBEF4: list_calls.push_back("view destroyed"); break;
    case 0x02699118: list_calls.push_back("reset"); break;
    default: original(c, fn);
    }
}
uint32_t list_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    if (hooked.count(fn)) {  // a game call that is hooked runs the hook, as in the game
        Cpu call = *c;
        auto it = args.begin();
        for (unsigned r = 3; it != args.end(); ++it, ++r) call.r[r] = *it;
        hooked.at(fn)(&call);
        return call.r[3];
    }
    return game_call(c, fn, args);
}
uint32_t list_hook(uint32_t fn, std::initializer_list<uint32_t> args) {
    Cpu c{};
    auto it = args.begin();
    for (unsigned r = 3; it != args.end(); ++it, ++r) c.r[r] = *it;
    hooked.at(fn)(&c);
    return c.r[3];
}
void test_chart_list() {
    assert(map(kList, 0x10000));
    for (unsigned n = 0; n < 61; n++) {  // 61 charts; chart n is on square n % 49, its save number n + 1
        const uint32_t e = game_entry(n);
        const unsigned sea = n % 49;
        st8(e + 0, uint8_t(sea + 1)), st8(e + 1, uint8_t(n + 1)), st8(e + 2, uint8_t(n + 1)), st8(e + 3, 7);
        st8(e + 4, uint8_t(int8_t(int(sea % 7) - 3))), st8(e + 5, uint8_t(int8_t(int(sea / 7) - 3)));
        for (unsigned k = 0; k < 4; k++)
            st16(e + 6 + 8 * k, 100), st16(e + 8 + 8 * k, 200), st16(e + 10 + 8 * k, 300), st16(e + 12 + 8 * k, 400);
        st32(kTable + 4 * n, e);
    }
    st32(kCoord + 0x174, 61), st32(kCoord + 0x17C, kTable);
    st32(kMapScreen + 0x84, kView);
    own(15), own(41);  // the save owns charts 14 and 40
    std::vector<uint32_t> bits_before;
    for (uint32_t o = 0xE4; o < 0x124; o += 4) bits_before.push_back(ld32(kSave + o));
    set_entry_memory(kEntryMem, kMaxCharts * 0x28);
    set_caption_text([](uint32_t number, const std::u16string* text) { armed_number = text ? number : 0; });
    set_game_calls(list_original, list_call);
    // a declared chart Link doesn't own: nothing changes
    assert(define("cq", "old-map", "Old map", "An X marks the lighthouse.", 1759, -202572) == kResultNone);
    assert(define("cq", "Bad", "x", "", 0, 0) == kResultInvalid && define("cq", "x", "", "", 0, 0) == kResultInvalid);
    assert(list_hook(0x0268A3A0, {kCoord}) == 2 && list_hook(0x0268A570, {kCoord, 61}) == 0 && list_hook(0x02687A64, {kCoord, 61}) == 0);
    // owned: chart 61, its own number after the game's last chart
    assert(give("cq", "old-map") == kResultOwned);
    assert(list_hook(0x0268A3A0, {kCoord}) == 3 && list_hook(0x0268A570, {kCoord, 61}) == 1 && list_hook(0x0268A570, {kCoord, 62}) == 0);
    const uint32_t e = list_hook(0x02687A64, {kCoord, 61});
    assert(e == kEntryMem);
    // its square's art and origin (square 0,-2: sea 11), its own number, a treasure chart's type, no save number, the spot
    assert(ld8(e) == 11 && ld8(e + 2) == 62 && ld8(e + 1) == 0 && ld8(e + 3) == 7 && int8_t(ld8(e + 4)) == 0 && int8_t(ld8(e + 5)) == -2);
    for (unsigned k = 0; k < 4; k++)
        assert(int16_t(ld16(e + 6 + 8 * k)) == 100 && int16_t(ld16(e + 8 + 8 * k)) == 200 && int16_t(ld16(e + 10 + 8 * k)) == 1759 &&
               int16_t(ld16(e + 12 + 8 * k)) == -2572);
    assert(list_hook(0x02687A64, {kCoord, 35}) == game_entry(35) && list_hook(0x02687A64, {kCoord, 70}) == 0);
    // opened in the list: the record, never the game's bit
    assert(list_hook(0x0268A5DC, {kCoord, 61}) == 0);
    list_calls.clear();
    list_hook(0x0268A52C, {kCoord, 61});
    assert(list_calls.empty() && list_hook(0x0268A5DC, {kCoord, 61}) == 1 && state("cq", "old-map") == kResultOwned);
    list_hook(0x0268A52C, {kCoord, 14});
    assert((list_calls == std::vector<std::string>{"game obtainChart 14"}));
    assert(list_hook(0x0268A648, {kCoord, 61}) == 0);
    assert(finish("cq", "old-map") == kResultDone && list_hook(0x0268A648, {kCoord, 61}) == 1 && list_hook(0x0268A5DC, {kCoord, 61}) == 1);
    // the list's order: the game's charts, then the mod charts
    auto next = [](uint32_t chart, int32_t step) { return list_hook(0x02698998, {kMapScreen, chart, uint32_t(step)}); };
    list_hook(0x0268A3A0, {kCoord});
    assert(next(14, 1) == 40 && next(40, 1) == 61 && next(61, 1) == 14 && next(14, -1) == 61 && next(61, 0) == 61 && next(62, 0) == 0xFFFFFFFFu &&
           next(20, 0) == 0xDEAD);  // a game chart's own question: the game's code
    // the functions that stop at the game's last chart run for the mod chart, the coordinator answering for it
    list_calls.clear();
    assert(list_hook(0x02698A94, {kMapScreen, 61}) == 1);  // a treasure chart
    list_hook(0x02698B44, {kMapScreen, 0, 61});
    list_hook(0x0269B414, {kMapScreen, 0, 61});
    list_hook(0x02698D74, {kMapScreen, 0, 61});
    list_hook(0x02699094, {kMapScreen, 61});
    list_hook(0x02699094, {kMapScreen, 14});
    assert((list_calls == std::vector<std::string>{"icon 3 sea 11 owned 1 kind 1", "details 3 sea 11 owned 1 kind 1", "view 3",
                                                   "caption 3 armed " + std::to_string(0x31DD + 3), "caption 14"}));
    assert(ld32(kView + 0x8C) == 61 && armed_number == 0);  // the chart view keeps the mod chart's own number
    // outside those calls chart 3 is the game's own
    assert(list_hook(0x02687A64, {kCoord, 3}) == game_entry(3) && list_hook(0x0268A570, {kCoord, 3}) == 0);
    // resetChartSelection(): the game shows a selected chart only below 61; a mod chart too
    st32(kMapScreen + 0xAC, 61);
    list_calls.clear();
    list_hook(0x02699118, {kMapScreen});
    assert(list_calls.size() == 4 && list_calls[0] == "reset" && list_calls[2] == "view 3");
    // "Open chart": the menu switches to the MAP screen with it selected
    st32(0x101F8344, kUi), st32(kUi + 0x218, kCoord), st32(kCoord + 0x28, kMapScreen), st32(kMapScreen + 0xAC, 0);
    assert(open_in_list("cq", "old-map") && ld32(kMapScreen + 0xAC) == 61 && ld32(kUi + 0x228) == 1);
    assert(!open_in_list("cq", "nothing"));
    // a real treasure chart obtained meanwhile changes nothing for either
    own(4);  // chart 3
    assert(list_hook(0x02687A64, {kCoord, 3}) == game_entry(3) && list_hook(0x0268A570, {kCoord, 3}) == 1);
    assert(list_hook(0x02687A64, {kCoord, 61}) == kEntryMem && ld8(kEntryMem + 2) == 62 && list_hook(0x0268A570, {kCoord, 61}) == 1);
    assert(next(40, 1) == 61 && next(61, 1) == 3 && list_hook(0x0268A3A0, {kCoord}) == 4);
    // a 100% save (every game chart) and 8 mod charts at once: 69 in the list, all mod charts in order after the game's
    for (int n = 1; n <= 61; n++) own(n);
    for (int i = 1; i < 8; i++) {
        const std::string id = "map-" + std::to_string(i);
        assert(define("cq", id, "Map " + std::to_string(i), "", float(i) * 1000, 0) == kResultNone && give("cq", id) == kResultOwned);
    }
    assert(list_hook(0x0268A3A0, {kCoord}) == 61 + 8);
    for (uint32_t m = 61; m < 69; m++) assert(list_hook(0x0268A570, {kCoord, m}) == 1 && ld8(list_hook(0x02687A64, {kCoord, m}) + 2) == m + 1);
    assert(next(60, 1) == 61 && next(68, 1) == 0 && next(0, -1) == 68);
    // the game's chart bits: only what the game itself set (own() above)
    unsigned changed = 0;
    for (uint32_t o = 0xE4, i = 0; o < 0x124; o += 4, i++) changed += ld32(kSave + o) != bits_before[i] && (o < 0xF4 || o >= 0x104);
    assert(changed == 0);  // the opened and complete bits are untouched
    auto shown = charts();
    assert(shown.size() == 8 && shown[0].name == "Old map" && shown[0].state == kResultDone);
    set_game_calls(original, game_call);
}

// the chart view of a mod chart: the X at its spot, inside the window, whatever the save's variant
uint32_t group(unsigned k) { return kList + 0x3800 + k * 0x40; }
uint32_t picture(unsigned k) { return kList + 0x3C00 + k * 0x100; }
bool picture_shown(unsigned k) { return ld8(picture(k) + 0x44) & 1; }
// a square's entry with four variants: {origin x, origin z} each (the salvage points: the origins)
void square(unsigned n, const int16_t origins[4][2]) {
    for (unsigned k = 0; k < 4; k++)
        for (unsigned a = 0; a < 2; a++) st16(game_entry(n) + 6 + 2 * (4 * k + a), uint16_t(origins[k][a])), st16(game_entry(n) + 10 + 2 * (4 * k + a), uint16_t(origins[k][a]));
}
void test_chart_view() {
    // where a point is: its square (column, row from square D4) and its place from the square's centre, z south
    SquarePlace p = square_place(-3500, -194000);  // the play test's spot, west-northwest of Windfall
    assert(p.column == 0 && p.row == -2 && close_to(p.x, -3500) && close_to(p.z, 6000));
    p = square_place(-198000, 8000);
    assert(p.column == -2 && p.row == 0 && close_to(p.x, 2000) && close_to(p.z, 8000));
    p = square_place(349000, -349000);
    assert(p.column == 3 && p.row == -3 && close_to(p.x, 49000) && close_to(p.z, -49000));
    p = square_place(-360000, 0);  // past the sea's edge: the outer square
    assert(p.column == -3 && close_to(p.x, -60000));
    // the game's windows (cmapdat.bin, USA): Windfall's square (sea 11) and sea 23
    const int16_t windfall[4][2] = {{3600, 1100}, {2400, -4100}, {-1500, 1600}, {-5000, -6800}};
    const int16_t sea23[4][2] = {{-3400, -2500}, {1200, 5500}, {-2600, 900}, {7700, -2700}};
    assert(view_variant(windfall, -3500, 6000) == 2);  // (the save's variant 0: 7100 west of its centre)
    assert(view_variant(sea23, 2000, 8000) == 1);
    assert(view_variant(windfall, 3600, 1100) == 0 && view_variant(windfall, 40000, 40000) == -1);
    const int16_t overlapping[4][2] = {{0, 0}, {3000, 0}, {-30000, 0}, {30000, 0}};
    assert(view_variant(overlapping, 2000, 0) == 1);  // in two windows: the one it is most central in

    // the view (hdTreMapScreen_c): its variant, its four pictures (one pane per G_MapPattern_0k group)
    for (unsigned k = 0; k < 4; k++) {
        const uint32_t node = group(k) + 0x20;
        st32(kView + 0x50 + 4 * k, group(k));
        st32(group(k) + 0xC, node), st32(node, group(k) + 0xC), st32(node + 8, picture(k));
        st8(picture(k) + 0x44, k == 0 ? 0x01 : 0x00);
    }
    st8(kView + 0x90, 0);  // the save's variant
    square(10, windfall);  // chart 10: sea 11
    square(22, sea23);     // chart 22: sea 23
    set_game_calls(list_original, list_call);
    assert(define("cq", "windfall-x", "W", "", -3500, -194000) == kResultNone && give("cq", "windfall-x") == kResultOwned);
    assert(define("cq", "sea23-x", "S", "", -198000, 8000) == kResultNone && give("cq", "sea23-x") == kResultOwned);
    assert(define("cq", "far-x", "F", "", 40000, -160000) == kResultNone && give("cq", "far-x") == kResultOwned);
    const uint32_t windfall_chart = 61 + 8, sea23_chart = 61 + 9, far_chart = 61 + 10;
    auto view = [](uint32_t chart) {  // the window's centre: mOrigin as the view has it after the call
        list_hook(0x02698D74, {kMapScreen, 0, chart});
        selected.origin_x = u32_as_f32(ld32(kView + 0x74)), selected.origin_z = u32_as_f32(ld32(kView + 0x78));
    };
    auto spot_of_x = [](float& x, float& z) { x = selected.origin_x + selected.x, z = selected.origin_z - selected.y; };
    float x, z;
    // Windfall's spot: the picture of variant 2, the X inside the window at the spot
    view(windfall_chart);
    assert(selected.kind == 2 && x_in_window());
    spot_of_x(x, z);
    assert(close_to(x, -3500) && close_to(z, -194000));
    assert(close_to(selected.x, -2000) && close_to(selected.y, -4400));  // west, and south (down)
    assert(!picture_shown(0) && !picture_shown(1) && picture_shown(2) && !picture_shown(3));
    assert(ld8(kView + 0x90) == 0 && ld32(kView + 0x8C) == windfall_chart);  // the game's variant again after the call
    // (the save's window, variant 0, has that spot 7100 west of its centre: outside, on the paper's border)
    assert(std::fabs(-3500.0f - windfall[0][0]) > 0.5f * kViewSize);
    // another square, another variant
    view(sea23_chart);
    assert(selected.kind == 1 && x_in_window());
    spot_of_x(x, z);
    assert(close_to(x, -198000) && close_to(z, 8000));
    assert(!picture_shown(0) && picture_shown(1) && !picture_shown(2));
    // a spot no picture holds: no picture, the window centred on the spot (plain sea); 40000 from its
    // square's centre, more than the entry's s16 origin holds
    view(far_chart);
    assert(x_in_window() && close_to(selected.x, 0) && close_to(selected.y, 0));
    spot_of_x(x, z);
    assert(close_to(x, 40000) && close_to(z, -160000));
    for (unsigned k = 0; k < 4; k++) assert(!picture_shown(k));
    // a game chart: the game's picture and variant again
    view(14);
    assert(selected.kind == 0 && picture_shown(0) && !picture_shown(1) && !picture_shown(2) && !picture_shown(3));
    // full save states hold the game's picture
    view(windfall_chart);
    capture_begin();
    assert(picture_shown(0) && !picture_shown(2));
    capture_end();
    assert(!picture_shown(0) && picture_shown(2));
    // the view goes: forgotten (a new view at that address is the game's)
    list_calls.clear();
    list_hook(0x026EBEF4, {kView, 1});
    assert((list_calls == std::vector<std::string>{"view destroyed"}));
    capture_begin();
    assert(!picture_shown(0) && picture_shown(2));  // nothing of ours touched it
    capture_end();
    set_game_calls(original, game_call);
}

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x10000));
    story_store::set_memory(kArena + 0xE000);
    test_record();
    test_services();
    test_hooks();
    test_world_markers();
    test_circles();
    test_balloon();
    test_chart_list();
    test_chart_view();
    std::puts("story_chart_test: ok");
    return 0;
}
