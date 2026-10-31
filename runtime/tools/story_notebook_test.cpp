// The story notebook (runtime/src/mods/story_notebook*): the quest list (order, tracking, state),
// the save record round trips, the services on a synthetic save, the page's navigation and drawing,
// the tracked quest's marker on the sea chart, and the item menu hooks on synthetic menu objects.
// The game's own functions are stubbed; no game files are needed.
#include "mods/story_notebook.h"
#include "mods/story_notebook_layout.h"
#include "mods/guest_hud.h"
namespace story_notebook_layout { extern bool test_layout_ready; }
#include "mods/story_notebook_list.h"
#include "mods/story_notebook_page.h"
#include "mods/story_notebook_font.h"
#include "mods/story_notebook_style.h"
#include "mods/story_notebook_caption.h"
#include "mods/story_notebook_art.h"
#include "mods/story_notebook_bag.h"
#include "mods/story_takeover.h"
#include "mods/story_sail.h"
#include "mods/story_manifest.h"
#include "mods/story_chart.h"
#include "mods/story_store.h"
#include "mods/story_mail.h"
#include "mods/story_mail_book.h"
#include "mods/story_items.h"
#include "mods/story_item_buttons.h"
#include "input.h"
#include "runtime.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_notebook;
using story_mail::Book;

void log_msg(const char* fmt, ...) {
    if (!std::getenv("TLOG")) return;
    va_list a; va_start(a, fmt); std::vfprintf(stderr, fmt, a); va_end(a); std::fputc('\n', stderr);
}
namespace mods {
uint64_t step() { return 0; }  // story_item_buttons.cpp's research aid
}
namespace input {
bool g_pro = false;
bool pro_controller() { return g_pro; }  // the menu on the GamePad unless a test says otherwise
}

namespace story_notebook {void action_rewards(Cpu*);std::vector<Takeover> takeovers();}
namespace {

constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000, kSave = kArena;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

Quest quest(const std::string& mod, const std::string& id, const std::string& condition = "") {
    Quest q;
    q.mod = mod, q.mod_name = "Mod " + mod, q.id = id, q.key = quest_key(mod, id), q.title = "Quest " + id, q.condition = condition;
    return q;
}
std::vector<std::string> order(const std::vector<Row>& rows) {
    std::vector<std::string> out;
    for (const Row& r : rows) out.push_back(r.quest->id);
    return out;
}

void test_list() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs = {quest("a", "done"), quest("a", "locked", "after the Tower of the Gods"), quest("b", "fresh"),
                             quest("b", "going"), quest("a", "going2")};
    // a fresh save: nothing saved, nothing written by reading
    auto rows0 = rows(qs, b);
    assert((order(rows0) == std::vector<std::string>{"done", "fresh", "going", "going2", "locked"}));
    assert(rows0[0].status == Status::New && rows0[4].status == Status::NotAvailable);
    for (uint8_t v : raw) assert(v == 0);
    // steps: never back, done stays done
    assert(reach_step(b, qs[3].key, 2) == 2 && reach_step(b, qs[3].key, 1) == 2 && reach_step(b, qs[3].key, 0) == 2);
    assert(reach_step(b, qs[4].key, 1) == 1);
    assert(reach_step(b, qs[0].key, 3) == 3 && finish(b, qs[0].key) == kStepDone && reach_step(b, qs[0].key, 9) == kStepDone);
    assert(reach_step(b, qs[2].key, 255) == -1);  // 255 means done, not a step
    // a started quest with a condition is in progress (the save wins over the session's condition)
    assert(reach_step(b, qs[1].key, 1) == 1);
    auto rows1 = rows(qs, b);
    assert((order(rows1) == std::vector<std::string>{"locked", "going", "going2", "fresh", "done"}));
    assert(rows1[4].status == Status::Done && rows1[0].status == Status::InProgress && rows1[0].step == 1);
    // tracking: one quest at a time, first in the list; choosing it again untracks
    assert(toggle_tracked(b, qs[2].key) && tracked_key(b) == qs[2].key);
    assert((order(rows(qs, b)) == std::vector<std::string>{"fresh", "locked", "going", "going2", "done"}));
    assert(rows(qs, b)[0].tracked);
    assert(toggle_tracked(b, qs[3].key) && tracked_key(b) == qs[3].key);
    unsigned tracked_records = 0;
    for (unsigned r = 0; r < story_mail::kRecords; r++) tracked_records += b.record_kind(r) == story_mail::kRecordTracked;
    assert(tracked_records == 1);
    assert(!toggle_tracked(b, qs[3].key) && tracked_key(b) == 0);
    assert(!toggle_tracked(b, qs[0].key) && tracked_key(b) == 0);  // a done quest can't be tracked
    // the tracked quest's place: only with a place, gone when done
    assert(toggle_tracked(b, qs[3].key));
    assert(!tracked_place(qs, b));
    qs[3].has_place = true, qs[3].x = 100000, qs[3].z = -200000;
    assert(tracked_place(qs, b) == &qs[3]);
    assert(finish(b, qs[3].key) == kStepDone && tracked_key(b) == 0 && !tracked_place(qs, b));
    // a quest of a mod that is off this session: its records stay, it has no row
    const uint16_t gone = quest_key("off", "old");
    assert(reach_step(b, gone, 4) == 4 && toggle_tracked(b, gone));
    auto rows2 = rows(qs, b);
    assert(rows2.size() == qs.size() && !tracked_place(qs, b));
    for (const Row& r : rows2) assert(!r.tracked);
}

void test_record() {
    // a vanilla save (all-zero block) stays all zero while nothing is started, and a block written by
    // something else is never touched
    uint8_t vanilla[story_mail::kBookSize] = {};
    Book v(vanilla);
    assert(rows({quest("a", "x")}, v).size() == 1 && tracked_key(v) == 0);
    for (uint8_t x : vanilla) assert(x == 0);
    uint8_t foreign[story_mail::kBookSize] = {};
    foreign[5] = 0x77;
    uint8_t copy[story_mail::kBookSize];
    memcpy(copy, foreign, sizeof copy);
    Book f(foreign);
    assert(reach_step(f, quest_key("a", "x"), 1) == -1 && !toggle_tracked(f, quest_key("a", "x")) && finish(f, 1) == -1);
    assert(!memcmp(copy, foreign, sizeof copy));
    // the layout of the records (format 2, kinds 4 and 5) and a round trip through the save bytes
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t k = quest_key("mod", "tale");
    assert(b.set(story_mail::kRecordItem, story_mail::kItemNotebook, 1));
    assert(reach_step(b, k, 3) == 3 && toggle_tracked(b, k));
    assert(raw[0] == story_mail::kMagic && raw[1] == 2 && raw[2] == 0 && raw[3] == 0);
    assert(raw[8] == 4 && raw[9] == uint8_t(k >> 8) && raw[10] == uint8_t(k) && raw[11] == 3);
    assert(raw[12] == 9 && raw[13] == uint8_t(k >> 8) && raw[14] == uint8_t(k) && raw[15] == 1);  // started first
    assert(raw[16] == 5 && raw[17] == uint8_t(k >> 8) && raw[18] == uint8_t(k) && raw[19] == 1);
    assert(start_order(b, k) == 1 && start_order(b, quest_key("mod", "other")) == 255);
    uint8_t saved_copy[story_mail::kBookSize];
    memcpy(saved_copy, raw, sizeof raw);  // saved and loaded with the save file
    Book loaded(saved_copy);
    assert(saved_step(loaded, k) == 3 && tracked_key(loaded) == k && loaded.get(story_mail::kRecordItem, story_mail::kItemNotebook) == 1);
    // untracking frees the record; other records stay where they are
    assert(!toggle_tracked(loaded, k) && saved_copy[16] == 0 && saved_copy[19] == 0 && saved_copy[11] == 3);
    // a full record: a new step can't be saved, existing ones still change
    uint8_t full[story_mail::kBookSize] = {};
    Book fb(full);
    for (unsigned i = 0; i < story_mail::kRecords; i++) assert(fb.set(story_mail::kRecordLetter, uint16_t(100 + i), 2));
    assert(reach_step(fb, k, 1) == -1 && !toggle_tracked(fb, k));
    assert(fb.set(story_mail::kRecordLetter, 100, 3));
}

void test_text() {
    assert(fit("short", 20, 400) == "short");
    const std::string cut = fit(std::string(200, 'a'), 20, 80);  // 10 characters
    assert(cut.size() == 10 && cut.ends_with("..."));
    auto w = wrap("one two three four five six seven eight nine ten", 20, 120, 2);  // 15 characters a line
    assert(w.size() == 2 && w[0] == "one two three" && w[1].ends_with("..."));
    auto nl = wrap("first\nsecond", 20, 1000, 4);
    assert((nl == std::vector<std::string>{"first", "second"}));
    assert(wrap("", 20, 100, 2).size() == 1);
}

PadInput press(uint32_t buttons) {
    PadInput p;
    p.buttons = buttons;
    return p;
}

void test_page() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs;
    for (int i = 0; i < 7; i++) qs.push_back(quest("m", "q" + std::to_string(i)));
    qs[2].has_place = true;
    for (const Quest& q : qs) reach_step(b, q.key, 1);  // started: only a quest in progress can be tracked
    auto rs = rows(qs, b);
    Page page;
    assert(!page.open() && page.draw(rs).empty());
    page.show();
    // the A that opened the page from the item menu is still held: nothing happens until released
    assert(page.input(press(kPadA), rs).kind == Page::Action::None && page.mode() == Page::Mode::List);
    assert(page.selected_key() == qs[0].key);
    page.input(press(0), rs);
    // down moves, and keeps the selection visible (5 rows shown)
    for (int i = 0; i < 6; i++) {
        page.input(press(kPadDown), rs);
        page.input(press(0), rs);
    }
    assert(page.selected_key() == qs[6].key && page.top() == 2);
    page.input(press(kPadDown), rs);  // the last row: stays
    page.input(press(0), rs);
    assert(page.selected_key() == qs[6].key);
    // the stick moves too, and held directions repeat
    PadInput stick;
    stick.ly = 0.9f;
    page.input(stick, rs);
    assert(page.selected_key() == qs[5].key);
    for (int i = 0; i < 18; i++) page.input(stick, rs);
    assert(page.selected_key() == qs[4].key);
    page.input(press(0), rs);
    // up to quest 2, open it, track it
    page.input(press(kPadUp), rs);
    page.input(press(0), rs);
    page.input(press(kPadUp), rs);
    page.input(press(0), rs);
    assert(page.selected_key() == qs[2].key);
    assert(page.input(press(kPadA), rs).kind == Page::Action::None && page.mode() == Page::Mode::Detail);
    page.input(press(0), rs);
    Page::Action a = page.input(press(kPadA), rs);
    assert(a.kind == Page::Action::Toggle && a.key == qs[2].key);
    page.input(press(0), rs);
    assert(toggle_tracked(b, a.key));
    rs = rows(qs, b);  // the tracked quest moves to the top; the selection follows the quest
    assert(rs[0].quest == &qs[2] && rs[0].tracked && page.selected_key() == qs[2].key);
    // detail drawing: the button says what A does now
    auto cmds = page.draw(rs);
    bool stop = false;
    for (const Command& c : cmds) stop |= c.kind == Command::Text && font::plain_text(c.text) == "Stop showing on chart";
    assert(stop);
    // B: back to the list, B again: the page closes
    page.input(press(kPadB), rs);
    assert(page.mode() == Page::Mode::List);
    page.input(press(0), rs);
    assert(page.input(press(kPadB), rs).kind == Page::Action::Close && !page.open());
    // touch: tap a row to open it, tap Show on chart, tap the close mark
    page.show();
    page.input(press(0), rs);
    PadInput tap;
    tap.touch = true, tap.x = kListX + 200, tap.y = kListY + kRowH * 1.5f;  // the second row
    page.input(tap, rs);
    assert(page.mode() == Page::Mode::Detail && page.selected_key() == rs[1].quest->key);
    page.input(press(0), rs);
    tap.x = kTrackX + 20, tap.y = kTrackY + 20;
    assert(!rs[1].quest->has_place && page.input(tap, rs).kind == Page::Action::None);  // no place: no Show on chart
    for (Quest& q : qs)
        if (&q == rs[1].quest) q.has_place = true;
    page.input(press(0), rs);
    a = page.input(tap, rs);
    assert(a.kind == Page::Action::Toggle && a.key == rs[1].quest->key);
    page.input(press(0), rs);
    tap.x = kCloseX, tap.y = kCloseY;
    assert(page.input(tap, rs).kind == Page::Action::Close);
    // a done quest has no Show on chart; the list draws its stamp
    assert(finish(b, qs[0].key) == kStepDone);
    rs = rows(qs, b);
    page.show();
    page.input(press(0), rs);
    cmds = page.draw(rs);
    bool title = false;
    for (const Command& c : cmds) {
        title |= c.kind == Command::Text && font::plain_text(c.text) == "Story Notebook";
        assert(std::isfinite(c.x) && std::isfinite(c.y));
    }
    assert(title);
    // the done quest is in the archive, not in the quests' list
    assert(page.tab() == 0 && page.shown(rs).size() == rs.size() - 1);
    for (const Row& r : page.shown(rs)) assert(r.status != Status::Done);
    page.input(press(kPadRight), rs);  // the archive
    page.input(press(0), rs);
    assert(page.tab() == 1 && page.shown(rs).size() == 1 && page.shown(rs)[0].quest == &qs[0]);
    assert(page.selected_key() == qs[0].key);
    bool archive = false;
    for (const Command& c : page.draw(rs)) archive |= font::plain_text(c.text) == "Archive (1)";
    assert(archive);
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    assert(page.mode() == Page::Mode::Detail);
    assert(page.input(press(kPadA), rs).kind == Page::Action::None);  // no Show on chart for a done quest
    page.input(press(kPadB), rs);
    page.input(press(0), rs);
    page.input(press(kPadLeft), rs);  // back to the quests
    page.input(press(0), rs);
    assert(page.tab() == 0 && page.mode() == Page::Mode::List);
    // a tap on a tab
    PadInput tab;
    tab.touch = true, tab.x = kTab1X + 10, tab.y = kTabY + 10;
    page.input(tab, rs);
    assert(page.tab() == 1);
    page.input(press(0), rs);
    // an empty notebook
    Page empty;
    empty.show();
    std::vector<Row> none;
    empty.input(press(0), none);
    assert(empty.input(press(kPadA), none).kind == Page::Action::None && empty.mode() == Page::Mode::List);
    bool hint = false;
    for (const Command& c : empty.draw(none)) hint |= c.text == "No stories yet.";
    assert(hint);
}

void test_bag_place() {
    // the icon pane at the item's root, the item in the bag window, the window on the screen root
    std::vector<PaneNode> chain = {{0, 0, 1, 1, false},       // the sentinel icon pane (hidden by the port)
                                   {-131, 130, 1, 1, true},   // the item's root pane
                                   {347, -92, 1, 1, true},    // the bag window
                                   {-73, -3, 1, 1, true}};    // the screen layout's root
    IconPlace at = place_icon(chain, 84, 255, 854, 480);
    assert(at.shown && at.x == 427 + 143 && at.y == 240 - 35 && at.size == 84 && at.alpha == 1);
    // the cursor's highlight scales the item: the icon grows around the same center
    chain[1].sx = chain[1].sy = 1.2f;
    at = place_icon(chain, 84, 255, 854, 480);
    assert(at.shown && at.x == 570 && at.y == 205 && std::fabs(at.size - 100.8f) < 0.01f);
    // a scaled parent scales the child's offset too (the window opening)
    chain[1].sx = chain[1].sy = 1;
    chain[2].sx = chain[2].sy = 0.5f;
    at = place_icon(chain, 84, 255, 854, 480);
    assert(at.x == 427 + 347 - 73 - 65.5f && at.y == 240 - (130 * 0.5f - 92 - 3) && at.size == 42);
    // the window slides
    chain[2].sx = chain[2].sy = 1;
    chain[2].tx += 100;
    assert(place_icon(chain, 84, 255, 854, 480).x == 670);
    // fades with the window, hides with it; NaN or zero scale while panes are built: not shown
    at = place_icon(chain, 84, 51, 854, 480);
    assert(at.shown && std::fabs(at.alpha - 0.2f) < 1e-6f);
    assert(!place_icon(chain, 84, 0, 854, 480).shown);
    chain[2].visible = false;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    chain[2].visible = true;
    chain[3].sx = NAN;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    chain[3].sx = 0;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    assert(!place_icon({}, 84, 255, 854, 480).shown);
}

void test_services() {
    // no save loaded
    float place_x=123,place_z=456;assert(!place_position("m","tale",place_x,place_z)&&place_x==123&&place_z==456);
    assert(define("m", "My mod", "tale", "The tale", "A heart piece", "") == kResultNew);
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == kResultNoRoom);
    st32(0x101F84DC, kSave);
    // arguments
    assert(define("m", "My mod", "Tale", "x", "", "") == kResultInvalid);
    assert(define("m", "My mod", "t2", "", "", "") == kResultInvalid);                     // no title
    assert(define("m", "My mod", "t2", "two\nlines", "", "") == kResultInvalid);
    assert(define("m", "My mod", "t2", std::string(kMaxTitleBytes + 1, 'a'), "", "") == kResultInvalid);
    assert(step("m", "unknown", 1, "", "") == kResultInvalid);                             // define first
    assert(step("m", "tale", 255, "", "") == kResultInvalid);
    assert(place("m", "tale", 400000, 0) == kResultInvalid && place("m", "tale", NAN, 0) == kResultInvalid);
    assert(known("m", "tale", std::string(kMaxKnownBytes + 1, 'a')) == kResultInvalid);
    assert(portrait("m", "tale", "../x.png", "/nonexistent") == kResultInvalid);
    assert(portrait("m", "tale", "assets/missing.png", "/nonexistent") == kResultInvalid);
    // a quest's life: new, steps, done; the save keeps only the step
    Book b = story_store::book();
    assert(state("m", "tale") == kResultNew && b.empty());
    assert(step("m", "tale", 0, "Read the letter.", "") == kResultNew && b.empty());  // texts only
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == 1);
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == 1);  // idempotent
    assert(place("m", "tale", 0, -200000) == 1 && known("m", "tale", "A storyteller came to Windfall.\nHe looks for Link.") == 1);
    const std::vector<uint8_t> before_place_area(story_store::area(),story_store::area()+story_block::kAreaSize);
    const std::vector<uint8_t> before_place_save(mem::ptr(kSave),mem::ptr(kSave)+0x800);
    assert(place_position("m","tale",place_x,place_z)&&place_x==0&&place_z==-200000);
    assert(!place_position("foreign","tale",place_x,place_z)&&!place_position("m","unknown",place_x,place_z)&&!place_position("m","Tale",place_x,place_z));
    assert(std::equal(before_place_area.begin(),before_place_area.end(),story_store::area())&&std::equal(before_place_save.begin(),before_place_save.end(),mem::ptr(kSave)));
    assert(saved_step(b, quest_key("m", "tale")) == 1);
    assert(define("m", "My mod", "later", "Later", "", "Finish the tale first") == kResultNew);
    std::vector<Quest> storage;
    auto rs = rows_now(storage);
    assert(rs.size() == 2 && rs[0].quest->id == "tale" && rs[0].status == Status::InProgress && rs[1].status == Status::NotAvailable);
    assert(rs[0].quest->has_place && rs[0].quest->where == "Windfall Island" && rs[0].quest->mod_name == "My mod");
    // the tracked quest on the sea chart (story_chart's source)
    assert(toggle_tracked(b, quest_key("m", "tale")));
    assert(no_place("m", "tale") == 1);
    assert(!place_position("m","tale",place_x,place_z)&&place_x==0&&place_z==-200000);
    assert(step("m", "tale", 2, "Bring him a Skull Necklace.", "") == 2);
    assert(done("m", "tale") == kResultDone && done("m", "tale") == kResultDone && state("m", "tale") == kResultDone);
    assert(tracked_key(b) == 0 && step("m", "tale", 3, "", "") == kResultDone);
    assert(!place_position("m","tale",place_x,place_z));
    // per-mod limit
    for (unsigned i = 2; i < kMaxQuestsPerMod; i++) assert(define("m", "My mod", "q" + std::to_string(i), "Q", "", "") == kResultNew);
    assert(define("m", "My mod", "one-more", "Q", "", "") == kResultNoRoom);
    assert(define("m", "My mod", "tale", "The tale, renamed", "", "") == kResultDone);  // an update is no new quest
}

// the game's code for the item menu hooks
uint8_t g_reserve[8] = {0x99, 0xFF, 0xFF, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF};  // Note to Mom, -, -, Cabana Deed, ...
std::map<uint32_t, void (*)(Cpu*)> hooked, mail_hooked;
std::vector<std::string> calls;
// the King of Red Lions (daShip_c) stubs: his forced line pending, the seen angle, the game's own orders
uint32_t g_ship_force = 0, g_seen_angle = 0, g_game_order = 0;
constexpr uint32_t kShipHint = 0xD5C;  // the talk hint the stub's setInitMessage picks
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x02477A24: {  // daShip_c::execute: a talk command starts the talk, else checkForceMessage and the forced order
        const uint32_t ship = c->r[3];
        calls.push_back("ship execute");
        if (ld16(ship + 0xF8) == 1 && ld8(ship + 0x635) != 8) {
            Cpu t = *c;
            hooked.at(0x024749DC)(&t);
        } else if (ld8(ship + 0x635) != 8) {
            Cpu f = *c;
            hooked.at(0x024731BC)(&f);
            if (f.r[3] || g_game_order) st8(0x20030000 + 0x5290, 1);  // the game orders its own talk
        }
        st32(ship + 0x39C, 0x80);  // attention flags are set anew each execute (here: "board")
        st16(ship + 0xFA, 0);
        c->r[3] = 1;
        break;
    }
    case 0x024731BC:  // checkForceMessage
        if (g_ship_force) st32(c->r[3] + 0x648, g_ship_force);
        c->r[3] = g_ship_force != 0;
        break;
    case 0x024749DC: {  // procTalk_init: the talk mode; setInitMessage picks the hint unless Link rides
        const uint32_t ship = c->r[3];
        calls.push_back("talk init");
        st8(ship + 0x635, 8);
        if (!(ld32(0x20030000 + 0x5CD8) & 0x10000)) st32(ship + 0x648, kShipHint);
        c->r[3] = 1;
        break;
    }
    case 0x025F50E8: break;  // the message manager's getMessage: the stub leaves the out struct as given
    case 0x0273A2E8: c->r[3] = 0; break;  // the MSBT text: none (no leading codes)
    case 0x02632D94: c->r[3] = c->r[3] >= 0x30 && c->r[3] < 0x38 ? g_reserve[c->r[3] - 0x30] : 0x01; break;
    case 0x0262F880: c->r[3] = c->r[4] == 0x99 ? 0x26 : c->r[4] == 0x9C ? 0x24 : 0x37; break;
    case 0x0268F060: calls.push_back("caption"); break;
    case 0x02630A64: calls.push_back("pick up"); break;
    case 0x02630FC4: calls.push_back("put back"); break;
    case 0x0268FF50: calls.push_back("window"); break;
    case 0x026302B8: calls.push_back("float"); break;
    case 0x026333F0: {  // setVisible: the icon pane follows the item's visibility
        const uint32_t icon = ld32(ld32(c->r[3] + 0x8C + 4));
        // The real function (0263340C..0263343C) only updates the pane when its
        // cached mVisible changes. A second setVisible(true) cannot repair a hidden pane.
        if (ld8(c->r[3] + 0xFC) != c->r[4]) {
            st8(c->r[3] + 0xFC, uint8_t(c->r[4]));
            st8(icon + 0x44, uint8_t((ld8(icon + 0x44) & ~1u) | (c->r[4] & 1)));
        }
        calls.push_back("visible");
        break;
    }
    case 0x0268F420: c->r[3] = c->r[4]; break;  // the stub's receivers are the keys themselves
    case 0x02714B0C: calls.push_back("assign " + std::to_string(c->r[5])); break;
    case 0x022C6C68: calls.push_back("getMsg"); c->r[3] = 0x2CF1; break;  // the resident's own line
    case 0x022C36D0: calls.push_back("resident"); break;
    case 0x022C6108: calls.push_back("next"); c->r[3] = 0x10; break;  // next_msgStatus: the game's talk ends
    default: assert(!"unexpected original");
    }
}
constexpr uint32_t kPlayObj = 0x20030000, kItemActor = 0x2000A000;
uint32_t get_item_over = 0;
uint32_t game_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    switch (fn) {
    case 0x0254DA38: calls.push_back("action reward " + std::to_string(a.at(0)));return 0;
    case 0x025200D4: return kPlayObj;
    case 0x02006478: return ld32(a.at(0) + 4);  // hdStateMachine: the current state
    case 0x025D7DEC: calls.push_back("present item " + std::to_string(a.at(1))); return 77;
    case 0x025D7B98: calls.push_back("order item event " + std::to_string(a.at(0) == kItemActor)); return 1;
    case 0x025D54C4: st32(a.at(1), a.at(0) == 77 ? kItemActor : 0); return 1;  // fopAcM_SearchByID
    case 0x0254457C: assert(a.at(0) == kPlayObj + 0x52C4 && a.at(1) == 0x10011EB8); return get_item_over;  // endCheck
    case 0x025D76A8: calls.push_back("order speak"); return 1;  // fopAcM_orderSpeakEvent
    case 0x025D68A0: return g_seen_angle;                         // fopAcM_seenActorAngleY
    default: assert(!"unexpected game call"); return 0;
    }
}
uint32_t call(uint32_t fn, uint32_t r3, uint32_t r4 = 0, uint32_t lr = 0, uint32_t r5 = 0) {
    Cpu c{};
    c.r[1] = 0x2003F000;  // a stack in the synthetic arena
    c.r[3] = r3, c.r[4] = r4, c.r[5] = r5, c.lr = lr;
    hooked.at(fn)(&c);
    return c.r[3];
}

void test_hooks() {
    constexpr uint32_t kMenu = kArena + 0x2000, kChildData = kArena + 0x3000, kChild = kArena + 0x4000;
    constexpr uint32_t kBuild = 0x02692548;  // the bag window's build
    // not installed without a quest service; all or nothing on a conflict
    set_game_calls(original, game_call);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x02714B0C ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 13);
    Book b = story_store::book();
    // no notebook yet: the bag window shows the save as it is
    b.erase(story_mail::kRecordItem, story_mail::kItemNotebook);
    for (uint32_t k = 0x30; k < 0x38; k++) assert(call(0x02632D94, k, 0, kBuild) == g_reserve[k - 0x30]);
    // the notebook in the first empty place, for the bag window only (not for X/Y/R or other callers)
    assert(b.set(story_mail::kRecordItem, story_mail::kItemNotebook, 1));
    assert(call(0x02632D94, 0x31, 0, kBuild) == 0xA0 && call(0x02632D94, 0x31, 0, 0x02693C24) == 0xA0);
    assert(call(0x02632D94, 0x32, 0, kBuild) == 0xFF && call(0x02632D94, 0x30, 0, kBuild) == 0x99);
    assert(call(0x02632D94, 0x31, 0, 0x0266FDFC) == 0xFF);  // the X/Y/R strip
    assert(call(0x02632D94, 0x12, 0, kBuild) == 0x01);       // other keys untouched
    // its icon: the parchment; other items keep theirs
    assert(call(0x0262F880, 0, 0xA0) == 0x24 && call(0x0262F880, 0, 0x99) == 0x26 && call(0x0262F880, 0, 0xFF) == 0x37);
    // A on its place opens the page, not a caption; A on a game item shows the game's caption
    st32(kMenu + 0x48, 0x2E);
    st32(kMenu + 0x4C, kChildData);
    st32(kChildData + 4 * 0x15, kChild);
    st32(kChild + 0xEC, 0xA0);
    st32(kChildData + 4 * 0x14, kChild + 0x200);
    st32(kChild + 0x200 + 0xEC, 0x99);
    calls.clear();
    call(0x0268F060, kMenu, 0x14);
    assert(calls == std::vector<std::string>{"caption"} && !page_open());
    call(0x0268F060, kMenu, 0x15);
    assert(calls.size() == 1 && page_open());
    // while open the game's controller is idle, and the B that closes the page stays with the page
    input::PadState p;
    p.buttons = input::kA;
    pad(p);
    assert(p.buttons == 0 && page_open());
    p = {};
    pad(p);
    p.buttons = input::kB;
    pad(p);
    assert(!page_open() && p.buttons == 0);
    input::PadState pro;
    pro.buttons = input::kB | input::kY;
    mute(pro);
    assert(pro.buttons == input::kY);  // the Pro Controller read: the held B is kept back too
    p.buttons = input::kB;
    pad(p);
    assert(p.buttons == 0);
    p = {};
    pad(p);  // released
    p.buttons = input::kB;
    pad(p);
    assert(p.buttons == input::kB);  // a new press reaches the game
    // a tap on the notebook: the game's pick-up and put-back, then the page; on a game item only the
    // game's; a touch that becomes a drag (lifted towards X/Y/R) opens nothing
    calls.clear();
    call(0x02630A64, kChild + 0x200);
    call(0x02630FC4, kChild + 0x200);
    assert((calls == std::vector<std::string>{"pick up", "put back"}) && !page_open());
    call(0x02630A64, kChild);
    assert(calls.size() == 3 && !page_open());
    call(0x026302B8, kChild);  // lifted
    call(0x02630FC4, kChild);
    assert(!page_open());
    call(0x02630A64, kChild);
    call(0x02630FC4, kChild);
    assert(page_open());
    close_page();
    p = {};
    pad(p);
    // A ready layout must not suppress gameplay rendering when the item screen isn't drawn.
    story_notebook_layout::test_layout_ready = true;
    for (bool pro : {false, true}) {
        input::g_pro = pro;
        for (int cycle = 0; cycle < 6; ++cycle) {
            assert(open_page_from_button(0));
            input::PadState held;
            held.buttons = input::kX;
            pad(held);
            assert(page_open() && held.buttons == 0);  // opening press isn't a toggle
            auto lists = guestmods::hud::store().snapshot(0);
            bool visible = false;
            for (const auto& list : lists)
                visible |= list->owner == "port:story-notebook" && !list->commands.empty();
            assert(visible);  // visible on TV for either controller, without a menu draw
            held = {};
            pad(held);
            if (cycle == 5) {
                held.buttons = input::kA;
                pad(held); // the toggle must also close from a quest's detail page
                assert(page_open());
                held = {};
                pad(held);
            }
            held.buttons = cycle % 2 ? input::kX : input::kB;
            pad(held);
            assert(!page_open() && held.buttons == 0);
            held.buttons = input::kA;
            pad(held);
            assert(held.buttons == input::kA);  // gameplay gets input again
        }
    }
    // Close without any layout draw: coverage must still end (otherwise the game cursor
    // stays missing when the Pro Controller's menu is opened afterwards).
    assert(open_page());
    p = {};
    pad(p);
    assert(story_notebook_layout::drawing());
    p.buttons = input::kB;
    pad(p);
    assert(!page_open());
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        p = {}; pad(p);
    }
    assert(!story_notebook_layout::drawing());
    input::g_pro = false;
    story_notebook_layout::test_layout_ready = false;
    close_page();
    // With only the HUD available, a stage transition still releases input before another draw.
    assert(open_page_from_button(0));
    Cpu context_cpu{};
    p = {};
    pad(p, &context_cpu);
    assert(page_open());
    st32(kPlayObj + 0x5134, 0x73656100); // a new start-stage record
    pad(p, &context_cpu);
    assert(!page_open());
    st32(kPlayObj + 0x5134, 0);
    p.buttons = input::kA;
    pad(p);
    assert(p.buttons == input::kA);
    // setVisible preserves the game's icon state; hiding lasts only for a draw pass.
    const uint32_t kGroups = kArena + 0x5000, kIconA = kArena + 0x5100, kIconB = kArena + 0x5200;
    st32(kChild + 0x8C + 4, kGroups);
    st32(kGroups, kIconA);
    st32(kChild + 0x200 + 0x8C + 4, kGroups + 0x10);
    st32(kGroups + 0x10, kIconB);
    calls.clear();
    call(0x026333F0, kChild, 1);
    call(0x026333F0, kChild + 0x200, 1);
    assert(calls.size() == 2 && (ld8(kIconA + 0x44) & 1) && (ld8(kIconB + 0x44) & 1));
    // the notebook goes on X/Y/R in the story block (record 15), never in the save's key; a game item
    // put on the same button takes it back
    calls.clear();
    assert(call(0x0268F420, kMenu, 0x31) == 0x31 && call(0x0268F420, kMenu, 0x30) == 0x30);
    call(0x02714B0C, 0, 1, 0, 0x31);
    assert(calls.empty() && story_items::notebook_on_button(1) && story_item_buttons::button_item(1) == 0xA0);
    call(0x02714B0C, 0, 1, 0, 0x30);
    assert(calls == std::vector<std::string>{"assign 48"} && !story_items::notebook_on_button(1) && !story_item_buttons::button_item(1));
    // a full bag: no place for the notebook until one is free
    uint8_t keep[8];
    memcpy(keep, g_reserve, 8);
    memset(g_reserve, 0x8C, 8);
    assert(call(0x02632D94, 0x31, 0, kBuild) == 0x8C);
    memcpy(g_reserve, keep, 8);
    // the tracked quest's marker reaches the sea chart layer
    assert(define("c", "Chart mod", "chart", "Charted", "", "") == kResultNew);
    assert(step("c", "chart", 1, "Sail to the island.", "Windfall Island") == 1 && place("c", "chart", 0, -200000) == 1);
    assert(story_chart::shown_markers().empty());
    assert(toggle_tracked(b, quest_key("c", "chart")));
    auto shown = story_chart::shown_markers();
    assert(shown.size() == 1 && shown[0].x == 0 && shown[0].z == -200000 && shown[0].label == "Charted");
    assert(place("c", "chart", 100000, 0) == 1 && story_chart::shown_markers()[0].x == 100000);  // moves with the quest
    assert(no_place("c", "chart") == 1 && story_chart::shown_markers().empty());               // a step without a place
    assert(place("c", "chart", 0, 0) == 1 && story_chart::shown_markers().size() == 1);
    assert(done("c", "chart") == kResultDone && story_chart::shown_markers().empty());        // gone when done
    // ---- the notebook's icon over its bag place: only while the Delivery Bag window runs ----
    {
        // panes: the place's icon pane under its root pane, under the window, under the screen root
        const uint32_t kLayout = kArena + 0x6000, kLyt = kArena + 0x6100, kRoot = kArena + 0x6200, kWin = kArena + 0x6300,
                       kScreen = kArena + 0x6400;
        auto pane = [](uint32_t p, uint32_t parent, float x, float y) {
            st32(p + 0xC, parent);
            st32(p + 0x1C, f32_as_u32(x)), st32(p + 0x20, f32_as_u32(y));
            st32(p + 0x34, f32_as_u32(1)), st32(p + 0x38, f32_as_u32(1));
            st8(p + 0x44, 0x01), st8(p + 0x46, 255);
        };
        pane(kIconA, kRoot, 0, 0);
        pane(kRoot, kWin, -131, 130);
        pane(kWin, kScreen, 347, -92);
        pane(kScreen, 0, 0, 0);  // the screen root (1280 x 720 layout)
        st32(kChild + 0x44, kLayout), st32(kLayout + 4, kLyt), st32(kLyt + 0xC, kRoot);
        st8(kChild + 0xFC, 1), st8(kChild + 0x200 + 0xFC, 1);
        st32(kMenu + 0x54, 2);  // the Delivery Bag window
        // a floating icon (the menu's sMenuIcon) showing the notebook is not its place
        const uint32_t kFloat = kArena + 0x7000;
        st32(kFloat + 0xEC, 0xA0), st8(kFloat + 0xFC, 1), st32(kFloat + 0x8C + 4, kGroups + 0x20), st32(kGroups + 0x20, kArena + 0x7100);
        call(0x026333F0, kFloat, 1);
        next_frame();
        assert(!icon_place().shown && std::string(icon_reason()) == "no bag window yet");
        // the window runs: the icon is over the place, the sentinel icon hidden
        st8(kIconA + 0x44, 1);
        calls.clear();
        call(0x0268FF50, kMenu);
        assert(calls == std::vector<std::string>{"window"});
        next_frame();
        IconPlace at = icon_place();
        // (640 + 216, 360 - 38) in the 1280 x 720 layout, on the 854 x 480 GamePad picture
        assert(at.shown && std::fabs(at.x - 856.0f * 854 / 1280) < 0.01f && std::fabs(at.y - 322.0f * 854 / 1280) < 0.01f &&
               std::fabs(at.size - 90.0f * 854 / 1280) < 0.01f && (ld8(kIconA + 0x44) & 1));
        // Repeated draw passes must restore every byte, including unrelated flag bits and alpha.
        // The floating icon is then reused for bait without another setVisible call.
        const uint32_t floating_pane = kArena + 0x7100;
        st32(0x101F5578, kFloat);
        for (int cycle = 0; cycle < 6; ++cycle) {
            st8(kIconA + 0x44, 0xA5);
            st8(floating_pane + 0x44, 0xB1);
            st8(floating_pane + 0x45, 123);
            std::vector<uint8_t> before(mem::ptr(kIconA), mem::ptr(kIconA) + 0x100);
            hide_floating_notebook_icon();
            assert(ld8(kIconA + 0x44) == 0xA4 && ld8(floating_pane + 0x44) == 0xB0);
            restore_notebook_icons();
            assert(!memcmp(before.data(), mem::ptr(kIconA), before.size()));
            assert(ld8(floating_pane + 0x44) == 0xB1 && ld8(floating_pane + 0x45) == 123);
            st32(kFloat + 0xEC, 0x8C); // bait reuses the floating icon
            hide_floating_notebook_icon();
            assert(ld8(floating_pane + 0x44) == 0xB1);
            restore_notebook_icons();
            st32(kFloat + 0xEC, 0xA0);
            // The real #9 reproduction: a Delivery Bag notebook place is reused for a
            // Hyoi Pear when switching to the bait bag. mVisible stays true, so the
            // game's setVisible(true) skips its pane write. Our draw must have restored it.
            assert(ld8(kChild + 0xFC) == 1);
            st32(kChild + 0xEC, 0x83);
            st32(kMenu + 0x54, 1);
            call(0x0268FF50, kMenu);
            call(0x026333F0, kChild, 1);
            hide_floating_notebook_icon();
            assert(ld8(kIconA + 0x44) == 0xA5);
            restore_notebook_icons();
            assert(ld8(kIconA + 0x44) == 0xA5 && ld8(kChild + 0xFC) == 1);
            st32(kChild + 0xEC, 0xA0);
            st32(kMenu + 0x54, 2);
            call(0x0268FF50, kMenu);
            std::vector<uint8_t> menu_before(mem::ptr(kMenu), mem::ptr(kMenu) + 0x100);
            std::vector<uint8_t> item_before(mem::ptr(kChild), mem::ptr(kChild) + 0x400);
            call(0x0268F060, kMenu, 0x15);
            assert(page_open());
            input::PadState idle;
            pad(idle);
            idle.buttons = input::kB;
            pad(idle);
            assert(!page_open());
            assert(!memcmp(menu_before.data(), mem::ptr(kMenu), menu_before.size()));
            assert(!memcmp(item_before.data(), mem::ptr(kChild), item_before.size()));
            assert(ld8(kIconA + 0x44) == 0xA5 && (ld8(kIconB + 0x44) & 1));
        }
        for (int i = 0; i < 4; i++) assert(icon_place().shown);  // frames drawn within the step (60/120 fps) keep it
        // the notebook page open over the menu: no bag icon over it
        call(0x0268F060, kMenu, 0x15);
        assert(page_open() && !icon_place().shown && std::string(icon_reason()) == "the notebook page is open");
        close_page();
        assert(icon_place().shown);
        call(0x0268FF50, kMenu);
        next_frame();
        assert(icon_place().shown);
        // the Pro Controller: the menu is on the TV only while the game is paused in it (the in-game UI's
        // state Menu or MenuSubWindow); closing it with + leaves the bag window running unseen
        {
            constexpr uint32_t kUiObj = kArena + 0x3F000;
            st32(0x101F8344, kUiObj), st32(kUiObj + 0x18, 0);
            input::g_pro = true;
            st32(kUiObj + 0x14, 0x1049ECDC);  // MenuSubWindow: the bag window is shown
            call(0x0268FF50, kMenu);
            assert(icon_place().shown);
            call(0x0268F060, kMenu, 0x15);
            assert(page_open());
            st32(kUiObj + 0x14, 0x1049EC00);  // Play: the menu was closed with +
            call(0x0268FF50, kMenu);
            assert(!icon_place().shown && std::string(icon_reason()) == "the menu is closed (Pro Controller)");
            assert(!page_open());  // an external close must release notebook input too
            assert(open_page_from_button(0));
            call(0x0268FF50, kMenu);
            assert(page_open());  // gameplay entry does not depend on the hidden menu
            close_page();
            input::g_pro = false;
            call(0x0268FF50, kMenu);
            assert(icon_place().shown);  // the GamePad: the menu is always shown
            st32(0x101F8344, 0);
            call(0x0268FF50, kMenu);
            next_frame();
        }
        // the window closes (Back, B, the menu closes, a tab switch): its state no longer runs, the icon goes
        next_frame();
        assert(!icon_place().shown && std::string(icon_reason()) == "the bag window is closed");
        // An outside close can stop executeWindow entirely. The next input read must
        // release the notebook even when that hook never gets a chance to run again.
        st32(kMenu + 0x54, 2);
        call(0x0268FF50, kMenu);
        call(0x0268F060, kMenu, 0x15);
        assert(page_open());
        st32(kMenu + 0x54, 0);
        input::PadState outside_close;
        pad(outside_close);
        assert(!page_open());
        // another bag's window: no icon
        call(0x0268FF50, kMenu);
        next_frame();
        assert(!icon_place().shown);
        st32(kMenu + 0x54, 2);
        // the window fading out (global alpha 0) or hidden
        call(0x0268FF50, kMenu);
        next_frame();
        st8(kRoot + 0x46, 0);
        assert(!icon_place().shown);
        st8(kRoot + 0x46, 255);
        st8(kWin + 0x44, 0);
        assert(!icon_place().shown);
        st8(kWin + 0x44, 1);
        assert(icon_place().shown);
        // X/Y/R lifts the notebook towards the button, as a game item
        calls.clear();
        call(0x026302B8, kChild);
        call(0x026302B8, kChild + 0x200);
        assert((calls == std::vector<std::string>{"float", "float"}));
        // a state load: nothing until the window runs again
        state_loaded();
        next_frame();
        assert(!icon_place().shown);
        call(0x0268FF50, kMenu);
        next_frame();
        assert(icon_place().shown);
        // repeated opening by A and by touch, closing with B each time
        for (int i = 0; i < 3; i++) {
            call(0x0268F060, kMenu, 0x15);
            assert(page_open());
            input::PadState p2;
            pad(p2);  // the A that opened it was released
            p2.buttons = input::kB;
            pad(p2);
            assert(!page_open());
            p2 = {};
            pad(p2);
            call(0x02630A64, kChild);
            call(0x02630FC4, kChild);
            assert(page_open());
            pad(p2);  // the touch ended
            p2.buttons = input::kB;
            pad(p2);
            p2 = {};
            pad(p2);
            assert(!page_open());
        }
    }
    // a state load closes the page
    call(0x0268F060, kMenu, 0x15);
    assert(page_open());
    state_loaded();
    assert(!page_open());
}

void test_page_screen() {
    // the page goes where the game's item menu is: the GamePad, or the TV with the Pro Controller
    assert(menu_screen(false) == 1 && menu_screen(true) == 0);
    Page page;
    page.show();
    std::vector<Row> none;
    std::vector<Command> tv = page.draw(none), pad = page.draw(none);
    fit_to_screen(tv, 0);
    fit_to_screen(pad, 1);
    assert(tv.size() == pad.size());
    for (size_t i = 0; i < tv.size(); i++) {
        assert(std::fabs(pad[i].x - tv[i].x * 854 / 1280) < 0.01f && std::fabs(pad[i].size - tv[i].size * 854 / 1280) < 0.01f);
        if (tv[i].kind == Command::Rect) assert(std::fabs(pad[i].w - tv[i].w * 854 / 1280) < 0.01f);
    }
}


// ---- the page in the game's look (story_notebook_font.h, story_notebook_style.h) ----

// a synthetic font: every character a w x 20 block of coverage, advance w + 2
font::Face block_font(const std::string& chars, int w, int h = 20) {
    font::Face f;
    f.cell_height = h;
    for (uint32_t cp : font::codes(chars)) {
        game_glyph::Glyph g;
        g.advance = w + 2, g.bitmap.width = cp == ' ' ? 0 : w, g.bitmap.height = h;
        g.bitmap.rgba.assign(size_t(g.bitmap.width) * h * 4, 255);
        f.glyphs[cp] = g;
    }
    return f;
}

void test_game_font() {
    // UTF-8 decoding and the private-use button characters
    assert((font::codes("Aé\xEE\x80\x80") == std::vector<uint32_t>{'A', 0xE9, 0xE000}));
    font::Face main = block_font("abc ?", 8), fallback = block_font("\xC3\xA9", 10);  // é only in the fallback
    assert(font::line_pixels(main, "ab c") == 40 && font::measure(main, "ab", 40) == 40.0f);
    std::vector<uint32_t> want;
    main.wanted("abz", want);
    assert((want == std::vector<uint32_t>{'z'}));
    // the atlas: each distinct line (and colour) once, at the font's height; a button as wide as the line is tall
    font::Atlas atlas;
    const uint32_t ink = 0x003800FF;
    const std::vector<std::string> lines = {font::line_key("abc", ink), font::line_key("a\xC3\xA9", ink), font::line_key("abc", ink),
                                            font::line_key(font::kGlyphA, 0xFFFFFFFF), font::line_key(font::kBold + "abc", ink)};
    atlas.build(lines, main, &fallback, nullptr, {}, 1);
    assert(atlas.holds(lines, {}, 1) && !atlas.holds(lines, {}, 2) && !atlas.holds({font::line_key("new", ink)}, {}, 1));
    assert(!atlas.holds({font::line_key("abc", 0xFFFFFFFF)}, {}, 1));  // another colour: another line
    const font::Atlas::Cell* abc = atlas.cell(font::line_key("abc", ink));
    assert(abc && abc->width == 30 && abc->height == 20);
    assert(atlas.cell(font::line_key("a\xC3\xA9", ink))->width == 22);  // é from the fallback font
    assert(atlas.cell(font::line_key(font::kBold + "abc", ink))->width == 30);  // the style code takes no room
    const font::Atlas::Cell* button = atlas.cell(font::line_key(font::kGlyphA, 0xFFFFFFFF));
    assert(button && button->width == 20);  // no icon font: the port's own disc
    auto img = atlas.image();
    assert(img && img->width == font::Atlas::kWidth && img->height >= 20);
    // a pixel of 'a' in the atlas: the line's colour, opaque
    const size_t at = (size_t(abc->v0 * img->height + 5) * img->width + size_t(abc->u0 * img->width) + 3) * 4;
    assert(img->rgba[at] == 0x00 && img->rgba[at + 1] == 0x38 && img->rgba[at + 3] == 255);
    // half the size: the line is half as tall and wide
    font::Atlas half;
    half.build({font::line_key("abc", ink)}, main, nullptr, nullptr, {}, 1, 0.5f);
    assert(half.cell(font::line_key("abc", ink))->height == 10 && half.cell(font::line_key("abc", ink))->width == 15);
    // sized lines: composed at the screen's pixel height (whole pixels, then 8 % steps never below it)
    assert(font::pixel_height(12.3f) == 13 && font::pixel_height(16.0f) == 16 && font::pixel_height(0) == 1);
    for (float px : {17.0f, 23.5f, 31.0f, 46.2f, 97.0f}) {
        const int h = font::pixel_height(px);
        assert(h >= px && h <= px * 1.09f + 1);
    }
    assert(font::pixel_height(46.2f) == font::pixel_height(45.0f));  // a resize by a pixel reuses the lines
    {
        // a bold glyph with the game's look: letters at full coverage inside a 2-texel border at half,
        // 40 texels tall; shrunk 4 x its border is half a pixel, and the line still gets a border
        font::Face big;
        big.cell_height = 40;
        game_glyph::Glyph g;
        g.advance = 24, g.bitmap.width = 20, g.bitmap.height = 40;
        g.bitmap.rgba.assign(20 * 40 * 4, 255);
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 20; x++)
                if (x < 2 || x >= 18 || y < 2 || y >= 38) g.bitmap.rgba[(size_t(y) * 20 + x) * 4 + 3] = 128;
        big.glyphs['a'] = g;
        const std::string key = font::sized_key(font::kBold + "a", ink, 10);
        font::Atlas sized;
        sized.build({key, font::line_key("a", ink)}, big, nullptr, nullptr, {}, 1, 1.0f);
        const font::Atlas::Cell* c = sized.cell(key);
        assert(c && c->height == 10 && c->width == 8 && c->margin == 1 && sized.cell(font::line_key("a", ink))->height == 40);
        auto im = sized.image();
        const size_t x0 = size_t(c->u0 * im->width + 0.5f), y0 = size_t(c->v0 * im->height + 0.5f);
        auto px = [&](int x, int y) { return &im->rgba[((y0 + y) * im->width + x0 + x) * 4]; };
        // the middle: the ink; left of the letter (the cell's border room): the light border, opaque enough to be seen
        assert(px(3, 5)[1] < 0x50 && px(3, 5)[3] == 255);
        assert(px(0, 5)[0] > 200 && px(0, 5)[3] > 100);
        // the same glyph not shrunk keeps its own 2-pixel border
        const std::string full = font::sized_key(font::kBold + "a", ink, font::pixel_height(40.0f));  // 41
        sized.build({full}, big, nullptr, nullptr, {}, 1);
        c = sized.cell(full), im = sized.image();
        const size_t fx = size_t(c->u0 * im->width + 0.5f), fy = size_t(c->v0 * im->height + 0.5f);
        const uint8_t* edge = &im->rgba[((fy + 20) * im->width + fx + 1) * 4];
        assert(edge[0] > 200 && edge[3] > 100);
        // to_pictures finds the sized line of a Text command at that screen's pixels
        Command st;
        st.kind = Command::Text, st.x = 0, st.y = 0, st.size = 40, st.rgba = ink, st.text = font::kBold + "a";
        auto pics = font::to_pictures({st}, &sized, 1.0f, 1.0f);
        assert(pics.size() == 1 && pics[0].kind == Command::Picture && pics[0].h == 40 && pics[0].x < 0 && pics[0].x > -1);  // the border room left of the line
        assert((font::lines_of({st}, 1.0f, 0.25f) == std::vector<std::string>{key}));
    }
    // Text commands become pictures of their lines, scaled to the text size, faded by their alpha
    Command t;
    t.kind = Command::Text, t.x = 100, t.y = 50, t.size = 40, t.rgba = 0x00380080, t.text = "abc";
    Command r;
    r.kind = Command::Rect;
    Command b = t;
    b.text = font::kGlyphB;  // not in the atlas: drawn as a disc with its letter
    atlas.build({font::line_key("abc", t.rgba)}, main, nullptr, nullptr, {}, 1);
    abc = atlas.cell(font::line_key("abc", t.rgba));
    auto out = font::to_pictures({r, t, b}, &atlas, 1.0f);
    assert(out.size() == 4 && out[0].kind == Command::Rect && out[1].kind == Command::Picture);
    assert(out[1].h == 40 && out[1].w == 60 && out[1].rgba == 0xFFFFFF80 && out[1].u0 == abc->u0);
    assert(out[2].kind == Command::Circle && out[3].kind == Command::Text && out[3].text == "B");
    // without the atlas, text stays text (the overlay font), without its style code
    Command boldtext = t;
    boldtext.text = font::kBold + "abc";
    out = font::to_pictures({boldtext}, nullptr);
    assert(out.size() == 1 && out[0].kind == Command::Text && out[0].text == "abc");
    // the button pictures follow the prompt setting: the PlayStation look is the port's shapes
    game_glyph::Glyph icon;
    icon.colour = true, icon.bitmap.width = icon.bitmap.height = 4, icon.bitmap.rgba.assign(64, 255);
    font::Prompts ps;
    ps.face = font::Prompts::PlayStation;
    assert(font::button_picture(&icon, 0, ps, 20).width == 64);
    font::Prompts xbox;
    xbox.face = font::Prompts::Xbox, xbox.colours = true;
    const game_glyph::Bitmap green = font::button_picture(&icon, 0, xbox, 20);
    assert(green.width == 4 && green.rgba[0] == 0x58 && green.rgba[1] == 0xBE && green.rgba[3] == 255);  // Xbox A green
    assert(font::button_picture(&icon, 0, {}, 20).rgba[1] == 255);  // the game's own icon
    // the page's text measure follows the game font when set
    g_text_measure = [](const std::string& s, float size) { return float(s.size()) * size; };
    assert(text_width("abcd", 10) == 40 && fit("abcdefgh", 10, 60) == "abc...");
    g_text_measure = nullptr;
}

style::Art synthetic_art() {
    style::Art a;
    auto solid = [](int w, int h, uint8_t l, uint8_t alpha) {
        game_glyph::Bitmap b;
        b.width = w, b.height = h;
        b.rgba.resize(size_t(w) * h * 4);
        for (size_t i = 0; i < b.rgba.size(); i += 4) b.rgba[i] = b.rgba[i + 1] = b.rgba[i + 2] = l, b.rgba[i + 3] = alpha;
        return b;
    };
    // the frame pieces: a white line 12..16 texels in from the outside, nothing elsewhere
    for (int k = 0; k < 8; k++) {
        const bool corner = k < 4, side = k == 4 || k == 5;
        a.edge[k] = solid(corner || side ? 48 : 256, corner || !side ? 48 : 256, 255, 0);
        for (int y = 0; y < a.edge[k].height; y++)
            for (int x = 0; x < a.edge[k].width; x++) {
                const int ox = (k == 1 || k == 3 || k == 5) ? 47 - x : x, oy = (k == 2 || k == 3 || k == 7) ? 47 - y : y;
                const bool on = (corner && ((ox >= 12 && ox < 16 && oy >= 12) || (oy >= 12 && oy < 16 && ox >= 12))) ||
                                (side && ox >= 12 && ox < 16) || (!corner && !side && oy >= 12 && oy < 16);
                if (on) a.edge[k].rgba[(size_t(y) * a.edge[k].width + x) * 4 + 3] = 255;
            }
    }
    a.fibre = solid(64, 64, 230, 255);
    a.plate_rim = solid(320, 100, 255, 255);
    a.plate = solid(320, 100, 200, 255);
    return a;
}

void test_style() {
    Page page;
    page.show();
    std::vector<Row> none;
    page.input(press(0), none);
    const std::vector<Command> cmds = page.draw(none);
    size_t markers = 0;
    for (const Command& c : cmds) markers += style::marker(c);
    assert(markers >= 3);  // the window, the panel, the tabs
    int w, h;
    style::box_size("nb:window@980x632", w, h);
    assert(w == 980 && h == 632 && style::box_name("nb:window@980x632") == "nb:window");
    // without the game's art: plain shapes, no marker left
    for (const Command& c : style::resolve(cmds, nullptr)) assert(!style::marker(c) && c.text.rfind("art:", 0) != 0);
    // with it: the boxes become pictures of composed art; the cursor stays the port's brackets
    const style::Art art = synthetic_art();
    assert(art.window() && art.plates());
    size_t pictures = 0;
    for (const Command& c : style::resolve(cmds, &art)) {
        assert(!style::marker(c));
        pictures += c.text.rfind("art:", 0) == 0;
    }
    assert(pictures >= 3);
    // the torn paper: the paper inside the line, nothing outside it, the line on top
    auto paper = style::compose(art, "nb:window", 200, 120);
    assert(paper && paper->width == 200 && paper->height == 120);
    auto px = [&](int x, int y, int c) { return paper->rgba[(size_t(y) * paper->width + x) * 4 + c]; };
    assert(px(100, 60, 3) == 255 && px(2, 2, 3) == 0);
    assert(px(100, 60, 0) < 255 && px(100, 60, 1) > px(100, 60, 2));  // green paper
    auto plate = style::compose(art, "nb:plate-on", 160, 40);
    assert(plate && plate->width == 160 && plate->height == 40 && plate->rgba[(20 * 160 + 80) * 4 + 3] == 255);
    assert(!style::compose(style::Art{}, "nb:window", 100, 100));  // no art: no picture
    // a cursor marker becomes four brackets (eight rects each with its outline: 16)
    Command cur;
    cur.kind = Command::Picture, cur.text = "nb:cursor@100x50", cur.x = 10, cur.y = 10, cur.w = 100, cur.h = 50, cur.rgba = 0xFFFFFFFF;
    std::vector<Command> shapes;
    style::shapes(cur, shapes);
    assert(shapes.size() == 16);
}

void test_caption() {
    Caption cap;
    assert(cap.draw().empty());
    cap.show("Sealed Letter", "A sealed letter from the storyteller for his old friend who keeps Windfall's light.");
    // the A or touch that opened it does nothing; then B, A or a tap closes it with the menu's sound
    PadInput held;
    held.buttons = kPadA;
    assert(cap.input(held) == Sound::None && cap.open());
    bool info = false, title = false, body = false;
    for (const Command& c : cap.draw()) {
        info |= c.text.rfind("nb:info@740x276", 0) == 0;
        title |= font::plain_text(c.text) == "Sealed Letter" && font::has_style(c.text, font::kBold);
        body |= c.kind == Command::Text && font::has_style(c.text, font::kMessage) && font::plain_text(c.text).rfind("A sealed letter", 0) == 0;
    }
    assert(info && title && body);
    assert(cap.input(press(0)) == Sound::None);
    assert(cap.input(press(kPadB)) == Sound::Close && !cap.open());
    cap.show("x", "y");
    cap.input(press(0));
    PadInput tap;
    tap.touch = true;
    assert(cap.input(tap) == Sound::Close && !cap.open());
    // the info box: the game's art when it is read, else a plain box
    style::Art art = synthetic_art();
    Command m;
    m.kind = Command::Picture, m.text = "nb:info@740x276", m.w = 740, m.h = 276, m.rgba = 0xFFFFFFFF;
    assert(style::resolve({m}, &art)[0].kind == Command::Rect);  // no Info art in the synthetic set: plain
    auto blank = [](int w, int h) {
        game_glyph::Bitmap b;
        b.width = w, b.height = h, b.rgba.assign(size_t(w) * h * 4, 255);
        return b;
    };
    art.info_top = blank(361, 16), art.info_bottom = blank(360, 16), art.swirl = blank(348, 144);
    assert(style::resolve({m}, &art)[0].text == "art:info@740x276");
    auto box = style::compose(art, "nb:info", 370, 138);
    assert(box && box->width == 370 && box->rgba[(69 * 370 + 185) * 4 + 3] == 255);
}

// the painted frame and pieces (story_notebook_art.h) and the page laid out on them
void test_framed() {
    auto solid = [](int w, int h, uint8_t alpha) {
        game_glyph::Bitmap b;
        b.width = w, b.height = h, b.rgba.assign(size_t(w) * h * 4, 200);
        for (size_t i = 3; i < b.rgba.size(); i += 4) b.rgba[i] = alpha;
        return b;
    };
    // the soft fringe goes, the picture is cropped to what is left (or keeps its size)
    game_glyph::Bitmap fringe = solid(40, 20, 40);
    for (int y = 5; y < 15; y++)
        for (int x = 10; x < 30; x++) fringe.rgba[(size_t(y) * 40 + x) * 4 + 3] = 255;
    const game_glyph::Bitmap cleaned = art::clean_edges(fringe);
    assert(cleaned.width == 20 && cleaned.height == 10 && cleaned.rgba[3] == 255);
    const game_glyph::Bitmap kept = art::clean_edges(fringe, 96, false);
    assert(kept.width == 40 && kept.height == 20 && kept.rgba[3] == 0);
    // stretching: the ends keep their shape
    assert(art::stretch3(solid(100, 20, 255), 400, 20, 0.1f, 0.1f).width == 400);
    assert(art::row(solid(676, 100, 255), 600, 40).width == 600);
    assert(art::detail_frame(solid(1448, 1086, 255)).width == 1448);
    assert(art::fit_into(solid(100, 50, 255), 300, 50).rgba[3] == 0);  // pushed to the right
    // the chart's mapping: the sea's middle is the chart's middle, its north-west corner the top left
    float u, v;
    chart_share(0, 0, u, v);
    assert(std::fabs(u - 0.5f) < 1e-6f && std::fabs(v - 0.5f) < 1e-6f);
    chart_share(-350000, -350000, u, v);
    assert(u == 0 && v == 0);
    chart_share(100000, 300000, u, v);
    assert(std::fabs(u - 4.5f / 7) < 1e-4f && std::fabs(v - 6.5f / 7) < 1e-4f);
    // the page on the frame
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs = {quest("m", "a"), quest("m", "b")};
    qs[0].steps = {"Read the letter.", "Bring it to the keeper.", "Come back."}, qs[0].next = "Bring it to the keeper.", qs[0].reward = "A tale";
    qs[0].has_place = true, qs[0].x = 0, qs[0].z = 0;
    assert(reach_step(b, qs[0].key, 2) == 2);
    auto rs = rows(qs, b);
    Page page;
    Scene scene;
    scene.framed = scene.pieces = scene.header = true, scene.link_known = true, scene.link_x = -200000, scene.link_z = 100000;
    page.set_scene(scene);
    page.show();
    page.input(press(0), rs);
    auto has = [](const std::vector<Command>& cmds, const char* name) {
        for (const Command& c : cmds)
            if (c.text.rfind(name, 0) == 0) return true;
        return false;
    };
    auto says = [](const std::vector<Command>& cmds, const std::string& t) {
        for (const Command& c : cmds)
            if (c.kind == Command::Text && font::plain_text(c.text).find(t) != std::string::npos) return true;
        return false;
    };
    std::vector<Command> list = page.draw(rs);
    assert(has(list, "nb:frame") && has(list, "nb:row") && !has(list, "nb:dframe"));
    // taps: the frame's tab plates, its bottom plates (B Close, A Open)
    const Page::Geo g = page.geo();
    PadInput tap;
    tap.touch = true, tap.x = g.tab[1].x + 10, tap.y = g.tab[1].y + 10;
    assert(page.input(tap, rs).sound == Sound::Tab && page.tab() == 1);
    page.input(press(0), rs);
    tap.x = g.tab[0].x + 10, tap.y = g.tab[0].y + 10;
    page.input(tap, rs);
    page.input(press(0), rs);
    assert(page.tab() == 0);
    tap.x = g.track.x + 10, tap.y = g.track.y + 10;  // A Open
    assert(page.input(tap, rs).sound == Sound::Decide && page.mode() == Page::Mode::Detail);
    page.input(press(0), rs);
    // the detail page: the pieces, the progress, the step done before, the mini chart with Link and the route
    std::vector<Command> detail = page.draw(rs);
    for (const char* n : {"nb:dframe", "nb:sheet", "nb:uline", "nb:band0", "nb:band1", "nb:band2", "nb:card", "nb:uplate", "nb:header", "nb:linkface"})
        assert(has(detail, n));
    assert(says(detail, "Step 2 of 3") && says(detail, "Done: Read the letter.") && says(detail, "Bring it to the keeper."));
    size_t dots = 0;
    for (const Command& c : detail) dots += c.kind == Command::Circle && c.rgba == 0xD12E1EFF && c.size > 2.0f && c.size < 2.5f;
    assert(dots > 3);
    scene.header = false;  // the header band can be left out
    page.set_scene(scene);
    assert(!has(page.draw(rs), "nb:header"));
    // no close in the slot under the header band; the plates: A tracks, B goes back
    assert(page.input(PadInput{0, 0, true, g.close_x, g.close_y}, rs).kind == Page::Action::None && page.open());
    page.input(press(0), rs);
    tap.x = g.track.x + 10, tap.y = g.track.y + 10;
    assert(page.input(tap, rs).kind == Page::Action::Toggle);
    page.input(press(0), rs);
    tap.x = g.back.x + 10, tap.y = g.back.y + 10;
    assert(page.input(tap, rs).sound == Sound::Cancel && page.mode() == Page::Mode::List);
    page.input(press(0), rs);
    // a done quest: all steps done, the reward received
    assert(finish(b, qs[0].key) == kStepDone);
    rs = rows(qs, b);
    page.input(press(kPadRight), rs);
    page.input(press(0), rs);
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    detail = page.draw(rs);
    assert(says(detail, "All 3 steps done") && says(detail, "A tale (received)") && says(detail, "Done"));
    // a step that isn't possible yet says so in the Next step band
    Book b2(raw);
    std::vector<Quest> q2 = {quest("m", "c")};
    q2[0].next = "Talk to him.", q2[0].hint = "Not yet possible: by day";
    assert(reach_step(b2, q2[0].key, 1) == 1);
    auto r2 = rows(q2, b2);
    Page p2;
    p2.set_scene(scene);
    p2.show();
    p2.input(press(0), r2);
    p2.input(press(kPadA), r2);
    assert(says(p2.draw(r2), "Not yet possible: by day"));
    // the list's close plate
    Page p3;
    p3.set_scene(scene);
    p3.show();
    p3.input(press(0), rs);
    tap.x = g.back.x + 10, tap.y = g.back.y + 10;
    assert(p3.input(tap, rs).kind == Page::Action::Close);
}

void test_page_feel() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs;
    for (int i = 0; i < 3; i++) qs.push_back(quest("m", "q" + std::to_string(i)));
    for (Quest& q : qs) q.has_place = true, reach_step(b, q.key, 1);  // trackable: in progress, with a place
    auto rs = rows(qs, b);
    Page page;
    page.show();
    page.tick(0);
    // the window opens: scaled down and faded at first, at rest after its 0.22 s
    auto window = [&](const std::vector<Command>& cmds) {
        for (const Command& c : cmds)
            if (c.text.rfind("nb:window", 0) == 0) return c;
        return Command{};
    };
    Command w0 = window(page.draw(rs));
    assert(w0.w < kPageW && (w0.rgba & 0xFF) < 255);
    page.tick(0.1f), page.tick(0.1f), page.tick(0.1f);
    Command w1 = window(page.draw(rs));
    assert(std::fabs(w1.w - kPageW) < 0.5f && (w1.rgba & 0xFF) == 255);
    // the menu sounds of the inputs
    assert(page.input(press(0), rs).sound == Sound::None);
    assert(page.input(press(kPadDown), rs).sound == Sound::Cursor);
    page.input(press(0), rs);
    assert(page.input(press(kPadDown), rs).sound == Sound::Cursor);
    page.input(press(0), rs);
    assert(page.input(press(kPadDown), rs).sound == Sound::None);  // the last row: no move, no sound
    page.input(press(0), rs);
    assert(page.input(press(kPadRight), rs).sound == Sound::Tab);
    page.input(press(0), rs);
    assert(page.input(press(kPadLeft), rs).sound == Sound::Tab);
    page.input(press(0), rs);
    assert(page.input(press(kPadA), rs).sound == Sound::Decide && page.mode() == Page::Mode::Detail);
    page.input(press(0), rs);
    Page::Action a = page.input(press(kPadA), rs);
    assert(a.kind == Page::Action::Toggle && a.sound == Sound::TrackOn);
    page.input(press(0), rs);
    assert(toggle_tracked(b, a.key));
    rs = rows(qs, b);
    assert(page.input(press(kPadA), rs).sound == Sound::TrackOff);
    page.input(press(0), rs);
    assert(page.input(press(kPadB), rs).sound == Sound::Cancel && page.mode() == Page::Mode::List);
    // the page turned back: the content slides in from the left and fades in
    page.tick(0.02f);
    bool sliding = false;
    for (const Command& c : page.draw(rs)) sliding |= c.kind == Command::Text && font::plain_text(c.text) == qs[0].title && (c.rgba & 0xFF) < 255;
    assert(sliding);
    page.tick(0.5f);
    // the cursor is around the selected row; it breathes
    auto cursor = [&] {
        for (const Command& c : page.draw(rs))
            if (c.text.rfind("nb:cursor", 0) == 0) return c;
        return Command{};
    };
    const Command c0 = cursor();
    page.tick(0.25f);
    const Command c1 = cursor();
    assert(c0.w > 0 && c1.w > 0 && std::fabs(c0.w - c1.w) > 0.5f);
    page.input(press(0), rs);
    assert(page.input(press(kPadB), rs).sound == Sound::Close && !page.open());
    // every command stays finite through the animations
    page.show();
    for (int i = 0; i < 30; i++) {
        page.tick(0.016f);
        page.input(press(i % 4 == 1 ? kPadDown : 0), rs);
        for (const Command& c : page.draw(rs)) assert(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.w) && std::isfinite(c.h) && c.size >= 0);
    }
}

Resident harbour() { return npc_catalogue().find("windfall-ub3"); }
Resident keeper() { return npc_catalogue().find("windfall-um1"); }

// The story block in cking.sav (story_block.h) and the store around it (story_store.h): the format, the
// one-time move from the game's reserved bytes, the game's save copy functions, the file kept with
// the game's writes, a damaged block ignored, a vanilla save left alone; and the quest flags.
std::vector<uint8_t> file_bytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void put_file(const std::filesystem::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
std::map<uint32_t, void (*)(Cpu*)> store_hooks;
std::vector<uint32_t> store_calls;
// the guest stack of the store hooks' calls (the top of the arena): initdata_to_card's frame lies below it
constexpr uint32_t kStoreStack = kArena + 0x3F000, kInitdataFrame = 0x420;
bool g_initdata_frame_clear = false;  // what the last initdata_to_card call found in its frame
void store_original(Cpu* c, uint32_t fn) {
    store_calls.push_back(fn);
    assert(fn == 0x025BA7B0 || fn == 0x025BA9FC || fn == 0x025BAC50);
    assert(c->r[1] == kStoreStack);
    if (fn == 0x025BAC50) g_initdata_frame_clear = story_block::all_zero(mem::ptr(c->r[1] - kInitdataFrame), kInitdataFrame);
}
void store_hook(uint32_t fn, uint32_t data_num) {
    Cpu c{};
    c.r[1] = kStoreStack, c.r[3] = kSave + 0x20, c.r[4] = kArena + 0x30000, c.r[5] = data_num;
    store_hooks.at(fn)(&c);
}

void test_story_store() {
    using namespace story_block;
    namespace fs = std::filesystem;
    // the format
    std::vector<uint8_t> areas(kPayloadSize, 0);
    areas[5] = 0x42, areas[kAreaSize * 2 + 9] = 0x17;
    std::vector<uint8_t> game(8966);
    for (size_t i = 0; i < game.size(); i++) game[i] = uint8_t(i * 7);
    std::vector<uint8_t> file = game;
    const std::vector<uint8_t> block = build(areas.data());
    assert(block.size() == kPayloadSize + kFooterSize);
    file.insert(file.end(), block.begin(), block.end());
    Split sp = split(file.data(), file.size());
    assert(sp.found == Found::Valid && sp.game_size == game.size() && !memcmp(sp.payload, areas.data(), kPayloadSize));
    sp = split(game.data(), game.size());
    assert(sp.found == Found::None && sp.game_size == game.size());
    std::vector<uint8_t> bad = file;
    bad[game.size() + 100] ^= 1;  // a payload byte: the checksum fails
    sp = split(bad.data(), bad.size());
    assert(sp.found == Found::Damaged && sp.game_size == game.size() && !sp.payload);
    bad = file;
    put32(bad.data() + bad.size() - 12, uint32_t(bad.size()));  // a length that doesn't fit: no footer
    sp = split(bad.data(), bad.size());
    assert(sp.found == Found::None && sp.game_size == bad.size());
    assert(split(file.data(), 3).found == Found::None);
    // the one-time move: our 19-record form leaves the game's bytes zero; foreign bytes stay
    uint8_t old[story_mail::kBookSize] = {}, area[kAreaSize] = {};
    story_mail::Book ob(old);
    assert(ob.set(story_mail::kRecordQuest, 0x1234, 3) && ob.set(story_mail::kRecordItem, story_mail::kItemNotebook, 1));
    assert(migrate(old, area) && all_zero(old, sizeof old));
    story_mail::Book ab(area, kAreaRecords);
    assert(ab.get(story_mail::kRecordQuest, 0x1234) == 3 && ab.get(story_mail::kRecordItem, story_mail::kItemNotebook) == 1);
    uint8_t foreign[story_mail::kBookSize] = {};
    foreign[7] = 9;
    assert(!migrate(foreign, area) && foreign[7] == 9);
    // the game's save copy functions
    story_store::set_game_calls(store_original);
    story_store::install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { store_hooks[a] = fn; });
    assert(story_store::installed() && store_hooks.size() == 3);
    st32(0x101F84DC, kSave);
    uint8_t* mem_area = story_store::area();
    memset(mem_area, 0, kAreaSize);
    assert(story_store::book().set(story_mail::kRecordQuest, 0x0BAD, 2));
    store_hook(0x025BA9FC, 1);  // saved as Quest Log 2
    const fs::path dir = fs::temp_directory_path() / ("wwhd-story-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path sav = dir / "cking.sav";
    const std::string guest = "/vol/save/user/cking.sav";
    assert(story_store::is_save_file(guest) && story_store::is_save_file("/vol/save/user/cking.sav.backup"));
    assert(!story_store::is_save_file("/vol/save/user/cking_pic0_0.sav") && !story_store::is_save_file("/vol/content/cking.sav"));
    // the game writes its file: the block goes after it; the game's part is unchanged and hidden
    put_file(sav, game);
    story_store::written(guest, sav.string());
    file = file_bytes(sav);
    assert(file.size() == game.size() + kPayloadSize + kFooterSize && std::equal(game.begin(), game.end(), file.begin()));
    assert(story_store::game_size(sav.string(), file.size()) == game.size());
    assert(story_store::opened_for_read(guest, sav.string()) == game.size());
    // a new session: the block read back, Quest Log 2 loaded
    memset(mem_area, 0, story_block::kAreaSize * 4);
    assert(story_store::opened_for_read(guest, sav.string()) == game.size());
    store_hook(0x025BA7B0, 1);
    assert(story_store::book().get(story_mail::kRecordQuest, 0x0BAD) == 2);
    store_hook(0x025BA7B0, 0);  // Quest Log 1 has nothing
    assert(story_store::book().get(story_mail::kRecordQuest, 0x0BAD) == 0);
    // the game rewrites the file in place (its part changed, the old block still there): replaced, not doubled
    std::vector<uint8_t> game2 = game;
    game2[10] ^= 0xFF;
    std::vector<uint8_t> inplace = game2;
    inplace.insert(inplace.end(), file.begin() + long(game.size()), file.end());
    put_file(sav, inplace);
    story_store::written(guest, sav.string());
    file = file_bytes(sav);
    assert(file.size() == game2.size() + kPayloadSize + kFooterSize && std::equal(game2.begin(), game2.end(), file.begin()));
    // a damaged block: not used, the file copied aside once, replaced at the next game save
    bad = file;
    bad[game2.size() + 3] ^= 0x55;
    put_file(sav, bad);
    memset(mem_area + kAreaSize, 0, kPayloadSize);
    assert(story_store::opened_for_read(guest, sav.string()) == game2.size());
    assert(all_zero(mem_area + kAreaSize, kPayloadSize));  // nothing taken from it
    assert(fs::exists(dir / "cking.sav.story-damaged") && file_bytes(dir / "cking.sav.story-damaged") == bad);
    assert(story_store::book().set(story_mail::kRecordQuest, 0x0BAD, 4));
    store_hook(0x025BA9FC, 1);
    story_store::written(guest, sav.string());
    file = file_bytes(sav);
    sp = split(file.data(), file.size());
    assert(sp.found == Found::Valid && sp.game_size == game2.size());
    // a portable state's scratch copies don't move story areas; a new Quest Log's are empty
    {
        story_store::Quiet quiet;
        assert(story_store::book().set(story_mail::kRecordQuest, 0x0BAD, 9));
        store_hook(0x025BA9FC, 1);
    }
    store_hook(0x025BA7B0, 1);
    assert(story_store::book().get(story_mail::kRecordQuest, 0x0BAD) == 4);
    // a new Quest Log's data comes from initdata_to_card's stack frame, padding included: the hook clears
    // the frame first, so stale bytes of earlier calls (0x14 -> 0xD4 in each status C's status A byte 0x17
    // after the item menu) never reach the save; the caller's frame above it is left alone
    memset(mem::ptr(kStoreStack - kInitdataFrame - 0x40), 0xC0, kInitdataFrame + 0x80);
    g_initdata_frame_clear = false;
    store_hook(0x025BAC50, 1);
    assert(g_initdata_frame_clear);
    for (uint32_t o = 0; o < 0x40; o++)
        assert(ld8(kStoreStack + o) == 0xC0 && ld8(kStoreStack - kInitdataFrame - 0x40 + o) == 0xC0);
    store_hook(0x025BA7B0, 1);
    assert(story_store::book().empty());
    // loading a Quest Log moves the old record out of the game's reserved bytes
    memset(mem::ptr(kSave + story_mail::kBookOffset), 0, story_mail::kBookSize);
    story_mail::Book reserved(mem::ptr(kSave + story_mail::kBookOffset));
    assert(reserved.set(story_mail::kRecordTracked, 0x0777, 1));
    store_hook(0x025BA7B0, 2);
    assert(all_zero(mem::ptr(kSave + story_mail::kBookOffset), story_mail::kBookSize) &&
           story_store::book().get(story_mail::kRecordTracked, 0x0777) == 1);
    // nothing of ours anywhere: the game's file stays exactly as the game wrote it
    memset(mem_area, 0, kAreaSize * 4);
    put_file(sav, game);
    story_store::written(guest, sav.string());
    assert(file_bytes(sav) == game);
    fs::remove_all(dir);

    // the quest flags, saved with the story progress
    memset(mem_area, 0, kAreaSize);
    assert(define("flags-mod", "Flags", "q", "Q", "", "") == kResultNew);
    assert(flag("flags-mod", "q", 3) == 0 && set_flag("flags-mod", "q", 3, true) == 1 && flag("flags-mod", "q", 3) == 1);
    assert(set_flag("flags-mod", "q", 8, true) == 1 && story_store::book().get(story_mail::kRecordQuestFlags, quest_key("flags-mod", "q")) == 0x84);
    assert(set_flag("flags-mod", "q", 3, false) == 0 && set_flag("flags-mod", "q", 8, false) == 0 &&
           !story_store::book().has(story_mail::kRecordQuestFlags, quest_key("flags-mod", "q")));
    assert(set_flag("flags-mod", "q", 0, true) == kResultInvalid && set_flag("flags-mod", "q", 9, true) == kResultInvalid &&
           set_flag("flags-mod", "nope", 1, true) == kResultInvalid);
}

// The NPC catalogue: the compiled-in recipes (our words and references only), the player's names and
// spots merged in, a stub second place added without any format change, the manifest's "npc_catalogue"
// version and unknown ids refused, the step's place and where from the entry.
void test_npc_catalogue() {
    NpcCatalogue& port = npc_catalogue_mutable();
    assert(port.version == 3 && port.entries.size() >= 40);
    size_t named = 0, talking = 0;
    for (const NpcEntry& e : port.entries) {
        assert(e.name.rfind(e.place + "-", 0) == 0 && !e.where.empty() && (e.day || e.night) && !e.description.empty() && e.figure_message &&
               !e.actors.empty() && !e.has_spot && e.game_name.empty());  // nothing of the game in the recipe
        for (const NpcActor& a : e.actors) assert(!a.stage.empty() && !a.code.empty() && (a.actor_class != "people" || a.people_number < 19));
        named += e.named, talking += e.talks();
    }
    assert(named >= 30 && talking == 19);  // the 19 townspeople (daNpcPeople_c) can talk for quests
    // each townsperson is in the table once
    for (int n = 0; n < 19; n++) {
        int found = 0;
        for (const NpcEntry& e : port.entries)
            for (const NpcActor& a : e.actors) found += a.actor_class == "people" && a.people_number == n;
        assert(found >= 1);
    }
    const NpcEntry* k = port.entry(keeper());
    assert(k && k->named && k->actors.size() == 1 && k->actors[0].code == "Um1" && k->actors[0].people_number == 9 && k->day && k->night &&
           k->figure_message == 0x37F8 && k->display() == k->description);
    // the player's game: names (only for entries that are one character) and spots, by id
    assert(port.add_local(R"({"catalogue_local": 1, "npcs": {"windfall-um1": {"name": "The Keeper", "figurine": "The Keeper",
        "spot": {"x": 1547, "z": -202539}}, "windfall-ub1": {"name": "Two Friends", "spot": {"x": 1, "z": 2}},
        "windfall-nobody": {"name": "x"}}})") == 2);
    assert(k->game_name == "The Keeper" && k->display() == "The Keeper" && k->has_spot && k->x == 1547 && k->z == -202539);
    assert(port.entry(port.find("windfall-ub1"))->game_name.empty() && port.entry(port.find("windfall-ub1"))->has_spot);  // a pair: no name
    assert(!port.add_local("{\"npcs\": {}}") && port.add_local("not json") == 0);
    const NpcEntry* h = port.entry(harbour());
    assert(h && h->actors[0].people_number == 5 && h->day && !h->night);
    // Linda: outside by day, and in the Café Bar
    const NpcEntry* linda = port.entry(port.find("windfall-uw2"));
    assert(linda && linda->actors.size() == 2 && linda->actors[1].stage == "Opub");
    // a pair the game names together but doesn't tie one by one: no own name
    assert(!port.entry(port.find("windfall-ub1"))->named && !port.entry(port.find("windfall-ug2"))->named);
    // characters of other classes are in the table but can't talk for a quest yet
    const NpcEntry* merchant = port.entry(port.find("windfall-rsh1"));
    assert(merchant && merchant->named && !merchant->talks() && merchant->actors[0].code == "Rsh1");
    // a second place is just another table
    const char* stub = R"({"catalogue_format": 3, "place": "testisle", "npcs": [
        {"id": "testisle-keeper", "since": 4, "own_name": false, "description": "a stub", "figure_message": "0x3800", "where": "Test Isle",
         "present": ["night"], "actors": [{"class": "people", "code": "Ub1", "stage": "Tstisl", "room": 0, "layers": ["1"]}]}]})";
    NpcCatalogue two = port;
    std::string why;
    assert(two.add_table(stub, why) && two.version == 4 && two.entries.size() == port.entries.size() + 1);
    const Resident tk = two.find("testisle-keeper");
    assert(tk && two.entry(tk)->actors[0].people_number == 3 && !two.entry(tk)->has_spot && two.entry(keeper())->name == "windfall-um1");
    // tables are checked: all or nothing
    NpcCatalogue bad = port;
    auto rejected = [&](std::string text, const std::string& from, const std::string& to) {
        text.replace(text.find(from), from.size(), to);
        NpcCatalogue c = port;
        std::string reason;
        const bool ok = c.add_table(text, reason);
        assert(c.entries.size() == port.entries.size());
        return !ok && !reason.empty();
    };
    assert(rejected(stub, "\"testisle-keeper\"", "\"keeper\""));         // not <place>-<role>
    assert(rejected(stub, "\"code\": \"Ub1\"", "\"code\": \"Zz9\""));  // a resident code the game doesn't have
    assert(rejected(stub, "\"class\": \"people\", \"code\": \"Ub1\"", "\"class\": \"npc_rsh1\""));  // another class needs its actor name
    assert(rejected(stub, "\"description\": \"a stub\", ", ""));            // every entry says who it is, in our words
    assert(rejected(stub, "\"figure_message\": \"0x3800\"", "\"figure_message\": \"x\""));  // and which figurine names it
    assert(rejected(stub, "\"present\"", "\"spot\": {\"x\": 1, \"z\": 2}, \"present\""));  // no coordinates in a recipe
    assert(rejected(stub, "[\"night\"]", "[]"));                             // never out
    assert(!bad.add_table(R"({"catalogue_format": 3, "place": "windfall", "npcs": [{"id": "windfall-um1", "since": 3, "own_name": true,
        "description": "x", "figure_message": "0x37F8", "actors": [{"class": "people", "code": "Um1", "stage": "sea"}], "where": "x",
        "present": ["day"]}]})", why));  // an id twice
    // a manifest naming the stub NPC: refused by the port's catalogue, accepted with the second table, the format unchanged
    const char* m = R"({"npc_catalogue": 4, "quests_format": 1, "quests": [{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go.",
        "available": {"time": "day", "text": "by day"}, "npc": {"who": "testisle-keeper", "lines": ["Hi."]}}]}]})";
    std::vector<ManifestQuest> qs;
    std::vector<std::string> warnings;
    assert(!parse_quests(mods::json::parse(m), qs, why, port) && why.find("needs NPC catalogue version 4") != std::string::npos);
    std::string unversioned = m;
    unversioned.replace(0, std::string(R"({"npc_catalogue": 4, )").size(), "{");
    assert(!parse_quests(mods::json::parse(unversioned), qs, why, port) && why.find("no NPC named \"testisle-keeper\"") != std::string::npos);
    assert(parse_quests(mods::json::parse(m), qs, why, two, &warnings));
    assert(qs[0].steps[0].resident == tk && qs[0].steps[0].where == "Test Isle" && !qs[0].steps[0].has_place);
    // the step is available only by day, the stub keeper is only out at night: a warning, not a refusal
    assert(warnings.size() == 1 && warnings[0].find("only out at night") != std::string::npos);
    // a windfall resident: the step's place and where come from the catalogue unless the manifest gives them
    const char* w = R"({"quests_format": 1, "quests": [{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go.",
        "npc": {"who": "windfall-um1", "lines": ["Hi."]}}]}]})";
    assert(parse_quests(mods::json::parse(w), qs, why) && qs[0].steps[0].has_place && qs[0].steps[0].x == k->x &&
           qs[0].steps[0].z == k->z && qs[0].steps[0].where == k->where);
    assert(!parse_quests(mods::json::parse(R"({"npc_catalogue": "1"})"), qs, why));
    // a character that can't talk for a quest: refused with the reason
    const char* z = R"({"quests_format": 1, "quests": [{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go.",
        "npc": {"who": "windfall-rsh1", "lines": ["Hi."]}}]}]})";
    assert(!parse_quests(mods::json::parse(z), qs, why) && why.find("can't speak") != std::string::npos);
    assert(parse_quests(mods::json::parse(R"({"npc_catalogue": 1})"), qs, why) && qs.empty());
}

void test_takeover_rules() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t a = quest_key("mod-a", "q"), bq = quest_key("mod-b", "q");
    std::vector<Takeover> all = {{a, 1, harbour(), true, "A", 6}, {bq, 1, harbour(), true, "B", 0}};
    // nothing started: nobody speaks for the mods
    assert(!live_takeover(all, b, harbour()));
    // started (a first, then b), letters carried, nothing tracked: the quest started first speaks
    assert(reach_step(b, a, 1) == 1 && reach_step(b, bq, 1) == 1);
    assert(b.set(kRecordCarried, a, 1) && b.set(kRecordCarried, bq, 1));
    const Takeover* t = live_takeover(all, b, harbour());
    assert(t && t->quest == a && wanting(all, b, harbour()) == 2);
    // the tracked quest comes first
    assert(toggle_tracked(b, bq));
    t = live_takeover(all, b, harbour());
    assert(t && t->quest == bq && t->lines == "B");
    assert(toggle_tracked(b, a));
    t = live_takeover(all, b, harbour());
    assert(t && t->quest == a);
    // only one wants it: that one, tracked or not
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    b.erase(kRecordCarried, a);
    t = live_takeover(all, b, harbour());
    assert(t && t->quest == bq && wanting(all, b, harbour()) == 1);
    // another resident; a step that isn't current: not live
    assert(!live_takeover(all, b, kResidentNone));
    assert(b.set(kRecordCarried, a, 1));
    assert(reach_step(b, a, 2) == 2 && reach_step(b, bq, 2) == 2 && !live_takeover(all, b, harbour()));
    // completing hands the letter over and finishes the quest (it leaves the tracked list)
    uint8_t raw2[story_mail::kBookSize] = {};
    Book c(raw2);
    assert(reach_step(c, a, 1) == 1 && c.set(kRecordCarried, a, 1) && toggle_tracked(c, a));
    assert(complete(c, all[0]) && saved_step(c, a) == kStepDone && !carries(c, a) && tracked_key(c) == 0);
    assert(harbour() && npc_catalogue().find("nobody") == kResidentNone);
}

void test_takeover_hooks() {
    constexpr uint32_t kNpc = kArena + 0x8000, kLink = kArena + 0x9000;
    st32(kPlayObj + 0x5B2C, kLink);
    Book b = story_store::book();
    // the lantern quest of one mod, the same resident in another mod's quest
    assert(define("lantern", "Lantern", "tale", "The storyteller's lantern", "100 rupees", "") == kResultNew);
    assert(define("other", "Other", "tale", "Another tale", "", "") == kResultNew);
    assert(talk("lantern", "tale", 1, "windfall-ub3", "A letter from him?\nThank you!", 6) == kResultNew);
    assert(talk("other", "tale", 1, "windfall-ub3", "Not me.", 0) == kResultNew);
    assert(talk("lantern", "tale", 1, "nobody", "x", 0) == kResultInvalid);
    assert(talk("lantern", "tale", 1, "windfall-ub3", "1\n2\n3\n4", 0) == kResultInvalid);  // one box: three lines
    assert(talk("lantern", "tale", 1, "windfall-ub3", "x", 0x0F) == kResultInvalid);        // rupees only
    assert(step("lantern", "tale", 1, "Bring the letter to his old friend.", "Windfall Island") == 1);
    assert(step("other", "tale", 1, "", "") == 1);
    assert(carry("lantern", "tale", true) == 1 && carry("other", "tale", true) == 1);
    // the carried letter shows in the next empty bag place after the notebook, with a letter's icon underneath
    assert(b.get(story_mail::kRecordItem, story_mail::kItemNotebook) == 1);
    assert(call(0x02632D94, 0x31, 0, 0x02692548) == 0xA0 && call(0x02632D94, 0x32, 0, 0x02692548) == 0xA1);
    assert(call(0x02632D94, 0x34, 0, 0x02692548) == 0xFF);
    assert(call(0x0262F880, 0, 0xA1) == 0x25);
    assert(call(0x0268F420, 0, 0x32) == 0xFF);  // it can't go on X/Y/R
    // a touch on the sealed letter (or A on it) shows its caption, never the notebook's page
    {
        constexpr uint32_t kLetterChild = kArena + 0x5000;
        st32(kLetterChild + 0xEC, 0xA1);
        call(0x02630A64, kLetterChild);
        call(0x02630FC4, kLetterChild);
        assert(page_open() && caption_open());
        input::PadState held;
        held.touch = true;
        pad(held);  // the touch that opened it does nothing
        assert(caption_open());
        input::PadState none;
        pad(none);
        input::PadState back;
        back.buttons = 0x4000;  // B
        pad(back);
        assert(!page_open() && !caption_open());
        pad(none);
    }
    st8(kNpc + 0x905, 5);  // the harbour lady, at the harbour (stage "sea", room 11)
    st8(kNpc + 0x326, 11);
    memcpy(mem::ptr(kPlayObj + 0x5134), "sea", 4);
    st32(kNpc + 0x8A8, 0x1234), st32(kNpc + 0x8AC, 0);
    // the event flags of the save, to prove nothing is set
    std::vector<uint8_t> flags(mem::ptr(kSave + 0x644), mem::ptr(kSave + 0x644) + 0x100);
    // no quest wants her (no letter carried): the resident's own line
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    assert(carry("lantern", "tale", false) == 1 && carry("other", "tale", false) == 1);
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1 && calls == std::vector<std::string>{"getMsg"});
    // tracking is not needed any more: the notebook doesn't ask for it
    std::vector<Quest> storage;
    for (const Row& r : rows_now(storage)) assert(r.quest->hint.find("track it") == std::string::npos);
    assert(carry("lantern", "tale", true) == 1 && carry("other", "tale", true) == 1);
    // only the OTHER mod's quest wants her (the lantern's letter put away), tracked: its line
    assert(toggle_tracked(b, quest_key("other", "tale")));
    assert(carry("lantern", "tale", false) == 1);
    // the same resident indoors (another stage) or in another room is not the catalogue's: the game's line
    memcpy(mem::ptr(kPlayObj + 0x5134), "Opub", 5);
    assert(call(0x022C6C68, kNpc) == 0x2CF1);
    memcpy(mem::ptr(kPlayObj + 0x5134), "sea", 4);
    st8(kNpc + 0x326, 12);
    assert(call(0x022C6C68, kNpc) == 0x2CF1);
    st8(kNpc + 0x326, 11);
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF3 && calls.empty());
    assert(ld32(kNpc + 0x8A8) == 0);  // the talk ends after one box
    // the talk runs, then ends: the other quest is done, no reward
    st16(kNpc + 0xF8, 1);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(calls == std::vector<std::string>{"resident"} && state("other", "tale") == 1);
    st16(kNpc + 0xF8, 0);
    call(0x022C36D0, kNpc);
    assert(state("other", "tale") == kResultDone && !carries(b, quest_key("other", "tale")));
    // now the lantern quest tracked: its line, then the reward through the get-item event
    assert(carry("lantern", "tale", true) == 1);
    assert(toggle_tracked(b, quest_key("lantern", "tale")));
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF3 && calls.empty());
    assert(call(0x022C6C68, kNpc) == 0x2CF1);  // a second talk while ours runs: the game's
    st16(kNpc + 0xF8, 1);
    call(0x022C36D0, kNpc);
    st16(kNpc + 0xF8, 0);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(state("lantern", "tale") == kResultDone && !carries(b, quest_key("lantern", "tale")));
    // the get-item event is ordered by the resident who hands the reward over, as the game's items do
    assert((calls == std::vector<std::string>{"resident", "present item 6", "order item event 0"}) && (ld16(kNpc + 0xFA) & 8));
    calls.clear();
    call(0x022C36D0, kNpc);  // not started yet: ordered again, the item created once
    assert((calls == std::vector<std::string>{"resident", "order item event 0"}));
    st16(kNpc + 0xF8, 4);  // the get-item event runs: its item is ours
    call(0x022C36D0, kNpc);
    assert(ld32(kPlayObj + 0x52A0) == 77);
    // the one who ordered the event ends it once the event manager says it is over
    st16(kPlayObj + 0x52B8, 0);
    call(0x022C36D0, kNpc);
    assert(!(ld16(kPlayObj + 0x52B8) & 8));
    get_item_over = 1;
    call(0x022C36D0, kNpc);
    assert(ld16(kPlayObj + 0x52B8) & 8);
    get_item_over = 0;
    st16(kNpc + 0xF8, 0);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(calls == std::vector<std::string>{"resident"});
    // delivered: the letter has left the bag
    assert(call(0x02632D94, 0x32, 0, 0x02692548) == 0xFF);
    // done: the resident is the game's again
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1);
    // no game flag was set by any of it
    assert(std::equal(flags.begin(), flags.end(), mem::ptr(kSave + 0x644)));
}

const char* const kLanternManifest = R"({
  "format_version": 1, "id": "lantern-mod", "name": "Lantern", "version": "1.0.0", "kind": "guest",
  "quests_format": 1,
  "quests": [{
    "id": "lantern", "title": "The storyteller's lantern", "journal": "on_enable",
    "start": {"text": "Check a Rito postbox for a letter."},
    "letter": {"id": "lantern", "sender": "The storyteller", "text": "Dear Link,\nplease help.", "encloses": ["notebook", "letter:friend"]},
    "reward": {"text": "100 rupees", "item": "rupee_orange"},
    "steps": [{"n": 1, "text": "Bring the letter to his friend.", "where": "Windfall Island", "place": {"cell": "D2", "x": 3827, "z": -202189},
      "npc": {"who": "windfall-ub3", "when": {"carries": "letter:friend"},
              "lines": ["One.", "Two."], "then": {"give": "rupee_orange", "take": "letter:friend", "complete": true}}}]
  }, {"id": "secret", "title": "A secret", "journal": "on_trigger"}]
})";

// Several quests want the same resident: the game's choice box first (stage 2): a chosen quest's lines
// continue the same talk; "Never mind" ends it, the next talk is the game's, the one after offers again.
void test_choice() {
    constexpr uint32_t kNpc = kArena + 0x8000, kManager = kArena + 0x9800, kMsgNo = kArena + 0x9C00;
    Book b = story_store::book();
    st32(0x101F4B5C, kManager);
    // the pure parts: the order offered, the labels
    {
        uint8_t raw[story_mail::kBookSize] = {};
        Book t(raw);
        const uint16_t a = quest_key("x", "a"), c = quest_key("x", "c"), d = quest_key("x", "d");
        std::vector<Takeover> all = {{a, 1, harbour(), false, "A"}, {c, 1, harbour(), false, "C"}, {d, 1, harbour(), false, "D"}};
        assert(reach_step(t, c, 1) == 1 && reach_step(t, a, 1) == 1 && reach_step(t, d, 1) == 1);
        auto w = wanted(all, t, harbour(), nullptr, 12, 2);
        assert(w.size() == 2 && w[0].quest == c && w[1].quest == a);  // started first, then next
        assert(toggle_tracked(t, d));
        w = wanted(all, t, harbour(), nullptr, 12, 2);
        assert(w.size() == 2 && w[0].quest == d && w[1].quest == c);  // the tracked one first
        assert(choice_label("The storyteller's lantern") == "The storyteller's..." && choice_label("Short") == "Short");
        assert(choice_label("Ünïcödé ünïcödé ünïcödé ünïcödé").size() <= 24);
    }
    assert(define("c1", "C1", "q", "First tale", "", "") == kResultNew && define("c2", "C2", "q", "Second tale", "", "") == kResultNew);
    assert(talk("c1", "q", 1, "windfall-ub3", "One.", 0) == kResultNew && talk("c2", "q", 1, "windfall-ub3", "Two.", 0) == kResultNew);
    assert(step("c1", "q", 1, "", "") == 1 && step("c2", "q", 1, "", "") == 1);
    assert(carry("c1", "q", true) == 1 && carry("c2", "q", true) == 1);
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    st8(kNpc + 0x905, 5), st8(kNpc + 0x326, 11);
    memcpy(mem::ptr(kPlayObj + 0x5134), "sea", 4);
    std::vector<uint8_t> flags(mem::ptr(kSave + 0x644), mem::ptr(kSave + 0x644) + 0x100);
    auto next = [&](uint32_t chosen) {
        st32(kManager + 0x948, chosen);
        st32(kMsgNo, 0);
        Cpu c{};
        c.r[1] = 0x2003F000, c.r[3] = kNpc, c.r[4] = kMsgNo;
        hooked.at(0x022C6108)(&c);
        return c.r[3];
    };
    auto end_talk = [&] {
        st16(kNpc + 0xF8, 1);
        call(0x022C36D0, kNpc);
        st16(kNpc + 0xF8, 0);
        call(0x022C36D0, kNpc);
    };
    // the box: both quests and "Never mind"; "Never mind" ends the talk
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF3 && calls.empty());
    assert(next(2) == 0x10 && calls.empty());
    end_talk();
    // the next talk is the game's line, the one after offers the quests again
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1 && calls == std::vector<std::string>{"getMsg"});
    end_talk();
    assert(call(0x022C6C68, kNpc) == 0x2CF3);
    // the second quest chosen: its lines continue the same talk, then it is done
    assert(next(1) == 0x0F && ld32(kMsgNo) == 0x2CF3);
    calls.clear();
    assert(next(0) == 0x10 && calls == std::vector<std::string>{"next"});  // after our box: the game's (the talk ends)
    end_talk();
    assert(state("c2", "q") == kResultDone && state("c1", "q") == 1);
    // only one wants her now: no box, its lines straight away
    assert(call(0x022C6C68, kNpc) == 0x2CF3);
    calls.clear();
    assert(next(0) == 0x10 && calls == std::vector<std::string>{"next"});
    end_talk();
    assert(state("c1", "q") == kResultDone);
    assert(std::equal(flags.begin(), flags.end(), mem::ptr(kSave + 0x644)));
}

void test_manifest() {
    std::vector<ManifestQuest> qs;
    std::string why;
    assert(parse_quests(mods::json::parse(kLanternManifest), qs, why) && qs.size() == 2);
    const ManifestQuest& q = qs[0];
    assert(q.id == "lantern" && q.start_text == "Check a Rito postbox for a letter." && q.reward_item == 6 && q.has_letter &&
           q.letter_encloses_letter && q.steps.size() == 1);
    assert(q.steps[0].has_place && q.steps[0].x == 3827 && q.steps[0].has_talk && q.steps[0].resident == harbour() &&
           q.steps[0].needs_letter && q.steps[0].lines == "One.\nTwo." && q.steps[0].reward == 6);
    // what is refused
    auto refused = [&](const std::string& from, const std::string& to) {
        std::string text = kLanternManifest;
        const size_t at = text.find(from);
        assert(at != std::string::npos);
        text.replace(at, from.size(), to);
        std::vector<ManifestQuest> out;
        std::string reason;
        return !parse_quests(mods::json::parse(text), out, reason) && !reason.empty();
    };
    assert(refused(R"("who": "windfall-ub3")", R"("who": "windfall-nobody")"));  // an NPC the catalogue doesn't know
    assert(refused(R"("who": "windfall-ub3")", R"("who": {"class": "people", "code": "Ub3"})"));  // names only
    assert(refused(R"(["One.", "Two."])", R"(["1", "2", "3", "4"])"));              // more than one box
    assert(refused(R"("give": "rupee_orange")", R"("give": "master_sword")"));      // only rupees
    assert(refused(R"("quests_format": 1)", R"("quests_format": 2)"));
    assert(refused(R"("id": "lantern", "title")", R"("id": "Lantern!", "title")"));
    assert(refused(R"("journal": "on_enable")", R"("journal": "sometimes")"));
    std::vector<ManifestQuest> none;
    assert(parse_quests(mods::json::parse(R"({"format_version": 1})"), none, why) && none.empty());
    // the sealed letter's caption: the defaults, or the enclosure's own name and description
    assert(enclosure_name(q) == "Sealed Letter" && enclosure_description(q).find("A sealed letter from ") == 0);
    {
        std::string text = kLanternManifest;
        const size_t at = text.find(R"("letter:friend")");
        assert(at != std::string::npos);
        text.replace(at, std::string(R"("letter:friend")").size(),
                     R"({"id": "letter:friend", "name": "Letter for the keeper", "description": "A sealed letter for his old friend."})");
        std::vector<ManifestQuest> out;
        assert(parse_quests(mods::json::parse(text), out, why) && out[0].letter_encloses_letter);
        assert(enclosure_name(out[0]) == "Letter for the keeper" && enclosure_description(out[0]) == "A sealed letter for his old friend.");
        assert(refused(R"("letter:friend")", R"({"id": "letter:friend", "description": 7})"));
        assert(refused(R"("letter:friend")", R"({"id": "letter:friend", "description": ")" + std::string(201, 'x') + R"("})"));
    }

    // declared: in the notebook at once (the secret one not), its letter sent, started when read
    Book b = story_store::book();
    assert(declare("lantern-mod", "Lantern", mods::json::parse(kLanternManifest)));
    std::vector<Quest> storage;
    bool found = false;
    for (const Row& r : rows_now(storage)) {
        assert(r.quest->id != "secret");
        if (r.quest->id == "lantern") {
            found = true;
            assert(r.status == Status::New && r.quest->next == "Check a Rito postbox for a letter.");
        }
    }
    assert(found);
    float manifest_x=0,manifest_z=0;assert(place_position("lantern-mod","lantern",manifest_x,manifest_z)&&manifest_x==3827&&manifest_z==-202189);
    assert(!place_position("lantern-mod","secret",manifest_x,manifest_z)); // hidden quest is not a navigation destination
    declared_tick();
    const uint16_t letter = story_mail::letter_key("lantern-mod", "lantern"), key = quest_key("lantern-mod", "lantern");
    assert(b.state(letter) == story_mail::kWaiting && saved_step(b, key) == 0);
    declared_tick();
    assert(saved_step(b, key) == 0);  // waiting: nothing yet
    assert(b.set_state(letter, story_mail::kRead));  // read at a postbox
    declared_tick();
    assert(saved_step(b, key) == 1 && carries(b, key));
    declared_tick();
    assert(saved_step(b, key) == 1);
    for (const Row& r : rows_now(storage))
        if (r.quest->id == "lantern")
            assert(r.quest->next == "Bring the letter to his friend." && r.quest->has_place && r.quest->x == 3827 &&
                   r.quest->hint.empty());
}

void test_available() {
    // the evaluator: event flags (byte, mask) and the time of day
    uint8_t flags[0x100] = {};
    Availability a;
    a.events = {0x2D01}, a.not_events = {0x0A02}, a.time = 1, a.text = "after the pearl, by day";
    assert(!available(a, flags, 12));
    flags[0x2D] |= 0x01;
    assert(available(a, flags, 12) && !available(a, flags, 20) && !available(a, flags, 5));
    flags[0x0A] |= 0x02;  // the Endless Night
    assert(!available(a, flags, 12));
    Availability none;
    assert(!none.any() && available(none, flags, 3));
    // the manifest form, and what it refuses
    const char* m = R"({"quests_format": 1, "quests": [{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go.",
        "available": {"event": "0x2D01", "not_event": ["0x0A02"], "time": "day", "text": "after Nayru's pearl, by day"},
        "npc": {"who": "windfall-um1", "lines": ["Hi."]}}]}]})";
    std::vector<ManifestQuest> qs;
    std::string why;
    assert(parse_quests(mods::json::parse(m), qs, why));
    const ManifestStep& st = qs[0].steps[0];
    assert(st.when.events == std::vector<uint16_t>{0x2D01} && st.when.not_events == std::vector<uint16_t>{0x0A02} && st.when.time == 1 &&
           st.when.text == "after Nayru's pearl, by day" && st.resident == keeper() && !st.needs_letter);
    auto refused = [](std::string text, const std::string& from, const std::string& to) {
        text.replace(text.find(from), from.size(), to);
        std::vector<ManifestQuest> out;
        std::string reason;
        return !parse_quests(mods::json::parse(text), out, reason);
    };
    assert(refused(m, "\"0x2D01\"", "\"2D01\""));
    assert(refused(m, "\"day\"", "\"noon\""));
    assert(refused(m, ", \"text\": \"after Nayru's pearl, by day\"", ""));
    // a talk whose step isn't available yet is not live
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t k = quest_key("m", "q");
    assert(reach_step(b, k, 1) == 1 && toggle_tracked(b, k));
    std::vector<Takeover> all = {{k, 1, keeper(), false, "Hi.", 0, st.when}};
    uint8_t game[0x100] = {};
    assert(!live_takeover(all, b, keeper(), game, 12));
    game[0x2D] = 0x01;
    assert(live_takeover(all, b, keeper(), game, 12) && !live_takeover(all, b, keeper(), game, 22));
    // in the notebook: "Not yet possible: ..." and no place until it holds (the save's flags and time)
    assert(declare("avail-mod", "Avail", mods::json::parse(m)));
    Book save = story_store::book();
    assert(reach_step(save, quest_key("avail-mod", "q"), 1) == 1);
    st32(kSave + 0x44, f32_as_u32(180.0f));  // noon
    std::vector<Quest> storage;
    auto row = [&]() -> const Quest* {
        static std::vector<Quest> keep;
        for (const Row& r : rows_now(keep))
            if (r.quest->mod == "avail-mod") return r.quest;
        return nullptr;
    };
    assert(row() && row()->hint == "Not yet possible: after Nayru's pearl, by day" && !row()->has_place);
    st8(kSave + 0x644 + 0x2D, 0x01);  // the game set the flag
    assert(row() && row()->hint.find("Not yet possible") == std::string::npos);
    st8(kSave + 0x644 + 0x2D, 0);
    (void)storage;
}


const char* const kSailManifest = R"({
  "format_version": 1, "id": "sail-mod", "name": "Sail", "version": "1.0.0", "kind": "guest",
  "quests_format": 1,
  "quests": [{
    "id": "q", "title": "A tale at sea",
    "steps": [{"n": 1, "text": "Sail to Windfall.", "sail_line": ["Windfall lies north of here.", "Let us go, Link!"]},
              {"n": 2, "text": "Talk to the keeper.", "sail_line": "The keeper is up on the windmill."},
              {"n": 3, "text": "Rest."}]
  }]
})";

void test_sail_manifest() {
    std::vector<ManifestQuest> qs;
    std::string why;
    assert(parse_quests(mods::json::parse(kSailManifest), qs, why) && qs.size() == 1 && qs[0].steps.size() == 3);
    assert(qs[0].steps[0].sail_line == "Windfall lies north of here.\nLet us go, Link!");
    assert(qs[0].steps[1].sail_line == "The keeper is up on the windmill." && qs[0].steps[2].sail_line.empty());
    auto refused = [](const std::string& from, const std::string& to) {
        std::string text = kSailManifest;
        const size_t at = text.find(from);
        assert(at != std::string::npos);
        text.replace(at, from.size(), to);
        std::vector<ManifestQuest> out;
        std::string reason;
        return !parse_quests(mods::json::parse(text), out, reason) && reason.find("sail_line") != std::string::npos;
    };
    assert(refused(R"(["Windfall lies north of here.", "Let us go, Link!"])", R"(["1", "2", "3", "4"])"));  // one box
    assert(refused(R"("The keeper is up on the windmill.")", R"("1\n2\n3\n4")"));
    assert(refused(R"("The keeper is up on the windmill.")", R"([])"));
    assert(refused(R"("The keeper is up on the windmill.")", R"(7)"));
    assert(refused(R"("The keeper is up on the windmill.")", "\"" + std::string(241, 'x') + "\""));
    assert(refused(R"("The keeper is up on the windmill.")", R"("a\tb")"));  // control characters
}

void test_sail_rules() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t a = quest_key("mod-a", "q"), o = quest_key("mod-b", "q");
    const std::vector<SailLine> all = {{a, 1, "A1"}, {a, 2, "A2"}, {o, 1, "B1"}};
    // untracked: nothing is due
    assert(reach_step(b, a, 1) == 1 && reach_step(b, o, 1) == 1);
    assert(!due_sail_line(all, b));
    // the tracked quest's step: its own line, once
    assert(toggle_tracked(b, a));
    const SailLine* s = due_sail_line(all, b);
    assert(s && s->text == "A1");
    assert(mark_sail_line_said(b, *s) && b.get(story_mail::kRecordSailLine, a) == 1 && !due_sail_line(all, b));
    // the next step has its own line: due once more
    assert(reach_step(b, a, 2) == 2);
    s = due_sail_line(all, b);
    assert(s && s->text == "A2" && mark_sail_line_said(b, *s) && !due_sail_line(all, b));
    // a step without a line, a finished quest: nothing
    assert(reach_step(b, a, 3) == 3 && !due_sail_line(all, b));
    // the other mod's quest tracked: only its line (one record per quest)
    assert(toggle_tracked(b, o));
    s = due_sail_line(all, b);
    assert(s && s->text == "B1" && s->quest == o);
    assert(toggle_tracked(b, o) == false && !due_sail_line(all, b));  // untracked again
    // the arbitration: the game first
    ShipMoment m;
    m.can_say = m.force_checked = m.riding = true, m.steps_since_busy = kSailQuietSteps;
    assert(may_order_sail_line(m));
    auto refused = [&](auto change) {
        ShipMoment x = m;
        change(x);
        return !may_order_sail_line(x) && sail_refusal(x);
    };
    assert(refused([](ShipMoment& x) { x.force_pending = true; }));        // a forced line of the game is pending
    assert(refused([](ShipMoment& x) { x.force_checked = false; }));       // the boat did not run its forced-line check
    assert(refused([](ShipMoment& x) { x.mode = kShipModeTalk; }));        // the boat talks
    assert(refused([](ShipMoment& x) { x.mode = 7; }));                    // a cutscene mode
    assert(refused([](ShipMoment& x) { x.mode = 9; }));                    // the cannon
    assert(refused([](ShipMoment& x) { x.event_running = true; }));        // an event runs
    assert(refused([](ShipMoment& x) { x.orders = 1; }));                  // the game ordered an event this step
    assert(refused([](ShipMoment& x) { x.mini_game = true; }));
    assert(refused([](ShipMoment& x) { x.steps_since_busy = kSailQuietSteps - 1; }));
    assert(refused([](ShipMoment& x) { x.riding = false; }));              // not at the boat
    assert(refused([](ShipMoment& x) { x.can_say = false; }));
    ShipMoment walk = m;
    walk.riding = false, walk.close_in_front = true;
    assert(may_order_sail_line(walk));
    walk.mode = kShipModeSteer;
    assert(may_order_sail_line(walk));
}

void test_sail_hooks() {
    constexpr uint32_t kShip = kArena + 0xA000, kLink = kArena + 0x9000, kOut = kArena + 0xB000;
    st32(kPlayObj + 0x5B2C, kLink);
    // the letters' message hooks with text buffers: the line is shown through them
    story_mail::set_game_calls(original, game_call);
    story_mail::set_text_buffers(kArena + 0x30000, 8192);
    story_mail::note_import();
    story_mail::install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { mail_hooked[a] = fn; });
    assert(story_mail::can_say());
    std::vector<uint8_t> flags(mem::ptr(kSave + 0x644), mem::ptr(kSave + 0x644) + 0x100);
    assert(declare("sail-mod", "Sail", mods::json::parse(kSailManifest)));
    // not hooked on a conflict; then the three functions of the boat
    install_sail([](uint32_t a) { return a == 0x02477A24 ? std::string("other") : std::string(); },
                 [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!sail_installed());
    install_sail([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(sail_installed() && hooked.count(0x02477A24) && hooked.count(0x024731BC) && hooked.count(0x024749DC));
    Book b = story_store::book();
    const uint16_t key = quest_key("sail-mod", "q");
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    assert(reach_step(b, key, 1) == 1);
    // the boat waits, Link rides it, no event
    st8(kShip + 0x635, 0), st32(kShip + 0x644, 0x00400000), st32(kShip + 0x648, 0x5E0), st16(kShip + 0xF8, 0);
    st32(kPlayObj + 0x5CD8, 0x10000), st8(kPlayObj + 0x5292, 0), st8(kPlayObj + 0x5290, 0), st8(kPlayObj + 0x5CEA, 0);
    uint64_t step = 1000;
    auto execute = [&]() {
        logic_step(++step);
        st8(kPlayObj + 0x5290, 0);  // the event control takes the orders every step
        calls.clear();
        call(0x02477A24, kShip);
        return std::find(calls.begin(), calls.end(), "order speak") != calls.end();
    };
    // untracked: the boat is the game's
    for (int i = 0; i < 100; i++) assert(!execute());
    assert(!b.has(story_mail::kRecordSailLine, key));
    assert(toggle_tracked(b, key));
    // the game's forced line pending, or ordered by the game: ours waits
    g_ship_force = 0x5EC;
    for (int i = 0; i < 5; i++) assert(!execute());
    g_ship_force = 0;
    st32(kShip + 0x648, 0x5E0);
    g_game_order = 1;
    assert(!execute());
    g_game_order = 0;
    // an event runs: ours waits, and a quiet moment after it
    st8(kPlayObj + 0x5292, 1);
    assert(!execute());
    st8(kPlayObj + 0x5292, 0);
    for (uint64_t i = 1; i < kSailQuietSteps; i++) assert(!execute());
    assert(execute());  // ordered the game's way: talk attention, the talk condition, no boarding offered
    assert((ld32(kShip + 0x39C) & 0x8A) == 0x0A && (ld16(kShip + 0xFA) & 1));
    // not taken (no talk command at the next execute): ordered again
    assert(execute());
    // taken: the talk starts with the carrier message; the boat doesn't turn back after it
    st16(kShip + 0xF8, 1);
    assert(!execute() && std::find(calls.begin(), calls.end(), "talk init") != calls.end());
    assert(ld32(kShip + 0x648) == 0x5DF && !(ld32(kShip + 0x644) & 0x00400000));
    st16(kShip + 0xF8, 0);
    // the message system looks the carrier up: its text is the quest's
    st32(kOut + 0x11C, 0x5DF), st32(kOut + 0x8, kArena + 0xB800), st32(kArena + 0xB800, kArena + 0xBC00), st32(kOut + 0x118, 3);
    {
        Cpu c{};
        c.r[3] = 0, c.r[4] = kOut;
        mail_hooked.at(0x025F50E8)(&c);
        Cpu t{};
        t.r[3] = kArena + 0xBC00, t.r[4] = 3, t.lr = 0;
        mail_hooked.at(0x0273A2E8)(&t);
        std::u16string shown;
        for (uint32_t p = t.r[3]; ld16(p); p += 2) shown.push_back(char16_t(ld16(p)));
        assert(shown == u"Windfall lies north of here.\nLet us go, Link!");
    }
    for (int i = 0; i < 10; i++) assert(!execute());  // the talk runs
    // the talk ends: the boat's message and its turn-back flag as they were, the line remembered
    st8(kShip + 0x635, 0);
    assert(!execute());
    assert(ld32(kShip + 0x648) == 0x5E0 && (ld32(kShip + 0x644) & 0x00400000));
    assert(b.get(story_mail::kRecordSailLine, key) == 1);
    {  // the carrier is the game's again
        Cpu t{};
        t.r[3] = kArena + 0xBC00, t.r[4] = 3;
        mail_hooked.at(0x0273A2E8)(&t);
        assert(t.r[3] == 0);
    }
    // said once: never again for this step
    for (int i = 0; i < 200; i++) assert(!execute());
    // Link's own talk to the boat: the game's hint, untouched
    st32(kPlayObj + 0x5CD8, 0);
    st16(kShip + 0xF8, 1);
    execute();
    assert(ld32(kShip + 0x648) == kShipHint);
    st16(kShip + 0xF8, 0), st8(kShip + 0x635, 0);
    execute();
    assert(ld32(kShip + 0x648) == kShipHint);
    // step 2 has its own line: Link walks up to the boat (the game's walk-up test)
    assert(reach_step(b, key, 2) == 2);
    for (uint64_t i = 0; i < kSailQuietSteps; i++) execute();
    stf32(kShip + 0x318, 10.0f), stf32(kShip + 0x390, 100.0f), stf32(kShip + 0x398, 100.0f);
    stf32(kLink + 0x314, 100.0f), stf32(kLink + 0x318, 0.0f), stf32(kLink + 0x31C, 400.0f);  // 300 away
    g_seen_angle = 0x1000;
    assert(!execute());
    stf32(kLink + 0x31C, 300.0f);  // 200 away, in front
    g_seen_angle = 0x7000;         // but behind the boat
    assert(!execute());
    g_seen_angle = 0x1000;
    stf32(kLink + 0x318, 80.0f);   // on another level
    assert(!execute());
    stf32(kLink + 0x318, 0.0f);
    assert(execute());
    // taken, but the line is never shown (the talk ends before its message): not remembered, due again
    st16(kShip + 0xF8, 1);
    execute();
    st16(kShip + 0xF8, 0), st8(kShip + 0x635, 0);
    execute();
    assert(b.get(story_mail::kRecordSailLine, key) == 1);
    for (uint64_t i = 0; i < kSailQuietSteps; i++) execute();
    assert(execute());
    // a state load during the talk: the carrier is the game's again
    st16(kShip + 0xF8, 1);
    execute();
    st16(kShip + 0xF8, 0);
    state_loaded();
    assert(!story_mail::said());
    st8(kShip + 0x635, 0);
    // the mod's quest not tracked: nothing, whatever is due
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    for (uint64_t i = 0; i < 3 * kSailQuietSteps; i++) assert(!execute());
    // no game flag was touched by any of it
    assert(std::equal(flags.begin(), flags.end(), mem::ptr(kSave + 0x644)));
}
}  // namespace

// review5 fixes: Where for a quest not started yet, no Show on chart without a place
void test_detail_texts() {
    Quest q = quest("m", "w");
    Row r{&q, Status::New, 0, false};
    assert(where_text(r) == "This step has no place on the chart." && !trackable(r));
    q.where = "Outset Island", q.has_place = true;
    assert(where_text(r) == "Not started yet: Outset Island" && trackable(r));  // not started: step 1's place
    r.tracked = true;
    assert(trackable(r));
    r.tracked = false;
    r.status = Status::InProgress, r.step = 1;
    assert(where_text(r) == "Outset Island" && trackable(r));
    q.has_place = false;
    assert(!trackable(r));
    r.tracked = true;
    assert(trackable(r));  // a tracked quest can always be untracked
    r.status = Status::Done, r.tracked = false;
    assert(!trackable(r));
}

// Mod charts in the manifest: the declarations, how quests give and finish them, the page's "Open chart".
void test_manifest_charts() {
    const char* text = R"j({"charts": [{"id": "old-map", "name": "Old map", "description": "An X marks the lighthouse.",
        "place": {"x": 1759, "z": -202572}}],
      "quests_format": 1, "quests": [{"id": "hunt", "title": "The old map",
        "letter": {"id": "hunt", "sender": "A sailor", "text": "Here.", "encloses": ["chart:old-map"]},
        "reward": {"text": "A chart", "chart": "chart:old-map"},
        "steps": [{"n": 1, "text": "Find the treasure marked on the Old map.", "where": "On your Old map (chart list)", "find": "chart:old-map"},
                  {"n": 2, "text": "Talk to him.", "npc": {"who": "windfall-um1", "lines": ["Hi."], "then": {"give": "chart:old-map"}}}]}]})j";
    std::vector<ManifestQuest> qs;
    std::vector<ManifestChart> cs;
    std::string why;
    const auto m = mods::json::parse(text);
    assert(parse_charts(m, cs, why) && cs.size() == 1 && cs[0].id == "old-map" && cs[0].name == "Old map" && cs[0].x == 1759);
    assert(parse_quests(m, qs, why) && qs[0].letter_charts == std::vector<std::string>{"old-map"} && qs[0].reward_chart == "old-map");
    assert(qs[0].steps[0].find_chart == "old-map" && qs[0].steps[1].give_chart == "old-map" && !qs[0].steps[1].reward);
    // a chart the quests name must be declared; declarations are checked
    std::string undeclared = text;
    undeclared.replace(undeclared.find("\"id\": \"old-map\""), 15, "\"id\": \"new-map\"");
    assert(!parse_quests(mods::json::parse(undeclared), qs, why) && why.find("not declared") != std::string::npos);
    for (const char* bad : {R"({"charts": [{"id": "x", "name": "", "place": {"x": 0, "z": 0}}]})",
                            R"({"charts": [{"id": "x", "name": "X"}]})", R"({"charts": [{"id": "x", "name": "X", "place": {"x": 900000, "z": 0}}]})",
                            R"({"charts": [{"id": "x", "name": "X", "place": {"x": 0, "z": 0}}, {"id": "x", "name": "Y", "place": {"x": 0, "z": 0}}]})"})
        assert(!parse_charts(mods::json::parse(bad), cs, why));
    // the page: "Open chart" (X or its plate) while the step's chart is owned
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> quests{quest("m", "a")};
    quests[0].chart_mod = "m", quests[0].chart = "old-map";
    auto rs = rows(quests, b);
    Page page;
    page.show();
    page.input(press(0), rs);
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    assert(page.mode() == Page::Mode::Detail);
    const auto act = page.input(press(kPadX), rs);
    assert(act.kind == Page::Action::OpenChart && act.key == quests[0].key && !page.open());
    quests[0].chart.clear();  // no chart: X does nothing
    rs = rows(quests, b);
    page.show();
    page.input(press(0), rs);
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    assert(page.input(press(kPadX), rs).kind == Page::Action::None && page.open());
}

// Public destination resolution must agree with notebook/chart state and
// remain independent of local NPC coordinates for an interior entrance.
void test_quest_destinations() {
    auto manifest=mods::json::parse(R"({"quests_format":1,
      "scenes":[{"id":"place-flags","steps":[{"type":"end","then":[
        {"action":"flag","id":"opened"},{"action":"flag","id":"closed"}]}]}],
      "quests":[{"id":"destinations","title":"Interior destination","steps":[
        {"n":1,"text":"Enter the building","where":"Inside the building",
         "place":{"stage":"Pnezumi","room":0,"point":3,"sea":{"x":-200000,"z":100000}},
         "npc":{"who":"windfall-ub3","lines":["Meet me inside."]}},
        {"n":2,"text":"Find the room","where":"The back room",
         "available":{"flag":["opened"],"not_flag":"closed","text":"Open the entrance"},
         "place":{"stage":"Pnezumi","room":1,"x":10,"y":20,"z":30,"sea":{"x":-200000,"z":100000}}},
        {"n":3,"text":"Return to sea","place":{"x":1000,"z":2000},"npc":{"who":"windfall-ub3","lines":["Back at sea."]}}]}]})");
    assert(declare("destination-test","Destinations",manifest));
    assert(reach("destination-test","destinations",1)==1);
    auto b=story_store::book();assert(toggle_tracked(b,quest_key("destination-test","destinations")));
    QuestDestination destination;
    assert(place_destination("destination-test","destinations",destination));
    assert(destination.stage=="Pnezumi"&&destination.room==0&&destination.point==3);
    float x=0,z=0;assert(place_position("destination-test","destinations",x,z)&&x==-200000&&z==100000);
    auto marker=[] {for(const auto&m:story_chart::shown_markers())if(m.chart=="notebook"&&m.marker=="tracked")return m;assert(false);return story_chart::MarkerView{};};
    auto before=marker();assert(before.x==-200000&&before.z==100000);
    constexpr uint32_t npc=kArena+0xB000;
    st8(npc+0x905,5);st8(npc+0x326,11);
    st32(npc+0x314,f32_as_u32(12345));st32(npc+0x31C,f32_as_u32(67890));
    memcpy(mem::ptr(kPlayObj+0x5134),"sea",4);
    call(0x022C36D0,npc); // actual public hook updates the live resident cache
    auto after=marker();assert(after.x==-200000&&after.z==100000);
    std::vector<Quest> storage;const auto rows=rows_now(storage);
    auto row=std::find_if(rows.begin(),rows.end(),[](const auto&r){return r.quest->mod=="destination-test";});
    assert(row!=rows.end()&&row->quest->where=="Inside the building");
    assert(reach("destination-test","destinations",2)==2);
    destination.stage="unchanged";
    assert(!place_destination("destination-test","destinations",destination)&&destination.stage=="unchanged");
    assert(story_actors::set_flag(b,"another-mod","opened"));
    assert(!place_destination("destination-test","destinations",destination));
    assert(story_actors::set_flag(b,"destination-test","opened"));
    assert(place_destination("destination-test","destinations",destination));
    assert(destination.stage=="Pnezumi"&&destination.room==1&&destination.point==-1&&destination.position.x==10&&destination.position.y==20&&destination.position.z==30);
    assert(story_actors::set_flag(b,"destination-test","closed"));
    assert(!place_destination("destination-test","destinations",destination));
    assert(story_actors::set_flag(b,"destination-test","closed",false));
    assert(reach("destination-test","destinations",3)==3);
    assert(place_destination("destination-test","destinations",destination)&&destination.stage=="sea"&&destination.position.x==1000&&destination.position.z==2000);
    call(0x022C36D0,npc);
    auto followed=marker();assert(followed.x==12345&&followed.z==67890); // positive control: sea following still updates the cache
    assert(step("destination-test","destinations",3,"Code-owned direction","Elsewhere")==3);
    assert(place("destination-test","destinations",500,-900)==3);
    assert(place_destination("destination-test","destinations",destination)&&destination.stage=="sea"&&destination.position.x==500&&destination.position.z==-900);
    assert(no_place("destination-test","destinations")==3);
    destination.stage="unchanged";
    assert(!place_destination("destination-test","destinations",destination)&&destination.stage=="unchanged");
    assert(!place_destination("another-mod","destinations",destination));
    // Code-only quests retain the existing sea-place counterpart too.
    assert(define("destination-test","Destinations","code-only","Code only","","")==kResultNew);
    assert(step("destination-test","code-only",1,"Go","Sea")==1);
    assert(place("destination-test","code-only",3000,4000)==1);
    assert(place_destination("destination-test","code-only",destination)&&destination.stage=="sea"&&destination.position.x==3000);
    const std::vector<uint8_t> saved_area(story_store::area(),story_store::area()+story_block::kAreaSize);
    const std::vector<uint8_t> game_save(mem::ptr(kSave),mem::ptr(kSave)+0x800);
    assert(place_destination("destination-test","code-only",destination));
    assert(std::equal(saved_area.begin(),saved_area.end(),story_store::area()));
    assert(std::equal(game_save.begin(),game_save.end(),mem::ptr(kSave)));
}


// The world editor's live story update (redeclare): only the edited mod's tables change, the save's records
// stay, a talk running keeps the story system busy, a removed talk gives the resident back to the game.
std::string live_manifest(const std::string& lines, const std::string& extra = "") {
    std::string npc = lines.empty() ? "" : R"(, "npc": {"who": "windfall-ub3", "lines": [")" + lines + R"("]})";
    return R"({"quests_format": 1, "quests": [{"id": "kreeb", "title": "Kreeb's lamp", "steps": [{"n": 1, "text": "Talk to her.")" + npc +
           R"(}, {"n": 2, "text": "Then sail on."}]})" + extra + R"(]})";
}
std::vector<Takeover> takeovers_of(const std::string& mod, const std::string& quest) {
    std::vector<Takeover> out;
    for (const Takeover& t : takeovers())
        if (t.quest == quest_key(mod, quest)) out.push_back(t);
    return out;
}
bool listed(const std::string& mod, const std::string& id) {
    for (const Quest& q : quests())
        if (q.mod == mod && q.id == id) return true;
    return false;
}
void test_redeclare() {
    constexpr uint32_t kNpc = kArena + 0x8000;
    Book b = story_store::book();
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    for (const Quest& q : quests()) b.set(story_mail::kRecordQuest, q.key, kStepDone);  // the earlier tests' quests want her no more
    st8(kNpc + 0x905, 5), st8(kNpc + 0x326, 11);  // the harbour lady at the harbour
    memcpy(mem::ptr(kPlayObj + 0x5134), "sea", 4);
    st32(kNpc + 0x8A8, 0x1234), st32(kNpc + 0x8AC, 0);
    auto end_talk = [&] {
        st16(kNpc + 0xF8, 1);
        call(0x022C36D0, kNpc);
        st16(kNpc + 0xF8, 0);
        call(0x022C36D0, kNpc);
    };
    // another mod with a talk for the same resident (not started), a letter and a chart of its own
    const char* other = R"({"charts": [{"id": "b-map", "name": "B's map", "place": {"x": 1000, "z": -200000}}],
      "letters": [{"id": "b-note", "sender": "B", "text": "Hello."}],
      "quests_format": 1, "quests": [{"id": "other", "title": "B's tale",
        "steps": [{"n": 1, "text": "Talk.", "npc": {"who": "windfall-ub3", "lines": ["B speaks."]}}]}]})";
    assert(declare("live-b", "B", mods::json::parse(other)));
    assert(declare("live-a", "A", mods::json::parse(live_manifest("Hello from A."))));
    const auto b_talks = takeovers_of("live-b", "other");
    const auto charts_before = story_chart::charts();
    assert(b_talks.size() == 1 && takeovers_of("live-a", "kreeb").size() == 1);
    const uint16_t ka = quest_key("live-a", "kreeb");
    assert(reach_step(b, ka, 1) == 1);
    // the talk runs: not a calm moment; it ends: the quest is done (the game's rules)
    std::string why;
    assert(story_idle(why));
    assert(call(0x022C6C68, kNpc) == 0x2CF3);
    assert(!story_idle(why) && why.find("talk") != std::string::npos);
    end_talk();
    assert(story_idle(why) && state("live-a", "kreeb") == kResultDone);
    // the edit: other lines; the progress the save has stays as it is (here: step 1 again, by hand)
    assert(b.set(story_mail::kRecordQuest, ka, 1));
    std::vector<uint8_t> area(story_store::area(), story_store::area() + story_block::kAreaSize);
    Redeclared r;
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("Edited line.")), r, why));
    assert(r.quests == 1 && r.talks == 1 && r.letters == 0 && r.charts == 0);
    assert(std::equal(area.begin(), area.end(), story_store::area()));  // nothing of the save written
    auto a_talks = takeovers_of("live-a", "kreeb");
    assert(a_talks.size() == 1 && a_talks[0].lines == "Edited line." && a_talks[0].step == 1);
    assert(call(0x022C6C68, kNpc) == 0x2CF3);
    end_talk();
    assert(state("live-a", "kreeb") == kResultDone);
    // the talk removed: the resident is the game's again, the quest stays where it is
    assert(b.set(story_mail::kRecordQuest, ka, 1));
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("")), r, why) && r.talks == 0);
    assert(takeovers_of("live-a", "kreeb").empty() && state("live-a", "kreeb") == 1);
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1 && calls == std::vector<std::string>{"getMsg"});
    // the talk back (an undo): it speaks again
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("Hello from A.")), r, why) && r.talks == 1);
    assert(call(0x022C6C68, kNpc) == 0x2CF3);
    end_talk();
    // a letter and a chart of A's: added, changed, and the chart dropped (kept until a restart)
    const std::string extra = R"(], "letters": [{"id": "a-note", "sender": "Kreeb", "text": "Thanks."}],
      "charts": [{"id": "a-map", "name": "A's map", "place": {"x": 2000, "z": -200000}}], "x": [0)";
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("", extra)), r, why) && r.letters == 1 && r.charts == 1);
    declared_tick();  // the letter is registered with its send
    auto sender_of = [](const std::string& id) {
        for (const auto& l : story_mail::letters())
            if (l.mod == "live-a" && l.id == id) return l.sender;
        return std::string();
    };
    assert(sender_of("a-note") == "Kreeb");
    std::string renamed = extra;
    renamed.replace(renamed.find("\"Kreeb\""), 7, "\"Kreeb, the lamp keeper\"");
    renamed.replace(renamed.find("A's map"), 7, "Old lamp");
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("", renamed)), r, why));
    assert(sender_of("a-note") == "Kreeb, the lamp keeper");
    auto chart_named = [](const std::string& mod, const std::string& id) {
        for (const auto& c : story_chart::charts())
            if (c.mod == mod && c.chart == id) return c.name;
        return std::string();
    };
    assert(chart_named("live-a", "a-map") == "Old lamp");
    assert(redeclare("live-a", "A", mods::json::parse(live_manifest("")), r, why));
    assert(sender_of("a-note").empty() && r.charts_kept == std::vector<std::string>{"a-map"} && chart_named("live-a", "a-map") == "Old lamp");
    // a quest removed, another added; a quest the mod's code defined keeps its registration
    assert(define("live-a", "A", "coded", "Coded quest", "", "") == kResultNew && step("live-a", "coded", 1, "By code.", "") == 1);
    const char* swapped = R"({"quests_format": 1, "quests": [{"id": "fresh", "title": "A new tale", "steps": [{"n": 1, "text": "Go."}]}]})";
    const uint8_t kreeb_was = b.get(story_mail::kRecordQuest, ka);
    assert(kreeb_was == kStepDone);
    assert(redeclare("live-a", "A", mods::json::parse(swapped), r, why));
    assert(!listed("live-a", "kreeb") && listed("live-a", "fresh") && listed("live-a", "coded"));
    assert(b.get(story_mail::kRecordQuest, ka) == kreeb_was);  // its record stays (an undo shows it again)
    // a refused manifest changes nothing
    assert(!redeclare("live-a", "A", mods::json::parse(R"({"quests_format": 1, "quests": [{"id": "", "title": "x"}]})"), r, why));
    assert(listed("live-a", "fresh") && !listed("live-a", "kreeb"));
    // the other mod: its quest, talk, chart and letter as they were
    assert(listed("live-b", "other"));
    const auto b_now = takeovers_of("live-b", "other");
    assert(b_now.size() == 1 && b_now[0].lines == b_talks[0].lines && b_now[0].resident == b_talks[0].resident);
    assert(chart_named("live-b", "b-map") == "B's map");
    assert(sender_of("b-note").empty());  // (A's lookup) and B's letter is still B's:
    bool b_letter = false;
    for (const auto& l : story_mail::letters()) b_letter |= l.mod == "live-b" && l.id == "b-note" && l.sender == "B";
    assert(b_letter);
    (void)charts_before;
}

int main() {
    {
        QuestDestination d;std::string why;
        auto v=mods::json::parse(R"({"stage":"Pnezumi","room":0,"sea":{"x":-200000,"z":100000}})");
        assert(parse_quest_destination(v,d,why)&&d.stage=="Pnezumi"&&d.room==0&&d.point==-2&&d.sea_x==-200000);
        v["point"]=3;assert(parse_quest_destination(v,d,why)&&d.point==3);
        v["x"]=1;v["y"]=2;v["z"]=3;assert(!parse_quest_destination(v,d,why));v.object.erase("point");
        assert(parse_quest_destination(v,d,why)&&d.point==-1&&d.position.y==2);
        v.object.erase("sea");assert(!parse_quest_destination(v,d,why));
        assert(!parse_quest_destination(mods::json::parse(R"({"stage":"../bad","room":0,"sea":{"x":0,"z":0}})"),d,why));
        assert(!parse_quest_destination(mods::json::parse(R"({"x":0})"),d,why));
        assert(parse_quest_destination(mods::json::parse(R"({"x":0,"z":0})"),d,why)&&d.stage=="sea"&&d.room==25&&d.point==-1);
    }

    {
        std::array<uint8_t,1024> bytes{};story_mail::Book b(bytes.data());
        Availability a;a.flag_mod="local";a.flags={"read"};a.not_flags={"blocked"};
        assert(!available(a,nullptr,12,b));
        assert(story_actors::set_flag(b,"foreign","read"));assert(!available(a,nullptr,12,b));
        assert(story_actors::set_flag(b,"local","read"));assert(available(a,nullptr,12,b));
        assert(story_actors::set_flag(b,"local","blocked"));assert(!available(a,nullptr,12,b));
        assert(story_actors::set_flag(b,"local","blocked",false));assert(available(a,nullptr,12,b));
        assert(!available(a,nullptr,12));
    }

    assert(map(kGlobals, 0x10000) && map(kArena, 0x40000));
    assert(map(0x10470000, 0x10000));  // g_dComIfG_gameInfo's play.mSelectItem (story_item_buttons.cpp)
    story_store::set_memory(kArena + 0x20000);
    test_list();
    test_record();
    test_text();
    test_page();
    test_detail_texts();
    test_bag_place();
    test_services();
    test_hooks();
    test_page_screen();
    test_game_font();
    test_style();
    test_page_feel();
    test_caption();
    test_framed();
    test_npc_catalogue();
    test_takeover_rules();
    test_takeover_hooks();
    test_choice();
    test_manifest();
    test_manifest_charts();
    test_available();
    test_story_store();
    test_sail_manifest();
    test_sail_rules();
    test_sail_hooks();
    test_quest_destinations();
    test_redeclare();
    {
        auto m=mods::json::parse(R"({"quests_format":1,"quests":[{"id":"action","title":"Action reward","reward":{"item":"rupee_orange"},"steps":[{"n":1,"text":"Go","where":"Inside","place":{"stage":"Pnezumi","room":0,"point":1,"sea":{"x":0,"z":-200000}}}]}]})");
        assert(declare("action-test","Action",m));auto b=story_store::book();
        assert(reach("action-test","action",1)==1);assert(toggle_tracked(b,quest_key("action-test","action")));
        QuestDestination destination;assert(place_destination("action-test","action",destination)&&destination.stage=="Pnezumi"&&destination.point==1);
        assert(done_action("action-test","missing")==kResultInvalid);
        assert(done_action("action-test","action")==255&&tracked_key(b)==0);
        assert(done_action("action-test","action")==255);
        calls.clear();Cpu c{};action_rewards(&c);action_rewards(&c);
        assert(std::count(calls.begin(),calls.end(),"action reward 6")==1);
        assert(done_action("action-test","action")==255);action_rewards(&c);
        assert(std::count(calls.begin(),calls.end(),"action reward 6")==1);
        assert(!place_destination("action-test","action",destination));
    }
    std::puts("story_notebook_test: ok");
    return 0;
}
