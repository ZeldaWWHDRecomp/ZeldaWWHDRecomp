// Save states: snapshot the whole running game (guest memory, guest threads, HLE state) into one
// of 5 slots and restore it later, also in a fresh process. See savestate.cpp for the design.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

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
SlotInfo slot_info(int slot);           // 1..5 (101..103: crash recovery's automatic states, crashrec.h)
void request_save(int slot);            // the kind full_states() selects (automatic states: always full)
// request_save, and `done` once the slot file is written (ok) or the save gave up (message in `why`;
// a portable state is refused while Link is not under the player's control); it runs on a background
// thread or the game thread. A later request for another slot replaces it
void request_save(int slot, std::function<void(bool ok, const std::string& why)> done);
void request_save_portable(int slot);
void request_save_full(int slot);
bool in_gameplay();                     // a save file is being played (quit_prompt.h: gameplay_stage)
void request_load(int slot);
void request_load_portable_file(const std::string& path);  // a .wwstate anywhere (WWHD_PORTABLE_LOAD)
bool full_states();                     // save button / shortcuts make full states
void set_full_states(bool on);          // the setting (kept in the states folder)
bool full_states_forced();              // the environment decides (WWHD_FULL_SAVE_STATES or test variables)
std::string bug_report_text();          // paths of the newest portable state and of cking.sav, for a bug report
std::string states_dir();               // where slots live (created on first use)
std::string last_message();             // short status for the title bar ("" when stale)
// a short notice shown the same way (overlay toast, title bar) for a few seconds: screenshots
void notice(const std::string& text);

// ---- game thread: call at the frame boundary (top of the per-frame function) ----
void service(Cpu* c);

// guest memory reader used while validating a snapshot (reads the snapshot's memory, not the live one)
uint32_t snap_ld32(uint32_t ea);

uint64_t last_load_frame();
uint64_t last_load_step();
uint32_t last_load_counter();  // g_Counter.mTimer right after the last load   // logic step (interp::logic_steps) of the last completed load  // TV frame of the last completed load (0: none); test scenarios start from it
}  // namespace ss
