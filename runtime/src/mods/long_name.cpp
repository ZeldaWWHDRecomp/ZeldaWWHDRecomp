// SPDX-License-Identifier: MPL-2.0
// Save file names up to 16 characters: see long_name.h.
#include "long_name.h"

#include <mutex>

#include "guest_addr.h"
#include "runtime.h"

namespace long_name {
namespace {

const uint32_t kSaveInfoPtr = GD(0x101F84DC);  // dComIfGs save area
constexpr uint32_t kCardNames = 0x12C0 + 0xA0;  // SaveMgr card info's name objects, 0x40 each (getCardName 02720154)
constexpr uint32_t kEntry = 0x40, kOwnBuffer = 0xC, kOwnCap = kOwn + 1, kLongCap = kMax + 1;

std::mutex g_mu;
std::array<std::u16string, 3> g_saved;  // each file's whole name as cking.sav has it ("" none)
std::array<std::u16string, 3> g_live;   // ... and as it is now (a rename since the last save)
std::array<std::u16string, 3> g_placed; // what service() last copied into the long name's place
// guest: three 0x40-byte copies of the whole names, at the same address in every session (a full save state's
// pointer to one stays good; its text is written again by service())
const uint32_t g_place = mem::fixed_slot(mem::kFixLongNames);

std::string utf8(const std::u16string& s) {  // (names: no surrogate pairs in the game's fonts)
    std::string o;
    for (char16_t c : s) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) o += char(0xC0 | c >> 6), o += char(0x80 | (c & 0x3F));
        else o += char(0xE0 | c >> 12), o += char(0x80 | (c >> 6 & 0x3F)), o += char(0x80 | (c & 0x3F));
    }
    return o;
}
bool log_on() {
    static const bool on = getenv("WWHD_LONG_NAME_LOG") != nullptr;
    return on;
}
uint32_t entries() {
    const uint32_t sv = ld32(kSaveInfoPtr);
    if ((sv & 3) || sv < mem::kMem2Start || sv >= mem::kMem2End - kCardNames - 3 * kEntry) return 0;
    const uint32_t e = sv + kCardNames;
    // the three objects as the game made them: own buffers' pointers or ours, capacities 9 or 17
    for (uint32_t f = 0; f < 3; f++) {
        const uint32_t x = e + f * kEntry, p = ld32(x), cap = ld32(x + 8);
        if ((p != x + kOwnBuffer && p != g_place + f * 0x40) || (cap != kOwnCap && cap != kLongCap)) return 0;
    }
    return e;
}
std::u16string read16(uint32_t a, size_t max) {
    std::u16string s;
    for (size_t i = 0; i < max; i++) {
        const char16_t ch = char16_t(ld16(a + 2 * uint32_t(i)));
        if (!ch) break;
        s.push_back(ch);
    }
    return s;
}
void write16(uint32_t a, const std::u16string& s, size_t cap) {
    for (size_t i = 0; i < cap; i++) st16(a + 2 * uint32_t(i), i < s.size() ? uint16_t(s[i]) : 0);
}
}  // namespace

void loaded(const uint8_t* payload) {
    std::lock_guard lock(g_mu);
    for (unsigned f = 0; f < 3; f++) {
        g_saved[f] = payload ? read(payload + f * story_block::kAreaSize) : std::u16string();
        g_live[f] = g_saved[f];
        if (log_on() && !g_saved[f].empty())
            LOG("[long-name] file %u: whole name \"%s\" in the story block", f + 1, utf8(g_saved[f]).c_str());
    }
}

bool written(uint8_t* file, size_t game_size, uint8_t* areas) {
    const unsigned fixed = fix_saved_names(file, game_size);
    std::lock_guard lock(g_mu);
    for (int f = 0; f < 3; f++) {
        const std::u16string own = game_size >= kFileSize ? own_name(file, f) : std::u16string();
        // the whole name goes with the save while the save's own name is its beginning (else the file has a
        // new name: a new game, the game's name entry)
        const std::u16string whole = holds(g_live[f], own) ? g_live[f] : std::u16string();
        if (!write(areas + f * story_block::kAreaSize, whole)) {
            if (whole.empty()) continue;  // (an area of data this port did not write: left alone)
            LOG("[long-name] file %d: no room in the story block for the whole name; the save keeps \"%s\"", f + 1,
                utf8(own).c_str());
            continue;
        }
        if (whole != g_saved[f] && (log_on() || !whole.empty()))
            LOG("[long-name] file %d saved with the whole name \"%s\"", f + 1, utf8(whole).c_str());
        g_saved[f] = whole;
        if (whole.empty()) g_live[f].clear();
    }
    if (fixed && log_on()) LOG("[long-name] the save's names cut back to 8 characters (files %x)", fixed);
    return fixed != 0;
}

void renamed(int slot, const std::u16string& whole) {
    if (slot < 0 || slot > 2) return;
    std::lock_guard lock(g_mu);
    g_live[size_t(slot)] = whole.size() > kOwn ? whole.substr(0, kMax) : std::u16string();
}

void release(int slot) {
    if (slot < 0 || slot > 2) return;
    std::lock_guard lock(g_mu);
    const uint32_t e = entries();
    if (!e) return;
    const uint32_t x = e + uint32_t(slot) * kEntry;
    st32(x, x + kOwnBuffer);
    st32(x + 8, kOwnCap);
    g_placed[size_t(slot)].clear();
}

std::u16string shown(int slot) {
    if (slot < 0 || slot > 2) return {};
    std::lock_guard lock(g_mu);
    const uint32_t e = entries();
    if (!e) return {};
    const uint32_t x = e + uint32_t(slot) * kEntry;
    const std::u16string own = read16(x + kOwnBuffer, kOwn);
    return holds(g_live[size_t(slot)], own) ? g_live[size_t(slot)] : own;
}

void service(Cpu*) {
    std::lock_guard lock(g_mu);
    const uint32_t e = entries();
    if (!e) return;
    for (uint32_t f = 0; f < 3; f++) {
        const uint32_t x = e + f * kEntry, at = g_place + f * 0x40;
        // the game wrote a name of its own through the pointer (its name entry for a new game): that name is
        // the file's own now, and the whole name is let go
        if (ld32(x) == at && !g_placed[f].empty() && read16(at, kMax) != g_placed[f]) {
            const std::u16string given = read16(at, kOwn);
            write16(x + kOwnBuffer, given, kOwnCap);
            st32(x, x + kOwnBuffer);
            st32(x + 8, kOwnCap);
            g_live[f].clear();
            g_placed[f].clear();
            LOG("[long-name] file %u: the game gave it a name of its own", f + 1);
        }
        const std::u16string own = read16(x + kOwnBuffer, kOwn);
        const std::u16string& whole = g_live[f];
        if (holds(whole, own)) {
            if (g_placed[f] != whole || read16(at, kMax) != whole) {
                write16(at, whole, kLongCap);
                g_placed[f] = whole;
            }
            if (ld32(x) != at || ld32(x + 8) != kLongCap) {
                st32(x, at);
                st32(x + 8, kLongCap);
                if (log_on()) LOG("[long-name] file %u: the game shows \"%s\"", f + 1, utf8(whole).c_str());
            }
        } else if (ld32(x) != x + kOwnBuffer || ld32(x + 8) != kOwnCap) {
            st32(x, x + kOwnBuffer);
            st32(x + 8, kOwnCap);
            if (log_on()) LOG("[long-name] file %u: the game shows its own name \"%s\"", f + 1, utf8(own).c_str());
        }
    }
}

}  // namespace long_name
