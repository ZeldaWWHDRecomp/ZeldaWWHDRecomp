// The story notebook's page as a game layout (runtime/src/mods/story_notebook_layout*): the page's state
// mapped onto the layout's view (story_notebook_layout_view.h: tabs, rows, statuses, selection, scroll
// marks, empty texts) and the layout's draw (the client's built() / frame() as story_lyt_screen calls
// them): pane visibility, texts (set only when they change), the cursor's glide and the animations
// (Open / Breathe, TurnLeft / TurnRight, Bump<n>, Close).
//
// story_lyt_screen, story_lyt_text and lyt_build are replaced at link time by the fakes below, which
// record what the layout does; the panes' fields that the layout reads (translate, size) live in a small
// piece of mapped guest memory. No game files are needed.
#include "mods/story_notebook_layout.h"
#include "mods/story_notebook_layout_view.h"
#include "mods/story_notebook_list.h"
#include "mods/story_notebook_page.h"
#include "mods/story_lyt_screen.h"
#include "mods/story_lyt_text.h"
#include "mods/lyt_build.h"
#include "ppc.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_notebook;
namespace nl = story_notebook_layout;

void log_msg(const char* fmt, ...) { (void)fmt; }

// ---- the fakes: what the layout did ----
namespace {

constexpr uint32_t kPanes = 0x30000000, kPaneSize = 0x100, kPanesBytes = 0x20000;

struct Record {
    std::map<std::string, uint32_t> addr;  // pane name -> fake guest address
    std::map<uint32_t, std::string> name;
    std::map<std::string, bool> visible;       // the last visibility each pane got
    std::map<std::string, std::string> text;   // the last text each pane got
    std::vector<std::string> plays;            // animations played, in order
    std::vector<std::string> text_calls;       // panes whose text was set, in order
    int visible_calls = 0;
    std::vector<std::pair<float, float>> cursor;  // translates of N_Cursor
    bool close_ended = false;                  // what ended("Close") answers
    bool has_client = false;
    story_lyt_screen::Client client;

    void clear_calls() { plays.clear(), text_calls.clear(), visible_calls = 0, cursor.clear(); }
};
Record g;

uint32_t pane_addr(const std::string& n) {
    auto it = g.addr.find(n);
    if (it != g.addr.end()) return it->second;
    const uint32_t a = kPanes + uint32_t(g.addr.size()) * kPaneSize;
    assert(a + kPaneSize <= kPanes + kPanesBytes);
    g.addr[n] = a, g.name[a] = n;
    return a;
}
void put_f(uint32_t ea, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    st32(ea, u);
}

}  // namespace

namespace lyt_build {
std::string build_cached(const std::filesystem::path&, const std::filesystem::path&) { return "fake/StoryNotebook_00.szs"; }
}  // namespace lyt_build

namespace story_lyt_screen {
bool set_texture_pixels(const std::string&, const uint8_t*, uint32_t, uint32_t) { return false; }
void set_client(Client client) { g.client = std::move(client), g.has_client = true; }
bool has_client() { return g.has_client; }
bool ready() { return true; }
uint32_t pane(Cpu*, const char* name) { return pane_addr(name); }
void set_visible(uint32_t pane, bool visible) {
    assert(g.name.count(pane));
    g.visible[g.name[pane]] = visible;
    g.visible_calls++;
}
void set_translate(uint32_t pane, float x, float y) {
    assert(g.name.count(pane));
    if (g.name[pane] == "N_Cursor") g.cursor.push_back({x, y});
}
void play(Cpu*, const std::string& anim, float) { g.plays.push_back(anim); }
bool ended(Cpu*, const std::string& anim) { return anim == "Close" && g.close_ended; }
}  // namespace story_lyt_screen

namespace story_notebook {
bool portrait_pixels(uint16_t, guestmods::hud::Image&) { return false; }  // no portraits in the tests
void hide_floating_notebook_icon() {}
void restore_notebook_icons() {}
void state_loaded() {}
}  // namespace story_notebook
namespace story_lyt_text {
bool set_text(Cpu*, uint32_t pane, const std::string& utf8, const Options&) {
    assert(g.name.count(pane));
    g.text[g.name[pane]] = utf8;
    g.text_calls.push_back(g.name[pane]);
    return true;
}
bool set_text(Cpu*, uint32_t pane, const std::u16string& text, const Options&) {
    assert(g.name.count(pane));
    g.text[g.name[pane]] = std::string(text.begin(), text.end());  // the tests' texts are ASCII
    g.text_calls.push_back(g.name[pane]);
    return true;
}
std::u16string wrap(Cpu*, uint32_t, const std::u16string& text, float) { return text; }
// a fixed advance per character, so nothing in the tests is cut
float text_width(Cpu*, uint32_t, const std::u16string& text) { return float(text.size()); }
bool set_capacity(Cpu*, uint32_t pane, uint32_t) {
    assert(g.name.count(pane));
    return true;
}
}  // namespace story_lyt_text

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                             \
        }                                                                             \
    } while (0)

// the cursor's place as built (N_Cursor) and the rows' places (one row step = 80 units down)
constexpr float kCursorX = 12, kCursorY = 100, kRow0Y = 50, kRow1Y = -30, kStep = kRow0Y - kRow1Y;

void map_panes() {
#ifdef _WIN32
    void* p = VirtualAlloc(ppc_ptr(kPanes), kPanesBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p = mmap(ppc_ptr(kPanes), kPanesBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) p = nullptr;
#endif
    assert(p == ppc_ptr(kPanes));
    put_f(pane_addr("N_Cursor") + 0x1C, kCursorX);
    put_f(pane_addr("N_Cursor") + 0x20, kCursorY);
    put_f(pane_addr("N_Row0") + 0x20, kRow0Y);
    put_f(pane_addr("N_Row1") + 0x20, kRow1Y);
}

Quest quest(const std::string& id, const std::string& condition = "") {
    Quest q;
    q.mod = "mod", q.mod_name = "Mod of " + id, q.id = id, q.key = quest_key("mod", id);
    q.title = "Quest " + id, q.next = "Next of " + id, q.condition = condition;
    return q;
}
Row row(const Quest& q, Status s, bool tracked = false) {
    return {&q, s, uint8_t(s == Status::Done ? kStepDone : s == Status::InProgress ? 1 : 0), tracked};
}
std::vector<Row> rows_of(const std::vector<Quest>& qs, Status s) {
    std::vector<Row> out;
    for (const Quest& q : qs) out.push_back(row(q, s));
    return out;
}

// a pressed button for one game frame (then released): the page reads edges
void press(Page& p, const std::vector<Row>& all, uint32_t button) {
    PadInput in;
    in.buttons = button;
    p.input(in, all);
    p.input(PadInput{}, all);
}
// the page opened (show() ignores the buttons held at that moment: one idle read)
void open(Page& p, const std::vector<Row>& all) {
    p.show();
    p.input(PadInput{}, all);
}

// one game frame: the page's state published (story_notebook.cpp), then the layout's draw
bool frame() { return g.client.frame(nullptr, 0x1000); }
bool step(const Page& p, const std::vector<Row>& all) {
    nl::update(p, all);
    return frame();
}
// the layout built with the item screen (story_lyt_screen calls the client's built())
void build() {
    g.client.built(nullptr, 0x1000);
    g.visible.clear(), g.text.clear(), g.close_ended = false;
    g.clear_calls();
}
// the page closed and the closing animation through: the layout draws nothing
void close_fully(Page& p) {
    p.hide();
    nl::update_closed();
    frame();
    g.close_ended = true;
    nl::update_closed();
    frame();
    CHECK(!nl::update_closed());
    g.close_ended = false;
    g.clear_calls();
}

// ---- view_of: the page's state as the layout shows it ----

void test_view_empty() {
    const std::vector<Row> all;
    Page p;
    CHECK(nl::view_of(p, all).mode == 0);  // closed
    open(p, all);
    nl::View v = nl::view_of(p, all);
    CHECK(v.mode == 1 && v.tab == 0);
    CHECK(v.rows.empty() && v.selected == -1);
    CHECK(!v.up && !v.down);
    CHECK(v.empty && !v.track_plate);
    CHECK(v.empty1 == "No stories yet.");
    CHECK(v.empty2 == "When a story mod writes to you, its quests are kept here.");
    CHECK(v.tab_labels[0] == "Quests" && v.tab_labels[1] == "Archive");
    press(p, all, kPadRight);
    v = nl::view_of(p, all);
    CHECK(v.tab == 1 && v.empty);
    CHECK(v.empty1 == "No finished stories yet." && v.empty2.empty());
}

void test_view_one() {
    const std::vector<Quest> qs = {quest("a")};
    const std::vector<Row> all = rows_of(qs, Status::New);
    Page p;
    open(p, all);
    const nl::View v = nl::view_of(p, all);
    CHECK(v.rows.size() == 1 && v.selected == 0);
    CHECK(v.rows[0].title == "Quest a");
    CHECK(!v.up && !v.down);
    CHECK(!v.empty && v.track_plate);
    CHECK(v.tab_labels[1] == "Archive");
}

void test_view_seven_scroll() {
    std::vector<Quest> qs;
    for (int i = 0; i < 7; i++) qs.push_back(quest(std::to_string(i)));
    const std::vector<Row> all = rows_of(qs, Status::InProgress);
    Page p;
    open(p, all);
    nl::View v = nl::view_of(p, all);
    CHECK(v.rows.size() == size_t(nl::kRows));
    for (int i = 0; i < nl::kRows; i++) CHECK(v.rows[size_t(i)].title == "Quest " + std::to_string(i));
    CHECK(v.selected == 0 && !v.up && v.down);
    for (int i = 0; i < 4; i++) press(p, all, kPadDown);  // the last row shown: no scrolling yet
    v = nl::view_of(p, all);
    CHECK(p.top() == 0 && v.selected == 4 && !v.up && v.down);
    press(p, all, kPadDown);  // one past it: the list scrolls by one
    v = nl::view_of(p, all);
    CHECK(p.top() == 1 && v.selected == 4 && v.up && v.down);
    CHECK(v.rows[0].title == "Quest 1" && v.rows[4].title == "Quest 5");
    press(p, all, kPadDown);  // the last quest
    v = nl::view_of(p, all);
    CHECK(p.top() == 2 && v.selected == 4 && v.up && !v.down);
    CHECK(v.rows.size() == size_t(nl::kRows) && v.rows[0].title == "Quest 2" && v.rows[4].title == "Quest 6");
    for (int i = 0; i < 6; i++) press(p, all, kPadUp);  // back to the top
    v = nl::view_of(p, all);
    CHECK(p.top() == 0 && v.selected == 0 && !v.up && v.down);
}

void test_view_tabs() {
    const std::vector<Quest> qs = {quest("a"), quest("b"), quest("c")};
    const std::vector<Row> all = {row(qs[0], Status::New), row(qs[1], Status::Done), row(qs[2], Status::Done)};
    Page p;
    open(p, all);
    nl::View v = nl::view_of(p, all);
    CHECK(v.tab_labels[0] == "Quests" && v.tab_labels[1] == "Archive (2)");
    CHECK(v.tab == 0 && v.rows.size() == 1 && v.rows[0].title == "Quest a");
    press(p, all, kPadRight);
    v = nl::view_of(p, all);
    CHECK(v.tab == 1 && v.rows.size() == 2 && v.selected == 0);
    CHECK(v.rows[0].title == "Quest b" && v.rows[1].title == "Quest c");
    CHECK(v.tab_labels[1] == "Archive (2)");  // counted over all quests, whichever tab is open
    press(p, all, kPadLeft);
    CHECK(nl::view_of(p, all).tab == 0);
}

void test_view_statuses() {
    const std::vector<Quest> qs = {quest("new"), quest("busy"), quest("later", "Finish the Tower of the Gods"), quest("done")};
    const std::vector<Row> all = {row(qs[1], Status::InProgress), row(qs[0], Status::New), row(qs[2], Status::NotAvailable),
                                  row(qs[3], Status::Done)};
    Page p;
    open(p, all);
    nl::View v = nl::view_of(p, all);
    CHECK(v.rows.size() == 3);
    const nl::RowView &busy = v.rows[0], &fresh = v.rows[1], &later = v.rows[2];
    CHECK(busy.status == "In progress" && busy.pill == 0 && !busy.dim && busy.next == "Next of busy");
    CHECK(fresh.status == "New" && fresh.pill == 1 && !fresh.dim && fresh.next == "Next of new");
    CHECK(later.status == "Not yet" && later.pill == 2 && later.dim);
    CHECK(later.next == "Not yet available: Finish the Tower of the Gods");
    press(p, all, kPadRight);
    v = nl::view_of(p, all);
    CHECK(v.rows.size() == 1);
    const nl::RowView& done = v.rows[0];
    CHECK(done.status == "Done" && done.pill == 3 && done.dim && done.next == "The story is told.");
}

void test_view_tracked_and_initial() {
    std::vector<Quest> qs = {quest("a"), quest("b"), quest("c")};
    qs[1].portrait = "assets/portrait_b.png";
    qs[2].mod_name.clear();
    const std::vector<Row> all = {row(qs[0], Status::InProgress, true), row(qs[1], Status::InProgress), row(qs[2], Status::New)};
    Page p;
    open(p, all);
    const nl::View v = nl::view_of(p, all);
    CHECK(v.rows[0].tracked && !v.rows[1].tracked && !v.rows[2].tracked);
    CHECK(v.rows[0].initial_shown && v.rows[0].initial == "M");  // no portrait: the mod's initial
    CHECK(!v.rows[1].initial_shown);                              // a portrait: no initial
    CHECK(v.rows[2].initial_shown && v.rows[2].initial == "?");   // no mod name
}

void test_view_selection_and_mode() {
    const std::vector<Quest> qs = {quest("a"), quest("b"), quest("c")};
    const std::vector<Row> all = rows_of(qs, Status::New);
    Page p;
    open(p, all);
    press(p, all, kPadDown);
    press(p, all, kPadDown);
    nl::View v = nl::view_of(p, all);
    CHECK(v.selected == 2 && p.selected_key() == qs[2].key);
    press(p, all, kPadUp);
    CHECK(nl::view_of(p, all).selected == 1);
    press(p, all, kPadA);
    CHECK(p.mode() == Page::Mode::Detail && nl::view_of(p, all).mode == 2);
    press(p, all, kPadB);
    CHECK(nl::view_of(p, all).mode == 1);
    p.hide();
    CHECK(nl::view_of(p, all).mode == 0);
}

// ---- the layout's draw ----

void test_configure() {
#ifdef _WIN32
    _putenv_s("WWHD_NOTEBOOK_LAYOUT", "");
#else
    unsetenv("WWHD_NOTEBOOK_LAYOUT");
#endif
    set_layout_description("/nowhere/notebook_layout.json", "/nowhere/data");
    CHECK(nl::configure());
    CHECK(g.has_client && g.client.layout == "StoryNotebook_00");
    CHECK(g.client.archive == "fake/StoryNotebook_00.szs");
    CHECK(g.client.frame && g.client.built);
    std::map<std::string, std::string> group;
    std::map<std::string, uint32_t> slot;
    for (const auto& a : g.client.anims) group[a.name] = a.group, slot[a.name] = a.slot;
    for (const char* n : {"Open", "Close", "TurnLeft", "TurnRight", "Breathe", "Bump0", "Bump4"}) CHECK(group.count(n));
    CHECK(slot["Open"] == slot["Close"] && slot["TurnLeft"] == slot["TurnRight"]);  // they replace each other
    CHECK(slot["Open"] != slot["TurnLeft"] && slot["Breathe"] != slot["Open"] && slot["Bump0"] != slot["Bump1"]);
}

void test_frame_open_and_panes() {
    build();
    std::vector<Quest> qs = {quest("a"), quest("b", "Meet the Rito"), quest("c")};
    qs[2].portrait = "assets/c.png";
    const std::vector<Row> all = {row(qs[0], Status::InProgress, true), row(qs[1], Status::NotAvailable), row(qs[2], Status::Done)};
    Page p;
    CHECK(!step(p, all));  // closed: nothing drawn, nothing played
    CHECK(g.plays.empty() && !nl::drawing());
    open(p, all);
    CHECK(nl::update(p, all));
    CHECK(nl::drawing());
    CHECK(frame());
    CHECK((g.plays == std::vector<std::string>{"Open", "Breathe"}));
    // tabs
    CHECK(g.visible["P_Tab0On"] && !g.visible["P_Tab0Off"] && g.visible["T_Tab0On"] && !g.visible["T_Tab0Off"]);
    CHECK(!g.visible["P_Tab1On"] && g.visible["P_Tab1Off"] && !g.visible["T_Tab1On"] && g.visible["T_Tab1Off"]);
    CHECK(g.text["T_Tab0On"] == "Quests" && g.text["T_Tab1Off"] == "Archive (1)" && g.text["T_Tab1On"] == "Archive (1)");
    // rows: two on the quests tab
    CHECK(g.visible["N_Row0"] && g.visible["N_Row1"] && !g.visible["N_Row2"] && !g.visible["N_Row3"] && !g.visible["N_Row4"]);
    CHECK(g.visible["T_Row0Title"] && !g.visible["T_Row0TitleDim"] && g.visible["T_Row0Next"] && !g.visible["T_Row0NextDim"]);
    CHECK(g.text["T_Row0Title"] == "Quest a" && g.text["T_Row0Next"] == "Next of a" && g.text["T_Row0Status"] == "In progress");
    CHECK(g.visible["P_Row0Ring"] && !g.visible["P_Row1Ring"]);
    CHECK(!g.visible["P_Row0PillNew"] && !g.visible["P_Row0PillNotYet"] && !g.visible["P_Row0PillDone"]);
    CHECK(g.visible["T_Row0Initial"] && g.text["T_Row0Initial"] == "M");
    // the not yet available quest: dim texts, its pill, its condition
    CHECK(!g.visible["T_Row1Title"] && g.visible["T_Row1TitleDim"] && !g.visible["T_Row1Next"] && g.visible["T_Row1NextDim"]);
    CHECK(g.text["T_Row1TitleDim"] == "Quest b" && g.text["T_Row1NextDim"] == "Not yet available: Meet the Rito");
    CHECK(g.text["T_Row1Status"] == "Not yet" && g.visible["P_Row1PillNotYet"] && !g.visible["P_Row1PillNew"]);
    CHECK(!g.text.count("T_Row1Title"));  // the hidden variant gets no text
    // selection, marks, plates
    CHECK(g.visible["P_Row0Wash"] && !g.visible["P_Row1Wash"]);
    CHECK(g.visible["N_Cursor"] && g.visible["N_PlateTrack"]);
    CHECK(!g.visible["T_Up"] && !g.visible["T_Down"] && !g.visible["T_Empty1"] && !g.visible["T_Empty2"]);
    // the cursor starts on the selected row's place (no glide on opening)
    CHECK(!g.cursor.empty() && g.cursor.back().first == kCursorX && g.cursor.back().second == kCursorY);
    // the archive: the done quest with its portrait; its picture isn't loaded (the fake has none), so the
    // medallion keeps the initial and the portrait pane stays hidden
    g.clear_calls();
    press(p, all, kPadRight);
    CHECK(step(p, all));
    CHECK(g.visible["N_Row0"] && !g.visible["N_Row1"]);
    CHECK(g.text["T_Row0TitleDim"] == "Quest c" && g.text["T_Row0NextDim"] == "The story is told.");
    CHECK(g.text["T_Row0Status"] == "Done" && g.visible["P_Row0PillDone"] && !g.visible["P_Row0Ring"]);
    CHECK(g.visible["T_Row0Initial"] && !g.visible["P_Row0Portrait"]);
    close_fully(p);
}

void test_frame_empty() {
    build();
    const std::vector<Row> all;
    Page p;
    open(p, all);
    CHECK(step(p, all));
    for (int i = 0; i < nl::kRows; i++) CHECK(!g.visible["N_Row" + std::to_string(i)]);
    CHECK(g.visible["T_Empty1"] && g.visible["T_Empty2"] && g.text["T_Empty1"] == "No stories yet.");
    CHECK(!g.visible["N_Cursor"] && !g.visible["N_PlateTrack"] && g.cursor.empty());
    press(p, all, kPadRight);
    CHECK(step(p, all));
    CHECK(g.visible["T_Empty1"] && !g.visible["T_Empty2"] && g.text["T_Empty1"] == "No finished stories yet.");
    close_fully(p);
}

void test_frame_scroll_marks() {
    build();
    std::vector<Quest> qs;
    for (int i = 0; i < 7; i++) qs.push_back(quest(std::to_string(i)));
    const std::vector<Row> all = rows_of(qs, Status::New);
    Page p;
    open(p, all);
    CHECK(step(p, all));
    for (int i = 0; i < nl::kRows; i++) CHECK(g.visible["N_Row" + std::to_string(i)]);
    CHECK(!g.visible["T_Up"] && g.visible["T_Down"]);
    for (int i = 0; i < 6; i++) press(p, all, kPadDown);
    CHECK(step(p, all));
    CHECK(g.visible["T_Up"] && !g.visible["T_Down"]);
    CHECK(g.text["T_Row0Title"] == "Quest 2" && g.text["T_Row4Title"] == "Quest 6");
    CHECK(g.visible["P_Row4Wash"] && !g.visible["P_Row0Wash"]);
    close_fully(p);
}

void test_frame_text_only_when_changed() {
    build();
    std::vector<Quest> qs = {quest("a"), quest("b")};
    const std::vector<Row> all = rows_of(qs, Status::InProgress);
    Page p;
    open(p, all);
    CHECK(step(p, all));
    CHECK(!g.text_calls.empty());
    g.clear_calls();
    CHECK(step(p, all));  // nothing changed: the panes are left alone
    CHECK(g.text_calls.empty() && g.visible_calls == 0 && g.plays.empty());
    press(p, all, kPadDown);  // only the selection changes: visibility, no text
    CHECK(step(p, all));
    CHECK(g.visible_calls > 0 && g.text_calls.empty());
    CHECK(g.visible["P_Row1Wash"] && !g.visible["P_Row0Wash"]);
    g.clear_calls();
    qs[0].next = "Sail to Windfall";  // one text changes: only that pane gets it
    CHECK(step(p, all));
    CHECK((g.text_calls == std::vector<std::string>{"T_Row0Next"}) && g.text["T_Row0Next"] == "Sail to Windfall");
    close_fully(p);
}

void test_frame_cursor_glide() {
    build();
    std::vector<Quest> qs;
    for (int i = 0; i < 4; i++) qs.push_back(quest(std::to_string(i)));
    const std::vector<Row> all = rows_of(qs, Status::New);
    Page p;
    open(p, all);
    CHECK(step(p, all));
    CHECK(g.cursor.back().second == kCursorY);
    g.clear_calls();
    press(p, all, kPadDown);
    press(p, all, kPadDown);  // two rows down within one frame
    const float to = kCursorY - 2 * kStep;
    std::vector<float> ys;
    for (int i = 0; i < 4; i++) {
        CHECK(step(p, all));
        CHECK(!g.cursor.empty() && g.cursor.back().first == kCursorX);
        ys.push_back(g.cursor.back().second);
    }
    CHECK((g.plays == std::vector<std::string>{"Bump2"}));  // the selected row bumps, once
    CHECK(ys[0] < kCursorY && ys[0] > to);                   // gliding ...
    CHECK(ys[1] < ys[0] && ys[1] > to);
    CHECK(std::fabs(ys[2] - to) < 1e-4f && std::fabs(ys[3] - to) < 1e-4f);  // ... it reaches the row's place and stays
    g.clear_calls();
    press(p, all, kPadUp);
    for (int i = 0; i < 3; i++) step(p, all);
    CHECK((g.plays == std::vector<std::string>{"Bump1"}));
    CHECK(std::fabs(g.cursor.back().second - (kCursorY - kStep)) < 1e-4f);
    close_fully(p);
}

void test_frame_tabs_turn() {
    build();
    const std::vector<Quest> qs = {quest("a"), quest("b")};
    const std::vector<Row> all = {row(qs[0], Status::New), row(qs[1], Status::Done)};
    Page p;
    open(p, all);
    CHECK(step(p, all));
    g.clear_calls();
    press(p, all, kPadRight);
    CHECK(step(p, all));
    CHECK((g.plays == std::vector<std::string>{"TurnRight"}));
    CHECK(g.visible["P_Tab1On"] && !g.visible["P_Tab0On"]);
    g.clear_calls();
    CHECK(step(p, all));
    CHECK(g.plays.empty());  // played once per change
    press(p, all, kPadLeft);
    CHECK(step(p, all));
    CHECK((g.plays == std::vector<std::string>{"TurnLeft"}));
    CHECK(g.visible["P_Tab0On"] && !g.visible["P_Tab1On"]);
    close_fully(p);
}

void test_frame_detail_and_back() {
    build();
    std::vector<Quest> qs = {quest("a"), quest("b")};
    qs[0].steps = {"Find the lantern", "Bring it back"}, qs[0].where = "Windfall", qs[0].reward = "100 rupees";
    std::vector<Row> all = rows_of(qs, Status::InProgress);
    all[0].step = 2, all[0].tracked = true;
    Page p;
    open(p, all);
    CHECK(step(p, all));
    g.clear_calls();
    press(p, all, kPadA);  // the quest's detail view: N_Detail instead of the list's content
    CHECK(p.mode() == Page::Mode::Detail);
    CHECK(nl::update(p, all) && nl::drawing());
    CHECK(frame());
    CHECK((g.plays == std::vector<std::string>{"DetailIn", "TrackBreathe"}));
    CHECK(g.visible["N_Detail"] && !g.visible["N_Content"] && !g.visible["P_Tab0On"] && !g.visible["T_Tab1Off"]);
    CHECK(g.text["D_Title"] == "Quest a" && g.text["D_SenderName"] == "Mod of a" && g.text["D_SenderInitial"] == "M");
    CHECK(g.text["D_Progress"] == "Step 2 of 2" && g.text["D_DoneBefore"] == "Done: Find the lantern" && g.visible["D_DoneBefore"]);
    // step 1 done (slot 1, linked to slot 0), step 2 the current one (slot 0); no third marker
    CHECK(g.visible["D_Step1"] && g.visible["D_Step1Done"] && !g.visible["D_Step1Now"] && g.visible["D_Step1Link"]);
    CHECK(g.visible["D_Step0"] && g.visible["D_Step0Now"] && !g.visible["D_Step0Done"] && !g.visible["D_Step2"]);
    CHECK(g.text["D_Body0"] == "Next of a" && g.text["D_Body1"] == "Windfall" && g.text["D_Body2"] == "100 rupees");
    CHECK(g.text["D_Status"] == "In progress" && !g.visible["D_PillNew"] && g.visible["D_Ring"] && g.visible["D_OnChart"]);
    CHECK(g.text["T_BackLabel"] == "Back" && g.text["T_TrackLabel"] == "Stop showing" && g.visible["D_TrackScroll"]);
    CHECK(g.visible["N_PlateTrack"] && g.visible["D_TrackCursorTL"]);
    g.clear_calls();
    CHECK(frame());  // nothing changed: no texts set again
    CHECK(g.text_calls.empty() && g.plays.empty());
    press(p, all, kPadB);  // back to the list: the page turns back, the list's parts and prompts again
    CHECK(p.mode() == Page::Mode::List);
    CHECK(nl::update(p, all) && nl::drawing());
    CHECK(frame());
    CHECK((g.plays == std::vector<std::string>{"TurnLeft"}));
    CHECK(g.visible["P_Row0Wash"] && g.visible["N_Cursor"] && g.visible["N_Content"] && !g.visible["N_Detail"]);
    CHECK(g.text["T_BackLabel"] == "Close" && g.text["T_TrackLabel"] == "Open" && !g.visible["D_TrackScroll"]);
    close_fully(p);
}

void test_frame_close() {
    build();
    const std::vector<Quest> qs = {quest("a")};
    const std::vector<Row> all = rows_of(qs, Status::New);
    Page p;
    open(p, all);
    CHECK(step(p, all));
    g.clear_calls();
    p.hide();
    CHECK(nl::update_closed());  // still drawn: the closing animation
    CHECK(frame());
    CHECK((g.plays == std::vector<std::string>{"Close"}));
    CHECK(nl::update_closed() && frame() && frame());  // until it ended
    CHECK(g.plays.size() == 1);
    g.close_ended = true;
    CHECK(nl::update_closed());  // the draw notices the end
    CHECK(!frame());
    CHECK(!nl::update_closed() && !nl::drawing());
    CHECK(!frame() && g.plays.size() == 1);
    g.close_ended = false;
    // opened again: Open and Breathe once more
    g.clear_calls();
    open(p, all);
    CHECK(step(p, all));
    CHECK((g.plays == std::vector<std::string>{"Open", "Breathe"}));
    close_fully(p);
}

}  // namespace

int main() {
    map_panes();
    test_view_empty();
    test_view_one();
    test_view_seven_scroll();
    test_view_tabs();
    test_view_statuses();
    test_view_tracked_and_initial();
    test_view_selection_and_mode();
    test_configure();
    test_frame_open_and_panes();
    test_frame_empty();
    test_frame_scroll_marks();
    test_frame_text_only_when_changed();
    test_frame_cursor_glide();
    test_frame_tabs_turn();
    test_frame_detail_and_back();
    test_frame_close();
    // Menu closure, stage teardown and state loading may happen without another draw pass.
    // No closing animation can finish then: cursor coverage must end immediately.
    for (int cycle = 0; cycle < 6; ++cycle) {
        Page p;
        const std::vector<Row> all;
        open(p, all);
        CHECK(step(p, all));
        CHECK(nl::drawing());
        nl::reset();
        CHECK(!nl::drawing() && !nl::animating());
        CHECK(!frame());
        g.clear_calls();
        CHECK(nl::update(p, all) && frame());
        CHECK((g.plays == std::vector<std::string>{"Open", "Breathe"}));
        close_fully(p);
    }
    if (g_failures) {
        std::fprintf(stderr, "story_notebook_layout_test: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("story_notebook_layout_test: ok\n");
    return 0;
}
