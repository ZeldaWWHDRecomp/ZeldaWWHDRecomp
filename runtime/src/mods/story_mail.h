// Story-mod letters: guest mods send a letter that the Rito postbox delivers like the game's own mail
// (docs/mod-sdk-v2.md, "Letters"). The record of received letters is kept in the save data
// (story_mail_book.h); the letter text stays in the sending mod.
//
// Nothing of this runs unless a loaded guest mod imports a wwhd_mail_* service: only then are the
// postbox and message hooks installed (install()).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Cpu;

namespace story_mail {

// Results of the guest services (wwhd_guest.h: WWHD_MAIL_*)
inline constexpr int32_t kResultNone = 0, kResultWaiting = 1, kResultRead = 2;
inline constexpr int32_t kResultInvalid = -1, kResultNoRoom = -2;

// wwhd_mail_send: registers the letter's text for this session and, if this save file never got
// the letter, puts it in the postbox.
int32_t send(const std::string& mod, const std::string& letter, const std::string& sender, const std::string& text);
// wwhd_mail_state: the letter's state in the current save file
int32_t state(const std::string& mod, const std::string& letter);

// World editor (game thread, while busy() is false): the texts of the mod's registered letters as its
// manifest has them now, and the letters it no longer declares forgotten (the postbox no longer hands
// them out; what the save file says of them stays). Letters not registered yet come with their send().
struct LetterText {
    std::string id, sender, text;
};
void reload(const std::string& mod, const std::vector<LetterText>& texts, const std::vector<std::string>& forget);
// A letter, a resident's quest line, a caption or the notebook's get message is being shown.
bool busy();

// Letters of the loaded mods and their state in the current save file (for lists of received letters).
struct LetterInfo {
    std::string mod, id, sender;
    int32_t state;
};
std::vector<LetterInfo> letters();

// The story notebook: owned once the Story Notebook package's letter (which encloses it) was read in this save
// file. Its page lists the letters read, one row each (a placeholder for the full notebook).
bool notebook_owned();
// the letter that brings the notebook: the Story Notebook package's ("encloses": ["notebook"]); other
// mod letters enclose a green rupee
void set_notebook_letter(const std::string& mod, const std::string& letter);
std::u16string notebook_page();

// The notebook's 3D model for the get-item scene (an archive built on the player's machine; empty: the
// stand-in's own model). model_override(path) is the file to read instead of the game's for a content
// path the game opens (empty: none); any thread.
void set_notebook_model(const std::string& file);
std::string model_override(const std::string& guest);

void note_import();  // a module imports a mail service: hooks are needed
bool imported();
// After all guest modules are loaded. replaced_by(addr) names a mod that replaces that game function
// (empty: none); replace(addr, fn) installs a host replacement. Nothing is installed on a conflict.
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
void state_loaded();  // a full state was loaded: forget the letter being shown

// A resident's line: while armed, the message `number` shows `text` (kept alive by the caller) for the
// talk that looks it up; unsay() ends it. Used by story_notebook's resident takeovers.
void say(uint32_t number, const std::u16string* text);
// Snapshot-owned provenance for world residents; never inferred from an arbitrary text lookup.
struct SayOwner {uint32_t actor=0,pid=0,token=0,partner=0;bool operator==(const SayOwner&) const=default;};
void set_say_restore_record(uint32_t guest,uint32_t bytes);
void say_owned(uint32_t number,const std::u16string* text,SayOwner owner);
bool restore_say_owner(uint32_t number,SayOwner& owner);
bool resume_say(uint32_t number,const std::u16string* text,SayOwner owner);

void unsay();
bool said();     // the armed line was looked up for display (its text replaced)
bool can_say();  // the hooks are installed and have text buffers: say() can work
// A caption the game sets from a message (the MAP screen's chart name): while armed, the message
// `number` shows `text` (kept alive by the caller); uncaption() ends it. Armed only around the game
// call that sets the caption.
void caption(uint32_t number, const std::u16string* text);
void uncaption();

// A get-item message (0x65 + the item number) with the port's text, for a mod item handed out with a
// game item's get demo (story_treasure_chest.cpp): while armed, the message `number` shows `text` (kept
// alive by the caller); null ends it. Shares the notebook's slot: false while the notebook is being
// handed out, or without the hooks.
bool item_get_text(uint32_t number, const std::u16string* text);

// guest memory for replaced message texts (host-only, at least 6 KiB), before install()
void set_text_buffers(uint32_t guest, uint32_t bytes);
// game code called from the hooks (guestmods::call_original / guest_call in the game)
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// execItemGet for items that aren't the notebook's stand-in: the mod items' handler (story_items.h),
// which runs the game's own function for the game's numbers. Null: the game's function.
void set_item_get_next(void (*next)(Cpu*));

// The hooks, exposed for runtime/tools/story_mail_test.cpp
namespace hooks {
void readable(Cpu* c);
void receive_count(Cpu* c);
void disp_letter_start(Cpu* c);
void present(Cpu* c);
void receive_demo(Cpu* c);
void next_msg(Cpu* c);
void get_message(Cpu* c);
void get_text(Cpu* c);
void symbol(Cpu* c);
void item_get(Cpu* c);
}  // namespace hooks

}  // namespace story_mail
