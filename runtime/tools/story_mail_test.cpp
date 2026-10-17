// Letters from story mods (runtime/src/mods/story_mail.*): the save record, the letter text, the
// services and the postbox hooks on a synthetic postbox, message object and save. The game's own
// functions are stubbed; no game files are needed.
#include "mods/story_mail.h"
#include "mods/story_mail_book.h"
#include "mods/story_store.h"
#include "runtime.h"

#include <cassert>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_mail;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

// synthetic guest memory: the game's data page (letter table, save pointer, event name), the save,
// a postbox actor, a message object and the mod heap
constexpr uint32_t kData = 0x10030000, kGlobals = 0x101F0000, kArena = 0x20000000;
constexpr uint32_t kSave = kArena, kActor = kArena + 0x2000, kPlay = kArena + 0x4000, kMsg = kArena + 0x8000,
                   kSet = kArena + 0x9000, kStaff = 5;
constexpr uint32_t kCarrier = 0x1234;  // the first table letter's message number
constexpr uint32_t kMsbt = 0x30001000, kIndex = 42, kGameText = kArena + 0xA000;  // starts with a code (0E 0001 0000 0000)

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}


// the game's code
int game_readable = 0, game_waiting = 0;
uint32_t pearl = 1;
uint32_t lookup_index = 42;  // the label index getMessage finds
std::vector<std::string> calls;
bool event_ended = false;
std::map<uint32_t, void (*)(Cpu*)> hooked;  // replaced functions: game calls go through the hooks
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x023A44A8: c->r[3] = uint32_t(game_readable); calls.push_back("readable"); break;
    case 0x023A51CC: c->r[3] = uint32_t(game_waiting); calls.push_back("count"); break;
    case 0x023A46E4: calls.push_back("disp"); break;
    case 0x023A4700: calls.push_back("present"); break;
    case 0x023A50CC: calls.push_back("demo"); break;
    case 0x023A55BC: c->r[3] = 16; calls.push_back("next"); break;
    case 0x025F50E8:  // getMessage(manager, out, name, label): fills the set and the label index
        st32(c->r[4] + 0x8, kSet);
        st32(c->r[4] + 0x118, lookup_index);
        calls.push_back("message");
        break;
    case 0x0273A2E8: c->r[3] = kGameText; calls.push_back("text"); break;
    case 0x025B7D90: c->r[3] = pearl; calls.push_back("symbol"); break;
    case 0x0254DA38: calls.push_back("itemGet" + std::to_string(c->r[3])); break;
    default: assert(!"unexpected original");
    }
}
uint32_t have_bag = 1;  // Link has the Delivery Bag (the game's checkGetItem)
unsigned bag_asks = 0;
uint32_t game_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    if (hooked.count(fn)) {
        for (size_t i = 0; i < a.size(); i++) c->r[3 + i] = a[i];
        hooked[fn](c);
        return c->r[3];
    }
    switch (fn) {
    case 0x025200D4: return kPlay;
    case 0x02520C0C: assert(a.size() == 1 && a[0] == 0x30); bag_asks++; return have_bag;  // checkGetItem(Delivery Bag)
    case 0x025D7DEC:  // the enclosure at the postbox
        assert(a.size() == 7 && a[0] == kActor + 0x314 && a[3] == 0xFFFFFFFFu);
        calls.push_back("item" + std::to_string(a[1]));
        return 77;
    case 0x02543280: assert(a.size() == 2 && a[0] == kPlay + 0x52C4 && a[1] == kStaff); calls.push_back("cutEnd"); return 0;
    case 0x0254457C:
        assert(a.size() == 2 && a[0] == kPlay + 0x52C4 && a[1] == 0x10031464);
        return event_ended;
    case 0x023A40E0: assert(a.size() == 3 && a[0] == kActor); calls.push_back("modeProc" + std::to_string(a[2])); return 0;
    default: assert(!"unexpected game call"); return 0;
    }
}

std::u16string guest_u16(uint32_t at) {
    std::u16string s;
    for (; ld16(at); at += 2) s.push_back(char16_t(ld16(at)));
    return s;
}

void test_book() {
    uint8_t raw[kBookSize] = {};
    Book b(raw);
    assert(b.empty() && b.usable() && b.free_records() == kRecords);
    const uint16_t k1 = letter_key("mod-a", "hello"), k2 = letter_key("mod-b", "hello");
    assert(k1 && k2 && k1 != k2 && letter_key("mod-a", "hello") == k1);
    assert(letter_key("ab", "c") != letter_key("a", "bc"));  // the separator counts
    assert(b.state(k1) == kNone);
    assert(b.set_state(k1, kWaiting) && b.state(k1) == kWaiting && b.state(k2) == kNone);
    assert(raw[0] == kMagic && raw[1] == 2 && raw[4] == kRecordLetter && raw[5] == uint8_t(k1 >> 8) &&
           raw[6] == uint8_t(k1) && raw[7] == kWaiting);
    assert(b.set_state(k1, kRead) && b.state(k1) == kRead && raw[8] == 0);  // updated in place
    // kinds are separate name spaces: the same key of another kind is another record
    assert(!b.has(kRecordItem, k1) && b.set(kRecordItem, k1, 1) && b.get(kRecordItem, k1) == 1 && b.state(k1) == kRead);
    // a record of a kind this code doesn't know (another feature's) is kept as it is
    raw[4 + 4 * 5] = 9, raw[4 + 4 * 5 + 1] = 0x12, raw[4 + 4 * 5 + 2] = 0x34, raw[4 + 4 * 5 + 3] = 0x56;
    assert(b.get(9, 0x1234) == 0x56 && b.free_records() == kRecords - 3);
    for (unsigned i = 1; b.free_records(); i++) assert(b.set_state(letter_key("m", "l" + std::to_string(i)), kWaiting));
    assert(!b.set_state(k2, kWaiting) && b.state(k2) == kNone);  // full
    assert(b.state(k1) == kRead && b.get(9, 0x1234) == 0x56 && !b.set(kRecordFree, 1, 1));
    // data this port did not write (or the unreleased format 1) is left alone
    uint8_t foreign[kBookSize] = {};
    foreign[10] = 7;
    Book f(foreign);
    assert(!f.usable() && !f.set_state(k1, kWaiting) && foreign[0] == 0 && f.state(k1) == kNone);
    foreign[0] = kMagic, foreign[1] = 1;
    assert(!Book(foreign).usable() && !Book(foreign).set(kRecordItem, kItemNotebook, 1));
}

void test_text() {
    assert(valid_id("hello") && valid_id("chapter-1.part_2") && !valid_id("") && !valid_id("Hello") &&
           !valid_id("a b") && !valid_id(std::string(33, 'a')) && valid_id(std::string(32, 'a')));
    assert(compose("Me", "Hi\nthere\n\n") == u"Hi\nthere\n\nMe");
    assert(compose("Zoë", "Grüße ✉") == u"Grüße ✉\n\nZoë");
    assert(compose("A", "\xF0\x9F\x90\x9F") == u"\xD83D\xDC1F\n\nA");  // a surrogate pair
    assert(compose("", "text").empty() && compose("A", "").empty() && compose("A", "\n\n").empty());
    assert(compose("A\nB", "text").empty());                              // the sender is one line
    assert(compose("A", "tab\there").empty() && compose("A", "a\x0E" "b").empty());  // no control codes
    assert(compose("A", "a\rb").empty() && compose("A", "\xC2\x85").empty());
    assert(compose("A", "\xC0\xAF").empty() && compose("A", "\xED\xA0\x80").empty() && compose("A", "\xE2\x82").empty());
    assert(compose(std::string(kMaxSenderBytes + 1, 'a'), "text").empty());
    assert(compose("A", std::string(kMaxTextBytes + 1, 'a')).empty());
    assert(compose("A", std::string(kMaxUnits, 'a')).empty());  // too long with the signature
    assert(!compose("A", std::string(600, 'a')).empty());
}

void test_services() {
    assert(state("mod", "hello") == kResultNone);  // no save loaded yet
    assert(send("mod", "hello", "Me", "Text") == kResultNoRoom);
    st32(0x101F84DC, kSave);
    assert(send("mod", "hello", "Me", "Text") == kResultWaiting);
    assert(send("mod", "hello", "Me", "Text") == kResultWaiting);  // not twice
    assert(state("mod", "hello") == kResultWaiting && state("other", "hello") == kResultNone);
    assert(send("mod", "Bad id", "Me", "Text") == kResultInvalid);
    assert(send("mod", "bad-text", "Me", "a\x01") == kResultInvalid);
    assert(state("mod", "bad-text") == kResultNone);
    auto list = letters();
    assert(list.size() == 1 && list[0].mod == "mod" && list[0].id == "hello" && list[0].sender == "Me" &&
           list[0].state == kResultWaiting);
    Book b = story_store::book();
    assert(b.state(letter_key("mod", "hello")) == kWaiting);
    assert(!notebook_owned() && notebook_page() == u"Story notebook\n\nNo letters yet.\n");
}

void test_hooks() {
    // install: all or nothing
    set_text_buffers(kArena + 0x18000, 8192);
    set_game_calls(original, game_call);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x0273A2E8 ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 10);

    st32(0x10031504 + 12 + 4, kCarrier);
    Cpu c{};
    // a game letter goes first, every call passes through
    game_readable = 4;
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 4);
    game_readable = 0;
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 13);
    game_waiting = 2;
    hooks::receive_count(&c);
    assert(c.r[3] == 3);
    st32(kActor + 0xA04, 4);
    calls.clear();
    for (auto h : {hooks::disp_letter_start, hooks::present, hooks::receive_demo, hooks::next_msg}) {
        c.r[3] = kActor;
        h(&c);
    }
    assert((calls == std::vector<std::string>{"disp", "present", "demo", "next"}));

    // the mod letter (index 13)
    st32(kActor + 0xA04, 13);
    st32(kActor + 0xA08, 2);
    calls.clear();
    st32(kMsg, 0xCF7);  // "letter 2": handed out without postage
    c.r[3] = kActor, c.r[4] = kMsg;
    hooks::next_msg(&c);
    assert(c.r[3] == 16 && ld8(kActor + 0xA01) == 1 && calls.empty());
    set_notebook_model("/data/notebook/Vbeso.szs");
    assert(model_override("/vol/content/Common/Object/Vbeso.szs").empty());  // nothing being handed out
    c.r[3] = kActor, c.r[4] = kStaff;
    set_notebook_letter("mod", "hello");  // the Story Notebook package's letter (here: this one)
    hooks::present(&c);  // it encloses the notebook (the Cabana Deed's get demo)
    assert((calls == std::vector<std::string>{"item156", "cutEnd"}) && ld32(kPlay + 0x52A0) == 77);
    // while it is handed out, the stand-in's model archive is the notebook's (any case); nothing else
    assert(model_override("/vol/content/Common/Object/Vbeso.szs") == "/data/notebook/Vbeso.szs");
    assert(model_override("/vol/content/common/object/VBESO.SZS") == "/data/notebook/Vbeso.szs");
    assert(model_override("/vol/content/Common/Object/Vbesox.szs").empty() && model_override("Vbeso.szs").empty());
    c.r[3] = kActor;
    hooks::disp_letter_start(&c);
    assert(ld32(kActor + 0x7C0) == kCarrier);
    // the message system looks the letter up; its text is the mod's
    st32(kSet, kMsbt);
    st32(kMsg + 0x11C, kCarrier);
    c.r[3] = 0x101F4AE8, c.r[4] = kMsg;
    hooks::get_message(&c);
    c.r[3] = kMsbt, c.r[4] = kIndex;
    hooks::get_text(&c);
    assert(ld16(c.r[3]) == 0x0E && ld16(c.r[3] + 2) == 1 && guest_u16(c.r[3] + 8) == u"Text\n\nMe");  // the game's code kept
    c.r[3] = kMsbt, c.r[4] = kIndex + 1;
    hooks::get_text(&c);
    assert(c.r[3] == kGameText);
    // the get-item message of the stand-in says "story notebook"
    st32(kMsg + 0x11C, 0x101);
    c.r[3] = 0x101F4AE8, c.r[4] = kMsg;
    lookup_index = kIndex + 5;
    hooks::get_message(&c);
    lookup_index = kIndex;
    c.r[3] = kMsbt, c.r[4] = kIndex + 5;
    hooks::get_text(&c);
    assert(ld16(c.r[3]) == 0x0E && guest_u16(c.r[3] + 8).rfind(u"You got the story notebook!", 0) == 0);
    // its get function doesn't run (no Cabana Deed in the bag); the notebook is recorded instead
    calls.clear();
    c.r[3] = 0x9C;
    hooks::item_get(&c);
    assert(calls.empty() && notebook_owned());
    assert(model_override("/vol/content/Common/Object/Vbeso.szs").empty());  // the real deed's model again
    c.r[3] = 0x9C;  // a real Cabana Deed later is the game's again
    hooks::item_get(&c);
    assert((calls == std::vector<std::string>{"itemGet156"}));
    c.r[3] = kMsbt, c.r[4] = kIndex + 5;
    hooks::get_text(&c);
    assert(c.r[3] == kGameText);
    st32(kMsg + 0x11C, kCarrier);
    // the letter message ends the talk like a game letter
    st32(kMsg, kCarrier);
    c.r[3] = kActor, c.r[4] = kMsg;
    hooks::next_msg(&c);
    assert(c.r[3] == 14);
    // the event is still running: nothing happens
    calls.clear();
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(calls.empty() && state("mod", "hello") == kResultWaiting);
    // the event ends: read, no more letters
    event_ended = true;
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "hello") == kResultRead && ld32(kActor + 0xA04) == 0 && ld32(kActor + 0xA08) == 1);
    assert(notebook_page() == u"Story notebook\n\nLetter from Me\n");
    assert((ld16(kPlay + 0x52B8) & 8) && calls.back() == "modeProc0");
    // the text is no longer replaced, and the letter isn't sent again
    c.r[3] = kMsbt, c.r[4] = kIndex;
    hooks::get_text(&c);
    assert(c.r[3] == kGameText);
    assert(send("mod", "hello", "Me", "Text") == kResultRead);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 0);
    game_waiting = 0;
    hooks::receive_count(&c);
    assert(c.r[3] == 0);

    // two mod letters in a row: the demo goes on with the next one
    assert(send("mod", "second", "Me", "Two") == kResultWaiting);
    assert(send("mod", "third", "Me", "Three") == kResultWaiting);
    st32(kActor + 0xA04, 13);
    c.r[3] = kActor;
    hooks::disp_letter_start(&c);
    calls.clear();
    c.r[3] = kActor, c.r[4] = kStaff;
    hooks::present(&c);  // the notebook is owned: later letters enclose a green rupee
    assert((calls == std::vector<std::string>{"item1", "cutEnd"}));
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "second") == kResultRead && state("mod", "third") == kResultWaiting);
    assert(ld32(kActor + 0xA04) == 13 && ld8(kActor + 0xA0F) == 1 && ld8(kActor + 0xA03) == 1 && ld32(kActor + 0xA08) == 2);
    assert(calls.back() == "modeProc1");
    // a full state load in the middle of a letter: the first waiting one is marked read
    state_loaded();
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "third") == kResultRead);

    // before the first pearl: the postbox's own check passes only while a mod letter waits, and
    // then the game's letters stay out
    const uint32_t gate = 0x023A4A94;
    pearl = 0, game_readable = 4, game_waiting = 2;
    auto check = [&](uint32_t lr, uint32_t no) {
        c.lr = lr, c.r[3] = kSave + 0xD4, c.r[4] = no;
        hooks::symbol(&c);
        return c.r[3];
    };
    assert(check(gate, 1) == 0);  // no mod letter waiting: the game's answer
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 4);  // (the game would not ask; unchanged)
    assert(send("mod", "fourth", "Me", "Four") == kResultWaiting);
    assert(check(0x02000000, 1) == 0 && check(gate, 2) == 0);  // other callers and symbols: the game's
    assert(check(gate, 1) == 1);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 13);  // game letter 4 stays closed before the pearl
    hooks::receive_count(&c);
    assert(c.r[3] == 1);
    pearl = 1;
    assert(check(gate, 1) == 1);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 4);  // with the pearl the game's letters come first again
    hooks::receive_count(&c);
    assert(c.r[3] == 3);
}

// Mod letters only once Link has the Delivery Bag: before it they wait in the record (and survive a
// save and load), the postbox shows only the game's mail; after it the next visit hands them out, once.
void test_bag_gate() {
    Cpu c{};
    // a fresh save: no bag, no pearl
    memset(story_store::area(), 0, story_block::kAreaSize);
    have_bag = 0, pearl = 0, game_readable = 0, game_waiting = 0;
    assert(send("mod", "held", "Me", "Held") == kResultWaiting && state("mod", "held") == kResultWaiting);
    const uint32_t gate = 0x023A4A94;
    c.lr = gate, c.r[3] = kSave + 0xD4, c.r[4] = 1;
    hooks::symbol(&c);
    assert(c.r[3] == 0);  // the postbox stays closed: nothing for Link yet
    c = Cpu{};
    c.r[3] = kActor, c.r[5] = 0x1234;
    hooks::readable(&c);
    assert(c.r[3] == 0 && c.r[5] == 0x1234);  // no mod letter; the hook's other registers kept
    c.r[3] = kActor;
    hooks::receive_count(&c);
    assert(c.r[3] == 0);
    // the held letter's record: saved and loaded with the save file, still waiting
    uint8_t saved[story_block::kAreaSize];
    memcpy(saved, story_store::area(), sizeof saved);
    memset(story_store::area(), 0, sizeof saved);
    memcpy(story_store::area(), saved, sizeof saved);
    assert(state("mod", "held") == kResultWaiting && Book(saved, story_block::kAreaRecords).state(letter_key("mod", "held")) == kWaiting);
    assert(send("mod", "held", "Me", "Held") == kResultWaiting);  // sent again: no second copy
    unsigned held = 0;
    for (unsigned r = 0; r < story_block::kAreaRecords; r++) held += Book(saved, story_block::kAreaRecords).record_kind(r) == kRecordLetter;
    assert(held == 1);
    // Link gets the bag: the next postbox visit hands the letter out (before the first pearl too), once
    have_bag = 1;
    c.lr = gate, c.r[3] = kSave + 0xD4, c.r[4] = 1;
    hooks::symbol(&c);
    assert(c.r[3] == 1);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 13);
    c.r[3] = kActor;
    hooks::receive_count(&c);
    assert(c.r[3] == 1);
    st32(kActor + 0xA04, 13);  // the postbox took the mod letter (its execute stores readable's answer)
    c.r[3] = kActor;
    hooks::disp_letter_start(&c);
    calls.clear();
    event_ended = 1;
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "held") == kResultRead);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 0);  // read: not handed out again
    assert(send("mod", "held", "Me", "Held") == kResultRead);
}

void test_owned_restore_text(){
    constexpr uint32_t record=kArena+0x17000;
    set_say_restore_record(record,128);
    const SayOwner owner{kActor,123,1,77};const std::u16string text=u"Own seated resident, restored exactly.";
    say_owned(0x2CF3,&text,owner);st32(kSet,kMsbt);st32(kMsg+0x11C,0x2CF3);lookup_index=kIndex;
    Cpu c{};c.r[3]=0x101F4AE8;c.r[4]=kMsg;hooks::get_message(&c);assert(said());
    std::array<uint8_t,128> saved;std::memcpy(saved.data(),mem::ptr(record),saved.size());
    // Full snapshots restore provenance, while host-only text bytes and host override are lost.
    state_loaded();unsay(); // A prior host owner cleans up after restore; it must not erase restored provenance.
    std::memset(mem::ptr(kArena+0x18000+4096),0xA5,2048);assert(!said());
    SayOwner restored;assert(restore_say_owner(0x2CF3,restored)&&restored==owner);
    calls.clear();assert(resume_say(0x2CF3,&text,owner));assert(calls.empty());
    c.r[3]=kMsbt;c.r[4]=kIndex;hooks::get_text(&c);
    assert(calls.empty()&&ld16(c.r[3])==0x0E&&guest_u16(c.r[3]+8)==text);
    c.r[3]=kMsbt;c.r[4]=kIndex+1;hooks::get_text(&c);assert(c.r[3]==kGameText&&calls==std::vector<std::string>{"text"});
    for(unsigned field=0;field<4;++field){SayOwner wrong=owner;if(field==0)++wrong.actor;else if(field==1)++wrong.pid;else if(field==2)++wrong.token;else ++wrong.partner;assert(!resume_say(0x2CF3,&text,wrong));}
    assert(!resume_say(0x999,&text,owner));const std::u16string oversized(701,u'x');assert(!resume_say(0x2CF3,&oversized,owner));
    auto checksum=[&]{uint32_t h=2166136261u;for(uint32_t i=4;i<104;++i)h=(h^ld8(record+i))*16777619u;st32(record+104,h);};
    // Invalidated/new conversations cannot reuse an older restored key accidentally.
    unsay();assert(!restore_say_owner(0x2CF3,restored));
    std::memcpy(mem::ptr(record),saved.data(),saved.size());st32(record+20,0xFFFFFFF0);checksum();assert(!restore_say_owner(0x2CF3,restored));
    std::memcpy(mem::ptr(record),saved.data(),saved.size());st32(record+36,65);checksum();assert(!resume_say(0x2CF3,&text,owner));
    std::memcpy(mem::ptr(record),saved.data(),saved.size());st32(record+28,129);checksum();assert(!restore_say_owner(0x2CF3,restored));
    std::memcpy(mem::ptr(record),saved.data(),saved.size());say(0x2CF3,&text);assert(!restore_say_owner(0x2CF3,restored));
    set_say_restore_record(0xFFFFFFF0,128);assert(!restore_say_owner(0x2CF3,restored));
    set_say_restore_record(record,127);assert(!restore_say_owner(0x2CF3,restored));
    set_say_restore_record(record,128);std::memcpy(mem::ptr(record),saved.data(),saved.size());state_loaded();assert(resume_say(0x2CF3,&text,owner));unsay();assert(!restore_say_owner(0x2CF3,restored));
}

}  // namespace

int main() {
    assert(map(kData, 0x10000) && map(kGlobals, 0x10000) && map(kArena, 0x20000));
    story_store::set_memory(kArena + 0x1C000);
    st16(kGameText, 0x0E), st16(kGameText + 2, 1), st16(kGameText + 8, 'H'), st16(kGameText + 10, 'i');
    test_book();
    test_text();
    test_services();
    test_hooks();
    test_bag_gate();
    test_owned_restore_text();
    std::puts("story_mail_test: ok");
    return 0;
}
