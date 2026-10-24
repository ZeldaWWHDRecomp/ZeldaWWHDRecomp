// Story-mod letters delivered by the Rito postbox (story_mail.h, docs/mod-sdk-v2.md "Letters").
//
// How the game delivers mail (public HD decomp: d/d_letter.cpp, d/actor/d_a_obj_toripost.cpp):
//   - A letter is an event register of two bits in the save's event flags (save base + 0x644):
//     0 not sent, 1 sent, 2 waiting in the postbox (dLetter::stock), 3 read (dLetter::read).
//   - The postbox actor (daObjTpost_c) has a fixed table of 12 letters at 0x10031504 (entries 1..12
//     of 12 bytes: +0 needs postage, +4 message number, +8 item, +10 event register). Each step,
//     execute() sets mNumReadable (+0xA04) to the first waiting letter (getReadableLetterNum).
//   - Talking to it, nextMsg() walks the messages "you have mail" (0xCE5/0xCE6), "n letters" (0xCEB,
//     getReceiveLetterNum), "letter k" (0xCF7) and, for letters with postage, the payment (0xCF3...).
//     Then the receive event runs with three cuts: present (the letter's item), set animation, and
//     DispLetter, which shows the letter's message in the letter window (mCurrMsgNo, +0x7C0). When
//     the event ends, modeReceiveDemo() marks the letter read and goes on with the next one.
//
// A mod letter takes the place right after the game's own letters: index 13 (kModIndex), which the
// game's loops (1..12) never reach. The hooks below handle exactly that index and pass every other
// call to the game unchanged:
//   - getReadableLetterNum / getReceiveLetterNum also count waiting mod letters;
//   - for index 13 the table is not read: no postage, the letter window of the game's first letter
//     (its message number is read from the table at run time), the read state goes to the record;
//   - the enclosure (the receive event waits for one): the story notebook with the Story Notebook package's letter
//     of a save file, shown with the Cabana Deed's get-item demo and the port's own text; its get
//     function is not run, the notebook goes to the record. Later letters enclose a green rupee;
//   - mod letters are handed out once Link has the Delivery Bag (bag_owned: the game's own
//     dComIfGs_checkGetItem); before that they wait in the record, nothing is lost;
//   - before the first pearl the postbox keeps its mail closed (execute: isSymbol(1)); that one
//     check passes while a mod letter can be handed out (the bag comes before the first pearl), and
//     then the game's own letters are left out;
//   - the letter window shows the mod's text: when the message system looks up that message for the
//     letter (hd_msg_res_mgr getMessage 0x025F50E8), its MSBT set and label index are noted, and the
//     text lookup (0x0273A2E8, text of a label index in an MSBT) returns the mod's text for exactly
//     that entry while the letter is shown (likewise the notebook's get-item message).
#include "story_mail.h"
#include "story_mail_book.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>

#include "guest_addr.h"
#include "story_store.h"
#include "runtime.h"

namespace story_mail {
namespace {

// game addresses: canonical USA ones, translated for this build (guest_addr.h)
const uint32_t kSavePtr = GD(0x101F84DC);          // save base pointer (d_com_inf_game)
const uint32_t kLetterTable = GD(0x10031504);      // daObjTpost_c letter table, 12-byte entries
const uint32_t kReceiveEventName = GD(0x10031464); // the receive event's name (end check)
const uint32_t kPlay = GC(0x025200D4);             // the play object
const uint32_t kCutEnd = GC(0x02543280);           // dEvent_manager_c::cutEnd(staff)
const uint32_t kPresentItem = GC(0x025D7DEC);      // fopAcM_createItemForPresentDemo(pos, item, ...)
const uint32_t kEndCheck = GC(0x0254457C);         // dEvent_manager_c::endCheck(name)
const uint32_t kModeProc = GC(0x023A40E0);         // daObjTpost_c::modeProc(proc, mode)
const uint32_t kReadable = GC(0x023A44A8);         // daObjTpost_c::getReadableLetterNum
const uint32_t kReceiveCount = GC(0x023A51CC);     // daObjTpost_c::getReceiveLetterNum
const uint32_t kDispLetterStart = GC(0x023A46E4);  // daObjTpost_c::cutDispLetterStart(staff)
const uint32_t kPresent = GC(0x023A4700);          // daObjTpost_c::cutPresentProc(staff)
const uint32_t kReceiveDemo = GC(0x023A50CC);      // daObjTpost_c::modeReceiveDemo
const uint32_t kNextMsg = GC(0x023A55BC);          // daObjTpost_c::nextMsg(u32* msg)
const uint32_t kGetMessage = GC(0x025F50E8);       // message manager: label -> set, index, entry
const uint32_t kGetText = GC(0x0273A2E8);          // MSBT: text of a label index
const uint32_t kTextLengthReturn = GC(0x0273A340); // its call in the MSBT text length (label check)
const uint32_t kIsSymbol = GC(0x025B7D90);         // dSv_player_collect_c::isSymbol(no)
const uint32_t kGateCheckReturn = GC(0x023A4A94);  // after daObjTpost_c::execute's isSymbol(1) call
const uint32_t kExecItemGet = GC(0x0254DA38);      // execItemGet(itemNo): the item's get function
const uint32_t kCheckGetItem = GC(0x02520C0C);     // dComIfGs_checkGetItem(itemNo): Link has the item
constexpr uint32_t kDeliveryBag = 0x30;            // dItemNo_DELIVERY_BAG_e

// daObjTpost_c fields (d_a_obj_toripost.h) and play fields used by the receive demo
constexpr uint32_t kCurrMsgNo = 0x7C0, kReceivePending = 0xA01, kNextLetter = 0xA03, kNumReadable = 0xA04,
                   kLetterOrdinal = 0xA08, kEventIdx = 0xA0F;
constexpr uint32_t kPlayEventManager = 0x52C4, kPlayEventFlags = 0x52B8, kPlayItemPartner = 0x52A0;
constexpr uint32_t kActorPos = 0x314;  // fopAc_ac_c::current.pos
// message text (hd_msg_text): +8 set, +0x118 label index, +0x11C message number
constexpr uint32_t kMsgSet = 0x8, kMsgLabelIndex = 0x118, kMsgNumber = 0x11C;

constexpr int32_t kModIndex = 13;
// Every game letter encloses an item that Link takes after reading (rupees, a piece of heart...);
// the receive event waits for it. A mod letter encloses the smallest one: a green rupee.
constexpr uint32_t kEnclosedItem = 0x01;
// The letter of the Story Notebook package (the one its manifest says "encloses": ["notebook"]; the
// required package of the story system) encloses the story notebook instead. Link takes it with the
// get-item demo of a game item that looks like it (kNotebookModel); its get function, which would
// put that item in the Delivery Bag, is not run: the notebook is recorded in the story record.
constexpr uint32_t g_notebook_model = 0x9C;  // Cabana Deed: a sheet of parchment
constexpr uint32_t kMsgItemGet = 0x101;  // the get-item message the stand-in shows
// what the get-item message says instead (the port's own text)
constexpr char16_t kNotebookGetText[] =
    u"You got the story notebook!\n"
    u"It keeps the letters you receive\n"
    u"and the stories they begin.";
constexpr uint32_t kMsgLetterNumber = 0xCF7;  // "letter k" before a letter is handed out

struct Letter {
    std::string mod, id, sender;
    uint16_t key = 0;
    std::u16string text;  // as the letter window shows it (compose)
};

std::mutex g_mu;
std::vector<Letter> g_letters;  // registered this session, in send order
bool g_imported = false, g_installed = false;

// A message whose text is replaced while it is shown: the letter (in the letter window) and the
// notebook's get-item message. getMessage notes the MSBT entry of the number; the text lookup of
// exactly that entry returns the replacement (game thread only).
// The replacement is written into a host-only guest buffer (not part of save states, so a full
// state load can't leave stale text) when the message is looked up, after the game's message codes
// at the start of the original text (window and sound controls such as the get-item fanfare tag).
struct Override {
    uint32_t number = 0, buffer = 0;  // buffer: kSlotBytes of guest memory for this replacement
    const std::u16string* text = nullptr;
    uint32_t msbt = 0, index = 0;
    bool keyed = false;
};
constexpr uint32_t kPrefixRoom = 64, kSlotBytes = 2048;  // 64 + 2 * (kMaxUnits + 1) fits
uint32_t g_buffers = 0;  // four slots: the letter, the notebook's get message, a resident's line (say), a caption
// the mod letter being handed out by a postbox
struct Showing {
    uint16_t key = 0;
    uint32_t carrier = 0;
    Override letter;
} g_show;
Override g_notebook_text;  // armed while the notebook is handed out
Override g_npc_text;       // armed while a resident says a quest's line (say)
uint32_t g_say_restore_record=0;
SayOwner g_say_owner;
bool g_say_restore_pending=false;
constexpr uint32_t kSayMagic=0x53545931,kSayRecordBytes=128,kSayPrefixOffset=40,kSayChecksumOffset=104;
bool snapshot_range(uint32_t address,uint32_t size){
    const uint64_t end=uint64_t(address)+size;
    return (address>=mem::kMem2Start&&end<=mem::kMem2End)||(address>=mem::kRuntimeStart&&end<=mem::kHostStart);
}
uint32_t say_checksum(){uint32_t value=2166136261u;for(uint32_t i=4;i<kSayChecksumOffset;++i)value=(value^ld8(g_say_restore_record+i))*16777619u;return value;}
void clear_say_record(){if(g_say_restore_record)st32(g_say_restore_record,0);g_say_owner={};}
bool read_say_record(uint32_t number,SayOwner& owner){
    if(!g_say_restore_record||ld32(g_say_restore_record)!=kSayMagic||ld32(g_say_restore_record+4)!=1||ld32(g_say_restore_record+8)!=number||!ld32(g_say_restore_record+12)||(ld32(g_say_restore_record+36)>kPrefixRoom||(ld32(g_say_restore_record+36)&1))||ld32(g_say_restore_record+kSayChecksumOffset)!=say_checksum())return false;
    owner={ld32(g_say_restore_record+20),ld32(g_say_restore_record+24),ld32(g_say_restore_record+28),ld32(g_say_restore_record+32)};
    return owner.actor>=mem::kMem2Start&&uint64_t(owner.actor)+0xA24<=mem::kMem2End&&owner.token>0&&owner.token<=128;
}
void capture_say_record(const Override& text,uint32_t prefix){
    if(!g_say_restore_record||!g_say_owner.actor||prefix>kPrefixRoom)return;
    std::memset(mem::ptr(g_say_restore_record),0,kSayRecordBytes);
    st32(g_say_restore_record+4,1);st32(g_say_restore_record+8,text.number);st32(g_say_restore_record+12,text.msbt);st32(g_say_restore_record+16,text.index);
    st32(g_say_restore_record+20,g_say_owner.actor);st32(g_say_restore_record+24,g_say_owner.pid);st32(g_say_restore_record+28,g_say_owner.token);st32(g_say_restore_record+32,g_say_owner.partner);st32(g_say_restore_record+36,prefix);
    std::memcpy(mem::ptr(g_say_restore_record+kSayPrefixOffset),mem::ptr(text.buffer),prefix);
    st32(g_say_restore_record+kSayChecksumOffset,say_checksum());st32(g_say_restore_record,kSayMagic);
}

Override g_caption_text;   // armed while the game sets a caption from a message (caption)
const std::u16string g_notebook_body = kNotebookGetText;

// the postbox's pearl check passed only for mod letters (this step), and a notebook being handed out
bool g_gate_forced = false;
std::atomic<bool> g_notebook_pending{false};  // also read by the file system (model_override)
std::string g_notebook_model_file;            // the notebook's model archive (set_notebook_model)

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_MAIL_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(kSavePtr); }
Book book() {
    uint32_t base = save_base();
    return base ? story_store::book() : Book(nullptr);
}
bool have_save() { return save_base() != 0; }

int32_t result_of(State s) { return s == kWaiting ? kResultWaiting : s == kRead ? kResultRead : kResultNone; }

// the first waiting letter that a loaded mod registered this session (key 0: none)
struct Waiting {
    uint16_t key = 0;
    const std::u16string* text = nullptr;  // g_letters only grows and keeps its strings
};
Waiting next_waiting() {
    if (!have_save()) return {};
    Book b = book();
    if (!b.usable()) return {};
    std::lock_guard lock(g_mu);
    for (const Letter& l : g_letters)
        if (b.state(l.key) == kWaiting) return {l.key, &l.text};
    return {};
}
// Mod letters are handed out only once Link has the Delivery Bag (the game's own check, as the
// NPCs that hand out bag items use it: dComIfGs_checkGetItem(dItemNo_DELIVERY_BAG_e), the bag in its
// inventory slot). Before that they stay waiting in the record and come with the first postbox visit
// after the bag. Asked with a copy of the CPU, so the hook's own registers stay as they are.
bool bag_owned(Cpu* c) {
    Cpu probe = *c;
    return (g_call(&probe, kCheckGetItem, {kDeliveryBag}) & 0xFF) != 0;
}
Waiting next_deliverable(Cpu* c) {
    const Waiting w = next_waiting();
    return w.key && bag_owned(c) ? w : Waiting{};
}

int waiting_count() {
    if (!have_save()) return 0;
    Book b = book();
    if (!b.usable()) return 0;
    std::lock_guard lock(g_mu);
    int n = 0;
    for (const Letter& l : g_letters) n += b.state(l.key) == kWaiting;
    return n;
}

// Bytes of message codes (0x0E tag: 0E, group, type, size, size bytes of parameters; 0x0F end tag:
// 0F, group, type) at the start of a UTF-16 text, at most kPrefixRoom.
uint32_t leading_codes(uint32_t text) {
    uint32_t n = 0;
    for (;;) {
        const uint16_t unit = ld16(text + n);
        const uint32_t len = unit == 0x0E ? 8u + ld16(text + n + 6) : unit == 0x0F ? 6u : 0u;
        if (!len || n + len > kPrefixRoom) return n;
        n += len;
    }
}

uint32_t carrier_message() { return ld32(kLetterTable + 12 + 4); }  // the first letter's message
uint32_t play(Cpu* c) { return g_call(c, kPlay, {}); }
bool mod_letter(uint32_t self) { return int32_t(ld32(self + kNumReadable)) == kModIndex; }

}  // namespace

int32_t send(const std::string& mod, const std::string& letter, const std::string& sender, const std::string& text) {
    if (!valid_id(letter)) return kResultInvalid;
    const uint16_t key = letter_key(mod, letter);
    {
        std::lock_guard lock(g_mu);
        bool known = false;
        for (const Letter& l : g_letters) {
            if (l.key != key) continue;
            if (l.mod != mod || l.id != letter) return kResultInvalid;  // a 16-bit key collision
            known = true;
        }
        if (!known) {
            std::u16string body = compose(sender, text);
            if (body.empty()) return kResultInvalid;
            g_letters.push_back({mod, letter, sender, key, std::move(body)});
            LOG("[mail] %s: letter \"%s\" from \"%s\" registered", mod.c_str(), letter.c_str(), sender.c_str());
        }
    }
    if (!have_save()) return kResultNoRoom;
    Book b = book();
    State s = b.state(key);
    if (trace()) {  // each change of a letter's state, as the mod sees it
        static std::map<uint16_t, int> seen;
        auto [it, added] = seen.try_emplace(key, -1);
        if (it->second != int(s)) LOG("[mail] %s: letter \"%s\" state %d in this save file", mod.c_str(), letter.c_str(), result_of(s));
        it->second = int(s);
    }
    if (s != kNone) return result_of(s);
    if (!b.set_state(key, kWaiting)) {
        LOG("[mail] %s: no room for letter \"%s\" in this save file", mod.c_str(), letter.c_str());
        return kResultNoRoom;
    }
    LOG("[mail] %s: letter \"%s\" is waiting in the postbox", mod.c_str(), letter.c_str());
    return kResultWaiting;
}

void reload(const std::string& mod, const std::vector<LetterText>& texts, const std::vector<std::string>& forget) {
    std::lock_guard lock(g_mu);
    for (const LetterText& t : texts)
        for (Letter& l : g_letters)
            if (l.mod == mod && l.id == t.id) {
                std::u16string body = compose(t.sender, t.text);
                if (!body.empty()) l.sender = t.sender, l.text = std::move(body);
            }
    std::erase_if(g_letters, [&](const Letter& l) { return l.mod == mod && std::find(forget.begin(), forget.end(), l.id) != forget.end(); });
}

bool busy() { return g_show.key || g_npc_text.text || g_caption_text.text || g_notebook_pending.load(); }

int32_t state(const std::string& mod, const std::string& letter) {
    if (!valid_id(letter)) return kResultInvalid;
    if (!have_save()) return kResultNone;
    return result_of(book().state(letter_key(mod, letter)));
}

std::vector<LetterInfo> letters() {
    std::vector<LetterInfo> out;
    std::lock_guard lock(g_mu);
    const bool save = have_save();
    for (const Letter& l : g_letters)
        out.push_back({l.mod, l.id, l.sender, save ? result_of(book().state(l.key)) : kResultNone});
    return out;
}

uint16_t g_notebook_letter = 0;  // the key of the letter that brings the notebook (0: none declared)
void set_notebook_letter(const std::string& mod, const std::string& letter) { g_notebook_letter = letter_key(mod, letter); }

bool notebook_owned() { return have_save() && book().get(kRecordItem, kItemNotebook) == 1; }
bool own_notebook_for_test() {
    if (!have_save() || !book().set(kRecordItem, kItemNotebook, 1)) return false;
    LOG("[mail] the story notebook is in this save file (the editor's test mode)");
    return true;
}

std::u16string notebook_page() {
    std::u16string page = u"Story notebook\n\n";
    bool any = false;
    for (const LetterInfo& l : letters()) {
        if (l.state != kResultRead) continue;
        std::u16string sender;
        append_utf16(l.sender, false, sender);
        page += u"Letter from " + sender + u"\n";
        any = true;
    }
    if (!any) page += u"No letters yet.\n";
    return page;
}

void set_notebook_model(const std::string& file) { g_notebook_model_file = file; }

// While the notebook is being handed out, the stand-in's model archive is the notebook's (a
// replacement built on the player's machine, tools/story_mods/notebook_model.py). The game loads the
// archive when the get-item demo starts and frees it when the demo item goes; outside that window
// (and if the stand-in's archive is already loaded) the game's own file is used.
std::string model_override(const std::string& guest) {
    if (!g_notebook_pending.load(std::memory_order_acquire) || g_notebook_model_file.empty()) return {};
    static const std::string kArchive = "common/object/vbeso.szs";
    if (guest.size() < kArchive.size()) return {};
    for (size_t i = 0; i < kArchive.size(); i++) {
        char ch = guest[guest.size() - kArchive.size() + i];
        if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        if (ch != kArchive[i]) return {};
    }
    static bool logged = false;
    if (!logged) LOG("[mail] the notebook's model is read from %s", g_notebook_model_file.c_str());
    logged = true;
    return g_notebook_model_file;
}

void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_text_buffers(uint32_t guest, uint32_t bytes) { g_buffers = bytes >= 4 * kSlotBytes ? guest : 0; }

// A resident's line replaced while one talk shows it (story_notebook's takeovers)
void say(uint32_t number, const std::u16string* text) {
    g_say_restore_pending=false;clear_say_record();
    g_npc_text = text && g_buffers ? Override{number, g_buffers + 2 * kSlotBytes, text} : Override{};
}
void unsay() { if(!g_say_restore_pending)clear_say_record();else g_say_owner={};g_npc_text = {}; }
void set_say_restore_record(uint32_t guest,uint32_t bytes){g_say_restore_record=bytes>=kSayRecordBytes&&!(guest&3)&&snapshot_range(guest,kSayRecordBytes)?guest:0;clear_say_record();}
void say_owned(uint32_t number,const std::u16string* text,SayOwner owner){if(text&&text->size()>kMaxUnits)text=nullptr;say(number,text);if(g_npc_text.text)g_say_owner=owner;}
bool restore_say_owner(uint32_t number,SayOwner& owner){g_say_restore_pending=false;return read_say_record(number,owner);}
bool resume_say(uint32_t number,const std::u16string* text,SayOwner owner){
    g_say_restore_pending=false;SayOwner saved;
    if(!text||text->size()>kMaxUnits||!g_buffers||!read_say_record(number,saved)||saved!=owner)return false;
    const uint32_t prefix=ld32(g_say_restore_record+36),buffer=g_buffers+2*kSlotBytes;
    std::memcpy(mem::ptr(buffer),mem::ptr(g_say_restore_record+kSayPrefixOffset),prefix);
    for(size_t i=0;i<text->size();++i)st16(buffer+prefix+uint32_t(2*i),(*text)[i]);
    st16(buffer+prefix+uint32_t(2*text->size()),0);
    g_npc_text={number,buffer,text,ld32(g_say_restore_record+12),ld32(g_say_restore_record+16),true};g_say_owner=owner;return true;
}
void caption(uint32_t number, const std::u16string* text) {
    g_caption_text = text && g_buffers && g_installed ? Override{number, g_buffers + 3 * kSlotBytes, text} : Override{};
}
void uncaption() { g_caption_text = {}; }
bool item_get_text(uint32_t number, const std::u16string* text) {
    if (g_notebook_pending) return false;
    if (!text) {
        g_notebook_text = {};
        return true;
    }
    if (!g_installed || !g_buffers) return false;
    g_notebook_text = Override{number, g_buffers + kSlotBytes, text};
    return true;
}
bool said() { return g_npc_text.text && g_npc_text.keyed; }
bool can_say() { return g_installed && g_buffers != 0; }

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original || !g_call) return;
    const std::pair<uint32_t, void (*)(Cpu*)> hooks[] = {
        {kReadable, hooks::readable},     {kReceiveCount, hooks::receive_count},
        {kDispLetterStart, hooks::disp_letter_start}, {kPresent, hooks::present},
        {kReceiveDemo, hooks::receive_demo}, {kNextMsg, hooks::next_msg},
        {kGetMessage, hooks::get_message}, {kGetText, hooks::get_text},
        {kIsSymbol, hooks::symbol}, {kExecItemGet, hooks::item_get},
    };
    // all or nothing: a postbox with only some of the hooks would read past the game's letter table
    for (const auto& [addr, fn] : hooks) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[mail] %s replaces %08X: letters from mods are not delivered this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : hooks) replace(addr, fn);
    g_installed = true;
    LOG("[mail] postbox delivers letters from mods");
    if (trace())
        for (uint32_t i = 1; i <= 12; i++) {
            const uint32_t e = kLetterTable + 12 * i;
            LOG("[mail] game letter %u: postage %u message %X item %02X register %04X", i, ld8(e), ld32(e + 4), ld8(e + 8), ld16(e + 10));
        }
}

void state_loaded() {
    g_say_restore_pending=true; // Old host owners may unsay while the remaining state callbacks drain.
    g_show = {};
    g_npc_text = {};g_say_owner={}; // Restored snapshot provenance is deliberately preserved.
    g_caption_text = {};
    g_notebook_pending = false;
    g_notebook_text = {};
}

namespace hooks {

// s32 getReadableLetterNum(): the game's first waiting letter, else 13 for a mod letter that can be
// handed out (Link has the Delivery Bag). Before the first pearl the game's letters stay closed (the
// game's own check); only mod letters.
void readable(Cpu* c) {
    if (trace()) {  // where the postboxes are (scripted test runs walk Link there)
        static std::map<uint32_t, int> seen;
        if (!seen[c->r[3]]++) {
            const uint32_t pos = c->r[3] + kActorPos;
            LOG("[mail] postbox %08X at %.1f %.1f %.1f facing %d", c->r[3], u32_as_f32(ld32(pos)), u32_as_f32(ld32(pos + 4)),
                u32_as_f32(ld32(pos + 8)), int(int16_t(ld16(pos + 0xE))));
        }
    }
    if (g_gate_forced) c->r[3] = 0;
    else g_original(c, kReadable);
    if (c->r[3] == 0 && next_deliverable(c).key) c->r[3] = kModIndex;
}

// s16 getReceiveLetterNum(): the game's waiting letters plus the mod letters
void receive_count(Cpu* c) {
    if (g_gate_forced) c->r[3] = 0;
    else g_original(c, kReceiveCount);
    const int game = int(c->r[3]);
    const int mod = waiting_count() && bag_owned(c) ? waiting_count() : 0;
    c->r[3] = uint32_t(int32_t(int16_t(game + mod)));
}

// isSymbol(no): the postbox opens its mail only after the first pearl (execute: isSymbol(1)). For
// that one call, and only while a mod letter can be handed out, the check passes; readable() and
// receive_count() then leave out the game's letters.
void symbol(Cpu* c) {
    const uint32_t lr = c->lr, no = c->r[4];
    g_original(c, kIsSymbol);
    if (lr != kGateCheckReturn) return;
    g_gate_forced = false;
    if (c->r[3] == 0 && no == 1 && next_deliverable(c).key) {
        c->r[3] = 1;
        g_gate_forced = true;
    }
}

}  // namespace hooks
void (*g_item_get_next)(Cpu*) = nullptr;
void set_item_get_next(void (*next)(Cpu*)) { g_item_get_next = next; }
namespace hooks {
// execItemGet(itemNo): the notebook's stand-in item goes to the story record, not the Delivery Bag
void item_get(Cpu* c) {
    if (g_notebook_pending && (c->r[3] & 0xFF) == g_notebook_model) {
        g_notebook_pending = false;
        g_notebook_text = {};
        if (have_save()) book().set(kRecordItem, kItemNotebook, 1);
        LOG("[mail] the story notebook is in this save file");
        return;
    }
    if (g_item_get_next) return g_item_get_next(c);
    g_original(c, kExecItemGet);
}

// cutDispLetterStart(staff): the letter window with the mod's text
void disp_letter_start(Cpu* c) {
    const uint32_t self = c->r[3];
    if (!mod_letter(self)) {
        g_original(c, kDispLetterStart);
        return;
    }
    const Waiting l = next_waiting();
    g_show = {};
    g_show.carrier = carrier_message();
    if (l.key && g_buffers) g_show.key = l.key, g_show.letter = {g_show.carrier, g_buffers, l.text};  // (none: unreachable)
    st32(self + kCurrMsgNo, g_show.carrier);
    if (trace()) LOG("[mail] showing letter %04X as message %X", l.key, g_show.carrier);
}

// cutPresentProc(staff): the game's code with the mod letter's enclosure instead of a table entry
void present(Cpu* c) {
    const uint32_t self = c->r[3], staff = c->r[4];
    if (!mod_letter(self)) {
        g_original(c, kPresent);
        return;
    }
    const uint16_t letter = g_show.key ? g_show.key : next_waiting().key;  // the mod letter this postbox hands out
    const bool notebook = have_save() && g_notebook_letter && letter == g_notebook_letter && !book().get(kRecordItem, kItemNotebook);
    g_notebook_pending = notebook;
    g_notebook_text = notebook && g_buffers ? Override{kMsgItemGet, g_buffers + kSlotBytes, &g_notebook_body} : Override{};
    const uint32_t item = notebook ? g_notebook_model : kEnclosedItem;
    if (trace()) LOG("[mail] enclosure: item %02X%s", item, notebook ? " (the story notebook)" : "");
    const uint32_t id = g_call(c, kPresentItem, {self + kActorPos, item, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0});
    const uint32_t p = play(c);
    if (id != 0xFFFFFFFFu) st32(p + kPlayItemPartner, id);
    g_call(c, kCutEnd, {p + kPlayEventManager, staff});
}

// modeReceiveDemo(): after the receive event, mark the mod letter read and go on
void receive_demo(Cpu* c) {
    const uint32_t self = c->r[3];
    if (!mod_letter(self)) {
        g_original(c, kReceiveDemo);
        return;
    }
    if (!g_call(c, kEndCheck, {play(c) + kPlayEventManager, kReceiveEventName})) return;
    uint16_t key = g_show.key;
    if (!key) key = next_waiting().key;  // a state was loaded while the letter was shown
    if (key && have_save()) book().set_state(key, kRead);
    if (trace()) LOG("[mail] letter %04X read", key);
    g_show = {};
    const uint32_t next = g_call(c, kReadable, {self});
    st32(self + kNumReadable, next);
    const uint32_t p = play(c);
    st16(p + kPlayEventFlags, uint16_t(ld16(p + kPlayEventFlags) | 8));
    if (next) {
        g_call(c, kModeProc, {self, 0, 1});
        st8(self + kEventIdx, 1);
        st8(self + kNextLetter, 1);
        st32(self + kLetterOrdinal, ld32(self + kLetterOrdinal) + 1);
    } else {
        g_call(c, kModeProc, {self, 0, 0});
        st32(self + kLetterOrdinal, 1);
    }
}

// u16 nextMsg(u32* msg): a mod letter needs no postage and ends like a game letter
void next_msg(Cpu* c) {
    const uint32_t self = c->r[3], msg = c->r[4];
    if (!mod_letter(self)) {
        g_original(c, kNextMsg);
        return;
    }
    if (ld32(msg) == kMsgLetterNumber) {
        st8(self + kReceivePending, 1);
        c->r[3] = 16;
        return;
    }
    g_original(c, kNextMsg);
    // the game compares the shown message with the (nonexistent) table entry 13
    const bool letter_shown = g_show.carrier && ld32(self + kCurrMsgNo) == g_show.carrier;
    const uint32_t status = c->r[3] & 0xFFFF;
    if (status == 16 && letter_shown) c->r[3] = 14;
    else if (status == 14 && !letter_shown) c->r[3] = 16;
}

// getMessage(manager, out, set name, label): note the MSBT entry of a message being replaced
void get_message(Cpu* c) {
    const uint32_t out = c->r[4];
    g_original(c, kGetMessage);
    static const bool numbers = [] { const char* e = getenv("WWHD_MAIL_MSGLOG"); return e && *e == '1'; }();
    if (numbers) LOG("[mail] message %X looked up (lr %08X)", ld32(out + kMsgNumber), c->lr);
    for (Override* o : {&g_show.letter, &g_notebook_text, &g_npc_text, &g_caption_text}) {
        if (!o->text || o->keyed || ld32(out + kMsgNumber) != o->number) continue;
        const uint32_t set = ld32(out + kMsgSet);
        if (!set) continue;
        o->msbt = ld32(set);
        o->index = ld32(out + kMsgLabelIndex);
        o->keyed = true;
        Cpu probe = *c;  // the original text, for its leading codes
        probe.r[3] = o->msbt, probe.r[4] = o->index;
        g_original(&probe, kGetText);
        const uint32_t prefix = probe.r[3] ? leading_codes(probe.r[3]) : 0;
        for (uint32_t i = 0; i < prefix; i++) st8(o->buffer + i, ld8(probe.r[3] + i));
        const std::u16string& t = *o->text;
        for (size_t i = 0; i < t.size(); i++) st16(o->buffer + prefix + uint32_t(2 * i), t[i]);
        st16(o->buffer + prefix + uint32_t(2 * t.size()), 0);
        if(o==&g_npc_text)capture_say_record(*o,prefix);
        if (trace()) LOG("[mail] message %X: text replaced (MSBT %08X entry %u, %u bytes of codes kept)", o->number, o->msbt, o->index, prefix);
    }
}

// text(msbt, index): the replacement for a noted entry while it is shown. The message manager's
// check that a label has text (setWithLabel -> 02757A80 -> text length) keeps the game's text.
void get_text(Cpu* c) {
    if (c->lr == kTextLengthReturn) {
        g_original(c, kGetText);
        return;
    }
    for (const Override* o : {&g_show.letter, &g_notebook_text, &g_npc_text, &g_caption_text})
        if (o->keyed && c->r[3] == o->msbt && c->r[4] == o->index) {
            c->r[3] = o->buffer;
            return;
        }
    g_original(c, kGetText);
}

}  // namespace hooks
}  // namespace story_mail
