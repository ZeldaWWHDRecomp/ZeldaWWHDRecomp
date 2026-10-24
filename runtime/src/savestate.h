// Save states: snapshot the whole running game (guest memory, guest threads, HLE state) into one
// of 5 slots and restore it later, also in a fresh process. See savestate.cpp for the design.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>
#include "save_path.h"

#include "ppc.h"

namespace ss {

// little serializer for host-side state (each module writes its own section)
struct Writer {
    std::vector<uint8_t> b;
    void bytes(const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    template <class T> void pod(const T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod");
        bytes(&v, sizeof v);
    }
    void u8(uint8_t v) { pod(v); }
    void u32(uint32_t v) { pod(v); }
    void u64(uint64_t v) { pod(v); }
    void str(const std::string& s) { u32((uint32_t)s.size()); bytes(s.data(), s.size()); }
};

struct Reader {
    const uint8_t* p = nullptr;
    const uint8_t* e = nullptr;
    bool ok = true;
    Reader() = default;
    Reader(const void* d, size_t n) : p((const uint8_t*)d), e((const uint8_t*)d + n) {}
    bool bytes(void* out, size_t n) {
        if (!ok || (size_t)(e - p) < n) { ok = false; if (out) memset(out, 0, n); return false; }
        if (out) memcpy(out, p, n);
        p += n;
        return true;
    }
    template <class T> T pod() {
        T v{};
        bytes(&v, sizeof v);
        return v;
    }
    uint8_t u8() { return pod<uint8_t>(); }
    uint32_t u32() { return pod<uint32_t>(); }
    uint64_t u64() { return pod<uint64_t>(); }
    std::string str() {
        uint32_t n = u32();
        if (!ok || (size_t)(e - p) < n) { ok = false; return {}; }
        std::string s((const char*)p, n);
        p += n;
        return s;
    }
    bool at_end() const { return p == e; }
};

// ---- UI / test API (any thread) ----
// Two kinds of save state share the 5 slots:
//   - portable (default): slotN.wwstate, a few KB of text with the save data and Link's place, no game
//     code or game data; meant for bug reports (portable_state.h). Loading enters the recorded stage
//     with the recorded progress; it is not an exact snapshot.
//   - full (debugging): slotN.bin, the whole running game (~300 MB of guest memory, contains game code
//     and data: never share it). On when the "Full save states" setting is on, with
//     WWHD_FULL_SAVE_STATES=1, and for the scripted test variables (WWHD_STATE_SAVE_AT /
//     WWHD_STATE_LOAD_AT / WWHD_TEST_SAVE / WWHD_TEST_LOAD) unless WWHD_FULL_SAVE_STATES=0.
// Loading a slot loads whichever kind it holds (the newer file if it holds both).
constexpr int kSlots = 5;
struct SlotInfo {
    bool used = false;
    bool compatible = true;
    bool portable = false;  // the slot's (newer) file is a portable state
    std::string when;  // local time of the save
    std::string controller; // empty for legacy states
    std::string area;  // stage name, if known
    std::string path;  // the slot's file
    bool older_other = false;  // the slot also has an older file of the other kind (kept, never deleted)
    uint64_t older_bytes = 0;
};
SlotInfo slot_info(int slot);           // 1..5 (101..103: crash recovery's automatic states, crashrec.h;
                                        //  110: last session, 111..113: auto saves, save_convenience.h)
void request_save(int slot);            // the kind full_states() selects (automatic states: always full)
// request_save, and `done` once the slot file is written (ok) or the save gave up (message in `why`;
// a portable state is refused while Link is not under the player's control); it runs on a background
// thread or the game thread. A later request for another slot replaces it
void request_save(int slot, std::function<void(bool ok, const std::string& why)> done);
void request_save_portable(int slot);
void request_save_full(int slot);
bool in_gameplay();                     // a save file is being played (quit_prompt.h: gameplay_stage)
void request_load(int slot);
// Completion runs once on refusal or after restore/portable arrival, outside
// the request mutex. Parsing failures may complete synchronously.
void request_load(int slot, std::function<void(bool,const std::string&)> done);
void request_load_portable_file(const std::string& path, std::function<void(bool,const std::string&)> done);
void request_load_portable_file(const std::string& path);  // a .wwstate anywhere (WWHD_PORTABLE_LOAD)
bool full_states();                     // save button / shortcuts make full states
void set_full_states(bool on);          // the setting (kept in the states folder)
SavePath save_path();                   // auto (Android streaming, desktop fast), streaming, fast
void set_save_path(SavePath path);      // stored beside the full-state option
bool full_states_forced();              // the environment decides (WWHD_FULL_SAVE_STATES or test variables)
std::string bug_report_text();          // paths of the newest portable state and of cking.sav, for a bug report
std::string states_dir();               // where slots live (created on first use)
std::string last_message();             // short status for the title bar ("" when stale)
// a short notice shown the same way (overlay toast, title bar) for a few seconds: screenshots
void notice(const std::string& text);

// ---- conveniences (save_convenience.h): portable states in their own slots ----
// "Last session" (slot 110): written when the game is quit, on Android also when the app goes to the
// background. Blocks the calling host thread (never the game thread) until the state is written or
// `timeout_ms` passed; nothing happens outside gameplay or while the game cannot save (cutscene), and
// the previous last session then stays. True once written.
bool save_last_session(int timeout_ms);
// At start: "intro" (default), "last" (the last session) or "slot1".."slot5". The state is loaded as
// soon as a Quest Log is being played (a portable state needs one; the player picks it on the title
// screen). Kept in the states folder.
std::string at_start();
void set_at_start(const std::string& choice);
// Auto save every 2, 5 or 10 minutes (0: off, the default) into Auto save 1..3 (slots 111..113),
// oldest first; quiet, and retried a little later when the game cannot save at that moment.
int auto_save_minutes();
void set_auto_save_minutes(int minutes);
// ---- world editor (editor/editor_game.cpp) ----
// A warp with the game's own stage change: a spawn point of the stage (point >= 0), or Link's restart
// point at `pos` facing `angle` (at_position), the way a void-out restarts a room. Done at the next frame
// boundary while Link is under the player's control (pause-menu conditions); refused with a message
// otherwise. The stage's own layer is kept when the warp stays in the current stage.
struct Warp {
    std::string stage;
    int room = 0, point = 0, angle = 0;
    bool at_position = false;
    float pos[3] = {};
};
void request_warp(const Warp& w, std::function<void(bool ok, const std::string& why)> done = nullptr);
// any thread, cheap (memory reads only): why Link is not under the player's control now, or ""
std::string control_problem();
struct LinkPlace {
    bool valid = false;
    std::string stage;
    int room = -1, angle = 0;
    float pos[3] = {};
};
LinkPlace link_place();  // any thread (a snapshot of the game's memory)
// a portable state to any path (the editor's test slots); `done` as request_save's
void request_save_portable_file(const std::string& path, std::function<void(bool ok, const std::string& why)> done);

// Full states for isolated editor test files. Same full-state validators and
// native gates as numbered slots; no regular slot path is changed.
void request_save_full_file(const std::string&,std::function<void(bool,const std::string&)>);
void request_load_full_file(const std::string&,std::function<void(bool,const std::string&)>);

// ---- game thread: call at the frame boundary (top of the per-frame function) ----
void service(Cpu* c);

// guest memory reader used while validating a snapshot (reads the snapshot's memory, not the live one)
uint32_t snap_ld32(uint32_t ea);

uint64_t last_load_frame();
uint64_t last_arrival_frame();  // TV frame a portable load arrived at (Link settled; 0: none)
uint64_t last_load_step();
uint32_t last_load_counter();  // g_Counter.mTimer right after the last load   // logic step (interp::logic_steps) of the last completed load  // TV frame of the last completed load (0: none); test scenarios start from it
}  // namespace ss
