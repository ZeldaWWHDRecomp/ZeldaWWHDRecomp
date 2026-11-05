// SPDX-License-Identifier: MPL-2.0
// Instant text (Gameplay mods, off by default): message boxes show their whole text at once, as the TWWHD
// Randomizer's "Instant text boxes", but switched while playing and without changing any game file.
// No pauses in texts (its own switch, off by default): the waits written into the texts are left out.
// Off, neither reads nor writes anything of the game's.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), mods/instant_text.cpp; reworked to need no hooks
// of its own (the game code stays as devel generates it).
//
// Instant text: the talk windows' message text work (HdMsgText, d/hd_msg_text.h in the decompilation) is the
// message manager's (*0x101F4B5C) +0x14. Its byte +0x696 is the draw type: 0 types letter by letter, 1 shows the
// text at once (the game sets it from the message's attribute, or from the text's own instant on / off tags
// 025FB148 / 025FB15C). Every logic step, before and after the processes execute, the byte is set to 1 while a
// message runs (manager +0x94C == 1), so the typing step (026014C8 and its readers 025FAA80, 026017A4) sees it.
// A new message clears it (hdMsgText init), and the next step sets it again. The fork set it at the typing
// step's instruction 026014F4; the randomizer's B-skip reads and its arrow-wait patches need instruction hooks
// and are not part of this (docs/settings-page.md).
//
// No pauses in texts: the wait code 0E 0001 0006 0002 <frames> gets 0 frames in every message file (MSBT) in
// memory (msbt_waits.h). The files are found by a scan of MEM2 for "MsgStdBn" on a thread of its own (about a
// second on a Mac), when a new message starts and the last scan is at least kRescanSeconds ago (a new place loads its message files). The
// game's values are kept and written back when it goes off (where the wait is still a 0).
// WWHD_INSTANT_TEXT_LOG=1 logs each message seen and how far it is laid out after 1, 2, 4 ... 64 steps.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "guest_addr.h"
#include "mods.h"
#include "msbt_waits.h"
#include "../runtime.h"

namespace mods {
namespace {
bool env_on(const char* n) {
    const char* e = std::getenv(n);
    return e && std::atoi(e) != 0;
}
std::atomic<bool> g_instant{env_on("WWHD_MOD_INSTANT_TEXT")};
std::atomic<bool> g_no_pauses{env_on("WWHD_MOD_NO_TEXT_PAUSES")};
const bool kLog = env_on("WWHD_INSTANT_TEXT_LOG");

constexpr uint32_t kMessage = 0x14;        // its message text work (HdMsgText)
constexpr uint32_t kMode = 0x94C;          // 1: a message runs
constexpr uint32_t kDrawType = 0x696;      // HdMsgText: 0 letter by letter, 1 at once
constexpr int64_t kRescanSeconds = 20;

std::atomic<bool> g_scanning{false};
std::atomic<int64_t> g_last_scan{-1000000};  // steady seconds
std::atomic<uint32_t> g_scan_request{0};       // the message steps since a scan was due
std::mutex g_mu;
std::unordered_map<uint32_t, msbt::Saved> g_patched;  // file address -> its waits' offsets and the game's values

int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool guest_ptr(uint32_t a) { return a >= mem::kMem2Start && a < mem::kMem2End; }

// the next "MsgStdBn" in [p, end), or null: memchr for its first letter, then the rest (memmem isn't on Windows)
const uint8_t* find_magic(const uint8_t* p, const uint8_t* end) {
    while (size_t(end - p) >= sizeof msbt::kMagic) {
        p = static_cast<const uint8_t*>(std::memchr(p, msbt::kMagic[0], size_t(end - p) - sizeof msbt::kMagic + 1));
        if (!p) return nullptr;
        if (!std::memcmp(p, msbt::kMagic, sizeof msbt::kMagic)) return p;
        ++p;
    }
    return nullptr;
}
void scan() {
    const auto t0 = std::chrono::steady_clock::now();
    int files = 0, waits = 0;
    {
        std::lock_guard<std::mutex> l(g_mu);
        const uint32_t lo = mem::kMem2Start, hi = mem::kMem2End;
        uint8_t* base = mem::ptr(lo);
        const size_t len = hi - lo;
        const uint8_t* p = base;
        while (g_no_pauses.load(std::memory_order_relaxed)) {
            p = find_magic(p, base + len);
            if (!p) break;
            const uint32_t a = lo + uint32_t(p - base);
            p += 8;
            if (a & 3) continue;
            uint8_t* file = mem::ptr(a);
            const size_t avail = hi - a;
            if (!msbt::file_size(file, avail)) continue;
            files++;
            msbt::Saved& saved = g_patched[a];
            waits += msbt::patch(file, avail, saved);
        }
    }
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    LOG("[mods] text pauses: %d message files, %d waits set to 0 (%lld ms)", files, waits, ms);
}

void restore() {
    std::lock_guard<std::mutex> l(g_mu);
    int n = 0;
    for (const auto& [a, saved] : g_patched) n += msbt::restore(mem::ptr(a), mem::kMem2End - a, saved);
    g_patched.clear();
    if (n) LOG("[mods] text pauses: %d waits back", n);
}

void start_scan() {
    if (g_scanning.exchange(true)) return;
    g_last_scan = now_s();
    std::thread([] {
        if (g_no_pauses.load()) scan();
        g_scanning = false;
    }).detach();
}

void pauses_changed() {
    if (g_no_pauses.load()) {
        g_last_scan = -1000000;  // at the next message
        return;
    }
    std::thread([] {
        while (g_scanning.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        restore();
    }).detach();
}
}  // namespace

bool instant_text() { return g_instant.load(std::memory_order_relaxed); }
void set_instant_text(bool on) {
    g_instant = on;
    LOG("[mods] instant text %s", on ? "on" : "off");
}
bool no_text_pauses() { return g_no_pauses.load(std::memory_order_relaxed); }
void set_no_text_pauses(bool on) {
    if (g_no_pauses.exchange(on) == on) return;
    LOG("[mods] no pauses in texts %s", on ? "on" : "off");
    pauses_changed();
}

// interp.cpp's fpcEx_Handler hook, before and after the processes' execute (full logic passes only)
void instant_text_step() {
    const bool instant = instant_text(), pauses = no_text_pauses();
    if (!instant && !pauses && !kLog) return;  // (the log also reports an unchanged game)
    const uint32_t manager = ld32(GD(0x101F4B5C));  // the message manager (hdMsgManager_l)
    if (!guest_ptr(manager) || ld32(manager + kMode) != 1) return;
    const uint32_t msg = manager + kMessage;
    if (kLog) {  // each message: its number, then the characters laid out after 1, 2, 4, ... 64 steps
        static uint32_t last = ~0u, steps = 0;
        static bool second = false;  // (called twice a step)
        const uint32_t number = ld32(msg + 0x11C);  // HdMsgText::mMessageNumber
        if (number != last) {
            last = number, steps = 0, second = false;
            LOG("[instant-text] message %u starts: draw type %u", number, ld8(msg + kDrawType));
        }
        if ((second = !second) && ++steps <= 64 && !(steps & (steps - 1)))
            LOG("[instant-text] message %u after %u steps: drawn %u of %u, draw type %u", number, steps,
                ld32(msg + 0x68C), ld32(msg + 0x63C), ld8(msg + kDrawType));  // HdMsgText mDrawIndex, mCharacterIndex
    }
    if (instant && ld8(msg + kDrawType) != 1) st8(msg + kDrawType, 1);
    // a new message (not every step of a long talk) may come from files loaded since the last scan
    static uint32_t scanned_for = ~0u;
    const uint32_t number = ld32(msg + 0x11C);
    if (pauses && number != scanned_for && !g_scanning.load(std::memory_order_relaxed) &&
        now_s() - g_last_scan.load(std::memory_order_relaxed) >= kRescanSeconds) {
        scanned_for = number;
        start_scan();
    }
}

}  // namespace mods
